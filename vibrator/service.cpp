// SPDX-License-Identifier: Apache-2.0
#include <aidl/android/hardware/vibrator/BnVibrator.h>
#include <android-base/file.h>
#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include "Tiktap.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <unistd.h>

namespace aidl::android::hardware::vibrator {

// m2381 has an Awinic I2C motor, independent of the QCOM haptics device.
// Meizu's waveform/enable interface plays the stock RAM effects. The firmware
// and this driver's sysfs ABI are documented in README.md beside this source.
// Tiktap RTP composition is isolated from the stock RAM effect path.
class Vibrator final : public BnVibrator {
  public:
    Vibrator() : compositionAvailable_(meizu::TiktapProcess::available()),
                 worker_([this] { runTimer(); }) {}
    ~Vibrator() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            changed_.notify_all();
        }
        worker_.join();
        tiktap_.stop();
        writeDuration(0);
    }
    ndk::ScopedAStatus getCapabilities(int32_t* result) override {
        *result = IVibrator::CAP_AMPLITUDE_CONTROL |
                  (compositionAvailable_ ? IVibrator::CAP_COMPOSE_EFFECTS : 0);
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus off() override {
        std::lock_guard<std::mutex> lock(mutex_);
        tiktap_.stop();
        callback_.reset();
        composing_ = false;
        auto result = writeDuration(0);
        if (!result.isOk()) return result;
        active_ = false;
        ++generation_;
        changed_.notify_all();
        return result;
    }
    ndk::ScopedAStatus on(int32_t timeoutMs,
                         const std::shared_ptr<IVibratorCallback>& callback) override {
        if (timeoutMs <= 0) return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        if (callback) return unsupported();
        std::lock_guard<std::mutex> lock(mutex_);
        // Reset the waveform after perform(): otherwise a notification can
        // inherit the preceding click's waveform (and vice versa).
        return startLocked(timeoutMs, kDefaultGain, kTimedWaveform);
    }
    ndk::ScopedAStatus getSupportedEffects(std::vector<Effect>* result) override {
        *result = {Effect::CLICK, Effect::TICK, Effect::HEAVY_CLICK, Effect::TEXTURE_TICK};
        if (compositionAvailable_) result->push_back(Effect::DOUBLE_CLICK);
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getSupportedPrimitives(std::vector<CompositePrimitive>* result) override {
        result->clear();
        if (compositionAvailable_) {
            *result = {CompositePrimitive::NOOP, CompositePrimitive::CLICK,
                       CompositePrimitive::THUD, CompositePrimitive::SPIN,
                       CompositePrimitive::QUICK_RISE, CompositePrimitive::SLOW_RISE,
                       CompositePrimitive::QUICK_FALL, CompositePrimitive::LIGHT_TICK,
                       CompositePrimitive::LOW_TICK};
        }
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getSupportedAlwaysOnEffects(std::vector<Effect>* result) override {
        result->clear();
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus perform(Effect effect, EffectStrength strength,
                             const std::shared_ptr<IVibratorCallback>& callback,
                             int32_t* durationMs) override {
        if (callback) return unsupported();
        if (effect == Effect::DOUBLE_CLICK) {
            if (!compositionAvailable_) return unsupported();
            float scale;
            switch (strength) {
                case EffectStrength::LIGHT: scale = 0.5f; break;
                case EffectStrength::MEDIUM: scale = 0.75f; break;
                case EffectStrength::STRONG: scale = 1.0f; break;
                default: return unsupported();
            }
            // The device has a real composition backend: advertise and perform
            // two clicks instead of relying on a framework waveform fallback.
            CompositeEffect click;
            click.primitive = CompositePrimitive::CLICK;
            click.scale = scale;
            click.delayMs = 0;
            CompositeEffect second = click;
            second.delayMs = 100;
            auto status = compose({click, second}, nullptr);
            if (status.isOk()) {
                *durationMs = 2 * meizu::primitiveDuration(CompositePrimitive::CLICK) +
                              second.delayMs + kEffectCompletionMs;
            }
            return status;
        }
        int32_t waveform;
        switch (effect) {
            case Effect::TEXTURE_TICK: waveform = 8; break;
            // Flyme maps TICK/CLICK/HEAVY_CLICK to vendor effects
            // 21000/31008/30900, selecting RAM 11/13/3 in haptic_hv.
            case Effect::TICK: waveform = 11; break;
            case Effect::CLICK: waveform = 13; break;
            case Effect::HEAVY_CLICK: waveform = 3; break;
            default: return unsupported();
        }
        // Keep Flyme's baseline at STRONG while honoring Android's user
        // intensity setting. Its framework sends the level through strength.
        int32_t gain;
        switch (strength) {
            case EffectStrength::LIGHT: gain = kDefaultGain / 2; break;
            case EffectStrength::MEDIUM: gain = kDefaultGain * 3 / 4; break;
            case EffectStrength::STRONG: gain = kDefaultGain; break;
            default: return unsupported();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        // <= 50 ms selects a single RAM playback for these waveforms. This
        // value is a completion budget, NOT a pulse length: the chip plays
        // the complete 6/10/16 ms waveform, including its tail, and stops itself.
        // Allow for the driver's asynchronous I2C setup before framework off().
        auto status = startLocked(kEffectCompletionMs, gain, waveform);
        if (status.isOk()) *durationMs = kEffectCompletionMs;
        return status;
    }
    ndk::ScopedAStatus setAmplitude(float amplitude) override {
        if (!std::isfinite(amplitude) || amplitude <= 0.0f || amplitude > 1.0f) {
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
        // Retain a nonzero gain for every valid nonzero framework amplitude.
        // Flyme truncates amplitude * 128 when converting it to an integer.
        const int32_t gain = std::max(1, static_cast<int32_t>(amplitude * kMaxGain));
        std::lock_guard<std::mutex> lock(mutex_);
        return writeGain(gain);
    }
    ndk::ScopedAStatus setExternalControl(bool) override { return unsupported(); }
    ndk::ScopedAStatus getCompositionDelayMax(int32_t* result) override {
        if (!compositionAvailable_) return unsupported();
        *result = meizu::kCompositionDelay;
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getCompositionSizeMax(int32_t* result) override {
        if (!compositionAvailable_) return unsupported();
        *result = meizu::kCompositionSize;
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getPrimitiveDuration(CompositePrimitive primitive, int32_t* result) override {
        if (!compositionAvailable_) return unsupported();
        *result = meizu::primitiveDuration(primitive);
        return *result < 0 ? unsupported() : ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus compose(const std::vector<CompositeEffect>& effects,
                             const std::shared_ptr<IVibratorCallback>& callback) override {
        if (!compositionAvailable_) return unsupported();
        meizu::Composition plan;
        const int error = meizu::makeComposition(effects, &plan);
        if (error) return ndk::ScopedAStatus::fromExceptionCode(error);
        std::lock_guard<std::mutex> lock(mutex_);
        tiktap_.stop();
        callback_.reset();
        active_ = false;
        ++generation_;
        changed_.notify_all();
        auto status = writeDuration(0);
        if (!status.isOk()) return status;
        if (!tiktap_.start(plan)) return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
        callback_ = callback;
        composing_ = true;
        active_ = true;
        deadline_ = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(plan.durationMs + 500);
        changed_.notify_all();
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus alwaysOnEnable(int32_t, Effect, EffectStrength) override {
        return unsupported();
    }
    ndk::ScopedAStatus alwaysOnDisable(int32_t) override { return unsupported(); }
    ndk::ScopedAStatus getResonantFrequency(float*) override { return unsupported(); }
    ndk::ScopedAStatus getQFactor(float*) override { return unsupported(); }
    ndk::ScopedAStatus getFrequencyResolution(float*) override { return unsupported(); }
    ndk::ScopedAStatus getFrequencyMinimum(float*) override { return unsupported(); }
    ndk::ScopedAStatus getBandwidthAmplitudeMap(std::vector<float>*) override {
        return unsupported();
    }
    ndk::ScopedAStatus getPwlePrimitiveDurationMax(int32_t*) override { return unsupported(); }
    ndk::ScopedAStatus getPwleCompositionSizeMax(int32_t*) override { return unsupported(); }
    ndk::ScopedAStatus getSupportedBraking(std::vector<Braking>*) override {
        return unsupported();
    }
    ndk::ScopedAStatus composePwle(const std::vector<PrimitivePwle>&,
                                 const std::shared_ptr<IVibratorCallback>&) override {
        return unsupported();
    }

  private:
    // Stock enable/off uses 0x80; Flyme's MzVibrateThread maps an explicit
    // waveform amplitude to amplitude * 128. Do not halve that baseline or
    // attenuate each RAM effect a second time: its samples define its shape.
    static constexpr int32_t kMaxGain = 0x80;
    static constexpr int32_t kDefaultGain = 0x80;
    static constexpr int32_t kTimedWaveform = 1;
    static constexpr int32_t kEffectCompletionMs = 30;
    static ndk::ScopedAStatus unsupported() {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    ndk::ScopedAStatus startLocked(int32_t timeoutMs, int32_t gain, int32_t waveform) {
        // Stop/cancel the preceding kernel work before changing its waveform.
        // In particular, do not leave the infinite loop from a long on() in
        // effect when a short UI effect is requested next.
        tiktap_.stop();
        callback_.reset();
        composing_ = false;
        auto result = writeDuration(0);
        if (!result.isOk()) return result;
        active_ = false;
        ++generation_;
        changed_.notify_all();
        result = writeWaveform(waveform);
        if (!result.isOk()) return result;
        // The stock driver's enable_store resets gain to 0x80 unless the
        // decimal gain is supplied as its second argument. A prior gain_store
        // only changes the hardware register and is overwritten at playback.
        result = writeDuration(timeoutMs, gain);
        if (!result.isOk()) {
            active_ = false;
            ++generation_;
            changed_.notify_all();
            writeDuration(0);
            return result;
        }
        // The driver can loop RAM playback beyond its requested duration.
        deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        active_ = true;
        ++generation_;
        changed_.notify_all();
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus writeWaveform(int32_t waveform) {
        char value[16];
        // waveform_store uses "%x". Decimal-looking "17" selects 0x17,
        // which is outside the 20-entry firmware, NOT waveform 17.
        std::snprintf(value, sizeof(value), "0x%x", waveform);
        if (!::android::base::WriteStringToFile(value,
                                               "/sys/class/timed_output/vibrator/waveform")) {
            PLOG(ERROR) << "Cannot select Awinic waveform";
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
        }
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus writeGain(int32_t gain) {
        char value[16];
        // gain_store parses hexadecimal, unlike enable's decimal duration.
        std::snprintf(value, sizeof(value), "0x%x", gain);
        if (!::android::base::WriteStringToFile(value, "/sys/class/timed_output/vibrator/gain")) {
            PLOG(ERROR) << "Cannot set Awinic amplitude";
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
        }
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus writeDuration(int32_t durationMs, int32_t gain = kDefaultGain) {
        // Meizu enable_store parses "%d %d %d": milliseconds, gain, stop delay.
        const auto value = std::to_string(durationMs) + " " + std::to_string(gain) + " 0";
        if (!::android::base::WriteStringToFile(value,
                                               "/sys/class/timed_output/vibrator/enable")) {
            PLOG(ERROR) << "Cannot control Awinic motor";
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
        }
        return ndk::ScopedAStatus::ok();
    }
    void runTimer() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!stopping_) {
            if (!active_) {
                changed_.wait(lock, [this] { return stopping_ || active_; });
                continue;
            }
            const auto generation = generation_;
            const auto wake = composing_ ? std::min(deadline_, std::chrono::steady_clock::now() +
                                                             std::chrono::milliseconds(2)) : deadline_;
            if (changed_.wait_until(lock, wake, [this, generation] {
                    return stopping_ || generation_ != generation;
                })) continue;
            if (composing_ && !tiktap_.finished()) {
                if (std::chrono::steady_clock::now() < deadline_) continue;
                LOG(ERROR) << "Tiktap completion watchdog expired";
                tiktap_.stop();
            }
            active_ = false;
            composing_ = false;
            writeDuration(0);
            auto callback = std::move(callback_);
            lock.unlock();
            if (callback) callback->onComplete();
            lock.lock();
        }
    }
    std::mutex mutex_;
    std::condition_variable changed_;
    std::chrono::steady_clock::time_point deadline_;
    uint64_t generation_ = 0;
    bool active_ = false;
    bool stopping_ = false;
    bool composing_ = false;
    const bool compositionAvailable_;
    meizu::TiktapProcess tiktap_;
    std::shared_ptr<IVibratorCallback> callback_;
    std::thread worker_;
};
}  // namespace aidl::android::hardware::vibrator

int main(int argc, char** argv) {
    ::android::base::InitLogging(argv, ::android::base::KernelLogger);
    if (argc > 1 && std::string(argv[1]) == "--tiktap-worker") _exit(meizu::tiktapWorker(argc, argv));
    if (argc > 1 && std::string(argv[1]) == "--stop-tiktap") {
        meizu::TiktapProcess::stopHardware();
        return 0;
    }
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    auto vibrator = ndk::SharedRefBase::make<aidl::android::hardware::vibrator::Vibrator>();
    CHECK(vibrator->off().isOk()) << "Awinic timed_output is unavailable";
    CHECK(vibrator->setAmplitude(1.0f).isOk()) << "Awinic gain is unavailable";
    const std::string instance = std::string(vibrator->descriptor) + "/default";
    CHECK_EQ(AServiceManager_addService(vibrator->asBinder().get(), instance.c_str()), STATUS_OK);
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
