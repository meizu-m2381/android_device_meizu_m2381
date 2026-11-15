// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <android-base/file.h>
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
    }

  private:
    static std::optional<int64_t> ReadUsb(const char* node) {
        std::string text;
        int64_t value;
        if (!::android::base::ReadFileToString(
                    std::string("/sys/class/power_supply/usb/") + node, &text) ||
            !::android::base::ParseInt(::android::base::Trim(text), &value)) {
            return std::nullopt;
        }
        return value;
    }
};

}  // namespace aidl::android::hardware::health
