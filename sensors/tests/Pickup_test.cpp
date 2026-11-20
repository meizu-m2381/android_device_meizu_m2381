// SPDX-License-Identifier: Apache-2.0
// Standalone device regression binary; includes the same adapter as production.
#include "../Pickup.cpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sys/socket.h>

using namespace android;
using namespace android::hardware;
using namespace android::hardware::sensors;
using namespace android::hardware::sensors::V2_1;
using namespace android::hardware::sensors::V2_1::implementation;
using namespace std::chrono_literals;

namespace android::hardware::sensors::V2_0::implementation {
// The upstream ScopedWakelock interface exposes this friend for test factories.
class ScopedWakelockTest {
public:
    static ScopedWakelock make(IScopedWakelockRefCounter* counter, bool locked) {
        return ScopedWakelock(counter, locked);
    }
};
}

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); abort(); } } while (0)

class Counter : public V2_0::implementation::IScopedWakelockRefCounter {
public:
    bool incrementRefCountAndMaybeAcquireWakelock(size_t n, int64_t* start) override {
        count.fetch_add(n);
        if (start) *start = V2_0::implementation::getTimeNow();
        return true;
    }
    void decrementRefCountAndMaybeReleaseWakelock(size_t n, int64_t) override {
        count.fetch_sub(n);
    }
    std::atomic<size_t> count{0};
};

class Sink : public IHalProxyCallback {
public:
    Return<void> onDynamicSensorsConnected(const hidl_vec<V1_0::SensorInfo>&) override { return Void(); }
    Return<void> onDynamicSensorsConnected_2_1(const hidl_vec<SensorInfo>&) override { return Void(); }
    Return<void> onDynamicSensorsDisconnected(const hidl_vec<int32_t>&) override { return Void(); }
    ScopedWakelock createScopedWakelock(bool locked) override {
        return V2_0::implementation::ScopedWakelockTest::make(counter.get(), locked);
    }
    void postEvents(const std::vector<Event>& events, ScopedWakelock wakelock) override {
        bool wake = false;
        for (auto& e : events) wake |= e.sensorHandle != 11;
        REQUIRE(wake == wakelock.isLocked());
        std::lock_guard lock(mutex);
        received.insert(received.end(), events.begin(), events.end());
        ready.notify_one();
    }
    std::vector<Event> until(int64_t marker) {
        std::unique_lock lock(mutex);
        REQUIRE(ready.wait_for(lock, 2s, [&] {
            for (auto& e : received) if (e.timestamp == marker && e.sensorHandle == 11) return true;
            return false;
        }));
        auto out = std::move(received);
        received.clear();
        return out;
    }
    std::vector<Event> waitForCount(size_t count) {
        std::unique_lock lock(mutex);
        REQUIRE(ready.wait_for(lock, 2s, [&] { return received.size() >= count; }));
        auto out = std::move(received);
        received.clear();
        return out;
    }
    void quiet() {
        std::unique_lock lock(mutex);
        REQUIRE(!ready.wait_for(lock, 50ms, [&] { return !received.empty(); }));
    }
    sp<Counter> counter = sp<Counter>::make();
    std::mutex mutex;
    std::condition_variable ready;
    std::vector<Event> received;
};

class Stock : public ISensorsSubHal {
public:
    Return<void> getSensorsList_2_1(getSensorsList_2_1_cb cb) override {
        SensorInfo accel{}; accel.sensorHandle = 11; accel.type = SensorType::ACCELEROMETER;
        SensorInfo raise{}; raise.sensorHandle = 0xa0020;
        raise.type = static_cast<SensorType>(65539);
        raise.typeAsString = "com.meizu.sensor.raise"; raise.flags = 3;
        SensorInfo prox{}; prox.sensorHandle = 0x52; prox.type = SensorType::PROXIMITY;
        prox.flags = 3; prox.maxRange = 5;
        cb(hidl_vec<SensorInfo>{accel, raise, prox}); return Void();
    }
    const std::string getName() override { return "test SSC"; }
    Return<Result> initialize(const sp<IHalProxyCallback>& cb) override {
        callback = cb; enabled.clear(); return Result::OK;
    }
    Return<Result> activate(int32_t h, bool enabled) override {
        if (h == failActivate) return Result::BAD_VALUE;
        lastHandle = h; this->enabled[h] = enabled; return Result::OK;
    }
    Return<Result> batch(int32_t h, int64_t period, int64_t latency) override {
        if (h == failBatch) return Result::BAD_VALUE;
        lastHandle = h; lastLatency = latency;
        periods[h] = period; latencies[h] = latency; return Result::OK;
    }
    Return<Result> flush(int32_t) override { return Result::OK; }
    Return<Result> injectSensorData_2_1(const Event&) override { return Result::INVALID_OPERATION; }
    Return<Result> setOperationMode(OperationMode) override { return Result::OK; }
    Return<void> registerDirectChannel(const SharedMemInfo&, registerDirectChannel_cb cb) override {
        cb(Result::INVALID_OPERATION, -1); return Void();
    }
    Return<Result> unregisterDirectChannel(int32_t) override { return Result::INVALID_OPERATION; }
    Return<void> configDirectReport(int32_t, int32_t, RateLevel, configDirectReport_cb cb) override {
        cb(Result::INVALID_OPERATION, 0); return Void();
    }
    Return<void> debug(const hidl_handle&, const hidl_vec<hidl_string>&) override { return Void(); }
    void emit(float action, int64_t timestamp, int64_t marker) {
        Event raise{}; raise.sensorHandle = 0xa0020;
        raise.sensorType = static_cast<SensorType>(65539);
        raise.timestamp = timestamp; raise.u.data[0] = action;
        Event accel{}; accel.sensorHandle = 11; accel.sensorType = SensorType::ACCELEROMETER;
        accel.timestamp = marker;
        callback->postEvents({raise, accel}, callback->createScopedWakelock(true));
    }
    void proximity(float distance, int64_t timestamp, int64_t marker) {
        Event prox{}; prox.sensorHandle = 0x52; prox.sensorType = SensorType::PROXIMITY;
        prox.timestamp = timestamp; prox.u.scalar = distance;
        Event accel{}; accel.sensorHandle = 11; accel.sensorType = SensorType::ACCELEROMETER;
        accel.timestamp = marker;
        callback->postEvents({prox, accel}, callback->createScopedWakelock(true));
    }
    sp<IHalProxyCallback> callback;
    std::map<int32_t, bool> enabled;
    std::map<int32_t, int64_t> periods, latencies;
    int32_t failActivate = -1, failBatch = -1;
    int32_t lastHandle = 0;
    int64_t lastLatency = -1;
};

void testDoubleTap() {
    int sockets[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sockets) == 0);
    auto sink = sp<Sink>::make();
    DoubleTap touch([&] { return dup(sockets[0]); });
    touch.initialize(sink);
    auto send = [&](uint16_t type, uint16_t code, int32_t value, int64_t timestamp = 0) {
        input_event event{};
        int64_t time = timestamp ? timestamp : nowNs();
        event.time.tv_sec = time / 1000000000;
        event.time.tv_usec = (time % 1000000000) / 1000;
        event.type = type; event.code = code; event.value = value;
        REQUIRE(write(sockets[1], &event, sizeof(event)) == sizeof(event));
    };
    auto report = [&] { send(EV_SYN, SYN_REPORT, 0); };
    auto gesture = [&] { send(EV_KEY, kDoubleTapKey, 1); report(); };
    const auto info = DoubleTap::info();
    REQUIRE(info.sensorHandle == kDoubleTap && info.flags == 5 && info.minDelay == -1);
    REQUIRE(info.typeAsString == "com.meizu.sensor.double_tap");
    gesture(); // old events must not fire when the interface is armed
    REQUIRE(touch.activate(true) == Result::OK);
    sink->quiet();
    send(EV_KEY, 625, 1); report(); // single tap, not double tap
    send(EV_KEY, 626, 1); report(); // fingerprint press
    send(EV_KEY, kDoubleTapKey, 0); report(); // key release
    send(EV_KEY, kDoubleTapKey, 2); report(); // repeat
    send(EV_KEY, kDoubleTapKey, 1, 1); report(); // old clock timestamp
    sink->quiet();
    gesture();
    auto events = sink->waitForCount(1);
    REQUIRE(events.size() == 1 && events[0].sensorHandle == kDoubleTap);
    REQUIRE(events[0].sensorType == kDoubleTapType && events[0].u.scalar == 1);
    gesture(); sink->quiet(); // one-shot
    REQUIRE(touch.activate(true) == Result::OK);
    REQUIRE(touch.activate(false) == Result::OK);
    gesture(); sink->quiet(); // cancellation
    REQUIRE(touch.activate(true) == Result::OK);
    send(EV_KEY, kDoubleTapKey, 1);
    sink->quiet(); // incomplete frame cannot trigger
    send(EV_SYN, SYN_DROPPED, 0); report();
    sink->quiet(); // dropped frame cannot trigger
    gesture();
    REQUIRE(sink->waitForCount(1).size() == 1);
    touch.initialize(sink);
    gesture(); sink->quiet();
    DoubleTap missing([] { return -1; });
    missing.initialize(sink);
    REQUIRE(missing.activate(true) == Result::INVALID_OPERATION);
    close(sockets[0]); close(sockets[1]);
    printf("PASS: double tap evdev code/frame filtering, one-shot, cancel, stale input, SYN_DROPPED, missing device, wake-lock ownership\n");
}

int main() {
    testDoubleTap();
    Stock stock;
    Pickup adapter(&stock);
    auto sink = sp<Sink>::make();
    REQUIRE(adapter.initialize(sink) == Result::OK);
    adapter.getSensorsList_2_1([](const auto& list) {
        REQUIRE(list.size() == 5);
        REQUIRE(list[1].typeAsString == "com.meizu.sensor.raise");
        REQUIRE(list[3].type == SensorType::PICK_UP_GESTURE && list[3].flags == 5);
        REQUIRE(list[3].minDelay == -1 && list[3].fifoMaxEventCount == 0);
    });
    int64_t marker = 1;
    auto proximity = [&](float distance, int64_t timestamp = 0) {
        stock.proximity(distance, timestamp ? timestamp : nowNs(), ++marker);
        return sink->until(marker);
    };
    auto arm = [&] {
        REQUIRE(adapter.activate(kPickup, true) == Result::OK);
        REQUIRE(stock.enabled[0xa0020] && stock.enabled[0x52]);
        REQUIRE(proximity(5).size() == 1); // internal proximity remains private
    };
    auto emit = [&](float action, int64_t timestamp = 0) {
        stock.emit(action, timestamp ? timestamp : nowNs(), ++marker);
        return sink->until(marker);
    };
    arm();
    REQUIRE(emit(7).size() == 1); // placement/status is not pickup
    REQUIRE(emit(2).size() == 1); // stock excludes 2 after 7
    auto events = emit(6);
    REQUIRE(events.size() == 2 && events[0].sensorHandle == kPickup && events[0].u.scalar == 1);
    REQUIRE(!stock.enabled[0xa0020] && !stock.enabled[0x52]);
    REQUIRE(emit(6).size() == 1); // exactly once until rearmed
    arm();
    REQUIRE(emit(6, 1).size() == 1); // reject stale vendor event after rearm
    REQUIRE(emit(6).size() == 2);
    arm();
    REQUIRE(adapter.activate(kPickup, false) == Result::OK);
    REQUIRE(emit(6).size() == 1 && !stock.enabled[0xa0020] && !stock.enabled[0x52]);
    REQUIRE(adapter.activate(0xa0020, true) == Result::OK);
    arm();
    events = emit(6);
    REQUIRE(events.size() == 3 && events[0].sensorHandle == 0xa0020);
    REQUIRE(stock.enabled[0xa0020]); // private client still owns its subscription
    REQUIRE(adapter.activate(0xa0020, false) == Result::OK && !stock.enabled[0xa0020]);
    arm();
    REQUIRE(emit(1).size() == 1);
    REQUIRE(emit(2).size() == 2); // stock accepts 2 after non-6/7
    // A missing or old sample cannot authorize a lift in a new enable period.
    REQUIRE(adapter.activate(kPickup, true) == Result::OK);
    REQUIRE(emit(6).size() == 1);
    REQUIRE(proximity(5, 1).size() == 1);
    REQUIRE(emit(6).size() == 1);
    REQUIRE(proximity(0).size() == 1);
    REQUIRE(proximity(5, nowNs() - 1000000000).size() == 1);
    REQUIRE(emit(6).size() == 1 && stock.enabled[0xa0020]);
    REQUIRE(proximity(5).size() == 1);
    REQUIRE(emit(6).size() == 2); // a covered lift did not consume the one-shot
    arm();
    REQUIRE(proximity(NAN).size() == 1);
    REQUIRE(proximity(5, nowNs() - 1000000000).size() == 1);
    REQUIRE(emit(6).size() == 1);
    REQUIRE(proximity(-1).size() == 1);
    REQUIRE(emit(6).size() == 1);
    REQUIRE(proximity(5).size() == 1);
    REQUIRE(emit(6).size() == 2);
    // Preserve normal proximity delivery and restore its batching requirements.
    REQUIRE(adapter.batch(0x52, 100000000, 1000000000) == Result::OK);
    REQUIRE(adapter.activate(0x52, true) == Result::OK);
    REQUIRE(proximity(0).size() == 2);
    REQUIRE(adapter.activate(kPickup, true) == Result::OK);
    REQUIRE(stock.latencies[0x52] == 0 && stock.periods[0x52] == 100000000);
    REQUIRE(emit(6).size() == 1); // do not discard an existing near sample
    REQUIRE(proximity(5).size() == 2);
    REQUIRE(emit(6).size() == 2);
    REQUIRE(stock.enabled[0x52] && stock.latencies[0x52] == 1000000000);
    REQUIRE(adapter.activate(kPickup, true) == Result::OK);
    REQUIRE(adapter.activate(0x52, false) == Result::OK);
    REQUIRE(stock.enabled[0x52] && stock.latencies[0x52] == 0);
    REQUIRE(proximity(0).size() == 1);
    REQUIRE(emit(6).size() == 1);
    REQUIRE(adapter.activate(kPickup, false) == Result::OK && !stock.enabled[0x52]);
    // If proximity cannot be enabled, the standard pickup must remain disabled.
    stock.failActivate = 0x52;
    REQUIRE(adapter.activate(kPickup, true) == Result::BAD_VALUE);
    REQUIRE(emit(6).size() == 1 && !stock.enabled[0xa0020]);
    stock.failActivate = -1;
    stock.failActivate = 0xa0020;
    REQUIRE(adapter.activate(kPickup, true) == Result::BAD_VALUE);
    REQUIRE(!stock.enabled[0x52]); // rollback the newly acquired proximity
    stock.failActivate = -1;
    stock.failBatch = 0x52;
    REQUIRE(adapter.activate(kPickup, true) == Result::BAD_VALUE);
    REQUIRE(!stock.enabled[0x52] && emit(6).size() == 1);
    stock.failBatch = -1;
    arm();
    REQUIRE(adapter.activate(0x52, true) == Result::OK);
    events = emit(7);
    REQUIRE(events.size() == 2 && events[0].sensorHandle == 0x52 && events[0].u.scalar == 5);
    REQUIRE(adapter.activate(0x52, false) == Result::OK);
    REQUIRE(proximity(0).size() == 1);
    REQUIRE(adapter.activate(0x52, true) == Result::OK);
    events = emit(7);
    REQUIRE(events.size() == 2 && events[0].sensorHandle == 0x52 && events[0].u.scalar == 0);
    REQUIRE(emit(6).size() == 1);
    REQUIRE(proximity(5).size() == 2);
    REQUIRE(emit(6).size() == 2);
    REQUIRE(stock.enabled[0x52]);
    REQUIRE(adapter.activate(0x52, false) == Result::OK && !stock.enabled[0x52]);
    REQUIRE(adapter.batch(kPickup, 0, 1000000000) == Result::OK);
    REQUIRE(stock.lastHandle == 0xa0020 && stock.lastLatency == 0);
    REQUIRE(adapter.flush(kPickup) == Result::BAD_VALUE);
    Event injected{};
    injected.sensorHandle = kPickup;
    injected.sensorType = SensorType::PICK_UP_GESTURE;
    REQUIRE(adapter.injectSensorData_2_1(injected) == Result::INVALID_OPERATION);
    bool directRejected = false;
    adapter.configDirectReport(kPickup, 1, RateLevel::NORMAL,
            [&](Result result, int32_t token) {
                directRejected = result == Result::BAD_VALUE && token == 0;
            });
    REQUIRE(directRejected);
    printf("PASS: metadata, actions, one-shot/cancel/stale, proximity near/unknown/invalid, shared subscriptions/batching, failure rollback, wake-lock ownership\n");
}
