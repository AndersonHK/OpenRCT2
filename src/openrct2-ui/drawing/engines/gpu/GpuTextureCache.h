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
#include "GpuBackend.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <openrct2/drawing/ImageId.hpp>
#include <unordered_map>
#include <vector>

namespace OpenRCT2::Drawing
{
    enum class FilterPaletteID : int32_t;
    struct PaletteMap;
}

namespace OpenRCT2::Ui::Gpu
{
    /**
     * Persistent indexed-sprite residency shared by explicit GPU recorders.
     * Cache mutations are serialised, while command recording and backend
     * submission remain owned by the render thread.
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

        uint32_t _maxAtlasLayers;
        std::vector<AtlasPage> _atlases;
        std::unordered_map<uint32_t, TextureLocation> _images;
        std::unordered_map<GlyphKey, TextureLocation, GlyphKeyHash> _glyphs;
        std::unordered_map<uint32_t, uint32_t> _generations;
        std::vector<PendingUpload> _pendingUploads;
        std::vector<TextureLocation> _frameTransientLocations;
        std::vector<uint32_t> _deferredInvalidations;
        bool _recordingFrame = false;
        mutable std::mutex _mutex;

    public:
        explicit TextureCache(uint32_t maxAtlasLayers = kAtlasLayers);

        [[nodiscard]] TextureBinding GetOrLoadImageTexture(ImageId imageId);
        [[nodiscard]] TextureBinding GetOrLoadGlyphTexture(ImageId imageId, const Drawing::PaletteMap& palette);
        [[nodiscard]] TextureBinding LoadTransientBitmapTexture(
            const void* pixels, size_t width, size_t height);

        void BeginFrame();
        void EndFrame();
        void InvalidateImage(uint32_t image);
        void FlushPendingUploads(Backend& backend, FrameCommandStream& commands);

        [[nodiscard]] static int32_t PaletteToY(Drawing::FilterPaletteID palette);

    private:
        [[nodiscard]] TextureLocation AllocateImage(uint32_t image, int32_t width, int32_t height);
        [[nodiscard]] TextureBinding QueueImage(ImageId imageId);
        [[nodiscard]] TextureBinding QueueGlyph(ImageId imageId, const Drawing::PaletteMap& palette);
        void ApplyInvalidation(uint32_t image);
        void Free(TextureLocation& location);
        void RemovePending(uint32_t image, uint32_t generation);
    };
} // namespace OpenRCT2::Ui::Gpu
