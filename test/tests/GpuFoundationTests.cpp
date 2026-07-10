/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>
#include <openrct2-ui/drawing/engines/gpu/GpuAtlas.h>
#include <openrct2-ui/drawing/engines/gpu/GpuBackend.h>
#include <openrct2-ui/drawing/engines/gpu/GpuCommandStream.h>

using namespace OpenRCT2::Ui::Gpu;

TEST(GpuFoundationTest, AtlasSizeOrdersUseTheLegacyPowerOfTwoClasses)
{
    EXPECT_EQ(AtlasPage::CalculateImageSizeOrder(1, 1), 5);
    EXPECT_EQ(AtlasPage::CalculateImageSizeOrder(32, 32), 5);
    EXPECT_EQ(AtlasPage::CalculateImageSizeOrder(33, 1), 6);
    EXPECT_EQ(AtlasPage::CalculateImageSizeOrder(128, 129), 8);
}

TEST(GpuFoundationTest, AtlasAllocationRetainsLayerAndPixelBounds)
{
    AtlasPage page(7, 64);
    page.Initialise(128, 128);

    const auto location = page.Allocate(40, 20);
    EXPECT_EQ(location.index, 7u);
    EXPECT_EQ(location.bounds.z - location.bounds.x, 40);
    EXPECT_EQ(location.bounds.w - location.bounds.y, 20);
    EXPECT_FLOAT_EQ(location.coords.z, 128.0f);
    EXPECT_FLOAT_EQ(location.coords.w, 128.0f);
    EXPECT_EQ(page.GetFreeSlots(), 3);

    page.Free(location);
    EXPECT_EQ(page.GetFreeSlots(), 4);
}

TEST(GpuFoundationTest, CommandBatchClearReusesReservedStorage)
{
    CommandBatch<LineCommand> commands;
    commands.reserve(8);
    commands.allocate() = { .bounds = { 1, 2, 3, 4 }, .colour = 5, .depth = 6 };
    commands.allocate() = { .bounds = { 7, 8, 9, 10 }, .colour = 11, .depth = 12 };
    EXPECT_EQ(commands.size(), 2u);

    commands.clear();
    EXPECT_TRUE(commands.empty());
    EXPECT_EQ(commands.capacity(), 8u);

    auto& reused = commands.allocate();
    reused = { .bounds = { 13, 14, 15, 16 }, .colour = 17, .depth = 18 };
    EXPECT_EQ(commands.size(), 1u);
    EXPECT_EQ(commands[0].depth, 18);
}

TEST(GpuFoundationTest, IntegratedTimingFieldsRemainExplicitlyOptional)
{
    FrameTimings timings{ .frameNumber = 42, .cpuSubmitMicroseconds = 125.0 };
    EXPECT_EQ(timings.frameNumber, 42u);
    EXPECT_DOUBLE_EQ(timings.cpuSubmitMicroseconds, 125.0);
    EXPECT_FALSE(timings.hasSimulationMeasurement);
    EXPECT_FALSE(timings.hasPaintMeasurement);
    EXPECT_FALSE(timings.hasGpuTimestamp);
    EXPECT_FALSE(timings.hasPresentWaitMeasurement);
}
