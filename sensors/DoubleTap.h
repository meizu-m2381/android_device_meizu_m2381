// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <V2_1/SubHal.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <log/log.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace android::hardware::sensors::V2_1::implementation {
using V1_0::Result;
constexpr int32_t kDoubleTap = 0x00f00002;
constexpr auto kDoubleTapType = static_cast<SensorType>(65541);
constexpr int kDoubleTapKey = 624; // stock Goodix DOUBLE-TAP 0xcc -> Meizu 0x270

// No event-number assumptions, exclusive grab, uinput, or touch-coordinate synthesis.
inline int openDoubleTapInput() {
    DIR* directory = opendir("/dev/input");
    if (!directory) return -1;
    int found = -1;
    while (auto* entry = readdir(directory)) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;
        std::string path = std::string("/dev/input/") + entry->d_name;
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        char name[128]{};
        unsigned long keys[(KEY_MAX + 8 * sizeof(unsigned long)) /
                           (8 * sizeof(unsigned long))]{};
        constexpr auto bits = 8 * sizeof(unsigned long);
        int clock = CLOCK_BOOTTIME;
        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0 &&
            std::string(name) == "main_touch" &&
            ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) >= 0 &&
            (keys[kDoubleTapKey / bits] & (1UL << (kDoubleTapKey % bits))) &&
            ioctl(fd, EVIOCSCLOCKID, &clock) == 0) {
            found = fd;
            break;
        }
        close(fd);
    }
    closedir(directory);
    return found;
}

class DoubleTap {
public:
    using Opener = std::function<int()>;
    explicit DoubleTap(Opener opener = openDoubleTapInput) : opener_(std::move(opener)) {
        wake_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (wake_ >= 0) worker_ = std::thread([this] { dispatch(); });
    }
    ~DoubleTap() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            notify();
        }
        if (worker_.joinable()) worker_.join();
        if (input_ >= 0) close(input_);
        if (wake_ >= 0) close(wake_);
    }
    static SensorInfo info() {
        SensorInfo sensor{};
        sensor.sensorHandle = kDoubleTap;
        sensor.name = "Meizu Double Tap Gesture";
        sensor.vendor = "meizu";
        sensor.version = 1;
        sensor.type = kDoubleTapType;
        sensor.typeAsString = "com.meizu.sensor.double_tap";
        sensor.maxRange = sensor.resolution = 1;
        sensor.minDelay = -1;
        sensor.flags = 5; // wake-up, one-shot
        return sensor;
    }
    void initialize(const sp<IHalProxyCallback>& callback) {
        std::lock_guard lock(mutex_);
        callback_ = callback;
        cancel();
    }
    Result activate(bool enabled) {
        std::lock_guard lock(mutex_);
        cancel();
        if (!enabled) return Result::OK;
        if (wake_ < 0 || callback_ == nullptr) return Result::INVALID_OPERATION;
        int fd = opener_();
        if (fd < 0) {
            ALOGE("Cannot open main_touch for double tap: %s", strerror(errno));
            return Result::INVALID_OPERATION;
        }
        // Evdev opens at the current position. A test transport or re-used fd
        // may contain old input; never replay it into a fresh one-shot request.
        input_event discarded[32];
        while (read(fd, discarded, sizeof(discarded)) > 0) {}
        input_ = fd;
        timespec time{};
        clock_gettime(CLOCK_BOOTTIME, &time);
        armedAt_ = time.tv_sec * INT64_C(1000000000) + time.tv_nsec;
        notify();
        return Result::OK;
    }
private:
    void notify() {
        uint64_t one = 1;
        if (wake_ >= 0) {
            ssize_t result = write(wake_, &one, sizeof(one));
            (void)result;
        }
    }
    void cancel() {
        ++generation_;
        if (input_ >= 0) close(input_);
        input_ = -1;
        down_ = dropped_ = false;
        notify();
    }
    void dispatch() {
        for (;;) {
            int fd;
            uint64_t generation;
            {
                std::lock_guard lock(mutex_);
                if (stopping_) return;
                // poll owns a duplicate: cancellation cannot recycle its fd.
                fd = input_ < 0 ? -1 : fcntl(input_, F_DUPFD_CLOEXEC, 0);
                generation = generation_;
            }
            pollfd wait[]{{wake_, POLLIN, 0}, {fd, POLLIN, 0}};
            int result;
            do { result = poll(wait, 2, -1); } while (result < 0 && errno == EINTR);
            sp<IHalProxyCallback> callback;
            Event event{};
            std::optional<V2_0::implementation::ScopedWakelock> wakelock;
            {
                std::lock_guard lock(mutex_);
                if (wait[0].revents & POLLIN) {
                    uint64_t count;
                    ssize_t readResult = read(wake_, &count, sizeof(count));
                    (void)readResult;
                }
                if (result < 0) {
                    if (fd >= 0) close(fd);
                    return;
                }
                if (input_ >= 0 && generation == generation_) {
                    if (wait[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                        ALOGE("Double tap input disconnected; canceling request");
                        cancel();
                    } else if (wait[1].revents & POLLIN) {
                        input_event events[32];
                        ssize_t bytes = read(fd, events, sizeof(events));
                        for (size_t i = 0; bytes > 0 && i < size_t(bytes) / sizeof(input_event); ++i) {
                            const auto& input = events[i];
                            int64_t timestamp = input.time.tv_sec * INT64_C(1000000000) +
                                                input.time.tv_usec * 1000;
                            if (input.type == EV_SYN && input.code == SYN_DROPPED) {
                                dropped_ = true;
                                down_ = false;
                            } else if (input.type == EV_SYN && input.code == SYN_REPORT) {
                                if (down_ && !dropped_) {
                                    callback = callback_;
                                    event.timestamp = timestamp_;
                                    event.sensorHandle = kDoubleTap;
                                    event.sensorType = kDoubleTapType;
                                    event.u.scalar = 1;
                                    wakelock = callback->createScopedWakelock(true);
                                    cancel();
                                    break;
                                }
                                down_ = dropped_ = false;
                            } else if (!dropped_ && input.type == EV_KEY &&
                                       input.code == kDoubleTapKey && input.value == 1 &&
                                       timestamp >= armedAt_) {
                                down_ = true;
                                timestamp_ = timestamp;
                            }
                        }
                    }
                }
            }
            if (fd >= 0) close(fd);
            if (callback != nullptr) {
                ALOGI("Forwarding stock double tap to SystemUI");
                callback->postEvents({event}, std::move(*wakelock));
            }
        }
    }
    Opener opener_;
    int wake_ = -1, input_ = -1;
    uint64_t generation_ = 0;
    int64_t armedAt_ = 0, timestamp_ = 0;
    bool down_ = false, dropped_ = false, stopping_ = false;
    sp<IHalProxyCallback> callback_;
    std::mutex mutex_;
    std::thread worker_;
};
}
