/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "GpuAtlas.h"
#include "GpuCommandStream.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <openrct2/drawing/ImageId.hpp>
#include <openrct2/interface/ZoomLevel.h>
#include <optional>
#include <unordered_map>
#include <vector>

struct TTFSurface;

namespace OpenRCT2::Drawing
{
    enum class FilterPaletteID : int32_t;
    struct PaletteMap;
} // namespace OpenRCT2::Drawing

namespace OpenRCT2::Ui::Gpu
{
    struct ResolvedSprite
    {
        Int2 atlasOrigin{};
        uint32_t descriptorIndex = 0;
        int16_t width = 0;
        int16_t height = 0;
        int16_t xOffset = 0;
        int16_t yOffset = 0;
        ZoomLevel zoom{};
        uint8_t coordinateShift = 0;
    };

    struct AtlasResidencyToken
    {
        uint64_t value = 0;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return value != 0;
        }

        bool operator==(const AtlasResidencyToken&) const = default;
    };

    // Only Presented commits persistent uploads. Every other outcome releases
    // residency while leaving those uploads available to a newer packet.
    enum class FrameRetirement
    {
        Presented,
        Failed,
        Superseded,
        Busy,
        Shutdown,
    };

    /**
     * Persistent indexed-sprite residency shared by explicit GPU recorders.
     * The recording owner applies all cache mutations. Backend threads only
     * enqueue completed-frame retirements for the next recording boundary.
     */
    class TextureCache final
    {
    private:
        struct GlyphKey
        {
            uint32_t image = 0;
            uint64_t palette = 0;
            uint32_t generation = 0;

            bool operator==(const GlyphKey&) const = default;
        };

        struct GlyphKeyHash
        {
            size_t operator()(const GlyphKey& key) const noexcept;
        };

        struct PendingUpload
        {
            TextureLocation location{};
            uint32_t pitch = 0;
            std::vector<std::byte> pixels;
        };

        struct SpriteMetadata
        {
            int16_t width = 0;
            int16_t height = 0;
            int16_t xOffset = 0;
            int16_t yOffset = 0;
            uint16_t flags = 0;
            int32_t zoomedOffset = 0;
        };

        struct ResidentImage
        {
            TextureLocation location{};
            SpriteMetadata metadata{};
        };

        struct AllocationState
        {
            TextureLocation location{};
            uint64_t lastBoundFrame = 0;
            uint32_t pinCount = 0;
            bool retireWhenUnpinned = false;
        };

        struct TTFSurfaceEntry
        {
            TextureLocation location{};
            uint64_t lastUseFrame = 0;
        };

        struct ResidencyLease
        {
            std::vector<AtlasAllocationId> allocations;
            std::vector<AtlasAllocationId> persistentUploads;
        };

        struct PendingFrameRetirement
        {
            AtlasResidencyToken token{};
            FrameRetirement retirement{};
        };

        uint32_t _maxAtlasLayers;
        std::vector<AtlasPage> _atlases;
        std::vector<std::optional<ResidentImage>> _images;
        std::vector<std::optional<SpriteMetadata>> _imageMetadata;
        std::unordered_map<GlyphKey, TextureLocation, GlyphKeyHash> _glyphs;
        std::unordered_map<uint64_t, TTFSurfaceEntry> _ttfSurfaces;
        std::unordered_map<uint32_t, uint32_t> _generations;
        std::unordered_map<uint64_t, AllocationState> _allocations;
        std::unordered_map<uint64_t, ResidencyLease> _residencyLeases;
        std::vector<PendingUpload> _pendingUploads;
        std::vector<AtlasAllocationId> _frameAllocations;
        std::vector<uint32_t> _deferredInvalidations;
        std::vector<PendingFrameRetirement> _pendingFrameRetirements;
        uint64_t _nextAllocationSerial = 1;
        uint64_t _nextResidencyToken = 1;
        uint64_t _recordingFrameSerial = 0;
        bool _recordingFrame = false;
        std::mutex _retirementMutex;

    public:
        explicit TextureCache(uint32_t maxAtlasLayers = kAtlasLayers);

        [[nodiscard]] TextureBinding GetOrLoadImageTexture(ImageId imageId);
        [[nodiscard]] std::optional<ResolvedSprite> GetOrLoadImageSprite(ImageId imageId, ZoomLevel zoom);
        [[nodiscard]] TextureBinding GetOrLoadGlyphTexture(ImageId imageId, const Drawing::PaletteMap& palette);
#ifndef DISABLE_TTF
        [[nodiscard]] TextureBinding GetOrLoadTTFTexture(const TTFSurface& surface);
#endif
        void BeginFrame();
        [[nodiscard]] AtlasResidencyToken SealFrame(FrameCommandStream& commands);
        // Worker threads enqueue retirement only; the recording owner applies it at a frame boundary.
        void RetireFrame(AtlasResidencyToken token, FrameRetirement retirement);
        void DrainFrameRetirements();
        void AbortFrame();
        void InvalidateImage(uint32_t image);

        [[nodiscard]] static int32_t PaletteToY(Drawing::FilterPaletteID palette);

    private:
        [[nodiscard]] TextureLocation AllocateImage(uint32_t image, int32_t width, int32_t height);
        [[nodiscard]] const SpriteMetadata* GetImageMetadata(uint32_t image);
        [[nodiscard]] ResidentImage* GetOrLoadResidentImage(ImageId imageId);
        [[nodiscard]] std::optional<TextureLocation> QueueRasterizedImage(ImageId imageId, const Drawing::PaletteMap* palette);
        void QueueUpload(const TextureLocation& location, const void* pixels, size_t size, uint32_t pitch);
        [[nodiscard]] TextureBinding BindForRecording(const TextureLocation& location);
        void ApplyFrameRetirement(AtlasResidencyToken token, FrameRetirement retirement);
        void EndRecordingFrame();
        void ApplyInvalidation(uint32_t image);
        void RetireAllocation(const TextureLocation& location);
        void FreeIfUnpinned(uint64_t allocationSerial);
        void RemovePending(AtlasAllocationId allocation);
        void TrimTTFSurfaceCache(size_t targetSize);
    };
} // namespace OpenRCT2::Ui::Gpu
