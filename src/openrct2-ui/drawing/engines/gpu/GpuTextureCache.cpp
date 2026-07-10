/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "GpuTextureCache.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <openrct2/core/EnumUtils.hpp>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/drawing/PaletteMap.h>
#include <stdexcept>

namespace OpenRCT2::Ui::Gpu
{
    using namespace Drawing;

    namespace
    {
        constexpr int32_t kResidentAtlasDimension = Gpu::kAtlasDimension;
        static_assert(sizeof(PaletteIndex) == 1, "GPU sprite uploads require byte-sized palette indices");

        struct OwnedRenderTarget
        {
            std::vector<PaletteIndex> pixels;
            RenderTarget target{};

            OwnedRenderTarget(int32_t width, int32_t height)
                : pixels(static_cast<size_t>(width) * height, PaletteIndex::transparent)
            {
                target.bits = pixels.data();
                target.width = width;
                target.height = height;
                target.pitch = 0;
                target.zoom_level = ZoomLevel{ 0 };
            }
        };

        [[nodiscard]] TextureBinding ToBinding(const TextureLocation& location)
        {
            return { location.index, location.coords };
        }
    } // namespace

    size_t TextureCache::GlyphKeyHash::operator()(const GlyphKey& key) const noexcept
    {
        size_t result = key.image;
        result ^= static_cast<size_t>(key.palette) + 0x9e3779b9 + (result << 6) + (result >> 2);
        result ^= static_cast<size_t>(key.palette >> 32) + 0x9e3779b9 + (result << 6) + (result >> 2);
        result ^= static_cast<size_t>(key.generation) + 0x9e3779b9 + (result << 6) + (result >> 2);
        return result;
    }

    TextureCache::TextureCache(uint32_t maxAtlasLayers)
        : _maxAtlasLayers(maxAtlasLayers)
    {
        if (maxAtlasLayers == 0)
        {
            throw std::invalid_argument("GPU texture cache requires at least one atlas layer");
        }
    }

    TextureBinding TextureCache::GetOrLoadImageTexture(ImageId imageId)
    {
        std::scoped_lock lock(_mutex);
        const auto image = imageId.GetIndex();
        if (const auto it = _images.find(image); it != _images.end())
        {
            return ToBinding(it->second);
        }
        return QueueImage(imageId);
    }

    TextureBinding TextureCache::GetOrLoadGlyphTexture(ImageId imageId, const PaletteMap& palette)
    {
        std::scoped_lock lock(_mutex);
        if (GfxGetG1Element(imageId) == nullptr)
        {
            return {};
        }
        GlyphKey key{ .image = imageId.GetIndex(), .generation = _generations[imageId.GetIndex()] };
        static_assert(sizeof(key.palette) == 8 * sizeof(PaletteIndex));
        std::array<PaletteIndex, 8> glyphPalette{};
        for (size_t i = 0; i < glyphPalette.size(); i++)
        {
            glyphPalette[i] = palette[i];
        }
        std::memcpy(&key.palette, glyphPalette.data(), sizeof(key.palette));

        if (const auto it = _glyphs.find(key); it != _glyphs.end())
        {
            return ToBinding(it->second);
        }

        const auto binding = QueueGlyph(imageId, palette);
        const auto location = _pendingUploads.back().location;
        _glyphs.emplace(key, location);
        return binding;
    }

    TextureBinding TextureCache::LoadTransientBitmapTexture(
        const void* pixels, size_t width, size_t height)
    {
        std::scoped_lock lock(_mutex);
        if (!_recordingFrame)
        {
            throw std::logic_error("Transient GPU bitmaps require an active recording frame");
        }
        if (pixels == nullptr || width == 0 || height == 0
            || width > static_cast<size_t>(std::numeric_limits<int32_t>::max())
            || height > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
        {
            throw std::invalid_argument("Invalid transient GPU bitmap upload");
        }
        if (height > std::numeric_limits<size_t>::max() / width)
        {
            throw std::overflow_error("Transient GPU bitmap upload size overflow");
        }

        constexpr uint32_t kTransientImage = std::numeric_limits<uint32_t>::max();
        auto location = AllocateImage(kTransientImage, static_cast<int32_t>(width), static_cast<int32_t>(height));
        PendingUpload pending{ location, static_cast<uint32_t>(width), {}, true };
        pending.pixels.resize(width * height);
        std::memcpy(pending.pixels.data(), pixels, pending.pixels.size());
        _pendingUploads.push_back(std::move(pending));
        _frameTransientLocations.push_back(location);
        return ToBinding(location);
    }

    void TextureCache::BeginFrame()
    {
        std::scoped_lock lock(_mutex);
        if (_recordingFrame)
        {
            throw std::logic_error("GPU texture cache frame is already active");
        }
        _recordingFrame = true;
    }

    void TextureCache::EndFrame()
    {
        std::scoped_lock lock(_mutex);
        if (!_recordingFrame)
        {
            throw std::logic_error("GPU texture cache has no active frame");
        }

        std::erase_if(_pendingUploads, [](const PendingUpload& pending) { return pending.transient; });
        for (auto& location : _frameTransientLocations)
        {
            Free(location);
        }
        _frameTransientLocations.clear();

        _recordingFrame = false;
        for (const auto image : _deferredInvalidations)
        {
            ApplyInvalidation(image);
        }
        _deferredInvalidations.clear();
    }

    void TextureCache::InvalidateImage(uint32_t image)
    {
        std::scoped_lock lock(_mutex);
        if (_recordingFrame)
        {
            if (std::find(_deferredInvalidations.begin(), _deferredInvalidations.end(), image)
                == _deferredInvalidations.end())
            {
                _deferredInvalidations.push_back(image);
            }
            return;
        }
        ApplyInvalidation(image);
    }

    void TextureCache::ApplyInvalidation(uint32_t image)
    {
        const uint32_t oldGeneration = _generations[image]++;
        RemovePending(image, oldGeneration);

        if (const auto it = _images.find(image); it != _images.end())
        {
            Free(it->second);
            _images.erase(it);
        }

        for (auto it = _glyphs.begin(); it != _glyphs.end();)
        {
            if (it->first.image == image)
            {
                Free(it->second);
                it = _glyphs.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void TextureCache::FlushPendingUploads(Backend& backend, FrameCommandStream& commands)
    {
        std::scoped_lock lock(_mutex);
        for (const auto& pending : _pendingUploads)
        {
            if (!pending.transient && _generations[pending.location.image] != pending.location.generation)
            {
                continue;
            }
            auto upload = backend.AllocateUpload(pending.pixels.size(), alignof(uint32_t));
            if (!upload)
            {
                throw std::runtime_error("GPU upload ring has no room for a sprite atlas upload");
            }
            if (upload.offset > std::numeric_limits<uint32_t>::max())
            {
                throw std::overflow_error("GPU sprite upload offset exceeds command-stream limits");
            }
            std::memcpy(upload.bytes.data(), pending.pixels.data(), pending.pixels.size());
            commands.textureUploads.insert({
                pending.location.index,
                pending.location.bounds,
                static_cast<uint32_t>(upload.offset),
                pending.pitch,
            });
        }
        _pendingUploads.clear();
    }

    int32_t TextureCache::PaletteToY(FilterPaletteID palette)
    {
        return palette > FilterPaletteID::paletteWater ? EnumValue(palette) + 5 : EnumValue(palette) + 1;
    }

    TextureLocation TextureCache::AllocateImage(uint32_t image, int32_t width, int32_t height)
    {
        if (width <= 0 || height <= 0 || width > kResidentAtlasDimension || height > kResidentAtlasDimension)
        {
            throw std::runtime_error("Sprite dimensions are not representable in the GPU atlas");
        }

        for (auto& atlas : _atlases)
        {
            if (atlas.GetFreeSlots() > 0 && atlas.IsImageSuitable(width, height))
            {
                auto location = atlas.Allocate(width, height);
                location.image = image;
                location.generation = _generations[image];
                return location;
            }
        }

        if (_atlases.size() >= _maxAtlasLayers)
        {
            throw std::runtime_error("GPU sprite atlas layer limit reached");
        }
        const int32_t order = AtlasPage::CalculateImageSizeOrder(width, height);
        const int32_t slotSize = 1 << order;
        _atlases.emplace_back(static_cast<uint32_t>(_atlases.size()), slotSize);
        _atlases.back().Initialise(kResidentAtlasDimension, kResidentAtlasDimension);

        auto location = _atlases.back().Allocate(width, height);
        location.image = image;
        location.generation = _generations[image];
        return location;
    }

    TextureBinding TextureCache::QueueImage(ImageId imageId)
    {
        const auto* element = GfxGetG1Element(imageId);
        if (element == nullptr)
        {
            return {};
        }

        OwnedRenderTarget raster(element->width, element->height);
        GfxDrawSpriteSoftware(raster.target, ImageId(imageId.GetIndex()), { -element->xOffset, -element->yOffset });
        auto location = AllocateImage(imageId.GetIndex(), raster.target.width, raster.target.height);
        _pendingUploads.push_back({ location, static_cast<uint32_t>(raster.target.width), {}, false });
        auto& upload = _pendingUploads.back();
        upload.pixels.resize(raster.pixels.size() * sizeof(PaletteIndex));
        std::memcpy(upload.pixels.data(), raster.pixels.data(), upload.pixels.size());
        _images.emplace(imageId.GetIndex(), location);
        return ToBinding(location);
    }

    TextureBinding TextureCache::QueueGlyph(ImageId imageId, const PaletteMap& palette)
    {
        const auto* element = GfxGetG1Element(imageId);
        if (element == nullptr)
        {
            return {};
        }

        OwnedRenderTarget raster(element->width, element->height);
        GfxDrawSpritePaletteSetSoftware(
            raster.target, imageId, { -element->xOffset, -element->yOffset }, palette);
        auto location = AllocateImage(imageId.GetIndex(), raster.target.width, raster.target.height);
        _pendingUploads.push_back({ location, static_cast<uint32_t>(raster.target.width), {}, false });
        auto& upload = _pendingUploads.back();
        upload.pixels.resize(raster.pixels.size() * sizeof(PaletteIndex));
        std::memcpy(upload.pixels.data(), raster.pixels.data(), upload.pixels.size());
        return ToBinding(location);
    }

    void TextureCache::Free(TextureLocation& location)
    {
        if (location.index < _atlases.size())
        {
            _atlases[location.index].Free(location);
        }
    }

    void TextureCache::RemovePending(uint32_t image, uint32_t generation)
    {
        std::erase_if(_pendingUploads, [image, generation](const PendingUpload& pending) {
            return pending.location.image == image && pending.location.generation == generation;
        });
    }
} // namespace OpenRCT2::Ui::Gpu
