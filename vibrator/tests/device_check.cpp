// SPDX-License-Identifier: Apache-2.0
#include <aidl/android/hardware/vibrator/IVibrator.h>
#include <android-base/file.h>
#include <android/binder_manager.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
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
    unsigned value = 0;
    const auto text = read("gain");
    std::sscanf(text.c_str(), "gain = 0x%x", &value);
    return static_cast<int>(value);
}

bool idle() {
    // Read-only register dump. GO and GLB_STATE must both indicate standby.
    // The driver's "state" node is only software state, not chip completion.
    const auto registers = read("reg");
    return registers.find("reg:0x05=0x00") != std::string::npos &&
           registers.find("reg:0x46=0x00") != std::string::npos;
}
}  // namespace

int main() {
    const std::string name = std::string(IVibrator::descriptor) + "/default";
    auto vibrator = IVibrator::fromBinder(ndk::SpAIBinder(AServiceManager_checkService(name.c_str())));
    if (!vibrator) {
        std::fprintf(stderr, "Vibrator service unavailable\n");
        return EXIT_FAILURE;
    }
    check(vibrator->off().isOk(), "initial off");
    int32_t caps = 0;
    check(vibrator->getCapabilities(&caps).isOk() && caps == IVibrator::CAP_AMPLITUDE_CONTROL,
          "capabilities match implemented APIs");
    std::vector<Effect> effects;
    check(vibrator->getSupportedEffects(&effects).isOk() && effects.size() == 4,
          "four supported UI effects");
    std::vector<CompositePrimitive> primitives;
    check(vibrator->getSupportedPrimitives(&primitives).isOk() && primitives.empty(),
          "unimplemented composition not advertised");
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
        int strongGain;
    };
    // Independent hardware expectations: decimal 12/16/17 must NOT become
    // hexadecimal 0x12/0x16/0x17, and all short effects must disable looping.
    const Profile profiles[] = {
            {Effect::TEXTURE_TICK, "texture", 8, 8},
            {Effect::TICK, "tick", 12, 32},
            {Effect::CLICK, "click", 17, 48},
            {Effect::HEAVY_CLICK, "heavy", 16, 96},
    };
    const EffectStrength strengths[] = {EffectStrength::LIGHT, EffectStrength::MEDIUM,
                                         EffectStrength::STRONG};
    const int percentages[] = {40, 65, 100};
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
            check(gain() == (profile.strongGain * percentages[s] + 50) / 100,
                  label + " applies requested strength");
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
    check(vibrator->setAmplitude(0.25f).isOk() && gain() == 24, "quarter amplitude reaches driver");
    check(vibrator->setAmplitude(1.0f).isOk() && gain() == 96, "full amplitude respects ceiling");
    int32_t duration = 0;
    check(vibrator->perform(Effect::CLICK, EffectStrength::STRONG, nullptr, &duration).isOk(),
          "click interrupts continuous vibration");
    std::this_thread::sleep_for(15ms);
    check(sequence() == 17 && read("loop").find("seq1 loop: 0x00") != std::string::npos,
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
    check(!vibrator->perform(Effect::DOUBLE_CLICK, EffectStrength::STRONG, nullptr,
                             &unsupportedDuration).isOk(),
          "unimplemented effect rejected for framework fallback");
    check(vibrator->off().isOk(), "final off");
    std::printf("Result: %d failures\n", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
