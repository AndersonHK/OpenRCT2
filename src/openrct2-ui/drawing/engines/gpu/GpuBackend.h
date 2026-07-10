/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "GpuCommandStream.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace OpenRCT2::Ui::Gpu
{
    enum class BackendApi : uint8_t
    {
        Vulkan,
        OpenGLLegacy,
    };

    enum class PresentMode : uint8_t
    {
        VSync,
        Immediate,
    };

    enum class OutputColorMode : uint8_t
    {
        Sdr,
        Hdr10IfAvailable,
    };

    struct Extent
    {
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct BackendCapabilities
    {
        BackendApi api = BackendApi::Vulkan;
        uint32_t framesInFlight = 1;
        uint32_t maxTextureDimension = 0;
        uint32_t maxTextureArrayLayers = 0;
        uint64_t deviceLocalMemory = 0;
        uint64_t uploadRingCapacity = 0;
        bool supportsLineCommands = false;
        bool supportsOpaqueRectCommands = false;
        bool supportsTransparencyCommands = false;
        bool supportsWeatherCommands = false;
        bool supportsAsyncReadback = false;
        bool supportsCanvasUpload = false;
        bool supportsHdr10Output = false;
        bool hdr10OutputActive = false;
        bool supportsIndexedDrawCommands = false;
    };

    struct BackendConfig
    {
        void* nativeWindow = nullptr;
        Extent logicalExtent{};
        PresentMode presentMode = PresentMode::VSync;
        OutputColorMode outputColorMode = OutputColorMode::Sdr;
        float hdrPaperWhiteNits = 203.0f;
        uint64_t uploadRingBytesPerFrame = 32 * 1024 * 1024;
        std::string shaderDirectory;
    };

    struct FrameHandle
    {
        uint64_t frameNumber = 0;
        uint32_t frameSlot = 0;
        uint32_t imageIndex = 0;
        Extent drawableExtent{};
    };

    struct UploadSlice
    {
        uint64_t offset = 0;
        std::span<std::byte> bytes;

        explicit operator bool() const noexcept
        {
            return !bytes.empty();
        }
    };

    struct ReadbackRequest
    {
        uint64_t id = 0;
        Extent extent{};
        bool indexed = true;
    };

    // One extensible record for the integrated EverythingPark benchmark.
    // The scheduler fills simulation/paint, while the backend fills command
    // submission, GPU and presentation fields it can measure independently.
    struct FrameTimings
    {
        uint64_t frameNumber = 0;
        double simulationMicroseconds = 0.0;
        double paintMicroseconds = 0.0;
        double cpuSubmitMicroseconds = 0.0;
        double cpuPresentMicroseconds = 0.0;
        double gpuMicroseconds = 0.0;
        double presentWaitMicroseconds = 0.0;
        bool hasSimulationMeasurement = false;
        bool hasPaintMeasurement = false;
        bool hasGpuTimestamp = false;
        bool hasPresentWaitMeasurement = false;
    };

    /**
     * API-neutral contract between OpenRCT2's deterministic paint recorder and
     * an explicit GPU backend. Implementations own all graphics resources,
     * synchronisation, staging, execution and presentation state.
     *
     * Submit must not retain references into FrameCommandStream. A backend may
     * copy commands into its current persistently mapped ring or consume them
     * synchronously while recording its native command buffer.
     */
    class Backend
    {
    public:
        virtual ~Backend() = default;

        virtual void Initialise(const BackendConfig& config) = 0;
        virtual void Dispose() = 0;
        [[nodiscard]] virtual const BackendCapabilities& GetCapabilities() const noexcept = 0;

        virtual void Resize(Extent logicalExtent) = 0;
        virtual void SetPresentMode(PresentMode mode) = 0;

        [[nodiscard]] virtual std::optional<FrameHandle> BeginFrame(uint64_t frameNumber) = 0;
        [[nodiscard]] virtual UploadSlice AllocateUpload(uint64_t size, uint64_t alignment) = 0;
        virtual void SetPalette(std::span<const std::byte> rgba) = 0;
        virtual void SetRemapPalette(std::span<const std::byte> indices) = 0;
        virtual void SetBlendPalette(std::span<const std::byte> indices) = 0;
        virtual void Submit(const FrameHandle& frame, const FrameCommandStream& commands) = 0;
        virtual void Present(const FrameHandle& frame) = 0;

        // CPU submission and presentation timings are available during bring-up.
        // GPU timestamps and an isolated presentation wait are reported only
        // when the backend can measure them without a synchronising readback.
        [[nodiscard]] virtual std::optional<FrameTimings> GetLatestTimings() const = 0;

        // Readback requests are recorded after Submit and before Present. They
        // complete with the owning frame fence; polling never waits for GPU
        // work, and backends preserve unread results across frame-slot reuse.
        virtual void RequestReadback(const FrameHandle& frame, ReadbackRequest request) = 0;
        [[nodiscard]] virtual bool TryTakeReadback(uint64_t requestId, std::span<std::byte> destination) = 0;
        virtual void WaitIdle() = 0;
    };

    [[nodiscard]] constexpr std::string_view GetBackendName(BackendApi api) noexcept
    {
        switch (api)
        {
            case BackendApi::Vulkan:
                return "Vulkan";
            case BackendApi::OpenGLLegacy:
                return "OpenGL (legacy)";
        }
        return "Unknown";
    }

    [[nodiscard]] uint32_t GetRequiredSdlWindowFlags(BackendApi api) noexcept;
} // namespace OpenRCT2::Ui::Gpu
