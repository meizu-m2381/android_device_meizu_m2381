// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <unistd.h>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <health-impl/Health.h>

#include "ChargingPolicy.h"

namespace aidl::android::hardware::health {

class HealthM2381 : public Health {
  public:
    using Health::Health;

    ndk::ScopedAStatus getChargeCounterUah(int32_t* out) override {
        auto status = Health::getChargeCounterUah(out);
        if (!status.isOk()) return status;
        const auto normalized = m2381::ChargeCounterUah(*out);
        *out = normalized.value_or(0);
        return normalized ? ndk::ScopedAStatus::ok()
                          : ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    ndk::ScopedAStatus getChargeStatus(BatteryStatus* out) override {
        HealthInfo info;
        auto status = getHealthInfo(&info);
        if (status.isOk()) *out = info.batteryStatus;
        return status;
    }

  protected:
    void UpdateHealthInfo(HealthInfo* info) override {
        info->batteryChargeCounterUah =
                m2381::ChargeCounterUah(info->batteryChargeCounterUah).value_or(0);
        const m2381::UsbInput usb{ReadUsb("online"), ReadUsb("current_now"),
                                 ReadUsb("input_current_limit")};
        const bool blocked = info->batteryStatus == BatteryStatus::CHARGING &&
                !info->chargerWirelessOnline && !info->chargerDockOnline &&
                m2381::IsBlockedUsbInput(usb, info->batteryCurrentMicroamps);
        if (blocked) info->batteryStatus = BatteryStatus::NOT_CHARGING;
        info->batteryChargeTimeToFullNowSeconds =
                m2381::TimeToFullSeconds(info->batteryChargeTimeToFullNowSeconds, blocked);
        MaybeRequestPpsDfpSwap(info->chargerAcOnline);
    }

  private:
    static constexpr const char* kSwapDfpAllowPath =
            "/sys/devices/platform/soc/soc:qcom,pmic_glink/"
            "soc:qcom,pmic_glink:qcom,ucsi/mz_ucsi/swap_dfp_allow";
    static constexpr const char* kPpsOnlinePath = "/sys/class/meizu/usb_c/pps_online";
    static constexpr const char* kDataRolePath = "/sys/class/typec/port0/data_role";

    static std::optional<int64_t> ReadSysfsInt64(const std::string& path) {
        std::string text;
        int64_t value;
        if (!::android::base::ReadFileToString(path, &text) ||
            !::android::base::ParseInt(::android::base::Trim(text), &value)) {
            return std::nullopt;
        }
        return value;
    }

    static std::optional<int64_t> ReadUsb(const char* node) {
        return ReadSysfsInt64(std::string("/sys/class/power_supply/usb/") + node);
    }

    static bool WriteDataRoleHost() {
        const int fd = TEMP_FAILURE_RETRY(open(kDataRolePath, O_WRONLY | O_CLOEXEC));
        if (fd < 0) {
            PLOG(ERROR) << "Failed to open " << kDataRolePath;
            return false;
        }
        static constexpr char kHostRole[] = "host";
        const ssize_t written =
                TEMP_FAILURE_RETRY(write(fd, kHostRole, sizeof(kHostRole) - 1));
        const int savedErrno = errno;
        close(fd);
        if (written != static_cast<ssize_t>(sizeof(kHostRole) - 1)) {
            errno = savedErrno;
            if (written < 0) {
                PLOG(ERROR) << "Failed to write host to " << kDataRolePath;
            } else {
                LOG(ERROR) << "Short write (" << written << ") to " << kDataRolePath;
            }
            return false;
        }
        LOG(INFO) << "Requested USB data_role=host for PD-PPS";
        return true;
    }

    void MaybeRequestPpsDfpSwap(bool chargerAcOnline) {
        const m2381::PpsDfpSwapInput input{
                ReadSysfsInt64(kSwapDfpAllowPath),
                ReadSysfsInt64(kPpsOnlinePath),
                chargerAcOnline,
        };
        std::lock_guard<std::mutex> lock(swapMutex_);
        const bool writeSucceeded =
                ppsDfpSwapState_.ShouldRequestHostRole(input) ? WriteDataRoleHost() : false;
        ppsDfpSwapState_.Update(input, writeSucceeded);
    }

    std::mutex swapMutex_;
    m2381::PpsDfpSwapState ppsDfpSwapState_;
};

}  // namespace aidl::android::hardware::health
