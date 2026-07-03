/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>
#include <openrct2/core/Money.hpp>

TEST(MoneyTests, LegacyMoneyTypesConvertToCentMoney64)
{
    EXPECT_EQ(ToMoney64(static_cast<money16>(25)), 2.50_GBP);
    EXPECT_EQ(ToMoney64(static_cast<money32>(1234)), 123.40_GBP);
    EXPECT_EQ(ToMoney64(kMoney16Undefined), kMoney64Undefined);
    EXPECT_EQ(ToMoney64(kMoney32Undefined), kMoney64Undefined);
}

TEST(MoneyTests, ToMoney16RoundsCentMoneyToTenths)
{
    EXPECT_EQ(ToMoney16(0.04_GBP), 0);
    EXPECT_EQ(ToMoney16(0.05_GBP), 1);
    EXPECT_EQ(ToMoney16(0.14_GBP), 1);
    EXPECT_EQ(ToMoney16(0.15_GBP), 2);

    EXPECT_EQ(ToMoney16(-0.04_GBP), 0);
    EXPECT_EQ(ToMoney16(-0.05_GBP), -1);
    EXPECT_EQ(ToMoney16(-0.14_GBP), -1);
    EXPECT_EQ(ToMoney16(-0.15_GBP), -2);
    EXPECT_EQ(ToMoney16(kMoney64Undefined), kMoney16Undefined);
}
