// SPDX-License-Identifier: Apache-2.0

#include <android-base/logging.h>
#include <binder/IServiceManager.h>
#include <binder/Parcel.h>
#include <binder/ProcessState.h>

#include <cstring>

namespace {
constexpr uint32_t kSetDimmingEnable = 54;
constexpr int32_t kPrimaryDisplay = 0;
constexpr char kVendorBinder[] = "/dev/vndbinder";

android::status_t setDimming(bool enabled) {
    // The stock M2381 composer publishes QService on the vendor Binder driver.
    const auto process = android::ProcessState::initWithDriver(kVendorBinder);
    if (process->getDriverName() != kVendorBinder) return android::PERMISSION_DENIED;
    auto service = android::defaultServiceManager()->checkService(android::String16("display.qservice"));
    if (!service) return android::NAME_NOT_FOUND;

    android::Parcel request;
    android::Parcel reply;
    auto status = request.writeInterfaceToken(service->getInterfaceDescriptor());
    if (status == android::OK) status = request.writeInt32(kPrimaryDisplay);
    if (status == android::OK) status = request.writeInt32(enabled ? 1 : 0);
    if (status == android::OK) status = service->transact(kSetDimmingEnable, request, &reply);
    if (status != android::OK) return status;

    // QService can return a successful Binder transaction with a failed HWC operation.
    int32_t composerStatus = 0;
    status = reply.readInt32(&composerStatus);
    return status == android::OK ? composerStatus : status;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || (std::strcmp(argv[1], "on") != 0 && std::strcmp(argv[1], "off") != 0)) {
        LOG(ERROR) << "Expected on or off";
        return EXIT_FAILURE;
    }
    const auto status = setDimming(std::strcmp(argv[1], "on") == 0);
    if (status != android::OK) {
        LOG(ERROR) << "Composer dimming request failed (" << status << ")";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
