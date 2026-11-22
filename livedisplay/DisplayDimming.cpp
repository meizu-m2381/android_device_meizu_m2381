// SPDX-License-Identifier: Apache-2.0

#include <android-base/logging.h>
#include <binder/IServiceManager.h>
#include <binder/Parcel.h>
#include <binder/ProcessState.h>

#include <string_view>

namespace {
constexpr uint32_t kSetDimmingEnable = 54;
constexpr int32_t kPrimaryDisplay = 0;
constexpr char kVendorBinder[] = "/dev/vndbinder";

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
    // The stock calibration profile supplies the digital-dimming minimum and
    // PCC threshold. Keep the transition atomic from the caller's point of
    // view by changing only the Composer dimming status here. Sending a
    // separate minimum-DBV command changes the panel backlight immediately
    // and causes the visible flash that this feature is meant to avoid.
    return sendCommand(service, kSetDimmingEnable, enabled ? 1 : 0);
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
