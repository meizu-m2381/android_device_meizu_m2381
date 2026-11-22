// SPDX-License-Identifier: Apache-2.0

#include <android-base/logging.h>
#include <binder/IServiceManager.h>
#include <binder/Parcel.h>
#include <binder/ProcessState.h>

#include <array>
#include <chrono>
#include <string_view>
#include <thread>

namespace {
constexpr uint32_t kSetDimmingEnable = 54;
constexpr uint32_t kSetDimmingMinBl = 55;
constexpr int32_t kPrimaryDisplay = 0;
constexpr char kVendorBinder[] = "/dev/vndbinder";
constexpr auto kStepInterval = std::chrono::milliseconds(45);
// Hold each minimum for at least one frame at the rates used on the lock screen.
// Above the normal minimum, limit the change to about 13% per step.
constexpr std::array kMinimumBacklightSteps = {
    1, 15, 17, 19, 21, 24, 27, 30, 34, 38, 43, 48, 54, 61, 69, 78,
    88, 99, 112, 126, 142, 160, 180, 203, 229, 258, 291, 328, 370, 400,
};

android::status_t sendCommand(const android::sp<android::IBinder>& service, uint32_t code,
                              int32_t value) {
    android::Parcel request;
    android::Parcel reply;
    auto status = request.writeInterfaceToken(service->getInterfaceDescriptor());
    if (status == android::OK) status = request.writeInt32(kPrimaryDisplay);
    if (status == android::OK) status = request.writeInt32(value);
    if (status == android::OK) status = service->transact(code, request, &reply);
    if (status != android::OK) return status;

    // QService can return a successful Binder transaction with a failed HWC operation.
    int32_t composerStatus = 0;
    status = reply.readInt32(&composerStatus);
    return status == android::OK ? composerStatus : status;
}

android::status_t setDimming(std::string_view mode) {
    // The stock M2381 composer publishes QService on the vendor Binder driver.
    const auto process = android::ProcessState::initWithDriver(kVendorBinder);
    if (process->getDriverName() != kVendorBinder) return android::PERMISSION_DENIED;
    auto service = android::defaultServiceManager()->checkService(android::String16("display.qservice"));
    if (!service) return android::NAME_NOT_FOUND;

    const bool enabled = mode == "on";
    if (mode == "reset") return sendCommand(service, kSetDimmingEnable, 0);

    if (enabled) {
        auto status = sendCommand(service, kSetDimmingEnable, 1);
        if (status != android::OK) return status;
    }
    // Composer applies PCC and the backlight LUT through separate updates. Limit
    // the visible luminance change while these updates may span different frames.
    for (size_t i = 1; i < kMinimumBacklightSteps.size(); ++i) {
        std::this_thread::sleep_for(kStepInterval);
        const size_t level = enabled ? i : kMinimumBacklightSteps.size() - 1 - i;
        const auto status = sendCommand(service, kSetDimmingMinBl, kMinimumBacklightSteps[level]);
        if (status != android::OK) {
            if (enabled) sendCommand(service, kSetDimmingEnable, 0);
            return status;
        }
    }
    if (enabled) return android::OK;

    std::this_thread::sleep_for(kStepInterval);
    return sendCommand(service, kSetDimmingEnable, 0);
}
}  // namespace

int main(int argc, char** argv) {
    const std::string_view mode = argc == 2 ? argv[1] : "";
    if (mode != "on" && mode != "off" && mode != "reset") {
        LOG(ERROR) << "Expected on, off, or reset";
        return EXIT_FAILURE;
    }
    const auto status = setDimming(mode);
    if (status != android::OK) {
        LOG(ERROR) << "Composer dimming request failed (" << status << ")";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
