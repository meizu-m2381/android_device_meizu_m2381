// SPDX-License-Identifier: Apache-2.0
#include "Tiktap.h"
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android/binder_status.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <thread>

extern char** environ;
namespace meizu {
using namespace aidl::android::hardware::vibrator;
using namespace std::chrono_literals;
namespace {
constexpr char kExecutable[] = "/vendor/bin/hw/android.hardware.vibrator-service.meizu";
constexpr char kLibrary[] = "/vendor/lib64/hw/tiktap_algo.default.so";
constexpr char kNode[] = "/proc/tiktap_buf";
constexpr int kMaxDuration = kCompositionSize * (kCompositionDelay + 100);
void stopRtp() {
    const int fd = open(kNode, O_RDWR | O_CLOEXEC);
    if (fd < 0) { PLOG(ERROR) << "Cannot open Tiktap stop node"; return; }
    int ret;
    do { ret = ioctl(fd, 0xff0b, 0); } while (ret < 0 && errno == EINTR);
    if (ret < 0) PLOG(ERROR) << "Cannot stop Tiktap RTP";
    close(fd);
}
int scaled(int intensity, float scale) {
    // Android scale=0 means minimum perceptible output, not the stock
    // algorithm's amplitude=0 sentinel (which selects FULL default peaks).
    return std::max(1, static_cast<int>(std::lround(intensity * (0.1f + 0.9f * scale))));
}
void curve(std::vector<int64_t>* data, int start, int duration, int t1, int t2,
           int a0, int a1, int a2, int frequency, float scale) {
    data->insert(data->end(), {4, start, a0 ? scaled(a0, scale) : 0, frequency,
                             start + t1, scaled(a1, scale), frequency,
                             start + t2, scaled(a2, scale), frequency,
                             start + duration, 0, frequency});
}
bool number(const char* text, int64_t* result) {
    char* end = nullptr;
    errno = 0;
    const auto value = strtoll(text, &end, 10);
    if (errno || !*text || *end || value < 0 || value > kMaxDuration) return false;
    *result = value;
    return true;
}
}  // namespace

int primitiveDuration(CompositePrimitive primitive) {
    switch (primitive) {
        case CompositePrimitive::NOOP: return 0;
        // The stock transient at frequency=50 has 467 samples at 24 kHz.
        case CompositePrimitive::CLICK: return 20;
        case CompositePrimitive::THUD: return 60;
        case CompositePrimitive::SPIN: return 100;
        // Stock model waves 15/3 include the opposite-phase braking tail.
        case CompositePrimitive::LIGHT_TICK: return 12;
        // The 23.5 ms stock pulse needs a quiet tail between slider ticks.
        // Its complete waveform is retained; the engine zero-fills until the
        // next primitive at 36 ms, leaving about 12.5 ms to separate grains.
        case CompositePrimitive::LOW_TICK: return 36;
        case CompositePrimitive::QUICK_RISE: return 30;
        case CompositePrimitive::SLOW_RISE: return 100;
        case CompositePrimitive::QUICK_FALL: return 25;
        default: return -1;
    }
}

int makeComposition(const std::vector<CompositeEffect>& effects, Composition* result) {
    if (effects.empty() || effects.size() > kCompositionSize) return EX_ILLEGAL_ARGUMENT;
    Composition plan;
    // One pass, no loop interval; retain the existing 128 amplitude baseline.
    plan.data = {1, 0, 128, 50, 0};
    bool first = true;
    for (const auto& effect : effects) {
        const int duration = primitiveDuration(effect.primitive);
        if (duration < 0) return EX_UNSUPPORTED_OPERATION;
        if (effect.delayMs < 0 || effect.delayMs > kCompositionDelay ||
            !std::isfinite(effect.scale) || effect.scale < 0 || effect.scale > 1) {
            return EX_ILLEGAL_ARGUMENT;
        }
        plan.durationMs += effect.delayMs;
        if (effect.primitive == CompositePrimitive::NOOP) continue;
        if (first) { plan.leadingMs = plan.durationMs; first = false; }
        const int start = plan.durationMs - plan.leadingMs;
        ++plan.data[4];
        switch (effect.primitive) {
            case CompositePrimitive::CLICK:
                plan.data.insert(plan.data.end(), {1, start, scaled(100, effect.scale), 50});
                break;
            case CompositePrimitive::THUD:
                // A low-frequency impact with a longer, decaying body.
                curve(&plan.data, start, duration, 6, 18, 0, 100, 35, 20, effect.scale);
                break;
            case CompositePrimitive::SPIN:
                // Build momentum with a rising pitch and energy, then release.
                // Point frequencies are stock abstract controls, not hertz.
                plan.data.insert(plan.data.end(),
                                 {4, start, 0, 10,
                                  start + 25, scaled(70, effect.scale), 30,
                                  start + 70, scaled(100, effect.scale), 80,
                                  start + duration, 0, 100});
                break;
            case CompositePrimitive::LIGHT_TICK:
                plan.data.insert(plan.data.end(), {1, start, scaled(25, effect.scale), 100});
                break;
            case CompositePrimitive::LOW_TICK:
                plan.data.insert(plan.data.end(), {1, start, scaled(35, effect.scale), 20});
                break;
            case CompositePrimitive::QUICK_RISE:
                curve(&plan.data, start, duration, 12, 25, 0, 30, 100, 50, effect.scale);
                break;
            case CompositePrimitive::SLOW_RISE:
                curve(&plan.data, start, duration, 40, 90, 0, 25, 100, 50, effect.scale);
                break;
            case CompositePrimitive::QUICK_FALL:
                curve(&plan.data, start, duration, 3, 12, 100, 85, 25, 50, effect.scale);
                break;
            default: break;
        }
        plan.durationMs += duration;
    }
    if (first) plan.leadingMs = plan.durationMs;
    *result = std::move(plan);
    return 0;
}

bool TiktapProcess::available() {
    return access(kNode, R_OK | W_OK) == 0 && access(kLibrary, R_OK) == 0 &&
           ::android::base::GetProperty("init.svc.vendor.tiktap_hidl-1-0", "stopped") != "running";
}

void TiktapProcess::stopHardware() { stopRtp(); }

bool TiktapProcess::start(const Composition& composition) {
    stop();
    std::vector<std::string> args = {kExecutable, "--tiktap-worker", std::to_string(getpid()),
                                   std::to_string(composition.durationMs),
                                   std::to_string(composition.leadingMs)};
    for (auto value : composition.data) args.push_back(std::to_string(value));
    std::vector<char*> argv;
    for (auto& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    const int error = posix_spawn(&pid_, kExecutable, nullptr, nullptr, argv.data(), environ);
    if (error) {
        pid_ = -1;
        LOG(ERROR) << "Cannot start isolated Tiktap producer: " << error;
        return false;
    }
    return true;
}

void TiktapProcess::stop() {
    if (pid_ <= 0) return;
    kill(pid_, SIGKILL);
    while (waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {}
    pid_ = -1;
    stopRtp();
}

bool TiktapProcess::finished() {
    if (pid_ <= 0) return true;
    int status = 0;
    const pid_t reaped = waitpid(pid_, &status, WNOHANG);
    if (!reaped || (reaped < 0 && errno == EINTR)) return false;
    pid_ = -1;
    if (reaped < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        LOG(ERROR) << "Tiktap producer failed: " << status;
    }
    // Also cover a producer crash before its own off().
    stopRtp();
    return true;
}

int tiktapWorker(int argc, char** argv) {
    if (argc < 10 || argc > 5 + 5 + kCompositionSize * 13) return 2;
    char* parentEnd;
    const auto parent = strtol(argv[2], &parentEnd, 10);
    if (*parentEnd || parent <= 1 || prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent) return 2;
    int64_t duration, leading;
    if (!number(argv[3], &duration) || !number(argv[4], &leading) || leading > duration) return 2;
    std::vector<int64_t> data;
    for (int i = 5; i < argc; ++i) {
        int64_t value;
        if (!number(argv[i], &value)) return 2;
        data.push_back(value);
    }
    if (data[0] != 1 || data[1] || data[2] != 128 || data[3] != 50 ||
        data[4] > kCompositionSize) return 2;
    size_t offset = 5;
    for (int64_t event = 0; event < data[4]; ++event) {
        if (offset >= data.size()) return 2;
        const auto points = data[offset++];
        if ((points != 1 && points != 4) || offset + points * 3 > data.size()) return 2;
        int64_t last = -1;
        for (int64_t point = 0; point < points; ++point) {
            if (data[offset] < last || data[offset] > duration - leading ||
                data[offset + 1] > 100 || data[offset + 2] > 100) return 2;
            last = data[offset];
            offset += 3;
        }
    }
    if (offset != data.size()) return 2;
    std::this_thread::sleep_for(std::chrono::milliseconds(leading));
    if (!data[4]) return 0;  // NOOP must not energize the motor.
    void* library = dlopen(kLibrary, RTLD_NOW | RTLD_LOCAL);
    if (!library) { LOG(ERROR) << dlerror(); return 3; }
    const auto init = reinterpret_cast<int (*)(const void*, const char*, void**)>(
            dlsym(library, "_Z11tiktap_openPK11hw_module_tPKcPP11hw_device_t"));
    const auto on = reinterpret_cast<int (*)(void*, void*, int)>(dlsym(library, "_Z2onP6tiktapPvi"));
    const auto off = reinterpret_cast<int (*)(void*)>(dlsym(library, "_Z3offP6tiktap"));
    void* module = dlsym(library, "HMI");
    const auto ring = reinterpret_cast<volatile unsigned char**>(dlsym(library, "start_buf"));
    void* device = nullptr;
    if (!init || !on || !off || !module || !ring || init(module, "tiktap", &device) || !device) return 3;
    if (on(device, data.data(), data.size() * sizeof(int64_t))) { off(device); return 4; }
    // A consumed slot is NOT completion: it may only have filled the chip FIFO.
    // Anchor the conservative full-duration deadline after consumption starts,
    // then synchronously stop hardware before this process reports completion.
    const auto waitUntil = std::chrono::steady_clock::now() + 150ms;
    bool consumed = false;
    while (std::chrono::steady_clock::now() < waitUntil && !consumed) {
        if (*ring) for (int slot = 0; slot < 16; ++slot) {
            if ((*ring)[slot * 1024] == 0xaa) { consumed = true; break; }
        }
        if (!consumed) std::this_thread::sleep_for(1ms);
    }
    if (!consumed) { off(device); LOG(ERROR) << "Tiktap never consumed first buffer"; return 5; }
    std::this_thread::sleep_for(std::chrono::milliseconds(duration - leading) + 10ms);
    off(device);
    // Do not dlclose: the proprietary library may still have detached workers.
    return 0;
}
}  // namespace meizu
