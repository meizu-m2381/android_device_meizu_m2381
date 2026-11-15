// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <limits>
#include <optional>

namespace m2381 {

// The stock M2381 fuel gauge exports charge_counter in mAh, unlike charge_full.
inline std::optional<int32_t> ChargeCounterUah(int32_t rawMah) {
    if (rawMah < 0 || rawMah > std::numeric_limits<int32_t>::max() / 1000) {
        return std::nullopt;
    }
    return rawMah * 1000;
}

struct UsbInput {
    std::optional<int64_t> online;
    std::optional<int64_t> currentUa;
    std::optional<int64_t> limitUa;
};

inline bool IsBlockedUsbInput(const UsbInput& usb, int32_t batteryCurrentUa) {
    // Missing readings, a full battery, and a brief net discharge under load
    // must not be confused with the reproduced USB_ICL=0 failure.
    return usb.online == 1 && usb.currentUa == 0 && usb.limitUa == 0 &&
           batteryCurrentUa < 0;
}

inline int64_t TimeToFullSeconds(int64_t rawSeconds, bool blocked) {
    // Firmware reports UINT16_MAX minutes when no estimate is available.
    return blocked || rawSeconds == 65535LL * 60 ? -1 : rawSeconds;
}

}  // namespace m2381
