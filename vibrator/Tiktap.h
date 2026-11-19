// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <aidl/android/hardware/vibrator/CompositeEffect.h>
#include <sys/types.h>
#include <cstdint>
#include <vector>

namespace meizu {
struct Composition {
    int durationMs = 0;
    int leadingMs = 0;
    std::vector<int64_t> data;
};
constexpr int kCompositionSize = 16;
constexpr int kCompositionDelay = 1000;
int primitiveDuration(aidl::android::hardware::vibrator::CompositePrimitive primitive);
int makeComposition(const std::vector<aidl::android::hardware::vibrator::CompositeEffect>& effects,
                    Composition* result);

// The stock algorithm has detached threads and process-global buffers. Reap the
// previous producer before issuing STOP, so it cannot write into the next effect.
class TiktapProcess {
  public:
    static bool available();
    static void stopHardware();
    bool start(const Composition& composition);
    void stop();
    bool finished();
  private:
    pid_t pid_ = -1;
};
int tiktapWorker(int argc, char** argv);
}  // namespace meizu
