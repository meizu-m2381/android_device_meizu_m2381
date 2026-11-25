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

struct PpsDfpSwapInput {
    std::optional<int64_t> swapDfpAllow;
    std::optional<int64_t> ppsOnline;
    bool chargerAcOnline = false;
};

class PpsDfpSwapState {
  public:
    // Mirrors stock health-service.qti: request USB data_role="host" once per
    // 0 -> non-zero swap_dfp_allow cycle while PD-PPS AC charging is online,
    // allowing retries if the write fails or PPS status settles later.
    bool ShouldRequestHostRole(const PpsDfpSwapInput& input) const {
        return !swappedForCurrentSession_ &&
               input.swapDfpAllow.value_or(0) != 0 &&
               input.ppsOnline.value_or(0) != 0 &&
               input.chargerAcOnline;
    }

    void Update(const PpsDfpSwapInput& input, bool writeSucceeded) {
        if (input.swapDfpAllow.has_value() && *input.swapDfpAllow == 0) {
            swappedForCurrentSession_ = false;
            return;
        }
        if (ShouldRequestHostRole(input) && writeSucceeded) {
            swappedForCurrentSession_ = true;
        }
    }

  private:
    bool swappedForCurrentSession_ = false;
};

}  // namespace m2381
