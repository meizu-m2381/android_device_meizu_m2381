/* SPDX-License-Identifier: Apache-2.0 */
package com.android.server.m2381;

/**
 * Decides whether physical refresh-rate switching should be disabled so the
 * panel stays locked at 144Hz without shifting gamma across frame-rate jumps.
 */
final class RefreshRateLockPolicy {
    static final float LOCK_ABOVE_HZ = 120.5f;

    private RefreshRateLockPolicy() {}

    static boolean shouldDisableSwitching(
            float peakRefreshRate,
            float minRefreshRate,
            float defaultPeakRefreshRate,
            float maxSupportedRefreshRate,
            boolean lowPowerMode) {
        if (lowPowerMode) {
            return false;
        }
        float effectiveMin = resolveMin(minRefreshRate, maxSupportedRefreshRate);
        float effectivePeak =
                resolvePeak(peakRefreshRate, defaultPeakRefreshRate, maxSupportedRefreshRate);
        return Math.max(effectiveMin, effectivePeak) > LOCK_ABOVE_HZ;
    }

    private static float resolveMin(float minRefreshRate, float maxSupportedRefreshRate) {
        if (Float.isNaN(minRefreshRate) || minRefreshRate < 0f) {
            return 0f;
        }
        if (Float.isInfinite(minRefreshRate)) {
            return maxSupportedRefreshRate;
        }
        return minRefreshRate;
    }

    private static float resolvePeak(
            float peakRefreshRate, float defaultPeakRefreshRate, float maxSupportedRefreshRate) {
        if (Float.isNaN(peakRefreshRate) || peakRefreshRate < 0f) {
            return defaultPeakRefreshRate;
        }
        if (peakRefreshRate == 0f || Float.isInfinite(peakRefreshRate)) {
            return maxSupportedRefreshRate;
        }
        return peakRefreshRate;
    }
}
