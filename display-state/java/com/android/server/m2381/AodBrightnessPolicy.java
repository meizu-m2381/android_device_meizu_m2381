/* SPDX-License-Identifier: Apache-2.0 */
package com.android.server.m2381;

/** Two hardware modes, using M2381 Flyme SensorPolicy's 2/6 lux hysteresis. */
final class AodBrightnessPolicy {
    private int mMode;
    private boolean mHasReading;

    void reset() {
        mMode = 0;
        mHasReading = false;
    }

    int update(float lux) {
        if (!Float.isFinite(lux) || lux < 0) return -1;
        if (!mHasReading) {
            mMode = lux > 2f ? 1 : 0;
        } else if (lux <= 2f) {
            mMode = 0;
        } else if (lux >= 6f) {
            mMode = 1;
        }
        mHasReading = true;
        return mMode;
    }
}
