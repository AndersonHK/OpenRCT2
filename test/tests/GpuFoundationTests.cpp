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
#include <openrct2-ui/drawing/engines/gpu/GpuFrameMailbox.h>
#include <openrct2-ui/drawing/engines/gpu/GpuTextureCache.h>
#ifdef ENABLE_VULKAN
    #include <openrct2-ui/drawing/engines/vulkan/VulkanDevice.h>
    #include <openrct2-ui/drawing/engines/vulkan/VulkanSurfaceFormat.h>
#endif
#include <openrct2/drawing/LightFX.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <utility>

using namespace OpenRCT2::Ui::Gpu;
namespace LightFx = OpenRCT2::Drawing::LightFx;

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

TEST(GpuFoundationTest, AtlasAllocationIdentityDistinguishesReusedSlots)
{
    TextureLocation first;
    first.index = 2;
    first.slot = 17;
    first.allocationSerial = 41;
    auto second = first;
    second.allocationSerial++;

    EXPECT_NE(first.GetAllocationId(), second.GetAllocationId());
}

TEST(GpuFoundationTest, ResidencyLeaseDefersTransientSlotReuseUntilRetirement)
{
    TextureCache cache(1);
    std::vector<std::byte> pixels(kAtlasDimension);

    FrameCommandStream firstCommands;
    cache.BeginFrame();
    const auto firstBinding = cache.LoadTransientBitmapTexture(pixels.data(), pixels.size(), 1);
    const auto firstLease = cache.SealFrame(firstCommands);
    ASSERT_TRUE(static_cast<bool>(firstLease));
    ASSERT_EQ(firstCommands.textureUploads.size(), 1u);
    EXPECT_EQ(firstCommands.textureUploads[0].pixels.size(), pixels.size());

    cache.BeginFrame();
    EXPECT_THROW((void)cache.LoadTransientBitmapTexture(pixels.data(), pixels.size(), 1), std::runtime_error);
    cache.AbortFrame();

    cache.RetireFrame(firstLease, FrameRetirement::Failed);

    FrameCommandStream secondCommands;
    cache.BeginFrame();
    const auto secondBinding = cache.LoadTransientBitmapTexture(pixels.data(), pixels.size(), 1);
    const auto secondLease = cache.SealFrame(secondCommands);
    EXPECT_NE(firstLease, secondLease);
    EXPECT_EQ(firstBinding.index, secondBinding.index);
    cache.RetireFrame(secondLease, FrameRetirement::Presented);
}

TEST(GpuFoundationTest, AbortingAnUnsealedFrameReleasesTransientAllocations)
{
    TextureCache cache(1);
    std::vector<std::byte> pixels(kAtlasDimension);

    cache.BeginFrame();
    const auto firstBinding = cache.LoadTransientBitmapTexture(pixels.data(), pixels.size(), 1);
    cache.AbortFrame();

    FrameCommandStream commands;
    cache.BeginFrame();
    const auto secondBinding = cache.LoadTransientBitmapTexture(pixels.data(), pixels.size(), 1);
    const auto lease = cache.SealFrame(commands);
    EXPECT_EQ(firstBinding.index, secondBinding.index);
    cache.RetireFrame(lease, FrameRetirement::Presented);
}

TEST(GpuFoundationTest, NewestFrameMailboxReplacesPendingVisualWork)
{
    LatestFrameMailbox mailbox;
    auto first = std::make_unique<RecordedFramePacket>();
    first->frameNumber = 10;
    first->presentation.paletteVersion = 3;
    auto firstResult = mailbox.Publish(std::move(first));
    EXPECT_TRUE(firstResult.accepted);
    EXPECT_EQ(firstResult.released, nullptr);

    auto newest = std::make_unique<RecordedFramePacket>();
    newest->frameNumber = 11;
    newest->presentation.paletteVersion = 4;
    newest->presentation.surfaceFormatVersion = 5;
    newest->presentation.graphicsLookupTablesVersion = 6;
    newest->presentation.logicalExtent = { 640, 480 };
    newest->presentation.drawableExtent = { 1280, 960 };
    auto newestResult = mailbox.Publish(std::move(newest));
    ASSERT_TRUE(newestResult.accepted);
    ASSERT_NE(newestResult.released, nullptr);
    EXPECT_EQ(newestResult.released->frameNumber, 10u);

    const auto taken = mailbox.WaitTakeNewest();
    ASSERT_NE(taken, nullptr);
    EXPECT_EQ(taken->frameNumber, 11u);
    EXPECT_EQ(taken->presentation.paletteVersion, 4u);
    EXPECT_EQ(taken->presentation.surfaceFormatVersion, 5u);
    EXPECT_EQ(taken->presentation.graphicsLookupTablesVersion, 6u);
    EXPECT_EQ(taken->presentation.logicalExtent, (Extent{ 640, 480 }));
    EXPECT_EQ(taken->presentation.drawableExtent, (Extent{ 1280, 960 }));
}

TEST(GpuFoundationTest, StoppedFrameMailboxReturnsAndRejectsUnconsumedPackets)
{
    LatestFrameMailbox mailbox;
    auto pending = std::make_unique<RecordedFramePacket>();
    pending->frameNumber = 20;
    ASSERT_TRUE(mailbox.Publish(std::move(pending)).accepted);

    const auto stopped = mailbox.Stop();
    ASSERT_NE(stopped, nullptr);
    EXPECT_EQ(stopped->frameNumber, 20u);
    EXPECT_TRUE(mailbox.IsStopping());
    EXPECT_EQ(mailbox.WaitTakeNewest(), nullptr);

    auto rejected = std::make_unique<RecordedFramePacket>();
    rejected->frameNumber = 21;
    auto rejectedResult = mailbox.Publish(std::move(rejected));
    EXPECT_FALSE(rejectedResult.accepted);
    ASSERT_NE(rejectedResult.released, nullptr);
    EXPECT_EQ(rejectedResult.released->frameNumber, 21u);
}

TEST(GpuFoundationTest, FrameMailboxKeepsOnlyOneRecycledPacket)
{
    LatestFrameMailbox mailbox;
    auto first = std::make_unique<RecordedFramePacket>();
    first->frameNumber = 30;
    mailbox.Recycle(std::move(first));
    auto newest = std::make_unique<RecordedFramePacket>();
    newest->frameNumber = 31;
    mailbox.Recycle(std::move(newest));

    const auto recycled = mailbox.TakeRecycled();
    ASSERT_NE(recycled, nullptr);
    EXPECT_EQ(recycled->frameNumber, 31u);
    EXPECT_EQ(mailbox.TakeRecycled(), nullptr);
}

TEST(GpuFoundationTest, SynchronousReadbackCompletesWithOwnedIndexedPixels)
{
    SynchronousReadback readback({ 2, 2 });
    const auto pixels = readback.GetPixels();
    ASSERT_EQ(pixels.size(), 4u);
    pixels[0] = std::byte{ 1 };
    pixels[3] = std::byte{ 4 };

    readback.Complete(true);

    EXPECT_TRUE(readback.Wait());
    EXPECT_EQ(readback.GetExtent(), (Extent{ 2, 2 }));
    EXPECT_EQ(readback.GetPixels()[0], std::byte{ 1 });
    EXPECT_EQ(readback.GetPixels()[3], std::byte{ 4 });
}

TEST(GpuFoundationTest, SynchronousReadbackCanReportNoPresentedCanvas)
{
    SynchronousReadback readback({ 1, 1 });

    readback.Complete(false);

    EXPECT_FALSE(readback.Wait());
}

TEST(GpuFoundationTest, SynchronousReadbackRejectsEmptyExtents)
{
    EXPECT_THROW((void)SynchronousReadback({ 0, 1 }), std::invalid_argument);
    EXPECT_THROW((void)SynchronousReadback({ 1, 0 }), std::invalid_argument);
}

TEST(GpuFoundationTest, ReadbackAttachesToNewestPendingVisualFrame)
{
    LatestFrameMailbox mailbox;
    auto visual = std::make_unique<RecordedFramePacket>();
    visual->frameNumber = 42;
    visual->hasVisualFrame = true;
    ASSERT_TRUE(mailbox.Publish(std::move(visual)).accepted);
    auto readback = std::make_shared<SynchronousReadback>(Extent{ 4, 3 });

    const auto result = mailbox.PublishReadback(readback);

    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.released, nullptr);
    const auto packet = mailbox.WaitTakeNewest();
    ASSERT_NE(packet, nullptr);
    EXPECT_TRUE(packet->hasVisualFrame);
    EXPECT_EQ(packet->frameNumber, 42u);
    EXPECT_EQ(packet->readback, readback);
}

TEST(GpuFoundationTest, ReadbackPublishesAControlPacketWithoutPendingVisualWork)
{
    LatestFrameMailbox mailbox;
    auto readback = std::make_shared<SynchronousReadback>(Extent{ 4, 3 });

    ASSERT_TRUE(mailbox.PublishReadback(readback).accepted);

    const auto packet = mailbox.WaitTakeNewest();
    ASSERT_NE(packet, nullptr);
    EXPECT_FALSE(packet->hasVisualFrame);
    EXPECT_EQ(packet->readback, readback);
}

TEST(GpuFoundationTest, AttachedReadbackFollowsReplacementVisualFrame)
{
    LatestFrameMailbox mailbox;
    auto first = std::make_unique<RecordedFramePacket>();
    first->frameNumber = 51;
    first->hasVisualFrame = true;
    ASSERT_TRUE(mailbox.Publish(std::move(first)).accepted);
    auto readback = std::make_shared<SynchronousReadback>(Extent{ 2, 2 });
    ASSERT_TRUE(mailbox.PublishReadback(readback).accepted);

    auto newest = std::make_unique<RecordedFramePacket>();
    newest->frameNumber = 52;
    newest->hasVisualFrame = true;
    auto result = mailbox.Publish(std::move(newest));

    ASSERT_TRUE(result.accepted);
    ASSERT_NE(result.released, nullptr);
    EXPECT_EQ(result.released->readback, nullptr);
    const auto packet = mailbox.WaitTakeNewest();
    ASSERT_NE(packet, nullptr);
    EXPECT_EQ(packet->frameNumber, 52u);
    EXPECT_EQ(packet->readback, readback);
}

TEST(GpuFoundationTest, TimingBoundaryFollowsReplacementVisualFrame)
{
    LatestFrameMailbox mailbox;
    auto first = std::make_unique<RecordedFramePacket>();
    first->frameNumber = 61;
    first->hasVisualFrame = true;
    ASSERT_TRUE(mailbox.Publish(std::move(first)).accepted);
    auto boundary = std::make_shared<SynchronousFrameBoundary>();
    ASSERT_TRUE(mailbox.PublishTimingBoundary(boundary).accepted);

    auto newest = std::make_unique<RecordedFramePacket>();
    newest->frameNumber = 62;
    newest->hasVisualFrame = true;
    auto result = mailbox.Publish(std::move(newest));

    ASSERT_TRUE(result.accepted);
    ASSERT_NE(result.released, nullptr);
    EXPECT_EQ(result.released->timingBoundary, nullptr);
    const auto packet = mailbox.WaitTakeNewest();
    ASSERT_NE(packet, nullptr);
    EXPECT_EQ(packet->frameNumber, 62u);
    EXPECT_EQ(packet->timingBoundary, boundary);
    packet->timingBoundary->Complete();
    boundary->Wait();
}

TEST(GpuFoundationTest, TimingBoundaryPublishesAControlPacketWithoutVisualWork)
{
    LatestFrameMailbox mailbox;
    auto boundary = std::make_shared<SynchronousFrameBoundary>();

    ASSERT_TRUE(mailbox.PublishTimingBoundary(boundary).accepted);

    const auto packet = mailbox.WaitTakeNewest();
    ASSERT_NE(packet, nullptr);
    EXPECT_FALSE(packet->hasVisualFrame);
    EXPECT_EQ(packet->timingBoundary, boundary);
}

TEST(GpuFoundationTest, TimingBoundaryOnUnpresentedVisualCanStillBeSettled)
{
    LatestFrameMailbox mailbox;
    auto visual = std::make_unique<RecordedFramePacket>();
    visual->frameNumber = 63;
    visual->hasVisualFrame = true;
    ASSERT_TRUE(mailbox.Publish(std::move(visual)).accepted);
    auto boundary = std::make_shared<SynchronousFrameBoundary>();
    ASSERT_TRUE(mailbox.PublishTimingBoundary(boundary).accepted);

    const auto packet = mailbox.WaitTakeNewest();
    ASSERT_NE(packet, nullptr);
    ASSERT_EQ(packet->timingBoundary, boundary);
    packet->timingBoundary->Complete();

    boundary->Wait();
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
    EXPECT_EQ(config.drawableExtent, (Extent{}));
    EXPECT_EQ(config.frameAcquireMode, FrameAcquireMode::Wait);
    EXPECT_EQ(config.outputColorMode, OutputColorMode::Sdr);
    EXPECT_FLOAT_EQ(config.hdrPaperWhiteNits, 203.0f);
}

#ifdef ENABLE_VULKAN
TEST(GpuFoundationTest, VulkanHdr10ClassificationRequiresAnApprovedExactPair)
{
    using OpenRCT2::Ui::Vulkan::IsHdr10SurfaceFormat;

    EXPECT_TRUE(IsHdr10SurfaceFormat(
        { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT }));
    EXPECT_TRUE(IsHdr10SurfaceFormat(
        { VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT }));
    EXPECT_FALSE(IsHdr10SurfaceFormat(
        { VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_HDR10_ST2084_EXT }));
    EXPECT_FALSE(IsHdr10SurfaceFormat(
        { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }));
}

TEST(GpuFoundationTest, VulkanGpuTimestampDurationsConvertDeviceTicksAndPassBoundaries)
{
    using namespace OpenRCT2::Ui::Vulkan;
    constexpr std::array<uint64_t, kGpuTimestampCount> timestamps = { 100, 130, 160, 190, 220 };
    const auto durations = CalculateGpuTimestampDurations(timestamps, 64, 2.0);
    ASSERT_TRUE(durations.has_value());
    EXPECT_DOUBLE_EQ(durations->totalMicroseconds, 0.24);
    EXPECT_DOUBLE_EQ(durations->uploadMicroseconds, 0.06);
    EXPECT_DOUBLE_EQ(durations->drawMicroseconds, 0.06);
    EXPECT_DOUBLE_EQ(durations->lightFxMicroseconds, 0.06);
    EXPECT_DOUBLE_EQ(durations->compositeMicroseconds, 0.06);
}

TEST(GpuFoundationTest, VulkanGpuTimestampDurationsHandleQueueCounterWrap)
{
    using namespace OpenRCT2::Ui::Vulkan;
    constexpr std::array<uint64_t, kGpuTimestampCount> timestamps = { 250, 5, 20, 40, 60 };
    const auto durations = CalculateGpuTimestampDurations(timestamps, 8, 1.0);
    ASSERT_TRUE(durations.has_value());
    EXPECT_DOUBLE_EQ(durations->totalMicroseconds, 0.066);
    EXPECT_DOUBLE_EQ(durations->uploadMicroseconds, 0.011);
    EXPECT_DOUBLE_EQ(durations->drawMicroseconds, 0.015);
    EXPECT_DOUBLE_EQ(durations->lightFxMicroseconds, 0.020);
    EXPECT_DOUBLE_EQ(durations->compositeMicroseconds, 0.020);
    EXPECT_FALSE(CalculateGpuTimestampDurations(timestamps, 0, 1.0).has_value());
    EXPECT_FALSE(CalculateGpuTimestampDurations(timestamps, 8, 0.0).has_value());
}

TEST(GpuFoundationTest, VulkanHdr10SelectionHonoursAvailabilityAndUserPreference)
{
    using OpenRCT2::Ui::Vulkan::SelectSurfaceFormat;
    constexpr VkSurfaceFormatKHR sdr = {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
    };
    constexpr VkSurfaceFormatKHR hdr10 = {
        VK_FORMAT_A2R10G10B10_UNORM_PACK32,
        VK_COLOR_SPACE_HDR10_ST2084_EXT,
    };
    constexpr std::array formats = { sdr, hdr10 };

    const auto enabled = SelectSurfaceFormat(formats, true);
    EXPECT_TRUE(enabled.hdr10Available);
    EXPECT_TRUE(enabled.hdr10Active);
    EXPECT_EQ(enabled.surfaceFormat.format, hdr10.format);
    EXPECT_EQ(enabled.surfaceFormat.colorSpace, hdr10.colorSpace);

    const auto disabled = SelectSurfaceFormat(formats, false);
    EXPECT_TRUE(disabled.hdr10Available);
    EXPECT_FALSE(disabled.hdr10Active);
    EXPECT_EQ(disabled.surfaceFormat.format, sdr.format);
    EXPECT_EQ(disabled.surfaceFormat.colorSpace, sdr.colorSpace);

    constexpr std::array hdrOnlyFormats = { hdr10 };
    const auto disabledHdrOnly = SelectSurfaceFormat(hdrOnlyFormats, false);
    EXPECT_TRUE(disabledHdrOnly.hdr10Available);
    EXPECT_FALSE(disabledHdrOnly.hdr10Active);
    EXPECT_EQ(disabledHdrOnly.surfaceFormat.format, hdr10.format);
    EXPECT_EQ(disabledHdrOnly.surfaceFormat.colorSpace, hdr10.colorSpace);
}

TEST(GpuFoundationTest, VulkanHdr10FallbacksNeverActivateWithoutAnApprovedPair)
{
    using OpenRCT2::Ui::Vulkan::SelectSurfaceFormat;
    constexpr VkSurfaceFormatKHR nonTenBitHdr = {
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_COLOR_SPACE_HDR10_ST2084_EXT,
    };
    constexpr std::array nonTenBitFormats = { nonTenBitHdr };

    const auto nonTenBit = SelectSurfaceFormat(nonTenBitFormats, true);
    EXPECT_FALSE(nonTenBit.hdr10Available);
    EXPECT_FALSE(nonTenBit.hdr10Active);
    EXPECT_EQ(nonTenBit.surfaceFormat.format, nonTenBitHdr.format);
    EXPECT_EQ(nonTenBit.surfaceFormat.colorSpace, nonTenBitHdr.colorSpace);

    constexpr VkSurfaceFormatKHR undefinedHdr = {
        VK_FORMAT_UNDEFINED,
        VK_COLOR_SPACE_HDR10_ST2084_EXT,
    };
    constexpr std::array undefinedFormats = { undefinedHdr };
    const auto undefined = SelectSurfaceFormat(undefinedFormats, true);
    EXPECT_FALSE(undefined.hdr10Available);
    EXPECT_FALSE(undefined.hdr10Active);
    EXPECT_EQ(undefined.surfaceFormat.format, VK_FORMAT_B8G8R8A8_UNORM);
    EXPECT_EQ(undefined.surfaceFormat.colorSpace, undefinedHdr.colorSpace);
}

TEST(GpuFoundationTest, VulkanOutputRejectsUnsupportedOrInactiveColourSpacePairs)
{
    using OpenRCT2::Ui::Vulkan::IsSupportedOutputSurfaceFormat;

    constexpr VkSurfaceFormatKHR sdr = {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
    };
    constexpr VkSurfaceFormatKHR hdr10 = {
        VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        VK_COLOR_SPACE_HDR10_ST2084_EXT,
    };
    constexpr VkSurfaceFormatKHR unsupportedHdr = {
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_COLOR_SPACE_HDR10_ST2084_EXT,
    };

    EXPECT_TRUE(IsSupportedOutputSurfaceFormat({ .surfaceFormat = sdr }));
    EXPECT_TRUE(IsSupportedOutputSurfaceFormat(
        { .surfaceFormat = hdr10, .hdr10Available = true, .hdr10Active = true }));
    EXPECT_FALSE(IsSupportedOutputSurfaceFormat({ .surfaceFormat = hdr10, .hdr10Available = true }));
    EXPECT_FALSE(IsSupportedOutputSurfaceFormat({ .surfaceFormat = unsupportedHdr }));
}

TEST(GpuFoundationTest, VulkanStraightAlphaCompositeSelectionNeverClaimsPremultipliedOutput)
{
    using OpenRCT2::Ui::Vulkan::SelectStraightAlphaCompositeMode;

    const auto opaque = SelectStraightAlphaCompositeMode(
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR | VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR
        | VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR);
    ASSERT_TRUE(opaque.has_value());
    EXPECT_EQ(*opaque, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);

    const auto straight = SelectStraightAlphaCompositeMode(
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR | VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR);
    ASSERT_TRUE(straight.has_value());
    EXPECT_EQ(*straight, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR);

    const auto inherited = SelectStraightAlphaCompositeMode(
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR | VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR);
    ASSERT_TRUE(inherited.has_value());
    EXPECT_EQ(*inherited, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR);

    EXPECT_FALSE(SelectStraightAlphaCompositeMode(VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR).has_value());
}
#endif

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

TEST(GpuFoundationTest, LightFxSnapshotOwnsViewportResolvedFrameData)
{
    LightFxFrameSnapshot snapshot;
    snapshot.width = 3;
    snapshot.height = 2;
    snapshot.intensities.resize(6, std::byte{ 7 });
    snapshot.lightPalette[4] = std::byte{ 11 };
    snapshot.lights.push_back({
        .destinationX = 1,
        .destinationY = 1,
        .width = 2,
        .height = 1,
        .sourceOffset = 8,
        .sourceStride = 32,
        .type = 4,
        .intensity = 200,
    });

    EXPECT_TRUE(snapshot.IsValid());
    EXPECT_EQ(snapshot.intensities.size(), 6u);
    EXPECT_EQ(snapshot.lightPalette[4], std::byte{ 11 });
    ASSERT_EQ(snapshot.lights.size(), 1u);
    EXPECT_EQ(snapshot.lights[0].sourceStride, 32u);
    EXPECT_EQ(snapshot.lights[0].intensity, 200u);

    snapshot.intensities.clear();
    EXPECT_TRUE(snapshot.IsValid());
    EXPECT_FALSE(snapshot.HasCpuIntensity());

    snapshot.intensities.resize(5);
    EXPECT_FALSE(snapshot.IsValid());
}

TEST(GpuFoundationTest, ResolvedLightCommandsValidateCanvasAndBakedTextureBounds)
{
    constexpr LightFxCommand valid = {
        .destinationX = 0,
        .destinationY = 0,
        .width = 16,
        .height = 16,
        .sourceOffset = 8,
        .sourceStride = 32,
        .type = 4,
        .intensity = 255,
    };
    EXPECT_TRUE(IsValidLightFxCommand(valid, 320, 200));

    auto invalidType = valid;
    invalidType.type = 12;
    EXPECT_FALSE(IsValidLightFxCommand(invalidType, 320, 200));

    auto outsideCanvas = valid;
    outsideCanvas.destinationX = 319;
    EXPECT_FALSE(IsValidLightFxCommand(outsideCanvas, 320, 200));

    auto outsideTexture = valid;
    outsideTexture.sourceOffset = 1020;
    EXPECT_FALSE(IsValidLightFxCommand(outsideTexture, 320, 200));
}

TEST(GpuFoundationTest, LightFxAtomicContributionMatchesLegacyIntegerScaling)
{
    EXPECT_EQ(GetLightFxContribution(255, 255), 255u);
    EXPECT_EQ(GetLightFxContribution(255, 0), 0u);
    EXPECT_EQ(GetLightFxContribution(255, 127), 127u);
    EXPECT_EQ(GetLightFxContribution(64, 200), (64u * 201u) >> 8);
    EXPECT_EQ(std::min(255u, GetLightFxContribution(200, 255) + GetLightFxContribution(100, 255)), 255u);
}

TEST(GpuFoundationTest, LightFxCommandRasterMatchesAllBakedFalloffsAndIntensityScales)
{
    LightFx::Init();
    const auto falloffs = LightFx::CaptureBakedFalloffs();
    ASSERT_EQ(falloffs.size(), 8u * 256 * 256);
    constexpr std::array types = {
        LightFx::LightType::lantern0, LightFx::LightType::lantern1, LightFx::LightType::lantern2,
        LightFx::LightType::lantern3, LightFx::LightType::spot0, LightFx::LightType::spot1,
        LightFx::LightType::spot2, LightFx::LightType::spot3,
    };
    constexpr std::array<uint8_t, 5> intensities = { 0, 1, 127, 254, 255 };

    for (const auto type : types)
    {
        const uint32_t typeValue = static_cast<uint32_t>(type);
        const uint32_t size = GetLightFxTextureSize(typeValue);
        const size_t layerOffset = static_cast<size_t>(typeValue - 4) * 256 * 256;
        for (const uint8_t intensity : intensities)
        {
            SCOPED_TRACE(testing::Message() << "type=" << typeValue << " intensity=" << static_cast<int>(intensity));
            LightFx::FrameSnapshot::ResolvedLight command;
            ASSERT_TRUE(LightFx::ResolveLightCommandForCanvas(
                static_cast<int32_t>(size / 2), static_cast<int32_t>(size / 2), size, size, type, intensity,
                command));
            ASSERT_EQ(command.destinationX, 0);
            ASSERT_EQ(command.destinationY, 0);
            ASSERT_EQ(command.width, size);
            ASSERT_EQ(command.height, size);
            ASSERT_EQ(command.sourceOffset, 0u);
            ASSERT_EQ(command.sourceStride, size);
            const auto compact = MakeLightFxCommand(command);
            ASSERT_TRUE(IsValidLightFxCommand(compact, size, size));
            EXPECT_EQ(compact.destinationX, command.destinationX);
            EXPECT_EQ(compact.destinationY, command.destinationY);
            EXPECT_EQ(compact.width, command.width);
            EXPECT_EQ(compact.height, command.height);
            EXPECT_EQ(compact.sourceOffset, command.sourceOffset);
            EXPECT_EQ(compact.sourceStride, command.sourceStride);
            EXPECT_EQ(compact.type, command.type);
            EXPECT_EQ(compact.intensity, command.intensity);

            std::vector<uint8_t> actual(static_cast<size_t>(size) * size);
            ASSERT_TRUE(LightFx::RasterizeResolvedLightCommands(size, size, { &command, 1 }, actual));
            const LightFx::FrameSnapshot::ResolvedLight compactReplay = {
                .destinationX = compact.destinationX,
                .destinationY = compact.destinationY,
                .width = compact.width,
                .height = compact.height,
                .sourceOffset = compact.sourceOffset,
                .sourceStride = compact.sourceStride,
                .type = compact.type,
                .intensity = compact.intensity,
            };
            std::vector<uint8_t> replayed(actual.size());
            ASSERT_TRUE(LightFx::RasterizeResolvedLightCommands(size, size, { &compactReplay, 1 }, replayed));
            EXPECT_EQ(replayed, actual);
            std::vector<uint8_t> expected(actual.size());
            for (uint32_t y = 0; y < size; y++)
            {
                for (uint32_t x = 0; x < size; x++)
                {
                    const uint32_t falloff = std::to_integer<uint8_t>(falloffs[layerOffset + y * 256 + x]);
                    expected[static_cast<size_t>(y) * size + x] = static_cast<uint8_t>(
                        GetLightFxContribution(falloff, intensity));
                }
            }
            EXPECT_EQ(actual, expected);
        }
    }
}

TEST(GpuFoundationTest, LightFxResolvedCommandsPreserveAllFourClippedEdges)
{
    LightFx::Init();
    constexpr std::array types = {
        LightFx::LightType::lantern0, LightFx::LightType::lantern1, LightFx::LightType::lantern2,
        LightFx::LightType::lantern3, LightFx::LightType::spot0, LightFx::LightType::spot1,
        LightFx::LightType::spot2, LightFx::LightType::spot3,
    };

    for (const auto type : types)
    {
        const uint32_t size = GetLightFxTextureSize(static_cast<uint32_t>(type));
        LightFx::FrameSnapshot::ResolvedLight fullCommand;
        ASSERT_TRUE(LightFx::ResolveLightCommandForCanvas(
            static_cast<int32_t>(size / 2), static_cast<int32_t>(size / 2), size, size, type, 255, fullCommand));
        std::vector<uint8_t> fullRaster(static_cast<size_t>(size) * size);
        ASSERT_TRUE(LightFx::RasterizeResolvedLightCommands(size, size, { &fullCommand, 1 }, fullRaster));

        const uint32_t canvasSize = size + 20;
        const std::array centres = {
            std::pair{ 0, static_cast<int32_t>(canvasSize / 2) },
            std::pair{ static_cast<int32_t>(canvasSize - 1), static_cast<int32_t>(canvasSize / 2) },
            std::pair{ static_cast<int32_t>(canvasSize / 2), 0 },
            std::pair{ static_cast<int32_t>(canvasSize / 2), static_cast<int32_t>(canvasSize - 1) },
        };
        for (const auto [centreX, centreY] : centres)
        {
            SCOPED_TRACE(testing::Message() << "type=" << static_cast<uint32_t>(type) << " centre=" << centreX << ','
                                            << centreY);
            LightFx::FrameSnapshot::ResolvedLight clipped;
            ASSERT_TRUE(LightFx::ResolveLightCommandForCanvas(
                centreX, centreY, canvasSize, canvasSize, type, 255, clipped));
            const int32_t unclippedLeft = centreX - static_cast<int32_t>(size / 2);
            const int32_t unclippedTop = centreY - static_cast<int32_t>(size / 2);
            const int32_t expectedLeft = std::max(unclippedLeft, 0);
            const int32_t expectedTop = std::max(unclippedTop, 0);
            const int32_t expectedRight = std::min(unclippedLeft + static_cast<int32_t>(size),
                                                   static_cast<int32_t>(canvasSize));
            const int32_t expectedBottom = std::min(unclippedTop + static_cast<int32_t>(size),
                                                    static_cast<int32_t>(canvasSize));
            ASSERT_EQ(clipped.destinationX, expectedLeft);
            ASSERT_EQ(clipped.destinationY, expectedTop);
            ASSERT_EQ(clipped.width, static_cast<uint32_t>(expectedRight - expectedLeft));
            ASSERT_EQ(clipped.height, static_cast<uint32_t>(expectedBottom - expectedTop));
            ASSERT_EQ(clipped.sourceStride, size);
            ASSERT_EQ(
                clipped.sourceOffset,
                static_cast<uint32_t>((expectedTop - unclippedTop) * static_cast<int32_t>(size)
                                      + expectedLeft - unclippedLeft));
            std::vector<uint8_t> actual(static_cast<size_t>(canvasSize) * canvasSize);
            ASSERT_TRUE(LightFx::RasterizeResolvedLightCommands(canvasSize, canvasSize, { &clipped, 1 }, actual));

            std::vector<uint8_t> expected(actual.size());
            for (uint32_t y = 0; y < canvasSize; y++)
            {
                for (uint32_t x = 0; x < canvasSize; x++)
                {
                    const bool inside = x >= static_cast<uint32_t>(clipped.destinationX)
                        && x < static_cast<uint32_t>(clipped.destinationX) + clipped.width
                        && y >= static_cast<uint32_t>(clipped.destinationY)
                        && y < static_cast<uint32_t>(clipped.destinationY) + clipped.height;
                    if (inside)
                    {
                        const uint32_t sourceX = x - static_cast<uint32_t>(clipped.destinationX);
                        const uint32_t sourceY = y - static_cast<uint32_t>(clipped.destinationY);
                        expected[static_cast<size_t>(y) * canvasSize + x] =
                            fullRaster[clipped.sourceOffset + sourceY * clipped.sourceStride + sourceX];
                    }
                }
            }
            EXPECT_EQ(actual, expected);
        }
    }
}

TEST(GpuFoundationTest, LightFxNarrowCanvasUsesTheLegacyFlatClampedSourceStride)
{
    LightFx::Init();
    constexpr std::array types = {
        LightFx::LightType::lantern0, LightFx::LightType::lantern1, LightFx::LightType::lantern2,
        LightFx::LightType::lantern3, LightFx::LightType::spot0, LightFx::LightType::spot1,
        LightFx::LightType::spot2, LightFx::LightType::spot3,
    };
    constexpr std::array<uint8_t, 5> intensities = { 0, 1, 127, 254, 255 };
    constexpr std::array smallCanvases = {
        std::pair{ 1u, 1u }, std::pair{ 3u, 2u }, std::pair{ 7u, 5u }, std::pair{ 31u, 9u }
    };

    for (const auto type : types)
    {
        const uint32_t nativeSize = GetLightFxTextureSize(static_cast<uint32_t>(type));
        for (const uint8_t intensity : intensities)
        {
            LightFx::FrameSnapshot::ResolvedLight fullCommand;
            ASSERT_TRUE(LightFx::ResolveLightCommandForCanvas(
                static_cast<int32_t>(nativeSize / 2), static_cast<int32_t>(nativeSize / 2), nativeSize, nativeSize,
                type, intensity, fullCommand));
            std::vector<uint8_t> fullRaster(static_cast<size_t>(nativeSize) * nativeSize);
            ASSERT_TRUE(
                LightFx::RasterizeResolvedLightCommands(nativeSize, nativeSize, { &fullCommand, 1 }, fullRaster));

            for (const auto [width, height] : smallCanvases)
            {
                SCOPED_TRACE(testing::Message() << "type=" << static_cast<uint32_t>(type)
                                                << " intensity=" << static_cast<int>(intensity) << " canvas=" << width
                                                << 'x' << height);
                LightFx::FrameSnapshot::ResolvedLight narrow;
                ASSERT_TRUE(LightFx::ResolveLightCommandForCanvas(
                    static_cast<int32_t>(width / 2), static_cast<int32_t>(height / 2), width, height, type,
                    intensity, narrow));
                ASSERT_EQ(narrow.destinationX, 0);
                ASSERT_EQ(narrow.destinationY, 0);
                ASSERT_EQ(narrow.width, width);
                ASSERT_EQ(narrow.height, height);
                ASSERT_EQ(narrow.sourceOffset, 0u);
                ASSERT_EQ(narrow.sourceStride, width);

                std::vector<uint8_t> actual(static_cast<size_t>(width) * height);
                ASSERT_TRUE(LightFx::RasterizeResolvedLightCommands(width, height, { &narrow, 1 }, actual));
                EXPECT_TRUE(std::equal(actual.begin(), actual.end(), fullRaster.begin()));
            }
        }
    }
}

TEST(GpuFoundationTest, LightFxOverlapsSaturateExactlyAfterExceeding255)
{
    LightFx::Init();
    constexpr uint32_t size = 32;
    LightFx::FrameSnapshot::ResolvedLight command;
    ASSERT_TRUE(LightFx::ResolveLightCommandForCanvas(
        size / 2, size / 2, size, size, LightFx::LightType::lantern0, 255, command));

    std::vector<uint8_t> single(size * size);
    ASSERT_TRUE(LightFx::RasterizeResolvedLightCommands(size, size, { &command, 1 }, single));
    const uint32_t maximumContribution = *std::max_element(single.begin(), single.end());
    ASSERT_GT(maximumContribution, 0u);
    const uint32_t overlapCount = 255u / maximumContribution + 1u;
    const std::vector overlapping(overlapCount, command);
    std::vector<uint8_t> combined(size * size);
    ASSERT_TRUE(LightFx::RasterizeResolvedLightCommands(size, size, overlapping, combined));

    std::vector<uint8_t> expected(single.size());
    for (size_t i = 0; i < single.size(); i++)
    {
        const uint32_t unsaturated = static_cast<uint32_t>(single[i]) * overlapCount;
        expected[i] = static_cast<uint8_t>(std::min<uint32_t>(255, unsaturated));
    }
    ASSERT_GT(maximumContribution * overlapCount, 255u);
    EXPECT_EQ(combined, expected);
}

TEST(GpuFoundationTest, LightFxComputeLimitsCoverTheFixedShaderDispatchShape)
{
    EXPECT_TRUE(AreLightFxComputeLimitsSufficient(256, 16, 16, kMaximumLightFxCommandCount));
    EXPECT_TRUE(AreLightFxComputeLimitsSufficient(1024, 1024, 1024, 65535));
    EXPECT_FALSE(AreLightFxComputeLimitsSufficient(255, 16, 16, kMaximumLightFxCommandCount));
    EXPECT_FALSE(AreLightFxComputeLimitsSufficient(256, 15, 16, kMaximumLightFxCommandCount));
    EXPECT_FALSE(AreLightFxComputeLimitsSufficient(256, 16, 15, kMaximumLightFxCommandCount));
    EXPECT_FALSE(AreLightFxComputeLimitsSufficient(256, 16, 16, kMaximumLightFxCommandCount - 1));
}

TEST(GpuFoundationTest, ClearingCommandStreamRetainsOnlyLightFxAllocationCapacity)
{
    FrameCommandStream commands;
    commands.lightFx.emplace();
    commands.lightFx->width = 1;
    commands.lightFx->height = 1;
    commands.lightFx->intensities.push_back(std::byte{ 1 });
    commands.lightFx->lights.push_back({ .width = 1, .height = 1, .sourceStride = 32, .type = 4, .intensity = 1 });
    ASSERT_TRUE(commands.lightFx->IsValid());
    const auto capacity = commands.lightFx->intensities.capacity();
    const auto lightCapacity = commands.lightFx->lights.capacity();

    commands.clear();

    ASSERT_TRUE(commands.lightFx.has_value());
    EXPECT_FALSE(commands.lightFx->IsValid());
    EXPECT_TRUE(commands.lightFx->intensities.empty());
    EXPECT_EQ(commands.lightFx->intensities.capacity(), capacity);
    EXPECT_TRUE(commands.lightFx->lights.empty());
    EXPECT_EQ(commands.lightFx->lights.capacity(), lightCapacity);
}

TEST(GpuFoundationTest, TextureUploadsRetainPersistentAtlasMetadata)
{
    FrameCommandStream commands;
    commands.textureUploads.push_back({
        .atlas = 7,
        .bounds = { 32, 64, 72, 84 },
        .sourcePitch = 40,
        .pixels = std::vector<std::byte>(40 * 20),
    });

    ASSERT_EQ(commands.textureUploads.size(), 1u);
    const auto& upload = commands.textureUploads[0];
    EXPECT_EQ(upload.atlas, 7u);
    EXPECT_EQ(upload.bounds.z - upload.bounds.x, 40);
    EXPECT_EQ(upload.bounds.w - upload.bounds.y, 20);
    EXPECT_EQ(upload.sourcePitch, 40u);
    EXPECT_EQ(upload.pixels.size(), 800u);

    commands.clear();
    EXPECT_TRUE(commands.textureUploads.empty());
}

TEST(GpuFoundationTest, OptionalIntegrationCapabilitiesDefaultToDisabled)
{
    const BackendCapabilities capabilities;
    EXPECT_FALSE(capabilities.supportsNonBlockingFrameAcquire);
    EXPECT_FALSE(capabilities.supportsLightFxComposition);
    EXPECT_FALSE(capabilities.supportsGpuLightFxRasterization);
    EXPECT_FALSE(capabilities.supportsAsyncReadback);
    EXPECT_FALSE(capabilities.supportsCanvasUpload);
    EXPECT_FALSE(capabilities.supportsGpuTimestamps);
    EXPECT_FALSE(capabilities.supportsHdrMetadata);
    EXPECT_FALSE(capabilities.supportsHdr10Output);
    EXPECT_FALSE(capabilities.hdr10OutputActive);
}

TEST(GpuFoundationTest, IntegratedTimingFieldsRemainExplicitlyOptional)
{
    FrameTimings timings{ .frameNumber = 42, .cpuSubmitMicroseconds = 125.0 };
    EXPECT_EQ(timings.frameNumber, 42u);
    EXPECT_DOUBLE_EQ(timings.cpuSubmitMicroseconds, 125.0);
    EXPECT_FALSE(timings.hasGpuTimestamp);
    EXPECT_FALSE(timings.hasGpuPassTimestamps);
    EXPECT_FALSE(timings.hasPresentCallMeasurement);
}
