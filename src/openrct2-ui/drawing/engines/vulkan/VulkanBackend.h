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

    #include "../gpu/GpuBackend.h"
    #include "VulkanDevice.h"
    #include "VulkanLightFxPipeline.h"
    #include "VulkanLinePipeline.h"
    #include "VulkanPalettePipeline.h"
    #include "VulkanRectPipeline.h"
    #include "VulkanResources.h"
    #include "VulkanTransparencyPipeline.h"
    #include "VulkanWeatherPipeline.h"

    #include <array>
    #include <cstddef>
    #include <memory>
    #include <mutex>
    #include <optional>
    #include <vector>

namespace OpenRCT2::Ui::Vulkan
{
    /**
     * Vulkan ownership and frame-lifetime implementation of the API-neutral
     * GPU backend. The resource upload and indexed presentation surface is
     * complete; gameplay selection remains gated on drawing-context wiring,
     * readback, and visual comparison even after indexed command execution.
     */
    class Backend final : public Gpu::Backend
    {
    private:
        Gpu::BackendConfig _config;
        Device _device;
        IndexedResources _resources;
        LinePipeline _linePipeline;
        RectPipeline _rectPipeline;
        TransparencyPipeline _transparencyPipeline;
        WeatherPipeline _weatherPipeline;
        LightFxPipeline _lightFxPipeline;
        PalettePipeline _palettePipeline;
        std::optional<FrameToken> _activeToken;
        std::optional<Gpu::FrameHandle> _activeFrame;
        std::optional<Gpu::FrameTimings> _latestTimings;
        static constexpr size_t kCompletedTimingCapacity = 256;
        std::array<Gpu::FrameTimings, kCompletedTimingCapacity> _completedTimings{};
        size_t _completedTimingStart = 0;
        size_t _completedTimingCount = 0;
        std::array<std::optional<Gpu::FrameTimings>, kFramesInFlight> _frameTimings;
        mutable std::mutex _timingsMutex;
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
        bool _submitted = false;
        bool _finalCanvasComposite = false;
        bool _ready = false;
        std::optional<uint32_t> _lastPresentedFrameIndex;
        bool _lastPresentedCanvasComposite = false;

    public:
        Backend() = default;
        ~Backend() override;

        void Initialise(const Gpu::BackendConfig& config) override;
        void Dispose() override;
        [[nodiscard]] bool SupportsGpuLightFxRasterization() const noexcept override;

        void Resize(Gpu::Extent logicalExtent, Gpu::Extent drawableExtent) override;
        void RequestSurfaceFormatRefresh() override;
        void SetPresentMode(Gpu::PresentMode mode) override;

        [[nodiscard]] std::optional<Gpu::FrameHandle> BeginFrame(uint64_t frameNumber) override;
        [[nodiscard]] Gpu::UploadSlice AllocateUpload(uint64_t size, uint64_t alignment) override;
        void SetPalette(std::span<const std::byte> rgba) override;
        void SetRemapPalette(std::span<const std::byte> indices) override;
        void SetBlendPalette(std::span<const std::byte> indices) override;
        void SetLightFxFalloffs(std::span<const std::byte> layers) override;
        void Submit(const Gpu::FrameHandle& frame, const Gpu::FrameCommandStream& commands) override;
        void Present(const Gpu::FrameHandle& frame) override;
        void AbandonFrame(const Gpu::FrameHandle& frame) override;
        [[nodiscard]] std::optional<Gpu::FrameTimings> GetLatestTimings() const override;
        void TakeCompletedTimings(std::vector<Gpu::FrameTimings>& samples) override;

        [[nodiscard]] bool ReadbackLatestIndexedCanvas(Gpu::Extent extent, std::span<std::byte> destination) override;
        void WaitIdle() override;

    private:
        void ValidateActiveFrame(const Gpu::FrameHandle& frame) const;
        void InitialiseDrawingPipelines(bool gpuLightFxSupported);
        void DisposeDrawingPipelines();
        void ClearActiveFrame() noexcept;
        [[nodiscard]] UploadAllocation StageUpload(std::span<const std::byte> source, const char* errorMessage);
        void RecordPendingPalette();
        void RecordPendingIndexTable(std::span<const std::byte> indices, bool& dirty, bool blend);
        void RecordPendingLightFalloffs();
        void RecordTextureUploads(const Gpu::FrameCommandStream& commands);
        [[nodiscard]] bool RecordLightFx(const Gpu::FrameCommandStream& commands);
        void HarvestGpuTimingsForFrame(uint32_t frameIndex);
        void PublishTimings(const Gpu::FrameTimings& timings);
    };

    [[nodiscard]] std::unique_ptr<Gpu::Backend> CreateBackend();
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
