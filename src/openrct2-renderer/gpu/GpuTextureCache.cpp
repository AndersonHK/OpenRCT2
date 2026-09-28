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
#include <openrct2/drawing/Drawing.Sprite.h>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/drawing/FilterPaletteIds.h>
#include <openrct2/drawing/G1Element.h>
#include <openrct2/drawing/PaletteMap.h>
#include <openrct2/drawing/SpriteAssetDecoder.h>
#include <openrct2/drawing/TTF.h>
#include <openrct2/profiling/Profiling.h>
#include <stdexcept>
#include <unordered_set>

namespace OpenRCT2::Ui::Gpu
{
    using namespace Drawing;

    namespace
    {
        constexpr int32_t kResidentAtlasDimension = Gpu::kAtlasDimension;
        constexpr size_t kMaxResidentTTFSurfaces = 256;
        constexpr uint32_t kResidentTTFImage = std::numeric_limits<uint32_t>::max() - 1;
        static_assert(sizeof(PaletteIndex) == 1, "GPU sprite uploads require byte-sized palette indices");

    } // namespace

    size_t TextureCache::GlyphKeyHash::operator()(const GlyphKey& key) const noexcept
    {
        size_t result = key.image;
        result ^= static_cast<size_t>(key.palette) + 0x9e3779b9 + (result << 6) + (result >> 2);
        result ^= static_cast<size_t>(key.palette >> 32) + 0x9e3779b9 + (result << 6) + (result >> 2);
        result ^= static_cast<size_t>(key.generation) + 0x9e3779b9 + (result << 6) + (result >> 2);
        result ^= static_cast<size_t>(key.sourceRemap) + 0x9e3779b9 + (result << 6) + (result >> 2);
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

    AtlasAssetLease::~AtlasAssetLease()
    {
        if (_id != 0)
            _owner->RetireAssetLease(_id);
    }

    AssetSpriteResolution TextureCache::ResolveAssetSprite(ImageId imageId, ZoomLevel zoom, std::vector<uint32_t>& dependencies)
    {
        if (!_recordingFrame)
            throw std::logic_error("Asset preparation requires an active recording frame");
        auto image = imageId.GetIndex();
        auto remaining = zoom;
        for (;;)
        {
            dependencies.push_back(image);
            const auto* metadata = GetImageMetadata(image);
            if (metadata == nullptr || metadata->width < 0 || metadata->height < 0)
                return {};
            if (metadata->width == 0 || metadata->height == 0)
                return { {}, false, !G1Flags{ metadata->flags }.has(G1Flag::isPalette) };
            if (remaining <= ZoomLevel{ 0 })
                break;
            const G1Flags flags{ metadata->flags };
            if (flags.has(G1Flag::hasZoomSprite))
            {
                const int64_t linked = int64_t{ image } - metadata->zoomedOffset;
                if (linked < 0 || linked >= kImageIndexUndefined)
                    return {};
                image = static_cast<uint32_t>(linked);
                remaining--;
            }
            else if (flags.has(G1Flag::noZoomDraw))
                return { {}, true };
            else
                break;
        }
        return { GetOrLoadImageSprite(imageId, zoom), false };
    }

    std::shared_ptr<const AtlasAssetLease> TextureCache::CreateAssetLease(
        std::span<const uint64_t> residencies, std::span<const uint32_t> dependencies)
    {
        if (!_recordingFrame)
            throw std::logic_error("Asset lease preparation requires an active frame");
        auto owner = shared_from_this(); // Native asset owners must use the shared cache lifetime.
        std::vector<uint64_t> serials(residencies.begin(), residencies.end());
        std::sort(serials.begin(), serials.end());
        serials.erase(std::unique(serials.begin(), serials.end()), serials.end());
        std::vector<uint32_t> images(dependencies.begin(), dependencies.end());
        std::sort(images.begin(), images.end());
        images.erase(std::unique(images.begin(), images.end()), images.end());
        for (const auto serial : serials)
        {
            const auto allocation = _allocations.find(serial);
            if (allocation == _allocations.end() || allocation->second.retireWhenUnpinned
                || allocation->second.pinCount == UINT32_MAX)
                throw std::invalid_argument("Asset generation references unavailable atlas storage");
        }
        if (_nextAssetLease == 0)
            throw std::overflow_error("Asset lease identity exhausted");
        // At most one queued retirement per live state. Reserve before publishing the destructor-visible ID.
        {
            std::scoped_lock lock(_retirementMutex);
            _pendingAssetRetirements.reserve(_assetLeases.size() + 1);
        }
        auto lease = std::shared_ptr<AtlasAssetLease>(new AtlasAssetLease(std::move(owner), 0));
        const auto id = _nextAssetLease++;
        auto [entry, inserted] = _assetLeases.try_emplace(id);
        if (!inserted)
            throw std::logic_error("Asset lease identity collision");
        try
        {
            auto& state = entry->second;
            state.allocations.reserve(serials.size());
            state.dependencies.reserve(images.size());
            for (const auto serial : serials)
            {
                auto& allocation = _allocations.at(serial);
                allocation.assetLeases.push_back(id);
                ++allocation.pinCount;
                state.allocations.push_back(allocation.location.GetAllocationId());
            }
            for (const auto image : images)
            {
                _assetDependencies[image].push_back(id);
                state.dependencies.push_back(image);
            }
            state.lease = lease;
            lease->_id = id;
        }
        catch (...)
        {
            ApplyAssetRetirement(id);
            throw;
        }
        return lease;
    }

    bool TextureCache::TryBindAssetLease(const std::shared_ptr<const AtlasAssetLease>& lease)
    {
        if (!_recordingFrame)
            throw std::logic_error("Asset generation binding requires an active frame");
        if (!lease || lease->_owner.get() != this || !lease->_current)
            return false;
        auto& state = _assetLeases.at(lease->_id);
        if (state.lastBoundFrame != _recordingFrameSerial)
        {
            _frameAssetLeases.push_back(lease);
            state.lastBoundFrame = _recordingFrameSerial;
        }
        return true;
    }

    void TextureCache::RetireAssetLease(uint64_t id) noexcept
    {
        std::scoped_lock lock(_retirementMutex);
        // Capacity was reserved before the handle became visible; destruction cannot allocate.
        _pendingAssetRetirements.push_back(id);
    }

    void TextureCache::ApplyAssetRetirement(uint64_t id)
    {
        const auto found = _assetLeases.find(id);
        if (found == _assetLeases.end())
            throw std::logic_error("Unknown asset lease retirement");
        for (const auto allocation : found->second.allocations)
        {
            auto& state = _allocations.at(allocation.serial);
            std::erase(state.assetLeases, id);
            --state.pinCount;
            FreeIfUnpinned(allocation.serial);
        }
        for (const auto image : found->second.dependencies)
        {
            auto dependency = _assetDependencies.find(image);
            std::erase(dependency->second, id);
            if (dependency->second.empty())
                _assetDependencies.erase(dependency);
        }
        _assetLeases.erase(found);
    }

    bool TextureCache::IsAllocationBound(const AllocationState& state) const
    {
        if (state.lastBoundFrame == _recordingFrameSerial)
            return true;
        return std::any_of(state.assetLeases.begin(), state.assetLeases.end(), [this](uint64_t id) {
            return _assetLeases.at(id).lastBoundFrame == _recordingFrameSerial;
        });
    }

    TextureBinding TextureCache::GetOrLoadImageTexture(ImageId imageId)
    {
        if (!_recordingFrame)
        {
            throw std::logic_error("GPU atlas textures can only be resolved while recording a frame");
        }
        auto* resident = GetOrLoadResidentImage(imageId);
        return resident == nullptr ? TextureBinding{} : BindResidentForRecording(*resident);
    }

    std::optional<ResolvedSprite> TextureCache::GetOrLoadImageSprite(ImageId imageId, ZoomLevel zoom)
    {
        if (!_recordingFrame)
        {
            throw std::logic_error("GPU atlas sprites can only be resolved while recording a frame");
        }

        uint8_t coordinateShift = 0;
        auto image = imageId.GetIndex();
        while (zoom > ZoomLevel{ 0 })
        {
            const auto* metadata = GetImageMetadata(image);
            if (metadata == nullptr || metadata->width <= 0 || metadata->height <= 0)
                return std::nullopt;
            const G1Flags flags{ metadata->flags };
            if (flags.has(G1Flag::hasZoomSprite))
            {
                image -= metadata->zoomedOffset;
                zoom--;
                coordinateShift++;
                continue;
            }
            if (flags.has(G1Flag::noZoomDraw))
                return std::nullopt;
            break;
        }

        auto selected = imageId.WithIndex(image);
        auto* resident = GetOrLoadResidentImage(selected);
        if (resident == nullptr)
            return std::nullopt;
        static_cast<void>(BindResidentForRecording(*resident));
        const auto& metadata = resident->metadata;
        const G1Flags flags{ metadata.flags };
        // Raw RLE's unscaled memcpy writes encoded zero, while its minified,
        // magnified and recoloured operations treat zero as transparent.
        const bool writesZero = !imageId.HasPrimary() && !imageId.HasSecondary() && !imageId.IsBlended()
            && (flags.has(G1Flag::hasRLECompression) ? zoom == ZoomLevel{ 0 } : !flags.has(G1Flag::hasTransparency));
        std::optional<TextureBinding> zeroCoverage;
        if (writesZero && resident->zeroCoverage.has_value())
            zeroCoverage = BindForRecording(*resident->zeroCoverage);
        return ResolvedSprite{
            .atlasOrigin = { resident->location.bounds.x, resident->location.bounds.y },
            .descriptorIndex = resident->location.GetDescriptorIndex(),
            .width = metadata.width,
            .height = metadata.height,
            .xOffset = metadata.xOffset,
            .yOffset = metadata.yOffset,
            .zoom = zoom,
            .coordinateShift = coordinateShift,
            .hasRleCompression = G1Flags{ metadata.flags }.has(G1Flag::hasRLECompression),
            .zeroCoverage = zeroCoverage,
            .residencyRevision = resident->location.allocationSerial,
        };
    }

    TextureBinding TextureCache::GetOrLoadGlyphTexture(ImageId imageId, const PaletteMap& palette)
    {
        if (!_recordingFrame)
        {
            throw std::logic_error("GPU atlas textures can only be resolved while recording a frame");
        }
        if (GfxGetG1Element(imageId) == nullptr)
        {
            return {};
        }
        if (imageId.IsBlended())
            throw std::invalid_argument("GPU glyph assets cannot contain destination blend effects");
        GlyphKey key{ .image = imageId.GetIndex(),
                      .generation = _generations[imageId.GetIndex()],
                      .sourceRemap = imageId.HasPrimary() };
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

        const auto location = QueueDecodedImage(imageId, &palette);
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
        PROFILED_FUNCTION();

        if (_recordingFrame)
        {
            throw std::logic_error("GPU texture cache frame is already active");
        }
        DrainFrameRetirements();
        ++_recordingFrameSerial;
        TrimTTFSurfaceCache(kMaxResidentTTFSurfaces);
        _recordingFrame = true;
        _frameAllocations.clear();
    }

    AtlasResidencyToken TextureCache::SealFrame(FrameCommandStream& commands)
    {
        PROFILED_FUNCTION();

        if (!_recordingFrame)
        {
            throw std::logic_error("GPU texture cache has no active frame");
        }

        auto allocations = _frameAllocations;

        std::vector<TextureUpload> uploads;
        std::vector<AtlasAllocationId> persistentUploads;
        uploads.reserve(allocations.size());
        persistentUploads.reserve(allocations.size());
        for (const auto& pending : _pendingUploads)
        {
            const auto allocation = pending.location.GetAllocationId();
            const auto state = _allocations.find(allocation.serial);
            if (state == _allocations.end() || state->second.location.GetAllocationId() != allocation
                || !IsAllocationBound(state->second))
            {
                continue;
            }
            const auto generationIt = _generations.find(pending.location.image);
            if ((generationIt == _generations.end() || generationIt->second != pending.location.generation)
                && state->second.assetLeases.empty())
            {
                continue;
            }
            uploads.push_back({
                .atlas = pending.location.index,
                .bounds = pending.location.bounds,
                .sourcePitch = pending.pitch,
                .descriptorIndex = pending.location.GetDescriptorIndex(),
                .descriptor = {
                    .atlasOrigin = { pending.location.bounds.x, pending.location.bounds.y },
                    .atlasLayer = static_cast<int32_t>(pending.location.index),
                },
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
        commands.atlasAssetLeases = std::move(_frameAssetLeases);
        EndRecordingFrame();
        return token;
    }

    void TextureCache::RetireFrame(AtlasResidencyToken token, FrameRetirement retirement)
    {
        if (!token)
        {
            throw std::logic_error("Cannot retire an empty GPU atlas residency token");
        }
        std::scoped_lock lock(_retirementMutex);
        _pendingFrameRetirements.push_back({ token, retirement });
    }

    void TextureCache::DrainFrameRetirements()
    {
        PROFILED_FUNCTION();

        if (_recordingFrame)
        {
            throw std::logic_error("GPU atlas retirements require a frame boundary");
        }

        std::vector<PendingFrameRetirement> pending;
        {
            std::scoped_lock lock(_retirementMutex);
            pending.swap(_pendingFrameRetirements);
        }
        for (const auto& item : pending)
        {
            ApplyFrameRetirement(item.token, item.retirement);
        }
        // Keep the preallocated queue's capacity available to concurrent handle destruction.
        std::scoped_lock lock(_retirementMutex);
        for (const auto id : _pendingAssetRetirements)
            ApplyAssetRetirement(id);
        _pendingAssetRetirements.clear();
    }

    void TextureCache::ApplyFrameRetirement(AtlasResidencyToken token, FrameRetirement retirement)
    {
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
        if (retirement == FrameRetirement::Presented && !leaseIt->second.persistentUploads.empty())
        {
            // A full catalog can contain hundreds of thousands of uploads. Removing
            // each individually repeatedly scans and compacts the same vector.
            // Keep the complete allocation identity: a recycled atlas slot must
            // never acknowledge a newer upload when an older frame completes.
            const auto hash = [](const AtlasAllocationId& allocation) { return std::hash<uint64_t>{}(allocation.serial); };
            const auto& completed = leaseIt->second.persistentUploads;
            std::unordered_set<AtlasAllocationId, decltype(hash)> presented(
                completed.begin(), completed.end(), completed.size(), hash);
            std::erase_if(_pendingUploads, [&](const PendingUpload& pending) {
                return presented.contains(pending.location.GetAllocationId());
            });
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
        if (!_recordingFrame)
        {
            throw std::logic_error("GPU texture cache has no active frame");
        }

        EndRecordingFrame();
    }

    void TextureCache::InvalidateImage(uint32_t image)
    {
        if (_recordingFrame)
        {
            // Dynamic preview images are commonly replaced immediately before their first draw in a frame. If this image has
            // not been referenced by the current command stream yet, retiring it now is both safe and necessary: deferring the
            // invalidation would make the following draw resolve the previous frame's resident pixels. Once an allocation has
            // been bound, however, the recorded commands and residency lease must keep it alive until the frame is sealed.
            bool boundThisFrame = std::any_of(
                _frameAllocations.begin(), _frameAllocations.end(), [this, image](const auto& allocation) {
                    const auto state = _allocations.find(allocation.serial);
                    return state != _allocations.end() && state->second.location.GetAllocationId() == allocation
                        && state->second.location.image == image;
                });
            if (const auto dependency = _assetDependencies.find(image); dependency != _assetDependencies.end())
                boundThisFrame |= std::any_of(dependency->second.begin(), dependency->second.end(), [this](uint64_t id) {
                    return _assetLeases.at(id).lastBoundFrame == _recordingFrameSerial;
                });
            if (!boundThisFrame)
            {
                ApplyInvalidation(image);
                return;
            }

            _generations.try_emplace(image, 0);
            if (std::find(_deferredInvalidations.begin(), _deferredInvalidations.end(), image) == _deferredInvalidations.end())
            {
                _deferredInvalidations.push_back(image);
            }
            return;
        }
        ApplyInvalidation(image);
    }

    void TextureCache::ApplyInvalidation(uint32_t image)
    {
        if (const auto dependency = _assetDependencies.find(image); dependency != _assetDependencies.end())
            for (const auto id : dependency->second)
                if (const auto lease = _assetLeases.at(id).lease.lock())
                    lease->_current = false;
        const uint32_t oldGeneration = _generations[image]++;
        std::erase_if(_pendingUploads, [this, image, oldGeneration](const PendingUpload& pending) {
            const auto state = _allocations.find(pending.location.allocationSerial);
            return pending.location.image == image && pending.location.generation == oldGeneration
                && (state == _allocations.end() || state->second.assetLeases.empty());
        });

        if (image < _images.size() && _images[image].has_value())
        {
            RetireAllocation(_images[image]->location);
            if (_images[image]->zeroCoverage.has_value())
                RetireAllocation(*_images[image]->zeroCoverage);
            _images[image].reset();
        }
        if (image < _imageMetadata.size())
            _imageMetadata[image].reset();

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

    const TextureCache::SpriteMetadata* TextureCache::GetImageMetadata(uint32_t image)
    {
        if (image == kImageIndexUndefined)
            return nullptr;
        if (image >= _imageMetadata.size())
            _imageMetadata.resize(static_cast<size_t>(image) + 1);
        auto& cached = _imageMetadata[image];
        if (!cached.has_value())
        {
            const auto* element = GfxGetG1Element(image);
            if (element == nullptr)
                return nullptr;
            cached = SpriteMetadata{
                .width = element->width,
                .height = element->height,
                .xOffset = element->xOffset,
                .yOffset = element->yOffset,
                .flags = element->flags.holder,
                .zoomedOffset = element->zoomedOffset,
            };
        }
        return &*cached;
    }

    TextureCache::ResidentImage* TextureCache::GetOrLoadResidentImage(ImageId imageId)
    {
        const auto image = imageId.GetIndex();
        const auto* metadata = GetImageMetadata(image);
        if (metadata == nullptr || metadata->width <= 0 || metadata->height <= 0)
            return nullptr;
        if (image >= _images.size())
            _images.resize(static_cast<size_t>(image) + 1);
        auto& resident = _images[image];
        if (resident.has_value())
            return &*resident;

        std::optional<TextureLocation> zeroCoverage;
        const auto location = QueueDecodedImage(imageId, nullptr, &zeroCoverage);
        if (!location.has_value())
            return nullptr;
        try
        {
            resident = ResidentImage{ *location, *metadata, 0, zeroCoverage };
        }
        catch (...)
        {
            RemovePending(location->GetAllocationId());
            RetireAllocation(*location);
            if (zeroCoverage.has_value())
            {
                RemovePending(zeroCoverage->GetAllocationId());
                RetireAllocation(*zeroCoverage);
            }
            throw;
        }
        return &*resident;
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
                throw std::runtime_error(
                    "GPU sprite atlas layer limit " + std::to_string(_maxAtlasLayers) + " reached for image "
                    + std::to_string(image) + " at " + std::to_string(width) + "x" + std::to_string(height));
            }
            const int32_t widthOrder = AtlasPage::CalculateImageSizeOrder(width);
            const int32_t heightOrder = AtlasPage::CalculateImageSizeOrder(height);
            _atlases.emplace_back(static_cast<uint32_t>(_atlases.size()), 1 << widthOrder, 1 << heightOrder);
            target = &_atlases.back();
            target->Initialise(kResidentAtlasDimension, kResidentAtlasDimension);
        }

        auto location = target->Allocate(width, height);
        location.image = image;
        location.generation = generation;
        location.allocationSerial = _nextAllocationSerial;
        try
        {
            const auto insertion = _allocations.emplace(location.allocationSerial, AllocationState{ .location = location });
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

    std::optional<TextureLocation> TextureCache::QueueDecodedImage(
        ImageId imageId, const PaletteMap* palette, std::optional<TextureLocation>* zeroCoverage)
    {
        const auto* element = GfxGetG1Element(imageId);
        if (element == nullptr || element->width <= 0 || element->height <= 0)
        {
            return std::nullopt;
        }

        std::array<PaletteIndex, 8> glyphPalette{};
        std::span<const PaletteIndex> remap;
        if (palette != nullptr)
        {
            // DrawGlyph's sole producer is drawCharacterSprite: FontSpriteGetCodepointSprite supplies a
            // source-remapped image and TextDrawInfo supplies eight entries. Destination effects belong
            // in GPU commands, never in asset decoding.
            if (imageId.HasPrimary())
            {
                for (size_t i = 0; i < glyphPalette.size(); i++)
                    glyphPalette[i] = (*palette)[i];
                remap = glyphPalette;
            }
        }
        const auto decoded = DecodeTrustedSpriteAsset(*element, remap);
        auto location = AllocateImage(imageId.GetIndex(), element->width, element->height);
        std::optional<TextureLocation> coverageLocation;
        try
        {
            QueueUpload(
                location, decoded.pixels.data(), decoded.pixels.size() * sizeof(PaletteIndex),
                static_cast<uint32_t>(element->width));
            if (zeroCoverage != nullptr)
            {
                std::vector<uint8_t> zeroPixels;
                for (size_t i = 0; i < decoded.pixels.size(); i++)
                {
                    if (decoded.coverage[i] != 0 && decoded.pixels[i] == PaletteIndex::transparent)
                    {
                        if (zeroPixels.empty())
                            zeroPixels.resize(decoded.pixels.size());
                        zeroPixels[i] = 255;
                    }
                }
                if (!zeroPixels.empty())
                {
                    coverageLocation = AllocateImage(imageId.GetIndex(), element->width, element->height);
                    QueueUpload(*coverageLocation, zeroPixels.data(), zeroPixels.size(), static_cast<uint32_t>(element->width));
                }
                *zeroCoverage = coverageLocation;
            }
            return location;
        }
        catch (...)
        {
            RemovePending(location.GetAllocationId());
            RetireAllocation(location);
            if (coverageLocation.has_value())
            {
                RemovePending(coverageLocation->GetAllocationId());
                RetireAllocation(*coverageLocation);
            }
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

    TextureBinding TextureCache::BindResidentForRecording(ResidentImage& resident)
    {
        const auto& location = resident.location;
        if (resident.lastBoundFrame != _recordingFrameSerial)
        {
            // Persistent image records remain valid until ApplyInvalidation removes them. Validate the allocation once per
            // image per frame, then make every repeated sprite reference a serial comparison instead of a hash lookup.
            const auto state = _allocations.find(location.allocationSerial);
            if (state == _allocations.end() || state->second.location.GetAllocationId() != location.GetAllocationId())
            {
                throw std::logic_error("GPU atlas image references an unknown allocation");
            }
            resident.lastBoundFrame = _recordingFrameSerial;
            if (state->second.lastBoundFrame != _recordingFrameSerial)
            {
                state->second.lastBoundFrame = _recordingFrameSerial;
                _frameAllocations.push_back(location.GetAllocationId());
            }
        }
        return { location.index, location.coords };
    }

    TextureBinding TextureCache::BindForRecording(const TextureLocation& location)
    {
        if (location.allocationSerial == 0)
        {
            throw std::logic_error("GPU atlas texture has no allocation identity");
        }
        // A park view can reference one sprite thousands of times. Recording
        // residency once here avoids a per-frame sort/unique over every draw.
        const auto state = _allocations.find(location.allocationSerial);
        if (state == _allocations.end() || state->second.location.GetAllocationId() != location.GetAllocationId())
        {
            throw std::logic_error("GPU atlas texture references an unknown allocation");
        }
        if (state->second.lastBoundFrame != _recordingFrameSerial)
        {
            state->second.lastBoundFrame = _recordingFrameSerial;
            _frameAllocations.push_back(location.GetAllocationId());
        }
        return { location.index, location.coords };
    }

    void TextureCache::EndRecordingFrame()
    {
        _recordingFrame = false;
        _frameAllocations.clear();
        _frameAssetLeases.clear();
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
        RemovePending(location.GetAllocationId());
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
