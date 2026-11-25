// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>
#include "../ChargingPolicy.h"

using m2381::ChargeCounterUah;
using m2381::IsBlockedUsbInput;
using m2381::PpsDfpSwapInput;
using m2381::PpsDfpSwapState;
using m2381::TimeToFullSeconds;
using m2381::UsbInput;

TEST(M2381Charging, ReproducedZeroInputIsBlocked) {
    EXPECT_TRUE(IsBlockedUsbInput({1, 0, 0}, -600000));
    EXPECT_EQ(-1, TimeToFullSeconds(3932100, true));
    EXPECT_EQ(-1, TimeToFullSeconds(2040, true));
}

TEST(M2381Charging, RecoveredChargingRetainsValidEstimate) {
    EXPECT_FALSE(IsBlockedUsbInput({1, 2715000, 3000000}, 1244000));
    EXPECT_EQ(2040, TimeToFullSeconds(2040, false));
    EXPECT_EQ(3740000, ChargeCounterUah(3740));
}

TEST(M2381Charging, FullBatteryAndHeavyLoadDoNotLookBlocked) {
    EXPECT_FALSE(IsBlockedUsbInput({1, 0, 0}, 0));
    EXPECT_FALSE(IsBlockedUsbInput({1, 500000, 500000}, -600000));
    EXPECT_FALSE(IsBlockedUsbInput({1, 0, 3000000}, -600000));
    EXPECT_EQ(0, TimeToFullSeconds(0, false));
}

TEST(M2381Charging, UnpluggedAndMissingReadingsAreNotZeroInputEvidence) {
    EXPECT_FALSE(IsBlockedUsbInput({0, 0, 0}, -600000));
    EXPECT_FALSE(IsBlockedUsbInput({std::nullopt, 0, 0}, -600000));
    EXPECT_FALSE(IsBlockedUsbInput({1, std::nullopt, 0}, -600000));
    EXPECT_FALSE(IsBlockedUsbInput({1, 0, std::nullopt}, -600000));
}

TEST(M2381Charging, UnknownFirmwareEstimateIsNotFortyFiveDays) {
    EXPECT_EQ(-1, TimeToFullSeconds(3932100, false));
    EXPECT_EQ(-1, TimeToFullSeconds(-1, false));
    EXPECT_EQ(7200, TimeToFullSeconds(7200, false));
}

TEST(M2381Charging, CounterUnitsAndInvalidInputs) {
    EXPECT_EQ(4239000, ChargeCounterUah(4239));
    EXPECT_EQ(0, ChargeCounterUah(0));
    EXPECT_FALSE(ChargeCounterUah(-1).has_value());
    EXPECT_FALSE(ChargeCounterUah(INT32_MAX).has_value());
}

TEST(M2381Charging, PpsDfpSwapRequestsOncePerPlugIn) {
    PpsDfpSwapState state;
    const PpsDfpSwapInput ready{1, 1, true};

    EXPECT_TRUE(state.ShouldRequestHostRole(ready));
    state.Update(ready, true);

    EXPECT_FALSE(state.ShouldRequestHostRole(ready));
    state.Update(ready, false);

    const PpsDfpSwapInput unplugged{0, 0, false};
    EXPECT_FALSE(state.ShouldRequestHostRole(unplugged));
    state.Update(unplugged, false);

    EXPECT_TRUE(state.ShouldRequestHostRole(ready));
}

TEST(M2381Charging, PpsDfpSwapRetriesOnWriteFailure) {
    PpsDfpSwapState state;
    const PpsDfpSwapInput ready{1, 1, true};

    EXPECT_TRUE(state.ShouldRequestHostRole(ready));
    state.Update(ready, false);

    EXPECT_TRUE(state.ShouldRequestHostRole(ready));
    state.Update(ready, true);
    EXPECT_FALSE(state.ShouldRequestHostRole(ready));
}

TEST(M2381Charging, PpsDfpSwapRequiresPpsAndAcOnline) {
    PpsDfpSwapState state;

    const PpsDfpSwapInput noPps{1, 0, true};
    EXPECT_FALSE(state.ShouldRequestHostRole(noPps));
    state.Update(noPps, false);

    const PpsDfpSwapInput noAc{1, 1, false};
    EXPECT_FALSE(state.ShouldRequestHostRole(noAc));
    state.Update(noAc, false);

    const PpsDfpSwapInput ready{1, 1, true};
    EXPECT_TRUE(state.ShouldRequestHostRole(ready));
    state.Update(ready, true);
    EXPECT_FALSE(state.ShouldRequestHostRole(ready));
}

TEST(M2381Charging, PpsDfpSwapMissingInputsPreserveState) {
    PpsDfpSwapState state;
    const PpsDfpSwapInput missingSwap{std::nullopt, 1, true};
    EXPECT_FALSE(state.ShouldRequestHostRole(missingSwap));
    state.Update(missingSwap, false);

    const PpsDfpSwapInput ready{1, 1, true};
    EXPECT_TRUE(state.ShouldRequestHostRole(ready));
    state.Update(ready, true);

    EXPECT_FALSE(state.ShouldRequestHostRole(missingSwap));
    state.Update(missingSwap, false);
    EXPECT_FALSE(state.ShouldRequestHostRole(ready));
}
