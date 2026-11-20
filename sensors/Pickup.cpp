// SPDX-License-Identifier: Apache-2.0
#define LOG_TAG "M2381Pickup"
#include <V2_1/SubHal.h>
#include <dlfcn.h>
#include <log/log.h>
#include <time.h>

#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace android::hardware::sensors::V2_1::implementation {
using V1_0::OperationMode;
using V1_0::RateLevel;
using V1_0::Result;
using V1_0::SharedMemInfo;
using V2_0::implementation::ScopedWakelock;
constexpr int32_t kPickup = 0x00f00001; // lower 24 bits belong to this sub-HAL
constexpr int32_t kRaiseType = 65539;

int64_t nowNs() {
    timespec ts{};
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return ts.tv_sec * INT64_C(1000000000) + ts.tv_nsec;
}

class Pickup final : public ISensorsSubHal {
    class Callback final : public IHalProxyCallback {
    public:
        explicit Callback(Pickup* owner) : owner_(owner) {}
        Return<void> onDynamicSensorsConnected(const hidl_vec<V1_0::SensorInfo>& infos) override {
            return owner_->callback_->onDynamicSensorsConnected(infos);
        }
        Return<void> onDynamicSensorsConnected_2_1(const hidl_vec<SensorInfo>& infos) override {
            {
                std::lock_guard lock(owner_->mutex_);
                for (const auto& info : infos) owner_->flags_[info.sensorHandle] = info.flags;
            }
            return owner_->callback_->onDynamicSensorsConnected_2_1(infos);
        }
        Return<void> onDynamicSensorsDisconnected(const hidl_vec<int32_t>& handles) override {
            return owner_->callback_->onDynamicSensorsDisconnected(handles);
        }
        void postEvents(const std::vector<Event>& events, ScopedWakelock wakelock) override {
            std::lock_guard lock(owner_->mutex_);
            owner_->pending_.push_back({events, std::move(wakelock), owner_->generation_});
            owner_->condition_.notify_one();
        }
        ScopedWakelock createScopedWakelock(bool locked) override {
            return owner_->callback_->createScopedWakelock(locked);
        }
    private:
        Pickup* owner_;
    };

public:
    explicit Pickup(ISensorsSubHal* stock) : stock_(stock) {
        stock_->getSensorsList_2_1([this](const auto& infos) {
            for (const auto& info : infos) {
                list_.push_back(info);
                flags_[info.sensorHandle] = info.flags;
                if (static_cast<int32_t>(info.type) == kRaiseType &&
                    info.typeAsString == "com.meizu.sensor.raise" && (info.flags & 1)) {
                    raise_ = info.sensorHandle;
                }
            }
        });
        bool collision = flags_.count(kPickup) != 0;
        if (raise_ >= 0 && !collision) {
            for (const auto& info : list_) {
                if (info.sensorHandle != raise_) continue;
                SensorInfo pickup = info;
                pickup.sensorHandle = kPickup;
                pickup.name = "Meizu Pickup Gesture";
                pickup.type = SensorType::PICK_UP_GESTURE;
                pickup.typeAsString = "android.sensor.pick_up_gesture";
                pickup.maxRange = pickup.resolution = 1;
                pickup.minDelay = -1;
                pickup.maxDelay = 0;
                pickup.fifoReservedEventCount = pickup.fifoMaxEventCount = 0;
                pickup.requiredPermission = "";
                pickup.flags = 5; // wake-up, one-shot; no direct/injection support
                list_.push_back(pickup);
                flags_[kPickup] = pickup.flags;
                available_ = true;
                break;
            }
        }
        ALOGI("Stock raise handle=%d; standard pickup available=%d", raise_, available_);
        worker_ = std::thread([this] { dispatch(); });
    }
    ~Pickup() override {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            condition_.notify_one();
        }
        worker_.join();
    }
    const std::string getName() override { return "M2381 SSC pickup adapter"; }
    Return<void> getSensorsList_2_1(getSensorsList_2_1_cb cb) override {
        cb(list_);
        return Void();
    }
    Return<Result> initialize(const sp<IHalProxyCallback>& callback) override {
        std::lock_guard control(control_);
        {
            std::lock_guard lock(mutex_);
            callback_ = callback;
            armed_ = privateEnabled_ = false;
            ++generation_;
            lastAction_ = -1;
            pending_.clear();
        }
        return stock_->initialize(sp<Callback>::make(this));
    }
    Return<Result> activate(int32_t handle, bool enabled) override {
        if (handle != kPickup && handle != raise_) return stock_->activate(handle, enabled);
        if (handle == kPickup && !available_) return Result::BAD_VALUE;
        std::lock_guard control(control_);
        bool oldArmed, oldPrivate, desired;
        {
            std::lock_guard lock(mutex_);
            oldArmed = armed_; oldPrivate = privateEnabled_;
            if (handle == kPickup) {
                armed_ = enabled;
                armedAt_ = nowNs();
                ++generation_;
            } else {
                privateEnabled_ = enabled;
            }
            desired = armed_ || privateEnabled_;
        }
        auto result = stock_->activate(raise_, desired);
        if (!result.isOk() || result != Result::OK) {
            std::lock_guard lock(mutex_);
            armed_ = oldArmed; privateEnabled_ = oldPrivate;
        }
        return result;
    }
    Return<Result> batch(int32_t h, int64_t period, int64_t latency) override {
        if (h == kPickup) return stock_->batch(raise_, 200000000, 0);
        return stock_->batch(h, period, latency);
    }
    Return<Result> flush(int32_t h) override {
        return h == kPickup ? Return<Result>(Result::BAD_VALUE) : stock_->flush(h);
    }
    Return<Result> injectSensorData_2_1(const Event& event) override {
        return event.sensorHandle == kPickup ? Return<Result>(Result::INVALID_OPERATION)
                                            : stock_->injectSensorData_2_1(event);
    }
    Return<Result> setOperationMode(OperationMode mode) override {
        return stock_->setOperationMode(mode);
    }
    Return<void> registerDirectChannel(const SharedMemInfo& mem,
                                      registerDirectChannel_cb cb) override {
        return stock_->registerDirectChannel(mem, cb);
    }
    Return<Result> unregisterDirectChannel(int32_t h) override {
        return stock_->unregisterDirectChannel(h);
    }
    Return<void> configDirectReport(int32_t h, int32_t channel, RateLevel rate,
                                   configDirectReport_cb cb) override {
        if (h == kPickup) { cb(Result::BAD_VALUE, 0); return Void(); }
        return stock_->configDirectReport(h, channel, rate, cb);
    }
    Return<void> debug(const hidl_handle& fd, const hidl_vec<hidl_string>& args) override {
        return stock_->debug(fd, args);
    }

private:
    struct Pending {
        std::vector<Event> events;
        ScopedWakelock wakelock;
        uint64_t generation;
    };
    void dispatch() {
        for (;;) {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
            if (stopping_) return;
            Pending pending = std::move(pending_.front());
            pending_.pop_front();
            lock.unlock();
            std::vector<Event> output;
            bool disable = false, wakeup = false;
            // Serialize disarming against framework cancellation/rearming, but
            // never call the stock HAL while holding the callback queue lock.
            std::unique_lock control(control_);
            lock.lock();
            auto callback = callback_;
            for (const auto& event : pending.events) {
                if (event.sensorHandle != raise_) {
                    output.push_back(event);
                } else {
                    if (privateEnabled_) output.push_back(event);
                    if (static_cast<int32_t>(event.sensorType) != kRaiseType) continue;
                    float value = event.u.data[0];
                    // Flyme RaiseFlipperController: 6 wakes; 2 wakes unless
                    // preceded by 6/7. These are discrete actions, not angles.
                    bool pickup = value == 6 || (value == 2 && lastAction_ != 6 && lastAction_ != 7);
                    lastAction_ = static_cast<int>(value);
                    if (pickup && armed_ && pending.generation == generation_ &&
                        event.timestamp >= armedAt_) {
                        Event translated{};
                        translated.timestamp = event.timestamp;
                        translated.sensorHandle = kPickup;
                        translated.sensorType = SensorType::PICK_UP_GESTURE;
                        translated.u.scalar = 1;
                        output.push_back(translated);
                        armed_ = false;
                        disable = !privateEnabled_;
                    }
                }
            }
            for (const auto& event : output) wakeup |= (flags_[event.sensorHandle] & 1) != 0;
            lock.unlock();
            if (disable) {
                auto result = stock_->activate(raise_, false);
                if (!result.isOk() || result != Result::OK) {
                    ALOGE("Could not disarm stock raise sensor");
                }
            }
            control.unlock();
            // Retain the vendor wake lock until the replacement batch owns its
            // wake lock. No long-lived wakelock or polling thread is added.
            if (!output.empty()) {
                callback->postEvents(output, callback->createScopedWakelock(wakeup));
            }
        }
    }
    ISensorsSubHal* stock_;
    int32_t raise_ = -1;
    bool available_ = false;
    std::vector<SensorInfo> list_;
    std::map<int32_t, uint32_t> flags_;
    sp<IHalProxyCallback> callback_;
    std::mutex mutex_, control_;
    std::condition_variable condition_;
    std::deque<Pending> pending_;
    std::thread worker_;
    bool stopping_ = false, armed_ = false, privateEnabled_ = false;
    int lastAction_ = -1;
    int64_t armedAt_ = 0;
    uint64_t generation_ = 0;
};
} // namespace android::hardware::sensors::V2_1::implementation

using android::hardware::sensors::V2_1::implementation::Pickup;
using android::hardware::sensors::V2_1::implementation::ISensorsSubHal;
extern "C" ISensorsSubHal* sensorsHalGetSubHal_2_1(uint32_t* version) {
    static Pickup* adapter = []() -> Pickup* {
        // Keep the unmodified stock HAL loaded for the process lifetime.
        void* library = dlopen("sensors.ssc.so", RTLD_NOW | RTLD_LOCAL);
        if (!library) { ALOGE("Stock SSC load failed: %s", dlerror()); return nullptr; }
        auto factory = reinterpret_cast<ISensorsSubHal* (*)(uint32_t*)>(
                dlsym(library, "sensorsHalGetSubHal_2_1"));
        if (!factory) return nullptr;
        uint32_t stockVersion = 0;
        ISensorsSubHal* stock = factory(&stockVersion);
        if (!stock || stockVersion != SUB_HAL_2_1_VERSION) return nullptr;
        return new Pickup(stock);
    }();
    *version = SUB_HAL_2_1_VERSION;
    return adapter;
}
