/* SPDX-License-Identifier: Apache-2.0 */
package com.android.server.m2381;

public final class RefreshRateLockPolicyTest {
    private static void expect(boolean expected, boolean actual, String label) {
        if (actual != expected) {
            throw new AssertionError(label + ": expected " + expected + ", got " + actual);
        }
    }

    public static void main(String[] args) {
        final float defPeak = 120f;
        final float maxHz = 144f;

        // Standard peak refresh rates with VRR enabled (min=0).
        expect(false, RefreshRateLockPolicy.shouldDisableSwitching(60f, 0f, defPeak, maxHz, false), "60Hz");
        expect(false, RefreshRateLockPolicy.shouldDisableSwitching(90f, 0f, defPeak, maxHz, false), "90Hz");
        expect(false, RefreshRateLockPolicy.shouldDisableSwitching(120f, 0f, defPeak, maxHz, false), "120Hz");
        expect(true, RefreshRateLockPolicy.shouldDisableSwitching(144f, 0f, defPeak, maxHz, false), "144Hz");
        expect(true, RefreshRateLockPolicy.shouldDisableSwitching(144.00002f, 0f, defPeak, maxHz, false), "144.00002Hz");

        // Unset (NaN / <0) falls back to defaultPeakRefreshRate (120Hz); 0 / +Infinity unlocks peak to max (144Hz).
        expect(false, RefreshRateLockPolicy.shouldDisableSwitching(Float.NaN, 0f, defPeak, maxHz, false), "NaN peak");
        expect(false, RefreshRateLockPolicy.shouldDisableSwitching(-1f, 0f, defPeak, maxHz, false), "negative peak");
        expect(true, RefreshRateLockPolicy.shouldDisableSwitching(0f, 0f, defPeak, maxHz, false), "0 peak (unlimited)");
        expect(true, RefreshRateLockPolicy.shouldDisableSwitching(Float.POSITIVE_INFINITY, 0f, defPeak, maxHz, false), "+Inf peak");

        // Min refresh rate at or above 144Hz (or +Infinity) forces > 120.5Hz even when peak is lower.
        expect(true, RefreshRateLockPolicy.shouldDisableSwitching(120f, 144f, defPeak, maxHz, false), "min 144Hz");
        expect(true, RefreshRateLockPolicy.shouldDisableSwitching(120f, Float.POSITIVE_INFINITY, defPeak, maxHz, false), "+Inf min");
        expect(false, RefreshRateLockPolicy.shouldDisableSwitching(120f, Float.NaN, defPeak, maxHz, false), "NaN min");
        expect(false, RefreshRateLockPolicy.shouldDisableSwitching(120f, 120f, defPeak, maxHz, false), "fixed 120Hz");

        // Battery saver disables locking regardless of peak/min settings.
        expect(false, RefreshRateLockPolicy.shouldDisableSwitching(144f, 0f, defPeak, maxHz, true), "144Hz low power");
        expect(false, RefreshRateLockPolicy.shouldDisableSwitching(144f, 144f, defPeak, maxHz, true), "min 144Hz low power");

        System.out.println("RefreshRateLockPolicyTest: all 15 cases passed");
    }
}
