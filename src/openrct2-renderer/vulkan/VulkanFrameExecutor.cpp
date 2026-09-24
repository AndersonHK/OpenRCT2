/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_VULKAN
    #include "VulkanFrameExecutor.h"

    #include <algorithm>
    #include <cstring>
    #include <limits>
    #include <openrct2-renderer/gpu/GpuTransparencyDepth.h>
    #include <stdexcept>
namespace OpenRCT2::Ui::Vulkan
{
    namespace
    {
        template<size_t N>
        void CopyExact(std::span<const std::byte> source, std::array<std::byte, N>& destination, const char* error)
        {
            if (source.size() != destination.size())
                throw std::invalid_argument(error);
            std::copy(source.begin(), source.end(), destination.begin());
        }
    } // namespace
    FrameExecutor::~FrameExecutor()
    {
        Dispose();
    }
    void FrameExecutor::Initialise(
        std::shared_ptr<DeviceContext> context, Gpu::Extent logicalExtent, std::filesystem::path shaderDirectory,
        uint32_t frameCount, uint32_t atlasLayers, bool enableWorldPasses)
    {
        Dispose();
        if (!context || shaderDirectory.empty() || logicalExtent.width == 0 || logicalExtent.height == 0)
            throw std::invalid_argument("Invalid Vulkan executor configuration");
        _context = std::move(context);
        _shaderDirectory = std::move(shaderDirectory);
        _logicalExtent = logicalExtent;
        _enableWorldPasses = enableWorldPasses;
        _terrainStatuses.resize(frameCount);
        try
        {
            const bool gpuLightFxSupported = _enableWorldPasses && LightFxPipeline::IsSupported(*_context, logicalExtent);
            _resources.Initialise(*_context, logicalExtent, gpuLightFxSupported, frameCount, atlasLayers);
            InitialiseDrawingPipelines(gpuLightFxSupported);
        }
        catch (...)
        {
            Dispose();
            throw;
        }
        _pendingPalette.fill(std::byte{ 0 });
        for (size_t i = 0; i < 256; i++)
            _pendingPalette[i * 4 + 3] = i == 0 ? std::byte{ 0 } : std::byte{ 0xff };
        for (size_t i = 0; i < _pendingRemapPalette.size(); i++)
            _pendingRemapPalette[i] = static_cast<std::byte>(i & 0xff);
        _pendingBlendPalette = _pendingRemapPalette;
    }
    void FrameExecutor::Dispose()
    {
        // The owner must retire its domain before destruction/reconfiguration.
        DisposeDrawingPipelines();
        _resources.Dispose();
        _terrainStatuses.clear();
        _terrainFailure.clear();
        _context.reset();
        _paletteVersion = 1;
        _framePaletteVersions.fill(0);
        _remapPaletteDirty = _blendPaletteDirty = true;
        _pendingLightFalloffs.clear();
        _lightFalloffsDirty = _lightFalloffsRecorded = false;
        _activeToken = nullptr;
    }
    void FrameExecutor::Resize(Gpu::Extent extent)
    {
        _logicalExtent = extent;
        DisposeDrawingPipelines();
        const bool supported = _enableWorldPasses && LightFxPipeline::IsSupported(*_context, extent);
        try
        {
            _resources.Resize(extent, supported);
            InitialiseDrawingPipelines(supported);
        }
        catch (...)
        {
            DisposeDrawingPipelines();
            throw;
        }
    }
    void FrameExecutor::RecordTerrainStatus(const SubmissionToken& token, const Gpu::Terrain::DrawCamera& camera)
    {
        // The pipeline already validated camera/column bounds. Capture only the error word,
        // before the next viewport reuses its single GPU column buffer.
        const int32_t columnWidth = 32 >> camera.zoom;
        static_assert(offsetof(Gpu::Terrain::DrawColumnStatus, error) == 0);
        const int32_t phase = (camera.x % columnWidth + columnWidth) % columnWidth;
        const auto columns = static_cast<uint32_t>((phase + camera.width + columnWidth - 1) / columnWidth);
        if (columns == 0 || columns > Gpu::Terrain::kDrawMaximumColumns)
            throw std::logic_error("Invalid retained terrain status column count");
        auto allocation = token.upload->Allocate(columns * sizeof(uint32_t), alignof(uint32_t));
        if (!allocation)
            throw std::runtime_error("Vulkan upload ring has no room for terrain completion status");
        auto& pending = _terrainStatuses.at(token.frameIndex);
        pending.push_back({ token.upload, allocation, static_cast<uint32_t>(pending.size()), columns });
        std::array<VkBufferCopy, Gpu::Terrain::kDrawMaximumColumns> copies{};
        for (uint32_t column = 0; column < columns; column++)
            copies[column] = { column * sizeof(Gpu::Terrain::DrawColumnStatus), allocation.offset + column * sizeof(uint32_t),
                               sizeof(uint32_t) };
        const VkMemoryBarrier sourceReady{ .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                           .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                           .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT };
        vkCmdPipelineBarrier(
            token.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &sourceReady, 0,
            nullptr, 0, nullptr);
        vkCmdCopyBuffer(
            token.commandBuffer, _terrainPipeline.GetColumnBuffer().GetBuffer(), allocation.buffer, columns, copies.data());
        _terrainUploads.statusReadbackBytes += columns * sizeof(uint32_t);
        const VkMemoryBarrier statusReady{ .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                           .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                                           .dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT };
        vkCmdPipelineBarrier(
            token.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &statusReady, 0, nullptr, 0, nullptr);
        if (token.telemetry != nullptr)
        {
            token.telemetry->statusReadbackRequests++;
            token.telemetry->statusReadbackBytes += allocation.size;
        }
    }

    void FrameExecutor::RecordWorldSurfaceStatus(const SubmissionToken& token)
    {
        // One safety word per complete world submission, retired by its existing fence.
        // No image download, owner-thread wait, or per-object operation is involved.
        auto allocation = token.upload->Allocate(sizeof(uint32_t), alignof(uint32_t));
        if (!allocation)
            throw std::runtime_error("Vulkan upload ring has no room for world completion status");
        _terrainStatuses.at(token.frameIndex).push_back({ token.upload, allocation, 0, 1, true });
        const VkMemoryBarrier ready{ .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                     .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT };
        vkCmdPipelineBarrier(
            token.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &ready, 0, nullptr,
            0, nullptr);
        // WorldSurfaceStatus: emittedCount, capacity, overflow, reserved.
        const VkBufferCopy copy{ 2 * sizeof(uint32_t), allocation.offset, sizeof(uint32_t) };
        vkCmdCopyBuffer(token.commandBuffer, _worldSurfacePipeline.GetStatusBuffer().GetBuffer(), allocation.buffer, 1, &copy);
        const VkMemoryBarrier visible{ .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                       .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                                       .dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT };
        vkCmdPipelineBarrier(
            token.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &visible, 0, nullptr, 0, nullptr);
        _terrainUploads.statusReadbackBytes += sizeof(uint32_t);
        if (token.telemetry != nullptr)
        {
            token.telemetry->statusReadbackRequests++;
            token.telemetry->statusReadbackBytes += sizeof(uint32_t);
        }
    }

    void FrameExecutor::CompleteTerrainStatus(uint32_t frameIndex)
    {
        if (_terrainStatuses.empty()) // Uninitialized/disposed backend has no status to retire.
            return;
        if (!_terrainFailure.empty())
            throw std::runtime_error(_terrainFailure);
        auto& pending = _terrainStatuses.at(frameIndex);
        for (const auto& status : pending)
        {
            status.ring->Invalidate(status.allocation.offset, status.allocation.size);
            for (uint32_t column = 0; column < status.columns; column++)
            {
                uint32_t error{};
                std::memcpy(&error, status.allocation.data + column * sizeof(error), sizeof(error));
                if (error != 0 && _terrainFailure.empty())
                    _terrainFailure = std::string(
                                          status.worldSurface ? "Vulkan world output overflow: slot="
                                                              : "Vulkan retained terrain GPU failure: slot=")
                        + std::to_string(frameIndex) + " viewport=" + std::to_string(status.viewport)
                        + " column=" + std::to_string(column) + " error=" + std::to_string(error);
            }
        }
        pending.clear();
        if (!_terrainFailure.empty())
            throw std::runtime_error(_terrainFailure);
    }

    void FrameExecutor::Commit() noexcept
    {
        _resources.CommitFrameLayouts();
        _terrainPipeline.Commit();
        _lightFalloffsRecorded = false;
    }
    void FrameExecutor::Discard(uint32_t frameIndex)
    {
        _resources.DiscardFrameLayouts(frameIndex);
        _worldSurfacePipeline.DiscardPendingUploads();
        _balloonPipeline.DiscardPendingUploads();
        _terrainPipeline.DiscardPendingUploads();
        _terrainStatuses.at(frameIndex).clear();
        if (_lightFalloffsRecorded)
        {
            _lightFalloffsDirty = true;
            _resources.DiscardLightFalloffLayout();
        }
        _lightFalloffsRecorded = false;
        _framePaletteVersions[frameIndex] = 0;
        _remapPaletteDirty = _blendPaletteDirty = true;
    }
    void FrameExecutor::SetPalette(std::span<const std::byte> rgba)
    {
        CopyExact(rgba, _pendingPalette, "GPU palettes must contain exactly 256 RGBA8 entries");
        _paletteVersion++;
    }

    void FrameExecutor::SetRemapPalette(std::span<const std::byte> indices)
    {
        CopyExact(indices, _pendingRemapPalette, "GPU remap palettes must contain exactly 256 by 256 indices");
        _remapPaletteDirty = true;
    }

    void FrameExecutor::SetBlendPalette(std::span<const std::byte> indices)
    {
        CopyExact(indices, _pendingBlendPalette, "GPU blend palettes must contain exactly 256 by 256 indices");
        _blendPaletteDirty = true;
    }

    void FrameExecutor::SetLightFxFalloffs(std::span<const std::byte> layers)
    {
        if (layers.size() != 8 * 256 * 256)
            throw std::invalid_argument("GPU LightFX falloffs have an invalid size");
        _pendingLightFalloffs.assign(layers.begin(), layers.end());
        _lightFalloffsDirty = true;
    }

    void FrameExecutor::InitialiseDrawingPipelines(bool gpuLightFxSupported)
    {
        const auto cacheLock = _context->LockPipelineCache();
        _linePipeline.Initialise(*_context, _resources, _shaderDirectory);
        if (_enableWorldPasses)
            _worldSurfacePipeline.Initialise(*_context, _resources, _shaderDirectory);
        _rectPipeline.Initialise(*_context, _resources, _shaderDirectory);
        _transparencyPipeline.Initialise(*_context, _resources, _shaderDirectory);
        _weatherPipeline.Initialise(*_context, _resources, _shaderDirectory);
        _lightFxPipeline.Initialise(*_context, _resources, _shaderDirectory, gpuLightFxSupported);
    }

    void FrameExecutor::DisposeDrawingPipelines()
    {
        _lightFxPipeline.Dispose();
        _weatherPipeline.Dispose();
        _transparencyPipeline.Dispose();
        _rectPipeline.Dispose();
        _worldSurfacePipeline.Dispose();
        _balloonPipeline.Dispose();
        _balloonPipelineReady = false;
        _terrainPipeline.Dispose();
        _terrainPipelineReady = false;
        _linePipeline.Dispose();
    }

    UploadAllocation FrameExecutor::StageUpload(
        std::span<const std::byte> source, const char* errorMessage, Drawing::UploadCategory category)
    {
        auto allocation = _activeToken->upload->Allocate(source.size(), alignof(uint32_t), category);
        if (!allocation)
        {
            throw std::runtime_error(errorMessage);
        }
        std::memcpy(allocation.data, source.data(), source.size());
        allocation.RecordHostWrite();
        return allocation;
    }

    void FrameExecutor::RecordPendingPalette()
    {
        const auto frameIndex = _activeToken->frameIndex;
        if (_framePaletteVersions[frameIndex] == _paletteVersion)
        {
            return;
        }
        const auto allocation = StageUpload(
            _pendingPalette, "Vulkan upload ring has no room for the palette", Drawing::UploadCategory::palette);
        _resources.RecordPaletteUpload(_activeToken->commandBuffer, frameIndex, allocation);
        _framePaletteVersions[frameIndex] = _paletteVersion;
    }

    void FrameExecutor::RecordPendingIndexTable(std::span<const std::byte> indices, bool& dirty, bool blend)
    {
        if (!dirty)
        {
            return;
        }
        const auto allocation = StageUpload(
            indices,
            blend ? "Vulkan upload ring has no room for the blend palette"
                  : "Vulkan upload ring has no room for the remap palette");
        _resources.RecordIndexTableUpload(_activeToken->commandBuffer, allocation, blend);
        dirty = false;
    }

    const Image* FrameExecutor::RecordLightFx(const Gpu::FrameCommandStream& commands)
    {
        if (!commands.lightFx.has_value())
        {
            _resources.EnsureLightFxShaderLayouts(_activeToken->commandBuffer, _activeToken->frameIndex);
            return nullptr;
        }
        const auto& snapshot = *commands.lightFx;
        if (!snapshot.IsValid() || snapshot.width != _logicalExtent.width || snapshot.height != _logicalExtent.height)
        {
            throw std::invalid_argument("Vulkan LightFX snapshot does not match the logical canvas");
        }

        const auto palette = StageUpload(
            snapshot.lightPalette, "Vulkan upload ring has no room for the LightFX palette", Drawing::UploadCategory::lightFx);
        if (_lightFxPipeline.IsAvailable() && !_pendingLightFalloffs.empty() && !_lightFalloffsDirty)
        {
            _resources.RecordLightPaletteUpload(_activeToken->commandBuffer, _activeToken->frameIndex, palette);
            if (_lightFxPipeline.Record(*_activeToken, snapshot, _resources))
            {
                return &_resources.GetLightAccumulator(_activeToken->frameIndex);
            }
        }
        if (!snapshot.HasCpuIntensity())
        {
            throw std::runtime_error("Vulkan LightFX compute path could not consume a command-only snapshot");
        }
        const auto intensities = StageUpload(
            snapshot.intensities, "Vulkan upload ring has no room for the LightFX snapshot", Drawing::UploadCategory::lightFx);
        _resources.RecordLightFxUpload(
            _activeToken->commandBuffer, _activeToken->frameIndex, intensities, palette, snapshot.width, snapshot.height);
        return &_resources.GetLightMap(_activeToken->frameIndex);
    }

    void FrameExecutor::RecordPendingLightFalloffs()
    {
        if (!_lightFxPipeline.IsAvailable() || !_lightFalloffsDirty || _pendingLightFalloffs.empty())
            return;
        const auto allocation = StageUpload(
            _pendingLightFalloffs, "Vulkan upload ring has no room for LightFX falloffs", Drawing::UploadCategory::lightFx);
        _resources.RecordLightFalloffUpload(_activeToken->commandBuffer, allocation);
        _lightFalloffsDirty = false;
        _lightFalloffsRecorded = true;
    }

    void FrameExecutor::RecordTextureUploads(const Gpu::FrameCommandStream& commands)
    {
        if (commands.textureUploads.empty())
        {
            return;
        }

        _resources.BeginAtlasUploads(_activeToken->commandBuffer);
        for (const auto& upload : commands.textureUploads)
        {
            const auto height = static_cast<uint32_t>(std::max(0, upload.bounds.w - upload.bounds.y));
            const auto size = static_cast<VkDeviceSize>(upload.sourcePitch) * height;
            if (size > std::numeric_limits<size_t>::max() || static_cast<size_t>(size) != upload.pixels.size())
            {
                throw std::invalid_argument("Vulkan texture upload payload does not match its bounds and pitch");
            }
            const auto allocation = StageUpload(
                upload.pixels, "Vulkan upload ring has no room for a sprite atlas upload", Drawing::UploadCategory::atlas);
            const std::span<const Gpu::SpriteAssetDescriptor> descriptor{ &upload.descriptor, 1 };
            const auto descriptorAllocation = StageUpload(
                std::as_bytes(descriptor), "Vulkan upload ring has no room for a sprite descriptor upload",
                Drawing::UploadCategory::atlas);
            _resources.RecordAtlasUpload(
                _activeToken->commandBuffer, allocation, upload.atlas, upload.bounds, upload.sourcePitch);
            _resources.RecordSpriteDescriptorUpload(_activeToken->commandBuffer, descriptorAllocation, upload.descriptorIndex);
        }
        _resources.EndAtlasUploads(_activeToken->commandBuffer);
    }

    FrameOutput FrameExecutor::Record(
        const SubmissionToken& token, const Gpu::FrameCommandStream& commands, uint8_t clearIndex,
        std::span<const std::byte> initialIndices, const std::function<void(GpuTimestampPoint)>& timestamp)
    {
        if (_activeToken)
            throw std::logic_error("Vulkan executor is already recording");
        if (token.frameIndex >= _resources.GetFrameCount())
            throw std::out_of_range("Vulkan executor slot out of range");
        if ((commands.worldSurfaces || !commands.balloons.empty() || !commands.terrainScenes.empty()) && !_enableWorldPasses)
            throw std::invalid_argument("Native world resources are not admitted in this auxiliary domain");
        if (!initialIndices.empty()
            && initialIndices.size() != static_cast<size_t>(_logicalExtent.width) * _logicalExtent.height)
            throw std::invalid_argument("Initial indices do not match the Vulkan target");
        if (!_terrainFailure.empty())
            throw std::runtime_error(_terrainFailure);
        if (!_terrainStatuses.at(token.frameIndex).empty())
            throw std::logic_error("Terrain status must retire before reusing its submission slot");
        _activeToken = &token;
        struct Reset
        {
            const SubmissionToken*& token;
            ~Reset()
            {
                token = nullptr;
            }
        } reset{ _activeToken };
        RecordPendingPalette();
        RecordPendingIndexTable(_pendingRemapPalette, _remapPaletteDirty, false);
        RecordPendingIndexTable(_pendingBlendPalette, _blendPaletteDirty, true);
        RecordPendingLightFalloffs();
        RecordTextureUploads(commands);
        _resources.EnsureAtlasShaderLayout(token.commandBuffer);
        if (timestamp)
            timestamp(GpuTimestampPoint::uploadsComplete);
        // Presented frames are complete generations. Clearing indexed colour and depth makes dropped generations harmless.
        _resources.RecordCanvasAndDepthClear(token.commandBuffer, token.frameIndex, clearIndex);
        if (!initialIndices.empty())
        {
            const auto allocation = StageUpload(
                initialIndices, "Vulkan upload ring has no room for initial indices", Drawing::UploadCategory::atlas);
            _resources.RecordInitialIndices(token.commandBuffer, token.frameIndex, allocation);
        }
        _terrainUploads = {};
        if (commands.worldSurfaces.has_value())
        {
            _worldSurfacePipeline.Record(token, *commands.worldSurfaces);
            if (commands.worldSurfaces->recordCount != 0)
                RecordWorldSurfaceStatus(token);
        }
        _balloonPipeline.BeginFrame();
        if (!commands.balloons.empty())
        {
            if (!_balloonPipelineReady)
            {
                const auto cacheLock = _context->LockPipelineCache();
                _balloonPipeline.Initialise(*_context, _resources, _shaderDirectory);
                _balloonPipelineReady = true;
            }
            for (const auto& scene : commands.balloons)
                _balloonPipeline.Record(token, scene);
        }
        if (!commands.terrainScenes.empty())
        {
            if (!_terrainPipelineReady)
            {
                // TerrainDrawPipeline serializes its nested pipeline-cache use internally.
                _terrainPipeline.Initialise(
                    *_context, _resources, _shaderDirectory, _shaderDirectory / "terrain_retained_emit.comp.spv",
                    _shaderDirectory / "terrain_columns.comp.spv");
                _terrainPipelineReady = true;
            }
            for (const auto& scene : commands.terrainScenes)
            {
                if (scene.sprites == nullptr)
                    throw std::invalid_argument("Missing immutable retained terrain sprite table");
                _terrainPipeline.Record(
                    token, scene.snapshot, *scene.sprites, scene.camera, _resources.GetAtlasLayers() * Gpu::kAtlasSlotsPerLayer,
                    scene.peeps, scene.peepAssets);
                _terrainUploads.tileBytes += _terrainPipeline.GetEmission().GetLastTileUploadBytes();
                _terrainUploads.materialBytes += _terrainPipeline.GetEmission().GetLastMaterialUploadBytes();
                _terrainUploads.spriteBytes += _terrainPipeline.GetLastSpriteUploadBytes();
                ++_terrainUploads.viewportSubmissions;
                RecordTerrainStatus(token, scene.camera);
            }
        }
        _linePipeline.Record(token, commands.lines);
        _rectPipeline.Record(token, commands.opaqueRects, commands.opaqueSprites);
        bool finalComposite = false;
        if (!commands.transparentRects.empty())
        {
            const auto layers = Gpu::MaxTransparencyDepth(commands.transparentRects);
            finalComposite = _transparencyPipeline.Record(token, commands.transparentRects, layers);
        }
        _weatherPipeline.Record(token, commands.weather, finalComposite);
        if (timestamp)
            timestamp(GpuTimestampPoint::indexedDrawComplete);
        const Image* lightMap = RecordLightFx(commands);
        if (timestamp)
            timestamp(GpuTimestampPoint::lightFxComplete);
        const auto& finalCanvas = finalComposite ? _resources.GetCompositeCanvas(token.frameIndex)
                                                 : _resources.GetIndexedCanvas(token.frameIndex);
        return { finalComposite, &finalCanvas, lightMap };
    }
} // namespace OpenRCT2::Ui::Vulkan
#endif
