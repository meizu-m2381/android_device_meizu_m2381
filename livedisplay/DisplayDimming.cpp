// SPDX-License-Identifier: Apache-2.0

#include <android-base/logging.h>
#include <binder/IServiceManager.h>
#include <binder/Parcel.h>
#include <binder/ProcessState.h>

#include <chrono>
#include <string_view>
#include <thread>

namespace {
constexpr uint32_t kSetDimmingEnable = 54;
constexpr uint32_t kSetDimmingMinBl = 55;
constexpr int32_t kPrimaryDisplay = 0;
constexpr char kVendorBinder[] = "/dev/vndbinder";
constexpr int32_t kNormalMinimumBacklight = 1;
constexpr int32_t kAntiFlickerMinimumBacklight = 400;

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
    if (mode == "reset") {
        auto status = sendCommand(service, kSetDimmingEnable, 0);
        if (status == android::OK) status = sendCommand(service, kSetDimmingMinBl,
                                                         kNormalMinimumBacklight);
        return status;
    }

    // The composer applies PCC and the backlight LUT in separate updates. Set
    // the floor while dimming is disabled, then toggle once so the panel sees a
    // single color/backlight transition instead of a series of visible pulses.
    if (enabled) {
        auto status = sendCommand(service, kSetDimmingMinBl, kAntiFlickerMinimumBacklight);
        if (status == android::OK) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            status = sendCommand(service, kSetDimmingEnable, 1);
        }
        return status;
    }

    auto status = sendCommand(service, kSetDimmingEnable, 0);
    if (status == android::OK) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        status = sendCommand(service, kSetDimmingMinBl, kNormalMinimumBacklight);
    }
    return status;
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
