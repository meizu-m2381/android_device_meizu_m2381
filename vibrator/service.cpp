// SPDX-License-Identifier: Apache-2.0
#include <aidl/android/hardware/vibrator/BnVibrator.h>
#include <android-base/file.h>
#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

namespace aidl::android::hardware::vibrator {

// m2381 has an Awinic I2C motor, independent of the QCOM haptics device.
// Meizu's waveform/enable interface plays the stock RAM effects. The firmware
// and this driver's sysfs ABI are documented in README.md beside this source.
// Composition and calibrated frequency control are not implemented.
class Vibrator final : public BnVibrator {
  public:
    Vibrator() : worker_([this] { runTimer(); }) {}
    ~Vibrator() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            changed_.notify_all();
        }
        worker_.join();
        writeDuration(0);
    }
    ndk::ScopedAStatus getCapabilities(int32_t* result) override {
        *result = IVibrator::CAP_AMPLITUDE_CONTROL;
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus off() override {
        std::lock_guard<std::mutex> lock(mutex_);
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
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getSupportedPrimitives(std::vector<CompositePrimitive>* result) override {
        result->clear();
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
    ndk::ScopedAStatus getCompositionDelayMax(int32_t*) override { return unsupported(); }
    ndk::ScopedAStatus getCompositionSizeMax(int32_t*) override { return unsupported(); }
    ndk::ScopedAStatus getPrimitiveDuration(CompositePrimitive, int32_t*) override {
        return unsupported();
    }
    ndk::ScopedAStatus compose(const std::vector<CompositeEffect>&,
                             const std::shared_ptr<IVibratorCallback>&) override {
        return unsupported();
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
            if (changed_.wait_until(lock, deadline_, [this, generation] {
                    return stopping_ || generation_ != generation;
                })) continue;
            active_ = false;
            writeDuration(0);
        }
    }
    std::mutex mutex_;
    std::condition_variable changed_;
    std::chrono::steady_clock::time_point deadline_;
    uint64_t generation_ = 0;
    bool active_ = false;
    bool stopping_ = false;
    std::thread worker_;
};
}  // namespace aidl::android::hardware::vibrator

int main(int argc, char** argv) {
    ::android::base::InitLogging(argv, ::android::base::KernelLogger);
    (void)argc;
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    auto vibrator = ndk::SharedRefBase::make<aidl::android::hardware::vibrator::Vibrator>();
    CHECK(vibrator->off().isOk()) << "Awinic timed_output is unavailable";
    CHECK(vibrator->setAmplitude(1.0f).isOk()) << "Awinic gain is unavailable";
    const std::string instance = std::string(vibrator->descriptor) + "/default";
    CHECK_EQ(AServiceManager_addService(vibrator->asBinder().get(), instance.c_str()), STATUS_OK);
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
