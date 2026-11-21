// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <string.h>

namespace meizu {
// Match the stock AODStateController night window, [22:00, 06:00).
inline int aodMode(const char* ambient, int hour) {
    if (hour < 0 || hour >= 24 || hour >= 22 || hour < 6) return 0;
    return ambient && strcmp(ambient, "1") == 0 ? 1 : 0;
}
} // namespace meizu
