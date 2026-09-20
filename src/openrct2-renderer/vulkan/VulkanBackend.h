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

    #include "VulkanDevice.h"
    #include "VulkanFrameExecutor.h"
    #include "VulkanLightFxPipeline.h"
    #include "VulkanLinePipeline.h"
    #include "VulkanPalettePipeline.h"
    #include "VulkanRectPipeline.h"
    #include "VulkanResources.h"
    #include "VulkanTransparencyPipeline.h"
    #include "VulkanWeatherPipeline.h"
    #include "VulkanWorldSurfacePipeline.h"

    #include <array>
    #include <cstddef>
    #include <memory>
    #include <mutex>
    #include <openrct2-renderer/gpu/GpuBackend.h>
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
        // Must outlive Device, including its destructor and failed initialisation cleanup.
        std::unique_ptr<PresentationHost> _presentationHost;
        Gpu::BackendConfig _config;
        Drawing::RenderUploadTelemetry* _activeTelemetry = nullptr;
        uint64_t _lostTelemetrySamples = 0;
        Device _device;
        FrameExecutor _executor;
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
        bool _submitted = false;
        bool _finalCanvasComposite = false;
        bool _ready = false;
        std::optional<uint32_t> _lastPresentedFrameIndex;
        bool _lastPresentedCanvasComposite = false;
        std::array<Image, kFramesInFlight> _captureImages;
        std::array<std::optional<Gpu::FrameRgbaCapture>, kFramesInFlight> _captures;
        bool _captureRequested = false;

    public:
        explicit Backend(std::unique_ptr<PresentationHost> presentationHost, std::shared_ptr<DeviceContextOwner> owner = {});
        ~Backend() override;

        void Initialise(const Gpu::BackendConfig& config) override;
        void Dispose() override;
        [[nodiscard]] bool SupportsGpuLightFxRasterization() const noexcept override;

        void Resize(Gpu::Extent logicalExtent, Gpu::Extent drawableExtent) override;
        void RequestSurfaceFormatRefresh() override;
        void SetPresentMode(Gpu::PresentMode mode) override;
        void SetScaleSettings(Gpu::ScaleSettings settings) override;

        [[nodiscard]] std::optional<Gpu::FrameHandle> BeginFrame(uint64_t frameNumber) override;
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
        [[nodiscard]] bool RequestFrameCapture(const Gpu::FrameHandle& frame) override;
        [[nodiscard]] std::optional<Gpu::FrameRgbaCapture> ReadbackFrameRgba(uint64_t frameNumber) override;
        void WaitIdle() override;
        std::shared_ptr<DeviceContext> GetDeviceContext() const noexcept
        {
            return _device.GetContext();
        }

    private:
        void ValidateActiveFrame(const Gpu::FrameHandle& frame) const;
        void ClearActiveFrame() noexcept;
        void RecordFrameCapture(bool lightFxEnabled);
        void PublishReadbackTelemetry(uint64_t bytes);
        void HarvestGpuTimingsForFrame(uint32_t frameIndex);
        void PublishTimings(const Gpu::FrameTimings& timings);
    };

    [[nodiscard]] std::unique_ptr<Gpu::Backend> CreateBackend(
        std::unique_ptr<PresentationHost> presentationHost, std::shared_ptr<DeviceContextOwner> owner = {});
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
