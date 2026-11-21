// SPDX-License-Identifier: Apache-2.0
#include <aidl/android/hardware/vibrator/IVibrator.h>
#include <aidl/android/hardware/vibrator/BnVibratorCallback.h>
#include <android-base/file.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <mutex>
#include <thread>
#include <vector>

using namespace aidl::android::hardware::vibrator;
using namespace std::chrono_literals;

namespace {
constexpr char kRoot[] = "/sys/class/timed_output/vibrator/";
int failures = 0;

void check(bool ok, const std::string& message) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", message.c_str());
    if (!ok) ++failures;
}

std::string read(const char* node) {
    std::string value;
    if (!android::base::ReadFileToString(std::string(kRoot) + node, &value)) {
        check(false, std::string("read ") + node);
    }
    return value;
}

int sequence() {
    int value = -1;
    const auto text = read("seq");
    std::sscanf(text.c_str(), "seq1 = %d", &value);
    return value;
}

int gain() {
    // gain_show reports cached driver state. Verify the chip's DATDBG
    // register instead, so an inherited or overwritten gain cannot pass.
    unsigned value = 0;
    const auto text = read("reg");
    const auto position = text.find("reg:0x39=");
    if (position == std::string::npos) return -1;
    std::sscanf(text.c_str() + position, "reg:0x39=0x%x", &value);
    return static_cast<int>(value);
}

bool idle() {
    // Read-only register dump. GO and GLB_STATE must both indicate standby.
    // The driver's "state" node is only software state, not chip completion.
    const auto registers = read("reg");
    return registers.find("reg:0x05=0x00") != std::string::npos &&
           registers.find("reg:0x46=0x00") != std::string::npos;
}

class Completion final : public BnVibratorCallback {
  public:
    ndk::ScopedAStatus onComplete() override {
        std::lock_guard<std::mutex> lock(mutex_);
        ++count_;
        changed_.notify_all();
        return ndk::ScopedAStatus::ok();
    }
    bool wait(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, timeout, [this] { return count_ > 0; });
    }
    int count() {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }
  private:
    std::mutex mutex_;
    std::condition_variable changed_;
    int count_ = 0;
};

void checkComposition(const std::shared_ptr<IVibrator>& vibrator,
                      const std::vector<CompositePrimitive>& primitives) {
    int32_t maxSize = 0, maxDelay = 0;
    check(vibrator->getCompositionSizeMax(&maxSize).isOk() && maxSize >= 5,
          "composition holds five slider ticks");
    check(vibrator->getCompositionDelayMax(&maxDelay).isOk() && maxDelay >= 100,
          "composition supports delayed effects");
    for (auto primitive : primitives) {
        int32_t duration = -1;
        const auto label = "primitive " + std::to_string(static_cast<int>(primitive));
        check(vibrator->getPrimitiveDuration(primitive, &duration).isOk() &&
                      (primitive == CompositePrimitive::NOOP ? duration == 0 : duration > 0),
              label + " reports duration");
        auto callback = ndk::SharedRefBase::make<Completion>();
        CompositeEffect effect;
        effect.primitive = primitive;
        effect.scale = 0.3f;
        effect.delayMs = 15;
        const auto start = std::chrono::steady_clock::now();
        check(vibrator->compose({effect}, callback).isOk(), label + " accepted");
        check(callback->wait(std::chrono::milliseconds(duration + 1000)), label + " callback arrives");
        const auto elapsed = std::chrono::steady_clock::now() - start;
        std::printf("TIMING %s callback=%lldms timeline=%dms\n", label.c_str(),
                    static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()),
                    duration + effect.delayMs);
        check(elapsed >= std::chrono::milliseconds(duration + effect.delayMs),
              label + " callback does not precede timeline end");
        check(idle(), label + " chip idle after callback");
        check(callback->count() == 1, label + " callback occurs once");
    }

    CompositeEffect tick;
    tick.primitive = CompositePrimitive::LOW_TICK;
    tick.scale = 0.3f;
    int32_t tickDuration = 0;
    vibrator->getPrimitiveDuration(tick.primitive, &tickDuration);
    auto callback = ndk::SharedRefBase::make<Completion>();
    const auto start = std::chrono::steady_clock::now();
    check(vibrator->compose(std::vector<CompositeEffect>(5, tick), callback).isOk(),
          "five LOW_TICK match SystemUI continuous slider request");
    check(callback->wait(1000ms) && std::chrono::steady_clock::now() - start >=
                                      std::chrono::milliseconds(5 * tickDuration),
          "five-tick sequence completes after its full timeline");
    check(idle(), "five-tick sequence leaves chip idle");

    tick.delayMs = maxDelay;
    auto cancelled = ndk::SharedRefBase::make<Completion>();
    check(vibrator->compose({tick}, cancelled).isOk(), "delayed composition starts");
    check(vibrator->off().isOk(), "cancel sleeping producer");
    check(!cancelled->wait(100ms), "cancelled composition callback discarded");
    check(idle(), "cancelled composition cannot start late");

    tick.delayMs = 0;
    for (float scale : {0.0f, 0.3f, 1.0f}) {
        tick.scale = scale;
        check(vibrator->compose({tick}, nullptr).isOk(), "valid composition scale accepted");
        vibrator->off();
    }
    for (float scale : {-0.01f, 1.01f, NAN}) {
        tick.scale = scale;
        check(vibrator->compose({tick}, nullptr).getExceptionCode() == EX_ILLEGAL_ARGUMENT,
              "invalid composition scale rejected");
    }
    tick.scale = 0.3f;
    tick.delayMs = -1;
    check(vibrator->compose({tick}, nullptr).getExceptionCode() == EX_ILLEGAL_ARGUMENT,
          "negative composition delay rejected");
    tick.delayMs = maxDelay + 1;
    check(vibrator->compose({tick}, nullptr).getExceptionCode() == EX_ILLEGAL_ARGUMENT,
          "excessive composition delay rejected");
    tick.delayMs = 0;
    check(vibrator->compose(std::vector<CompositeEffect>(maxSize + 1, tick), nullptr)
                          .getExceptionCode() == EX_ILLEGAL_ARGUMENT,
          "excessive composition size rejected");
    tick.primitive = static_cast<CompositePrimitive>(99);
    check(vibrator->compose({tick}, nullptr).getExceptionCode() == EX_UNSUPPORTED_OPERATION,
          "unknown primitive rejected");
    int32_t unknownDuration = 0;
    check(vibrator->getPrimitiveDuration(tick.primitive, &unknownDuration).getExceptionCode() ==
                  EX_UNSUPPORTED_OPERATION,
          "unknown primitive duration rejected");

    CompositeEffect thud, spin;
    thud.primitive = CompositePrimitive::THUD;
    thud.scale = 0.3f;
    spin.primitive = CompositePrimitive::SPIN;
    spin.scale = 0.3f;
    spin.delayMs = 40;
    int32_t thudDuration = 0, spinDuration = 0;
    vibrator->getPrimitiveDuration(thud.primitive, &thudDuration);
    vibrator->getPrimitiveDuration(spin.primitive, &spinDuration);
    auto mixed = ndk::SharedRefBase::make<Completion>();
    const auto mixedStart = std::chrono::steady_clock::now();
    check(vibrator->compose({thud, spin}, mixed).isOk(), "THUD and SPIN mixed sequence accepted");
    check(mixed->wait(1000ms) && std::chrono::steady_clock::now() - mixedStart >=
                  std::chrono::milliseconds(thudDuration + spin.delayMs + spinDuration),
          "THUD and SPIN complete after full mixed timeline");
    check(idle() && mixed->count() == 1, "mixed sequence finishes idle with one callback");

    // Exercise cancellation after the new continuous profile has actually started.
    spin.delayMs = 0;
    auto spinCancelled = ndk::SharedRefBase::make<Completion>();
    check(vibrator->compose(std::vector<CompositeEffect>(maxSize, spin), spinCancelled).isOk(),
          "maximum-size SPIN sequence starts");
    std::this_thread::sleep_for(60ms);
    check(vibrator->off().isOk(), "cancel active SPIN sequence");
    check(!spinCancelled->wait(150ms) && idle(), "cancelled SPIN stays idle without callback");

    tick.primitive = CompositePrimitive::SLOW_RISE;
    auto replaced = ndk::SharedRefBase::make<Completion>();
    check(vibrator->compose(std::vector<CompositeEffect>(5, tick), replaced).isOk(),
          "long composition before RAM replacement");
    std::this_thread::sleep_for(35ms);
    int32_t duration = 0;
    check(vibrator->perform(Effect::CLICK, EffectStrength::STRONG, nullptr, &duration).isOk(),
          "RAM click preempts live RTP producer");
    check(sequence() == 13 && !replaced->wait(100ms),
          "RAM selection restored and stale callback discarded");
    check(idle(), "RTP to RAM transition finishes");

    check(vibrator->on(1000, nullptr).isOk(), "RAM continuous before composition");
    auto replacement = ndk::SharedRefBase::make<Completion>();
    tick.primitive = CompositePrimitive::LOW_TICK;
    check(vibrator->compose({tick}, replacement).isOk() && replacement->wait(1000ms),
          "composition preempts RAM continuous playback");
    check(idle(), "RAM to RTP transition finishes");

    for (int i = 0; i < 20; ++i) {
        check(vibrator->compose(std::vector<CompositeEffect>(5, tick), nullptr).isOk(),
              "rapid slider request replaces producer");
        std::this_thread::sleep_for(5ms);
    }
    vibrator->off();
    std::this_thread::sleep_for(80ms);
    check(idle(), "rapid replacements leave no late playback");
}
}  // namespace

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(1);
    ABinderProcess_startThreadPool();
    const std::string name = std::string(IVibrator::descriptor) + "/default";
    auto vibrator = IVibrator::fromBinder(ndk::SpAIBinder(AServiceManager_checkService(name.c_str())));
    if (!vibrator) {
        std::fprintf(stderr, "Vibrator service unavailable\n");
        return EXIT_FAILURE;
    }
    check(vibrator->off().isOk(), "initial off");
    int32_t caps = 0;
    check(vibrator->getCapabilities(&caps).isOk() &&
                  caps == (IVibrator::CAP_AMPLITUDE_CONTROL | IVibrator::CAP_COMPOSE_EFFECTS),
          "capabilities match implemented APIs");
    std::vector<Effect> effects;
    check(vibrator->getSupportedEffects(&effects).isOk() && effects.size() == 5 &&
                  std::find(effects.begin(), effects.end(), Effect::DOUBLE_CLICK) != effects.end(),
          "five supported UI effects include real double click");
    std::vector<CompositePrimitive> primitives;
    check(vibrator->getSupportedPrimitives(&primitives).isOk() && primitives.size() == 9,
          "all nine composition primitives advertised");
    check(!vibrator->on(0, nullptr).isOk() && !vibrator->on(-1, nullptr).isOk(),
          "invalid durations rejected");
    check(!vibrator->setAmplitude(0).isOk() && !vibrator->setAmplitude(-1).isOk() &&
                  !vibrator->setAmplitude(1.01f).isOk() &&
                  !vibrator->setAmplitude(NAN).isOk(),
          "invalid amplitudes rejected");

    struct Profile {
        Effect effect;
        const char* name;
        int waveform;
    };
    // Flyme's vendor effects 21000/31008/30900 select RAM 11/13/3.
    // Decimal wave numbers must not become hex, and short effects must not loop.
    const Profile profiles[] = {
            {Effect::TEXTURE_TICK, "texture", 8},
            {Effect::TICK, "tick", 11},
            {Effect::CLICK, "click", 13},
            {Effect::HEAVY_CLICK, "heavy", 3},
    };
    const EffectStrength strengths[] = {EffectStrength::LIGHT, EffectStrength::MEDIUM,
                                         EffectStrength::STRONG};
    const int expectedGains[] = {64, 96, 128};
    for (const auto& profile : profiles) {
        for (int s = 0; s < 3; ++s) {
            const std::string label = std::string(profile.name) + " strength " + std::to_string(s);
            int32_t duration = 0;
            check(vibrator->perform(profile.effect, strengths[s], nullptr, &duration).isOk() &&
                          duration > 0 && duration <= 50,
                  label + " accepted with single-shot completion budget");
            // After driver setup, the programmed sequence must be stable even
            // if the chip has already completed its naturally finite pulse.
            std::this_thread::sleep_for(15ms);
            check(gain() == expectedGains[s], label + " scales stock baseline in chip register");
            check(sequence() == profile.waveform, label + " selects correct RAM entry");
            check(read("loop").find("seq1 loop: 0x00") != std::string::npos,
                  label + " does not loop");
            std::this_thread::sleep_for(50ms);
            check(idle(), label + " finishes without caller off");
            check(vibrator->off().isOk(), label + " off is safe after completion");
            std::this_thread::sleep_for(30ms);
        }
    }

    // A continuous notification must not inherit the previous click waveform,
    // and a following click must clear the continuous loop.
    check(vibrator->on(200, nullptr).isOk(), "continuous vibration starts");
    std::this_thread::sleep_for(15ms);
    check(sequence() == 1 && read("loop").find("seq1 loop: 0x0f") != std::string::npos,
          "continuous playback resets waveform and enables loop");
    check(vibrator->setAmplitude(0.25f).isOk() && gain() == 32, "quarter amplitude reaches chip");
    check(vibrator->setAmplitude(0.1f).isOk() && gain() == 12, "amplitude conversion truncates like Flyme");
    check(vibrator->setAmplitude(1.0f).isOk() && gain() == 128, "full amplitude uses stock baseline");
    int32_t duration = 0;
    check(vibrator->perform(Effect::CLICK, EffectStrength::STRONG, nullptr, &duration).isOk(),
          "click interrupts continuous vibration");
    std::this_thread::sleep_for(15ms);
    check(sequence() == 13 && read("loop").find("seq1 loop: 0x00") != std::string::npos,
          "interruption replaces waveform and removes infinite loop");
    std::this_thread::sleep_for(60ms);
    check(idle(), "interrupted playback stops");

    // An old deadline must never stop a later on(), including the deadline
    // from a short perform() that was interrupted by the new on().
    check(vibrator->perform(Effect::TICK, EffectStrength::STRONG, nullptr, &duration).isOk(),
          "start short effect before replacement");
    check(vibrator->on(250, nullptr).isOk(), "replace effect with continuous playback");
    std::this_thread::sleep_for(80ms);
    check(!idle(), "old short-effect deadline cannot stop replacement");
    std::this_thread::sleep_for(220ms);
    check(idle(), "continuous watchdog stops without caller off");

    check(vibrator->on(1000, nullptr).isOk(), "long playback before cancellation");
    check(vibrator->off().isOk(), "explicit cancellation succeeds");
    std::this_thread::sleep_for(60ms);
    check(idle(), "explicit cancellation leaves chip idle");
    int32_t unsupportedDuration = 0;
    for (const auto strength : strengths) {
        int32_t duration = 0;
        check(vibrator->perform(Effect::DOUBLE_CLICK, strength, nullptr, &duration).isOk() &&
                      duration > 100 && duration < 500,
              "double click uses two finite primitives and reports completion");
        std::this_thread::sleep_for(std::chrono::milliseconds(duration + 100));
        check(idle(), "double click completes without caller off");
    }
    check(vibrator->perform(Effect::DOUBLE_CLICK, EffectStrength::STRONG, nullptr,
                            &unsupportedDuration).isOk(), "double click starts before cancellation");
    check(vibrator->off().isOk(), "double click cancellation accepted");
    std::this_thread::sleep_for(250ms);
    check(idle(), "canceled double click cannot restart its second pulse");
    check(!vibrator->perform(Effect::THUD, EffectStrength::STRONG, nullptr,
                             &unsupportedDuration).isOk(),
          "unimplemented effect rejected for framework fallback");
    check(vibrator->off().isOk(), "final off");
    if (caps & IVibrator::CAP_COMPOSE_EFFECTS) checkComposition(vibrator, primitives);
    std::printf("Result: %d failures\n", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
