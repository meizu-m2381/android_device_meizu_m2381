// SPDX-License-Identifier: Apache-2.0
#include "../AodMode.h"
#include <assert.h>
#include <stdio.h>

int main() {
    for (int hour = 0; hour < 24; ++hour) {
        const bool night = hour >= 22 || hour < 6;
        assert(meizu::aodMode("1", hour) == (night ? 0 : 1));
        assert(meizu::aodMode("0", hour) == 0);
        const char* invalidInputs[]{nullptr, "", "2", "-1", "1x"};
        for (const char* invalid : invalidInputs) {
            assert(meizu::aodMode(invalid, hour) == 0);
        }
    }
    assert(meizu::aodMode("1", -1) == 0);
    assert(meizu::aodMode("1", 24) == 0);
    puts("AOD hardware mode: all hours, night boundaries and missing/invalid ambient input passed");
}
