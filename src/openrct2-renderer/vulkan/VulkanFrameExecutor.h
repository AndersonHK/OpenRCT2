/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once
#ifdef ENABLE_VULKAN
    #include "VulkanBalloonPipeline.h"
    #include "VulkanLightFxPipeline.h"
    #include "VulkanLinePipeline.h"
    #include "VulkanRectPipeline.h"
    #include "VulkanResources.h"
    #include "VulkanTerrainDrawPipeline.h"
    #include "VulkanTransparencyPipeline.h"
    #include "VulkanWeatherPipeline.h"
    #include "VulkanWorldSurfacePipeline.h"

    #include <functional>
    #include <string>

namespace OpenRCT2::Ui::Vulkan
{
    struct FrameOutput
    {
        bool composite = false;
        const Image* canvas = nullptr;
        const Image* lightMap = nullptr;
    };

    // One indexed pass implementation for the main and bounded auxiliary domains.
    // Domain resources are never shared while mutable; the device owner is shared.
    class FrameExecutor final
    {
        IndexedResources _resources;
        LinePipeline _linePipeline;
        RectPipeline _rectPipeline;
        WorldSurfacePipeline _worldSurfacePipeline;
        BalloonPipeline _balloonPipeline;
        bool _balloonPipelineReady{};
        Gpu::TerrainUploadStats _terrainUploads{};
        TerrainDrawPipeline _terrainPipeline;
        bool _terrainPipelineReady{};
        struct PendingTerrainStatus
        {
            UploadRing* ring{};
            UploadAllocation allocation{};
            uint32_t viewport{}, columns{};
            bool worldSurface{};
        };
        std::vector<std::vector<PendingTerrainStatus>> _terrainStatuses;
        // Large catalog admissions live until the submitting slot's existing fence retires.
        // Ordinary frames continue to use the shared ring without another allocation.
        std::vector<std::unique_ptr<UploadRing>> _atlasAdmissions;
        std::vector<std::unique_ptr<UploadRing>> _worldAdmissions;
        std::string _terrainFailure; // Terminal for this executor generation.

        TransparencyPipeline _transparencyPipeline;
        WeatherPipeline _weatherPipeline;
        LightFxPipeline _lightFxPipeline;
        std::array<std::byte, 256 * 4> _pendingPalette{};
        std::array<std::byte, 256 * 256> _pendingRemapPalette{};
        std::array<std::byte, 256 * 256> _pendingBlendPalette{};
        std::vector<std::byte> _pendingLightFalloffs;
        uint64_t _paletteVersion = 1;
        std::array<uint64_t, kFramesInFlight> _framePaletteVersions{};
        bool _remapPaletteDirty = true;
        bool _blendPaletteDirty = true;
        bool _lightFalloffsDirty = false;
        bool _lightFalloffsRecorded = false;

        std::shared_ptr<DeviceContext> _context;
        std::filesystem::path _shaderDirectory;
        Gpu::Extent _logicalExtent{};
        bool _enableWorldPasses = true;
        const SubmissionToken* _activeToken = nullptr;

    public:
        ~FrameExecutor();
        void Initialise(
            std::shared_ptr<DeviceContext> context, Gpu::Extent logicalExtent, std::filesystem::path shaderDirectory,
            uint32_t frameCount = kFramesInFlight, uint32_t atlasLayers = Gpu::kAtlasLayers, bool enableWorldPasses = true);
        void Dispose();
        void Resize(Gpu::Extent logicalExtent);
        bool SupportsGpuLightFxRasterization() const noexcept
        {
            return _lightFxPipeline.IsAvailable();
        }
        IndexedResources& GetResources() noexcept
        {
            return _resources;
        }
        const IndexedResources& GetResources() const noexcept
        {
            return _resources;
        }
        uint64_t GetPaletteVersion(uint32_t slot) const
        {
            return _framePaletteVersions.at(slot);
        }
        void SetPalette(std::span<const std::byte> rgba);
        void SetRemapPalette(std::span<const std::byte> indices);
        void SetBlendPalette(std::span<const std::byte> indices);
        void SetLightFxFalloffs(std::span<const std::byte> layers);
        FrameOutput Record(
            const SubmissionToken& token, const Gpu::FrameCommandStream& commands, uint8_t clearIndex = 0,
            std::span<const std::byte> initialIndices = {}, const std::function<void(GpuTimestampPoint)>& timestamp = {});
        Gpu::TerrainUploadStats GetTerrainUploadStats() const noexcept
        {
            return _terrainUploads;
        }
        Gpu::BalloonUploadStats GetBalloonUploadStats() const noexcept
        {
            return _balloonPipeline.GetUploadStats();
        }
        // Owner must call after this slot's existing fence completes, before its upload ring resets.
        // Does not wait or reenter Device; safe from the slot-retirement callback.
        void CompleteTerrainStatus(uint32_t frameIndex);
        // Auxiliary domains remain bitmap-only until their first recorded native world request.
        void EnableWorldPasses();
        void Commit() noexcept;
        void Discard(uint32_t frameIndex);

    private:
        void InitialiseDrawingPipelines(bool gpuLightFxSupported);
        void DisposeDrawingPipelines();
        UploadAllocation StageUpload(
            std::span<const std::byte> source, const char* errorMessage,
            Drawing::UploadCategory category = Drawing::UploadCategory::lookup);
        void RecordTerrainStatus(const SubmissionToken& token, const Gpu::Terrain::DrawCamera& camera);
        void RecordWorldSurfaceStatus(const SubmissionToken& token);
        void RecordPendingPalette();
        void RecordPendingIndexTable(std::span<const std::byte> indices, bool& dirty, bool blend);
        void RecordPendingLightFalloffs();
        void RecordTextureUploads(const Gpu::FrameCommandStream& commands);
        const Image* RecordLightFx(const Gpu::FrameCommandStream& commands);
    };
} // namespace OpenRCT2::Ui::Vulkan
#endif
