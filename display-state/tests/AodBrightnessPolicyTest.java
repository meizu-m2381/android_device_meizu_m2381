/* SPDX-License-Identifier: Apache-2.0 */
package com.android.server.m2381;

public final class AodBrightnessPolicyTest {
    private static void expect(int expected, int actual) {
        if (actual != expected) throw new AssertionError("expected " + expected + ", got " + actual);
    }
    public static void main(String[] args) {
        AodBrightnessPolicy policy = new AodBrightnessPolicy();
        policy.reset();
        expect(-1, policy.update(Float.NaN));
        expect(-1, policy.update(Float.POSITIVE_INFINITY));
        expect(-1, policy.update(-1));
        // Initial room light selects bright; noise in the dead band retains it.
        expect(1, policy.update(4));
        for (float lux : new float[]{5, 3, 2.01f}) expect(1, policy.update(lux));
        expect(0, policy.update(2));
        for (float lux : new float[]{0, 3, 5.99f}) expect(0, policy.update(lux));
        expect(1, policy.update(6));
        expect(1, policy.update(10000));
        expect(-1, policy.update(Float.NEGATIVE_INFINITY));
        expect(1, policy.update(4));
        // A new AOD session must not inherit the previous bright/dim decision.
        policy.reset();
        expect(0, policy.update(0));
        expect(0, policy.update(4));
        policy.reset();
        expect(1, policy.update(4));
        System.out.println("AOD lux policy: hysteresis, exact thresholds, invalid data and new session passed");
    }
}
