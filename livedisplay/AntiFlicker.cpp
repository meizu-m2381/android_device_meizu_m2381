// SPDX-License-Identifier: Apache-2.0

#include <aidl/vendor/lineage/livedisplay/BnAntiFlicker.h>
#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <spawn.h>
#include <sys/wait.h>
#include <signal.h>

#include <cerrno>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>

extern char** environ;

namespace {
using aidl::vendor::lineage::livedisplay::BnAntiFlicker;
constexpr char kComposerClient[] = "/vendor/bin/m2381-display-dimming";

int waitForComposerClient(pid_t pid) {
    using namespace std::chrono_literals;
    const auto deadline = std::chrono::steady_clock::now() + 3000ms;
    int status = 0;
    do {
        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            return WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS ? 0 : EIO;
        }
        if (waited < 0 && errno != EINTR) return errno;
        std::this_thread::sleep_for(10ms);
    } while (std::chrono::steady_clock::now() < deadline);

    kill(pid, SIGKILL);
    while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
    return ETIMEDOUT;
}

class AntiFlicker final : public BnAntiFlicker {
  public:
    ndk::ScopedAStatus getEnabled(bool* enabled) override {
        std::lock_guard lock(mutex_);
        *enabled = enabled_.value_or(false);
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus setEnabled(bool enabled) override {
        std::lock_guard lock(mutex_);
        if (enabled_ == enabled) return ndk::ScopedAStatus::ok();
        char on[] = "on";
        char off[] = "off";
        char reset[] = "reset";
        char* mode = enabled ? on : (enabled_.has_value() ? off : reset);
        char* const argv[] = {const_cast<char*>(kComposerClient), mode, nullptr};
        pid_t pid;
        const int error = posix_spawn(&pid, kComposerClient, nullptr, nullptr, argv, environ);
        if (error != 0) {
            LOG(ERROR) << "Cannot start composer client (" << error << ")";
            return ndk::ScopedAStatus::fromServiceSpecificError(error);
        }
        const int result = waitForComposerClient(pid);
        if (result != 0) {
            return ndk::ScopedAStatus::fromServiceSpecificError(result);
        }

        enabled_ = enabled;
        return ndk::ScopedAStatus::ok();
    }

  private:
    std::mutex mutex_;
    std::optional<bool> enabled_;
};
}  // namespace

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    const auto service = ndk::SharedRefBase::make<AntiFlicker>();
    const std::string instance = std::string(AntiFlicker::descriptor) + "/default";
    CHECK_EQ(AServiceManager_addService(service->asBinder().get(), instance.c_str()), STATUS_OK);
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
