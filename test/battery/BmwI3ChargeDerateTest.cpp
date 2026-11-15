#include <gtest/gtest.h>

#include "../../Software/src/battery/BMW-I3-BATTERY.h"

// Charge derate by the highest cell voltage near full. The 60 Ah values are used as example:
// no limit up to 4068 mV, linear down to 1000 W at 4079 mV, 1000 W above it.
namespace {
constexpr uint16_t START_MV = 4068;
constexpr uint16_t END_MV = 4079;
constexpr uint32_t BMS_W = 24600;
}  // namespace

TEST(BmwI3ChargeDerate, BelowAndAtStartPassesBmsAllowance) {
  EXPECT_EQ(BmwI3Battery::cell_voltage_charge_limit_W(3900, START_MV, END_MV, BMS_W), BMS_W);
  EXPECT_EQ(BmwI3Battery::cell_voltage_charge_limit_W(START_MV, START_MV, END_MV, BMS_W), BMS_W);
}

TEST(BmwI3ChargeDerate, FallsLinearlyBetweenStartAndEnd) {
  // 11 mV span, 23600 W to drop: one step of 1 mV is about 2145 W
  EXPECT_EQ(BmwI3Battery::cell_voltage_charge_limit_W(START_MV + 1, START_MV, END_MV, BMS_W), 22455u);
  // Ten of eleven steps down: 24600 - 23600 * 10 / 11 = 3145 W
  EXPECT_EQ(BmwI3Battery::cell_voltage_charge_limit_W(START_MV + 10, START_MV, END_MV, BMS_W), 3145u);
  uint32_t previous = BMS_W;
  for (uint16_t mV = START_MV + 1; mV <= END_MV; mV++) {
    uint32_t limit = BmwI3Battery::cell_voltage_charge_limit_W(mV, START_MV, END_MV, BMS_W);
    EXPECT_LT(limit, previous) << "at " << mV << " mV";
    previous = limit;
  }
}

TEST(BmwI3ChargeDerate, HoldsEndPowerAtAndAboveEnd) {
  EXPECT_EQ(BmwI3Battery::cell_voltage_charge_limit_W(END_MV, START_MV, END_MV, BMS_W),
            BmwI3Battery::CHARGE_DERATE_END_W);
  EXPECT_EQ(BmwI3Battery::cell_voltage_charge_limit_W(4102, START_MV, END_MV, BMS_W),
            BmwI3Battery::CHARGE_DERATE_END_W);
}

TEST(BmwI3ChargeDerate, NeverRaisesTheBmsAllowance) {
  // A zero from the BMS stays zero
  EXPECT_EQ(BmwI3Battery::cell_voltage_charge_limit_W(4075, START_MV, END_MV, 0), 0u);
  EXPECT_EQ(BmwI3Battery::cell_voltage_charge_limit_W(4090, START_MV, END_MV, 0), 0u);
  // A BMS allowance already below the end power is passed through
  EXPECT_EQ(BmwI3Battery::cell_voltage_charge_limit_W(4075, START_MV, END_MV, 600), 600u);
  EXPECT_EQ(BmwI3Battery::cell_voltage_charge_limit_W(4090, START_MV, END_MV, 600), 600u);
}

TEST(BmwI3ChargeDerate, InvalidRangeDisablesTheDerate) {
  EXPECT_EQ(BmwI3Battery::cell_voltage_charge_limit_W(4090, END_MV, START_MV, BMS_W), BMS_W);
  EXPECT_EQ(BmwI3Battery::cell_voltage_charge_limit_W(4090, START_MV, START_MV, BMS_W), BMS_W);
}
