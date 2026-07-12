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
#include <openrct2/drawing/TTF.h>
#include <stdexcept>

namespace OpenRCT2::Ui::Gpu
{
    using namespace Drawing;

    namespace
    {
        constexpr int32_t kResidentAtlasDimension = Gpu::kAtlasDimension;
        constexpr size_t kMaxResidentTTFSurfaces = 256;
        constexpr uint32_t kResidentTTFImage = std::numeric_limits<uint32_t>::max() - 1;
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

        [[nodiscard]] bool AllocationLess(const AtlasAllocationId& lhs, const AtlasAllocationId& rhs) noexcept
        {
            if (lhs.atlas != rhs.atlas)
                return lhs.atlas < rhs.atlas;
            if (lhs.slot != rhs.slot)
                return lhs.slot < rhs.slot;
            return lhs.serial < rhs.serial;
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
        if (!_recordingFrame)
        {
            throw std::logic_error("GPU atlas textures can only be resolved while recording a frame");
        }
        const auto image = imageId.GetIndex();
        if (const auto it = _images.find(image); it != _images.end())
        {
            return BindForRecording(it->second);
        }
        const auto location = QueueRasterizedImage(imageId, nullptr);
        if (!location.has_value())
        {
            return {};
        }
        try
        {
            if (!_images.emplace(image, *location).second)
            {
                throw std::logic_error("GPU image was inserted into the texture cache twice");
            }
        }
        catch (...)
        {
            RemovePending(location->GetAllocationId());
            RetireAllocation(*location);
            throw;
        }
        return BindForRecording(*location);
    }

    TextureBinding TextureCache::GetOrLoadGlyphTexture(ImageId imageId, const PaletteMap& palette)
    {
        std::scoped_lock lock(_mutex);
        if (!_recordingFrame)
        {
            throw std::logic_error("GPU atlas textures can only be resolved while recording a frame");
        }
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
            return BindForRecording(it->second);
        }

        const auto location = QueueRasterizedImage(imageId, &palette);
        if (!location.has_value())
        {
            return {};
        }
        try
        {
            const auto insertion = _glyphs.emplace(key, *location);
            if (!insertion.second)
            {
                throw std::logic_error("GPU glyph was inserted into the texture cache twice");
            }
        }
        catch (...)
        {
            RemovePending(location->GetAllocationId());
            RetireAllocation(*location);
            throw;
        }
        return BindForRecording(*location);
    }

#ifndef DISABLE_TTF
    TextureBinding TextureCache::GetOrLoadTTFTexture(const TTFSurface& surface)
    {
        std::scoped_lock lock(_mutex);
        if (!_recordingFrame)
        {
            throw std::logic_error("GPU TTF textures require an active recording frame");
        }
        if (surface.cacheId == 0 || surface.pixels == nullptr || surface.w <= 0 || surface.h <= 0)
        {
            throw std::invalid_argument("Invalid cached TTF bitmap upload");
        }
        if (const auto it = _ttfSurfaces.find(surface.cacheId); it != _ttfSurfaces.end())
        {
            it->second.lastUseFrame = _recordingFrameSerial;
            return BindForRecording(it->second.location);
        }

        TrimTTFSurfaceCache(kMaxResidentTTFSurfaces - 1);
        const auto width = static_cast<size_t>(surface.w);
        const auto height = static_cast<size_t>(surface.h);
        if (height > std::numeric_limits<size_t>::max() / width)
        {
            throw std::overflow_error("Cached TTF bitmap upload size overflow");
        }

        auto location = AllocateImage(kResidentTTFImage, surface.w, surface.h);
        try
        {
            QueueUpload(location, surface.pixels, width * height, static_cast<uint32_t>(surface.w));
            const auto [it, inserted] = _ttfSurfaces.emplace(
                surface.cacheId, TTFSurfaceEntry{ location, _recordingFrameSerial });
            if (!inserted)
            {
                throw std::logic_error("GPU TTF surface was inserted into the texture cache twice");
            }
            return BindForRecording(it->second.location);
        }
        catch (...)
        {
            _ttfSurfaces.erase(surface.cacheId);
            RemovePending(location.GetAllocationId());
            RetireAllocation(location);
            throw;
        }
    }
#endif

    void TextureCache::BeginFrame()
    {
        std::scoped_lock lock(_mutex);
        if (_recordingFrame)
        {
            throw std::logic_error("GPU texture cache frame is already active");
        }
        ++_recordingFrameSerial;
        TrimTTFSurfaceCache(kMaxResidentTTFSurfaces);
        _recordingFrame = true;
        _frameAllocations.clear();
    }

    AtlasResidencyToken TextureCache::SealFrame(FrameCommandStream& commands)
    {
        std::scoped_lock lock(_mutex);
        if (!_recordingFrame)
        {
            throw std::logic_error("GPU texture cache has no active frame");
        }

        auto allocations = _frameAllocations;
        std::sort(allocations.begin(), allocations.end(), AllocationLess);
        allocations.erase(std::unique(allocations.begin(), allocations.end()), allocations.end());

        std::vector<TextureUpload> uploads;
        std::vector<AtlasAllocationId> persistentUploads;
        uploads.reserve(allocations.size());
        persistentUploads.reserve(allocations.size());
        for (const auto& pending : _pendingUploads)
        {
            const auto allocation = pending.location.GetAllocationId();
            if (!std::binary_search(allocations.begin(), allocations.end(), allocation, AllocationLess))
            {
                continue;
            }
            const auto generationIt = _generations.find(pending.location.image);
            if (generationIt == _generations.end() || generationIt->second != pending.location.generation)
            {
                continue;
            }
            uploads.push_back({
                .atlas = pending.location.index,
                .bounds = pending.location.bounds,
                .sourcePitch = pending.pitch,
                .pixels = pending.pixels,
            });
            persistentUploads.push_back(allocation);
        }

        if (_nextResidencyToken == 0)
        {
            throw std::overflow_error("GPU atlas residency token space exhausted");
        }
        const AtlasResidencyToken token{ _nextResidencyToken++ };
        ResidencyLease lease{ std::move(allocations), std::move(persistentUploads) };
        const auto [leaseIt, inserted] = _residencyLeases.emplace(token.value, std::move(lease));
        if (!inserted)
        {
            throw std::logic_error("GPU atlas residency token collision");
        }

        for (const auto& allocation : leaseIt->second.allocations)
        {
            const auto stateIt = _allocations.find(allocation.serial);
            if (stateIt == _allocations.end() || stateIt->second.location.GetAllocationId() != allocation)
            {
                _residencyLeases.erase(leaseIt);
                throw std::logic_error("GPU frame references an unknown atlas allocation");
            }
        }
        for (const auto& allocation : leaseIt->second.allocations)
        {
            const auto stateIt = _allocations.find(allocation.serial);
            stateIt->second.pinCount++;
        }

        commands.textureUploads = std::move(uploads);
        EndRecordingFrame();
        return token;
    }

    void TextureCache::RetireFrame(AtlasResidencyToken token, FrameRetirement retirement)
    {
        std::scoped_lock lock(_mutex);
        const auto leaseIt = _residencyLeases.find(token.value);
        if (!token || leaseIt == _residencyLeases.end())
        {
            throw std::logic_error("Unknown GPU atlas residency token");
        }

        for (const auto allocation : leaseIt->second.allocations)
        {
            const auto stateIt = _allocations.find(allocation.serial);
            if (stateIt == _allocations.end() || stateIt->second.location.GetAllocationId() != allocation
                || stateIt->second.pinCount == 0)
            {
                throw std::logic_error("GPU atlas residency accounting is inconsistent");
            }
        }
        if (retirement == FrameRetirement::Presented)
        {
            for (const auto allocation : leaseIt->second.persistentUploads)
            {
                RemovePending(allocation);
            }
        }
        for (const auto allocation : leaseIt->second.allocations)
        {
            const auto stateIt = _allocations.find(allocation.serial);
            stateIt->second.pinCount--;
            FreeIfUnpinned(allocation.serial);
        }
        _residencyLeases.erase(leaseIt);
    }

    void TextureCache::AbortFrame()
    {
        std::scoped_lock lock(_mutex);
        if (!_recordingFrame)
        {
            throw std::logic_error("GPU texture cache has no active frame");
        }

        EndRecordingFrame();
    }

    void TextureCache::InvalidateImage(uint32_t image)
    {
        std::scoped_lock lock(_mutex);
        if (_recordingFrame)
        {
            _generations.try_emplace(image, 0);
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
        std::erase_if(_pendingUploads, [image, oldGeneration](const PendingUpload& pending) {
            return pending.location.image == image && pending.location.generation == oldGeneration;
        });

        if (const auto it = _images.find(image); it != _images.end())
        {
            RetireAllocation(it->second);
            _images.erase(it);
        }

        for (auto it = _glyphs.begin(); it != _glyphs.end();)
        {
            if (it->first.image == image)
            {
                RetireAllocation(it->second);
                it = _glyphs.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    int32_t TextureCache::PaletteToY(FilterPaletteID palette)
    {
        return palette > FilterPaletteID::paletteWater ? EnumValue(palette) + 5 : EnumValue(palette) + 1;
    }

    TextureLocation TextureCache::AllocateImage(uint32_t image, int32_t width, int32_t height)
    {
        if (width <= 0 || height <= 0 || width > kResidentAtlasDimension || height > kResidentAtlasDimension)
        {
            throw std::runtime_error(
                "GPU atlas cannot represent image " + std::to_string(image) + " at " + std::to_string(width) + "x"
                + std::to_string(height) + " pixels");
        }
        if (_nextAllocationSerial == 0)
        {
            throw std::overflow_error("GPU atlas allocation identity space exhausted");
        }
        const auto generation = _generations[image];

        AtlasPage* target = nullptr;
        for (auto& atlas : _atlases)
        {
            if (atlas.GetFreeSlots() > 0 && atlas.IsImageSuitable(width, height))
            {
                target = &atlas;
                break;
            }
        }

        if (target == nullptr)
        {
            if (_atlases.size() >= _maxAtlasLayers)
            {
                throw std::runtime_error("GPU sprite atlas layer limit reached");
            }
            const int32_t order = AtlasPage::CalculateImageSizeOrder(width, height);
            _atlases.emplace_back(static_cast<uint32_t>(_atlases.size()), 1 << order);
            target = &_atlases.back();
            target->Initialise(kResidentAtlasDimension, kResidentAtlasDimension);
        }

        auto location = target->Allocate(width, height);
        location.image = image;
        location.generation = generation;
        location.allocationSerial = _nextAllocationSerial;
        try
        {
            const auto insertion = _allocations.emplace(
                location.allocationSerial, AllocationState{ .location = location });
            if (!insertion.second)
            {
                throw std::logic_error("GPU atlas allocation identity collision");
            }
        }
        catch (...)
        {
            target->Free(location);
            throw;
        }
        _nextAllocationSerial++;
        return location;
    }

    std::optional<TextureLocation> TextureCache::QueueRasterizedImage(
        ImageId imageId, const PaletteMap* palette)
    {
        const auto* element = GfxGetG1Element(imageId);
        if (element == nullptr || element->width <= 0 || element->height <= 0)
        {
            return std::nullopt;
        }

        OwnedRenderTarget raster(element->width, element->height);
        if (palette == nullptr)
        {
            GfxDrawSpriteSoftware(
                raster.target, ImageId(imageId.GetIndex()), { -element->xOffset, -element->yOffset });
        }
        else
        {
            GfxDrawSpritePaletteSetSoftware(
                raster.target, imageId, { -element->xOffset, -element->yOffset }, *palette);
        }
        auto location = AllocateImage(imageId.GetIndex(), raster.target.width, raster.target.height);
        try
        {
            QueueUpload(
                location, raster.pixels.data(), raster.pixels.size() * sizeof(PaletteIndex),
                static_cast<uint32_t>(raster.target.width));
            return location;
        }
        catch (...)
        {
            RemovePending(location.GetAllocationId());
            RetireAllocation(location);
            throw;
        }
    }

    void TextureCache::QueueUpload(const TextureLocation& location, const void* pixels, size_t size, uint32_t pitch)
    {
        PendingUpload pending{ location, pitch, {} };
        pending.pixels.resize(size);
        std::memcpy(pending.pixels.data(), pixels, size);
        _pendingUploads.push_back(std::move(pending));
    }

    TextureBinding TextureCache::BindForRecording(const TextureLocation& location)
    {
        if (location.allocationSerial == 0)
        {
            throw std::logic_error("GPU atlas texture has no allocation identity");
        }
        _frameAllocations.push_back(location.GetAllocationId());
        return { location.index, location.coords };
    }

    void TextureCache::EndRecordingFrame()
    {
        _recordingFrame = false;
        _frameAllocations.clear();
        for (const auto image : _deferredInvalidations)
        {
            ApplyInvalidation(image);
        }
        _deferredInvalidations.clear();
    }

    void TextureCache::RetireAllocation(const TextureLocation& location)
    {
        const auto stateIt = _allocations.find(location.allocationSerial);
        if (stateIt == _allocations.end() || stateIt->second.location.GetAllocationId() != location.GetAllocationId())
        {
            throw std::logic_error("Unknown GPU atlas allocation");
        }
        stateIt->second.retireWhenUnpinned = true;
        FreeIfUnpinned(location.allocationSerial);
    }

    void TextureCache::FreeIfUnpinned(uint64_t allocationSerial)
    {
        const auto stateIt = _allocations.find(allocationSerial);
        if (stateIt == _allocations.end() || stateIt->second.pinCount != 0 || !stateIt->second.retireWhenUnpinned)
        {
            return;
        }
        const auto& location = stateIt->second.location;
        if (location.index < _atlases.size())
        {
            _atlases[location.index].Free(location);
        }
        _allocations.erase(stateIt);
    }

    void TextureCache::RemovePending(AtlasAllocationId allocation)
    {
        std::erase_if(_pendingUploads, [allocation](const PendingUpload& pending) {
            return pending.location.GetAllocationId() == allocation;
        });
    }

    void TextureCache::TrimTTFSurfaceCache(size_t targetSize)
    {
        while (_ttfSurfaces.size() > targetSize)
        {
            auto oldest = _ttfSurfaces.end();
            for (auto it = _ttfSurfaces.begin(); it != _ttfSurfaces.end(); ++it)
            {
                if (it->second.lastUseFrame == _recordingFrameSerial)
                {
                    continue;
                }
                if (oldest == _ttfSurfaces.end() || it->second.lastUseFrame < oldest->second.lastUseFrame
                    || (it->second.lastUseFrame == oldest->second.lastUseFrame && it->first < oldest->first))
                {
                    oldest = it;
                }
            }
            if (oldest == _ttfSurfaces.end())
            {
                return;
            }
            RemovePending(oldest->second.location.GetAllocationId());
            RetireAllocation(oldest->second.location);
            _ttfSurfaces.erase(oldest);
        }
    }
} // namespace OpenRCT2::Ui::Gpu
