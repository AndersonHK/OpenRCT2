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

TEST(GpuFoundationTest, InclusiveRectangleVisibilityRejectsInvalidAndFullyClippedBounds)
{
    constexpr Int4 clip{ 10, 20, 30, 40 };
    EXPECT_TRUE(InclusiveRectIntersectsClip({ 10, 20, 10, 20 }, clip));
    EXPECT_TRUE(InclusiveRectIntersectsClip({ 29, 39, 35, 45 }, clip));
    EXPECT_FALSE(InclusiveRectIntersectsClip({ 20, 20, 19, 30 }, clip));
    EXPECT_FALSE(InclusiveRectIntersectsClip({ 30, 20, 40, 30 }, clip));
    EXPECT_FALSE(InclusiveRectIntersectsClip({ 0, 40, 9, 50 }, clip));
}

TEST(GpuFoundationTest, PackedRectangleLayoutMatchesNativeCommandConsumers)
{
    EXPECT_EQ(sizeof(RectCommand), 100u);
    EXPECT_EQ(offsetof(RectCommand, clip), 0u);
    EXPECT_EQ(offsetof(RectCommand, texColourBounds), 20u);
    EXPECT_EQ(offsetof(RectCommand, palettes), 56u);
    EXPECT_EQ(offsetof(RectCommand, bounds), 76u);
    EXPECT_EQ(offsetof(RectCommand, zoom), 96u);
}

TEST(GpuFoundationTest, OutputDefaultsPreserveLegacySdrPresentation)
{
    const BackendConfig config;
    EXPECT_EQ(config.outputColorMode, OutputColorMode::Sdr);
    EXPECT_FLOAT_EQ(config.hdrPaperWhiteNits, 203.0f);
}

TEST(GpuFoundationTest, PackedWeatherLayoutMatchesNativeCommandConsumers)
{
    EXPECT_EQ(sizeof(WeatherCommand), 28u);
    EXPECT_EQ(offsetof(WeatherCommand, bounds), 0u);
    EXPECT_EQ(offsetof(WeatherCommand, offset), 16u);
    EXPECT_EQ(offsetof(WeatherCommand, pattern), 24u);
}

TEST(GpuFoundationTest, EffectBatchesRetainRecorderOrder)
{
    FrameCommandStream commands;
    commands.transparentRects.allocate().depth = 20;
    commands.transparentRects.allocate().depth = 10;
    commands.weather.allocate().pattern = 1;
    commands.weather.allocate().pattern = 0;
    EXPECT_EQ(commands.transparentRects[0].depth, 20);
    EXPECT_EQ(commands.transparentRects[1].depth, 10);
    EXPECT_EQ(commands.weather[0].pattern, 1);
    EXPECT_EQ(commands.weather[1].pattern, 0);
}

TEST(GpuFoundationTest, CanvasUploadIsAnExplicitMutuallyExclusiveFrameSource)
{
    FrameCommandStream commands;
    commands.canvasUpload = CanvasUpload{ 64, 320, 320, 200 };
    ASSERT_TRUE(commands.canvasUpload.has_value());
    EXPECT_EQ(commands.canvasUpload->sourceOffset, 64u);
    EXPECT_EQ(commands.canvasUpload->sourcePitch, 320u);
    commands.clear();
    EXPECT_FALSE(commands.canvasUpload.has_value());
}

TEST(GpuFoundationTest, TextureUploadsRetainPersistentAtlasMetadata)
{
    FrameCommandStream commands;
    commands.textureUploads.insert({
        .atlas = 7,
        .bounds = { 32, 64, 72, 84 },
        .sourceOffset = 4096,
        .sourcePitch = 40,
    });

    ASSERT_EQ(commands.textureUploads.size(), 1u);
    const auto& upload = commands.textureUploads[0];
    EXPECT_EQ(upload.atlas, 7u);
    EXPECT_EQ(upload.bounds.z - upload.bounds.x, 40);
    EXPECT_EQ(upload.bounds.w - upload.bounds.y, 20);
    EXPECT_EQ(upload.sourceOffset, 4096u);
    EXPECT_EQ(upload.sourcePitch, 40u);

    commands.clear();
    EXPECT_TRUE(commands.textureUploads.empty());
}

TEST(GpuFoundationTest, OptionalIntegrationCapabilitiesDefaultToDisabled)
{
    const BackendCapabilities capabilities;
    EXPECT_FALSE(capabilities.supportsAsyncReadback);
    EXPECT_FALSE(capabilities.supportsCanvasUpload);
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
