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
#include <openrct2-ui/drawing/engines/gpu/GpuDamageTracker.h>
#include <openrct2-ui/drawing/engines/gpu/GpuFrameMailbox.h>
#include <openrct2-ui/drawing/engines/gpu/GpuTextureCache.h>
#include <openrct2-ui/drawing/engines/gpu/GpuTransparencyDepth.h>
#ifdef ENABLE_VULKAN
    #include <openrct2-ui/drawing/engines/vulkan/VulkanDevice.h>
    #include <openrct2-ui/drawing/engines/vulkan/VulkanSurfaceFormat.h>
#endif
#include <openrct2/drawing/LightFX.h>
#include <openrct2/drawing/TTF.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>

using namespace OpenRCT2::Ui::Gpu;
namespace LightFx = OpenRCT2::Drawing::LightFx;

namespace
{
    constexpr std::array kLightTypes = {
        LightFx::LightType::lantern0, LightFx::LightType::lantern1, LightFx::LightType::lantern2,
        LightFx::LightType::lantern3, LightFx::LightType::spot0, LightFx::LightType::spot1,
        LightFx::LightType::spot2, LightFx::LightType::spot3,
    };
    constexpr std::array<uint8_t, 5> kLightIntensities = { 0, 1, 127, 254, 255 };

    [[nodiscard]] std::unique_ptr<RecordedFramePacket> MakeFramePacket(uint64_t frameNumber, bool visual = false)
    {
        auto packet = std::make_unique<RecordedFramePacket>();
        packet->frameNumber = frameNumber;
        packet->hasVisualFrame = visual;
        return packet;
    }

#ifdef ENABLE_VULKAN
    constexpr VkSurfaceFormatKHR kSdrFormat{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
    constexpr VkSurfaceFormatKHR kHdr10Format{ VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT };
    constexpr VkSurfaceFormatKHR kNonTenBitHdrFormat{ VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_HDR10_ST2084_EXT };
    constexpr VkSurfaceFormatKHR kUndefinedHdrFormat{ VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_HDR10_ST2084_EXT };

    template<size_t N>
    void ExpectSurfaceSelection(
        const std::array<VkSurfaceFormatKHR, N>& formats, bool preferHdr, bool available, bool active,
        VkSurfaceFormatKHR expected)
    {
        const auto actual = OpenRCT2::Ui::Vulkan::SelectSurfaceFormat(formats, preferHdr);
        EXPECT_EQ(actual.hdr10Available, available);
        EXPECT_EQ(actual.hdr10Active, active);
        EXPECT_EQ(actual.surfaceFormat.format, expected.format);
        EXPECT_EQ(actual.surfaceFormat.colorSpace, expected.colorSpace);
    }
#endif
} // namespace

TEST(GpuFoundationTest, AtlasSizeOrdersUseTheLegacyPowerOfTwoClasses)
{
    EXPECT_EQ(AtlasPage::CalculateImageSizeOrder(1, 1), 5);
    EXPECT_EQ(AtlasPage::CalculateImageSizeOrder(32, 32), 5);
    EXPECT_EQ(AtlasPage::CalculateImageSizeOrder(33, 1), 6);
    EXPECT_EQ(AtlasPage::CalculateImageSizeOrder(128, 129), 8);
}

TEST(GpuFoundationTest, DamageRemainsPendingUntilAnAppliedSerialIsAcknowledged)
{
    DamageTracker tracker;
    tracker.Reset(128, 64, 64, 64);
    auto initial = tracker.Snapshot();
    ASSERT_TRUE(initial.fullRedraw);
    ASSERT_EQ(initial.rectangles.size(), 1u);
    EXPECT_EQ(initial.rectangles[0].x, 0);
    EXPECT_EQ(initial.rectangles[0].z, 128);
    tracker.Acknowledge(initial.serial);
    EXPECT_TRUE(tracker.Snapshot().rectangles.empty());

    tracker.Invalidate(0, 0, 32, 32);
    const auto discarded = tracker.Snapshot();
    tracker.Invalidate(96, 0, 128, 32);
    const auto replacement = tracker.Snapshot();
    ASSERT_EQ(replacement.rectangles.size(), 1u);
    EXPECT_EQ(replacement.rectangles[0].x, 0);
    EXPECT_EQ(replacement.rectangles[0].z, 128);

    tracker.Acknowledge(discarded.serial);
    const auto afterOldAcknowledgement = tracker.Snapshot();
    ASSERT_EQ(afterOldAcknowledgement.rectangles.size(), 1u);
    EXPECT_EQ(afterOldAcknowledgement.rectangles[0].x, 64);
    EXPECT_EQ(afterOldAcknowledgement.rectangles[0].z, 128);
}

TEST(GpuFoundationTest, NewDamageSurvivesAnOlderInFlightAcknowledgement)
{
    DamageTracker tracker;
    tracker.Reset(64, 64, 64, 64);
    const auto initial = tracker.Snapshot();
    tracker.Acknowledge(initial.serial);

    tracker.Invalidate(0, 0, 1, 1);
    const auto inFlight = tracker.Snapshot();
    tracker.Invalidate(1, 1, 2, 2);
    tracker.Acknowledge(inFlight.serial);
    EXPECT_FALSE(tracker.Snapshot().rectangles.empty());
}

TEST(GpuFoundationTest, DroppedFullRedrawRemainsAFullRedraw)
{
    DamageTracker tracker;
    tracker.Reset(96, 96, 32, 32);
    const auto dropped = tracker.Snapshot();
    ASSERT_TRUE(dropped.fullRedraw);

    tracker.Invalidate(0, 0, 1, 1);
    const auto replacement = tracker.Snapshot();
    EXPECT_TRUE(replacement.fullRedraw);
    tracker.Acknowledge(replacement.serial);
    EXPECT_FALSE(tracker.Snapshot().fullRedraw);
    EXPECT_TRUE(tracker.Snapshot().rectangles.empty());
}

TEST(GpuFoundationTest, DenseDamageCrossesOverToOneFullRedraw)
{
    DamageTracker tracker;
    tracker.Reset(256, 256, 64, 64);
    const auto initial = tracker.Snapshot();
    tracker.Acknowledge(initial.serial);

    tracker.Invalidate(0, 0, 256, 192);
    const auto damage = tracker.Snapshot();

    ASSERT_TRUE(damage.fullRedraw);
    ASSERT_EQ(damage.rectangles.size(), 1u);
    EXPECT_EQ(damage.rectangles[0].x, 0);
    EXPECT_EQ(damage.rectangles[0].y, 0);
    EXPECT_EQ(damage.rectangles[0].z, 256);
    EXPECT_EQ(damage.rectangles[0].w, 256);
}

TEST(GpuFoundationTest, CoalescedFullRedrawPreservesNewerMailboxDamage)
{
    DamageTracker tracker;
    tracker.Reset(128, 128, 64, 64);
    const auto initial = tracker.Snapshot();
    tracker.Acknowledge(initial.serial);

    tracker.ForceFullRedraw();
    const auto inFlight = tracker.Snapshot();
    ASSERT_TRUE(inFlight.fullRedraw);
    EXPECT_TRUE(tracker.CoalesceFullRedrawInvalidation());

    tracker.Acknowledge(inFlight.serial);
    const auto replacement = tracker.Snapshot();
    EXPECT_TRUE(replacement.fullRedraw);
    EXPECT_GT(replacement.serial, inFlight.serial);

    tracker.Acknowledge(replacement.serial);
    EXPECT_FALSE(tracker.IsFullRedrawPending());
    EXPECT_TRUE(tracker.Snapshot().rectangles.empty());
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

TEST(GpuFoundationTest, AtlasDescriptorIdentityTracksLayerAndSlotButNotAllocationSerial)
{
    TextureLocation location;
    location.index = 2;
    location.slot = 17;
    location.allocationSerial = 41;

    EXPECT_EQ(location.GetDescriptorIndex(), 2u * kAtlasSlotsPerLayer + 17u);

    auto reused = location;
    reused.allocationSerial++;
    EXPECT_EQ(reused.GetDescriptorIndex(), location.GetDescriptorIndex());

    auto nextLayer = location;
    nextLayer.index++;
    EXPECT_NE(nextLayer.GetDescriptorIndex(), location.GetDescriptorIndex());
    EXPECT_LT(nextLayer.GetDescriptorIndex(), kSpriteAssetDescriptorCount);
}

TEST(GpuFoundationTest, CompactSpritePackingPreservesPalettesAndEffects)
{
    constexpr auto palettes = SpriteCommand::PackPalettes(11, 22, 33, 3);
    EXPECT_EQ(SpriteCommand::GetPalette(palettes, 0), 11);
    EXPECT_EQ(SpriteCommand::GetPalette(palettes, 1), 22);
    EXPECT_EQ(SpriteCommand::GetPalette(palettes, 2), 33);
    EXPECT_EQ(SpriteCommand::GetPaletteCount(palettes), 3);

    constexpr uint32_t flags = RectCommand::FLAG_NO_TEXTURE | RectCommand::FLAG_MASK | 2u;
    constexpr auto effects = SpriteCommand::PackEffects(flags, 197);
    EXPECT_EQ(SpriteCommand::GetEffectFlags(effects), flags);
    EXPECT_EQ(SpriteCommand::GetEffectColour(effects), 197);
}

#ifndef DISABLE_TTF
TEST(GpuFoundationTest, ResidencyLeaseDefersEvictedTtfSlotReuseUntilRetirement)
{
    constexpr int32_t surfaceSize = 128;
    constexpr uint64_t residentSurfaceCount = 256;
    TextureCache cache(1);
    std::vector<std::byte> pixels(surfaceSize * surfaceSize);
    TTFSurface firstSurface{ pixels.data(), surfaceSize, surfaceSize, 1 };

    FrameCommandStream firstCommands;
    cache.BeginFrame();
    const auto firstBinding = cache.GetOrLoadTTFTexture(firstSurface);
    const auto firstLease = cache.SealFrame(firstCommands);
    ASSERT_TRUE(static_cast<bool>(firstLease));
    ASSERT_EQ(firstCommands.textureUploads.size(), 1u);
    EXPECT_EQ(firstCommands.textureUploads[0].pixels.size(), pixels.size());

    cache.BeginFrame();
    for (uint64_t cacheId = 2; cacheId <= residentSurfaceCount; cacheId++)
    {
        TTFSurface surface{ pixels.data(), surfaceSize, surfaceSize, cacheId };
        static_cast<void>(cache.GetOrLoadTTFTexture(surface));
    }
    TTFSurface replacementSurface{ pixels.data(), surfaceSize, surfaceSize, residentSurfaceCount + 1 };
    EXPECT_THROW((void)cache.GetOrLoadTTFTexture(replacementSurface), std::runtime_error);
    cache.AbortFrame();

    cache.RetireFrame(firstLease, FrameRetirement::Failed);

    FrameCommandStream secondCommands;
    cache.BeginFrame();
    const auto secondBinding = cache.GetOrLoadTTFTexture(replacementSurface);
    const auto secondLease = cache.SealFrame(secondCommands);
    EXPECT_NE(firstLease, secondLease);
    EXPECT_EQ(firstBinding.index, secondBinding.index);
    cache.RetireFrame(secondLease, FrameRetirement::Presented);
}

TEST(GpuFoundationTest, CachedTtfSurfaceUploadsOnceAndRemainsAtlasResident)
{
    TextureCache cache(1);
    std::array<std::byte, 8> pixels{};
    TTFSurface surface{ pixels.data(), 4, 2, 1 };

    FrameCommandStream firstCommands;
    cache.BeginFrame();
    const auto firstBinding = cache.GetOrLoadTTFTexture(surface);
    const auto repeatedBinding = cache.GetOrLoadTTFTexture(surface);
    const auto firstLease = cache.SealFrame(firstCommands);
    EXPECT_EQ(repeatedBinding.index, firstBinding.index);
    ASSERT_EQ(firstCommands.textureUploads.size(), 1u);
    const auto& upload = firstCommands.textureUploads.front();
    EXPECT_EQ(upload.pixels.size(), pixels.size());
    EXPECT_LT(upload.descriptorIndex, kSpriteAssetDescriptorCount);
    EXPECT_EQ(upload.descriptor.atlasLayer, static_cast<int32_t>(firstBinding.index));
    EXPECT_EQ(upload.descriptor.atlasOrigin.x, static_cast<int32_t>(firstBinding.coords.x));
    EXPECT_EQ(upload.descriptor.atlasOrigin.y, static_cast<int32_t>(firstBinding.coords.y));
    cache.RetireFrame(firstLease, FrameRetirement::Presented);

    FrameCommandStream secondCommands;
    cache.BeginFrame();
    const auto secondBinding = cache.GetOrLoadTTFTexture(surface);
    const auto secondLease = cache.SealFrame(secondCommands);
    EXPECT_EQ(secondBinding.index, firstBinding.index);
    EXPECT_EQ(secondBinding.coords.x, firstBinding.coords.x);
    EXPECT_EQ(secondBinding.coords.y, firstBinding.coords.y);
    EXPECT_TRUE(secondCommands.textureUploads.empty());
    cache.RetireFrame(secondLease, FrameRetirement::Presented);
}
#endif

TEST(GpuFoundationTest, NewestFrameMailboxReplacesPendingVisualWork)
{
    LatestFrameMailbox mailbox;
    auto first = MakeFramePacket(10);
    first->presentation.paletteVersion = 3;
    auto firstResult = mailbox.Publish(std::move(first));
    EXPECT_TRUE(firstResult.accepted);
    EXPECT_EQ(firstResult.released, nullptr);

    auto newest = MakeFramePacket(11);
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

TEST(GpuFoundationTest, FrameMailboxAdmissionRejectsOnlyRedundantQueuedVisualWork)
{
    LatestFrameMailbox mailbox;
    EXPECT_TRUE(mailbox.CanPublishVisualFrame());

    auto boundary = std::make_shared<SynchronousFrameBoundary>();
    ASSERT_TRUE(mailbox.PublishTimingBoundary(boundary).accepted);
    EXPECT_TRUE(mailbox.CanPublishVisualFrame());

    auto visual = MakeFramePacket(12, true);
    ASSERT_TRUE(mailbox.Publish(std::move(visual)).accepted);
    EXPECT_FALSE(mailbox.CanPublishVisualFrame());

    const auto taken = mailbox.WaitTakeNewest();
    ASSERT_NE(taken, nullptr);
    EXPECT_EQ(taken->frameNumber, 12u);
    EXPECT_EQ(taken->timingBoundary, boundary);
    EXPECT_TRUE(mailbox.CanPublishVisualFrame());

    static_cast<void>(mailbox.Stop());
    EXPECT_FALSE(mailbox.CanPublishVisualFrame());
}

TEST(GpuFoundationTest, StoppedFrameMailboxReturnsAndRejectsUnconsumedPackets)
{
    LatestFrameMailbox mailbox;
    auto pending = MakeFramePacket(20);
    ASSERT_TRUE(mailbox.Publish(std::move(pending)).accepted);

    const auto stopped = mailbox.Stop();
    ASSERT_NE(stopped, nullptr);
    EXPECT_EQ(stopped->frameNumber, 20u);
    EXPECT_EQ(mailbox.WaitTakeNewest(), nullptr);

    auto rejected = MakeFramePacket(21);
    auto rejectedResult = mailbox.Publish(std::move(rejected));
    EXPECT_FALSE(rejectedResult.accepted);
    ASSERT_NE(rejectedResult.released, nullptr);
    EXPECT_EQ(rejectedResult.released->frameNumber, 21u);
}

TEST(GpuFoundationTest, FrameMailboxKeepsOnlyOneRecycledPacket)
{
    LatestFrameMailbox mailbox;
    auto first = MakeFramePacket(30);
    mailbox.Recycle(std::move(first));
    auto newest = MakeFramePacket(31);
    mailbox.Recycle(std::move(newest));

    const auto recycled = mailbox.TakeRecycled();
    ASSERT_NE(recycled, nullptr);
    EXPECT_EQ(recycled->frameNumber, 31u);
    EXPECT_EQ(mailbox.TakeRecycled(), nullptr);
}

TEST(GpuFoundationTest, SynchronousReadbackOwnsPixelsAndReportsAvailability)
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
    SynchronousReadback unavailable({ 1, 1 });
    unavailable.Complete(false);
    EXPECT_FALSE(unavailable.Wait());
    EXPECT_THROW((void)SynchronousReadback({ 0, 1 }), std::invalid_argument);
    EXPECT_THROW((void)SynchronousReadback({ 1, 0 }), std::invalid_argument);
}

TEST(GpuFoundationTest, ReadbackAttachesToNewestPendingVisualFrame)
{
    LatestFrameMailbox mailbox;
    auto visual = MakeFramePacket(42, true);
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
    auto first = MakeFramePacket(51, true);
    ASSERT_TRUE(mailbox.Publish(std::move(first)).accepted);
    auto readback = std::make_shared<SynchronousReadback>(Extent{ 2, 2 });
    ASSERT_TRUE(mailbox.PublishReadback(readback).accepted);

    auto newest = MakeFramePacket(52, true);
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
    auto first = MakeFramePacket(61, true);
    ASSERT_TRUE(mailbox.Publish(std::move(first)).accepted);
    auto boundary = std::make_shared<SynchronousFrameBoundary>();
    ASSERT_TRUE(mailbox.PublishTimingBoundary(boundary).accepted);

    auto newest = MakeFramePacket(62, true);
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
    auto visual = MakeFramePacket(63, true);
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

TEST(GpuFoundationTest, OutputDefaultsPreserveLegacySdrPresentation)
{
    const BackendConfig config;
    EXPECT_EQ(config.drawableExtent, (Extent{}));
    EXPECT_EQ(config.frameAcquireMode, FrameAcquireMode::Wait);
    EXPECT_EQ(config.outputColorMode, OutputColorMode::Sdr);
    EXPECT_FLOAT_EQ(config.hdrPaperWhiteNits, 203.0f);
}

#ifdef ENABLE_VULKAN
TEST(GpuFoundationTest, VulkanPresentModeSelectionHonoursVSync)
{
    using OpenRCT2::Ui::Vulkan::SelectPresentMode;
    constexpr std::array modes = {
        VK_PRESENT_MODE_FIFO_KHR,
        VK_PRESENT_MODE_MAILBOX_KHR,
        VK_PRESENT_MODE_IMMEDIATE_KHR,
    };

    EXPECT_EQ(SelectPresentMode(modes, true), VK_PRESENT_MODE_MAILBOX_KHR);
    EXPECT_EQ(SelectPresentMode(modes, false), VK_PRESENT_MODE_IMMEDIATE_KHR);
    constexpr std::array fifoModes = {
        VK_PRESENT_MODE_FIFO_KHR,
        VK_PRESENT_MODE_IMMEDIATE_KHR,
    };
    EXPECT_EQ(SelectPresentMode(fifoModes, true), VK_PRESENT_MODE_FIFO_KHR);
    EXPECT_EQ(SelectPresentMode(fifoModes, false), VK_PRESENT_MODE_IMMEDIATE_KHR);
}

TEST(GpuFoundationTest, VulkanHdr10ClassificationRequiresAnApprovedExactPair)
{
    using OpenRCT2::Ui::Vulkan::IsHdr10SurfaceFormat;

    EXPECT_TRUE(IsHdr10SurfaceFormat({ VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT }));
    EXPECT_TRUE(IsHdr10SurfaceFormat(kHdr10Format));
    EXPECT_FALSE(IsHdr10SurfaceFormat(kNonTenBitHdrFormat));
    EXPECT_FALSE(IsHdr10SurfaceFormat(
        { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }));
}

TEST(GpuFoundationTest, VulkanGpuTimestampDurationsHandleLinearAndWrappedCounters)
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
    constexpr std::array<uint64_t, kGpuTimestampCount> wrapped = { 250, 5, 20, 40, 60 };
    const auto wrappedDurations = CalculateGpuTimestampDurations(wrapped, 8, 1.0);
    ASSERT_TRUE(wrappedDurations.has_value());
    EXPECT_DOUBLE_EQ(wrappedDurations->totalMicroseconds, 0.066);
    EXPECT_DOUBLE_EQ(wrappedDurations->uploadMicroseconds, 0.011);
    EXPECT_DOUBLE_EQ(wrappedDurations->drawMicroseconds, 0.015);
    EXPECT_DOUBLE_EQ(wrappedDurations->lightFxMicroseconds, 0.020);
    EXPECT_DOUBLE_EQ(wrappedDurations->compositeMicroseconds, 0.020);
    EXPECT_FALSE(CalculateGpuTimestampDurations(wrapped, 0, 1.0).has_value());
    EXPECT_FALSE(CalculateGpuTimestampDurations(wrapped, 8, 0.0).has_value());
}

TEST(GpuFoundationTest, VulkanHdr10SelectionHonoursAvailabilityAndUserPreference)
{
    constexpr std::array formats = { kSdrFormat, kHdr10Format };
    ExpectSurfaceSelection(formats, true, true, true, kHdr10Format);
    ExpectSurfaceSelection(formats, false, true, false, kSdrFormat);
    ExpectSurfaceSelection(std::array{ kHdr10Format }, false, true, false, kHdr10Format);
}

TEST(GpuFoundationTest, VulkanHdr10FallbacksNeverActivateWithoutAnApprovedPair)
{
    ExpectSurfaceSelection(
        std::array{ kNonTenBitHdrFormat }, true, false, false, kNonTenBitHdrFormat);
    ExpectSurfaceSelection(
        std::array{ kUndefinedHdrFormat }, true, false, false,
        { VK_FORMAT_B8G8R8A8_UNORM, kUndefinedHdrFormat.colorSpace });
}

TEST(GpuFoundationTest, VulkanOutputRejectsUnsupportedOrInactiveColourSpacePairs)
{
    using OpenRCT2::Ui::Vulkan::IsSupportedOutputSurfaceFormat;

    constexpr VkSurfaceFormatKHR hdr10{ VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT };
    EXPECT_TRUE(IsSupportedOutputSurfaceFormat({ .surfaceFormat = kSdrFormat }));
    EXPECT_TRUE(IsSupportedOutputSurfaceFormat(
        { .surfaceFormat = hdr10, .hdr10Available = true, .hdr10Active = true }));
    EXPECT_FALSE(IsSupportedOutputSurfaceFormat({ .surfaceFormat = hdr10, .hdr10Available = true }));
    EXPECT_FALSE(IsSupportedOutputSurfaceFormat({ .surfaceFormat = kNonTenBitHdrFormat }));
}

TEST(GpuFoundationTest, VulkanStraightAlphaCompositeSelectionNeverClaimsPremultipliedOutput)
{
    using OpenRCT2::Ui::Vulkan::SelectStraightAlphaCompositeMode;
    const auto expect = [](VkCompositeAlphaFlagsKHR supported, std::optional<VkCompositeAlphaFlagBitsKHR> expected) {
        EXPECT_EQ(SelectStraightAlphaCompositeMode(supported), expected);
    };
    expect(
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR | VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR
            | VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);
    expect(
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR | VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR);
    expect(
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR | VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR);
    expect(VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, std::nullopt);
}
#endif

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
    for (const auto type : kLightTypes)
    {
        const uint32_t typeValue = static_cast<uint32_t>(type);
        const uint32_t size = GetLightFxTextureSize(typeValue);
        const size_t layerOffset = static_cast<size_t>(typeValue - 4) * 256 * 256;
        for (const uint8_t intensity : kLightIntensities)
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
                std::transform(
                    falloffs.begin() + layerOffset + y * 256, falloffs.begin() + layerOffset + y * 256 + size,
                    expected.begin() + static_cast<size_t>(y) * size,
                    [intensity](std::byte falloff) {
                        return static_cast<uint8_t>(GetLightFxContribution(std::to_integer<uint8_t>(falloff), intensity));
                    });
            EXPECT_EQ(actual, expected);
        }
    }
}

TEST(GpuFoundationTest, LightFxResolvedCommandsPreserveAllFourClippedEdges)
{
    LightFx::Init();
    for (const auto type : kLightTypes)
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
            for (uint32_t y = 0; y < clipped.height; y++)
                std::copy_n(
                    fullRaster.begin() + clipped.sourceOffset + y * clipped.sourceStride, clipped.width,
                    expected.begin() + (static_cast<size_t>(clipped.destinationY) + y) * canvasSize
                        + clipped.destinationX);
            EXPECT_EQ(actual, expected);
        }
    }
}

TEST(GpuFoundationTest, LightFxNarrowCanvasUsesTheLegacyFlatClampedSourceStride)
{
    LightFx::Init();
    constexpr std::array smallCanvases = {
        std::pair{ 1u, 1u }, std::pair{ 3u, 2u }, std::pair{ 7u, 5u }, std::pair{ 31u, 9u }
    };

    for (const auto type : kLightTypes)
    {
        const uint32_t nativeSize = GetLightFxTextureSize(static_cast<uint32_t>(type));
        for (const uint8_t intensity : kLightIntensities)
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

TEST(GpuFoundationTest, ClearingCommandStreamRetainsLightFxAllocationCapacity)
{
    FrameCommandStream commands;
    commands.textureUploads.push_back({
        .atlas = 7,
        .bounds = { 32, 64, 72, 84 },
        .sourcePitch = 40,
        .pixels = std::vector<std::byte>(40 * 20),
    });
    commands.lightFx.emplace();
    commands.lightFx->width = 1;
    commands.lightFx->height = 1;
    commands.lightFx->intensities.push_back(std::byte{ 1 });
    commands.lightFx->lights.push_back({ .width = 1, .height = 1, .sourceStride = 32, .type = 4, .intensity = 1 });
    ASSERT_TRUE(commands.lightFx->IsValid());
    const auto capacity = commands.lightFx->intensities.capacity();
    const auto lightCapacity = commands.lightFx->lights.capacity();
    ASSERT_EQ(commands.textureUploads.size(), 1u);
    const auto& upload = commands.textureUploads[0];
    EXPECT_EQ(upload.atlas, 7u);
    EXPECT_EQ(upload.bounds.z - upload.bounds.x, 40);
    EXPECT_EQ(upload.bounds.w - upload.bounds.y, 20);
    EXPECT_EQ(upload.sourcePitch, 40u);
    EXPECT_EQ(upload.pixels.size(), 800u);

    commands.clear();

    ASSERT_TRUE(commands.lightFx.has_value());
    EXPECT_FALSE(commands.lightFx->IsValid());
    EXPECT_TRUE(commands.lightFx->intensities.empty());
    EXPECT_EQ(commands.lightFx->intensities.capacity(), capacity);
    EXPECT_TRUE(commands.lightFx->lights.empty());
    EXPECT_EQ(commands.lightFx->lights.capacity(), lightCapacity);
    EXPECT_TRUE(commands.textureUploads.empty());
}

TEST(GpuFoundationTest, TransparencyDepthUsesClippedHalfOpenRectangles)
{
    CommandBatch<RectCommand> commands;
    const auto add = [&](Int4 bounds, Int4 clip) {
        auto& command = commands.allocate();
        command.bounds = bounds;
        command.clip = clip;
    };

    EXPECT_EQ(MaxTransparencyDepth(commands), 1u);
    add({ 0, 0, 10, 10 }, { 0, 0, 20, 20 });
    add({ 10, 0, 20, 10 }, { 0, 0, 20, 20 });
    EXPECT_EQ(MaxTransparencyDepth(commands), 1u);
    add({ 5, 5, 15, 15 }, { 0, 0, 20, 20 });
    add({ 4, 4, 16, 16 }, { 6, 6, 14, 14 });
    EXPECT_EQ(MaxTransparencyDepth(commands), 3u);
    add({ 0, 0, 20, 20 }, { 2, 2, 2, 10 });
    EXPECT_EQ(MaxTransparencyDepth(commands), 3u);
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
