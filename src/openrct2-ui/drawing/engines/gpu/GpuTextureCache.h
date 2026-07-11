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
#include <optional>
#include <unordered_map>
#include <vector>

namespace OpenRCT2::Drawing
{
    enum class FilterPaletteID : int32_t;
    struct PaletteMap;
}

namespace OpenRCT2::Ui::Gpu
{
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
     * Cache mutations are serialised so recording and lease retirement can
     * move to separate threads without exposing atlas internals.
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
            bool transient = false;
        };

        struct AllocationState
        {
            TextureLocation location{};
            uint32_t pinCount = 0;
            bool retireWhenUnpinned = false;
        };

        struct ResidencyLease
        {
            std::vector<AtlasAllocationId> allocations;
            std::vector<AtlasAllocationId> persistentUploads;
        };

        uint32_t _maxAtlasLayers;
        std::vector<AtlasPage> _atlases;
        std::unordered_map<uint32_t, TextureLocation> _images;
        std::unordered_map<GlyphKey, TextureLocation, GlyphKeyHash> _glyphs;
        std::unordered_map<uint32_t, uint32_t> _generations;
        std::unordered_map<uint64_t, AllocationState> _allocations;
        std::unordered_map<uint64_t, ResidencyLease> _residencyLeases;
        std::vector<PendingUpload> _pendingUploads;
        std::vector<TextureLocation> _frameTransientLocations;
        std::vector<AtlasAllocationId> _frameAllocations;
        std::vector<uint32_t> _deferredInvalidations;
        uint64_t _nextAllocationSerial = 1;
        uint64_t _nextResidencyToken = 1;
        bool _recordingFrame = false;
        mutable std::mutex _mutex;

    public:
        explicit TextureCache(uint32_t maxAtlasLayers = kAtlasLayers);

        [[nodiscard]] TextureBinding GetOrLoadImageTexture(ImageId imageId);
        [[nodiscard]] TextureBinding GetOrLoadGlyphTexture(ImageId imageId, const Drawing::PaletteMap& palette);
        [[nodiscard]] TextureBinding LoadTransientBitmapTexture(
            const void* pixels, size_t width, size_t height);

        void BeginFrame();
        [[nodiscard]] AtlasResidencyToken SealFrame(FrameCommandStream& commands);
        void RetireFrame(AtlasResidencyToken token, FrameRetirement retirement);
        void AbortFrame();
        void InvalidateImage(uint32_t image);

        [[nodiscard]] static int32_t PaletteToY(Drawing::FilterPaletteID palette);

    private:
        [[nodiscard]] TextureLocation AllocateImage(uint32_t image, int32_t width, int32_t height);
        [[nodiscard]] std::optional<TextureLocation> QueueImage(ImageId imageId);
        [[nodiscard]] std::optional<TextureLocation> QueueGlyph(
            ImageId imageId, const Drawing::PaletteMap& palette);
        [[nodiscard]] TextureBinding BindForRecording(const TextureLocation& location);
        void ApplyInvalidation(uint32_t image);
        void RetireAllocation(const TextureLocation& location);
        void FreeIfUnpinned(uint64_t allocationSerial);
        void RemovePending(uint32_t image, uint32_t generation);
        void RemovePending(AtlasAllocationId allocation);
    };
} // namespace OpenRCT2::Ui::Gpu
