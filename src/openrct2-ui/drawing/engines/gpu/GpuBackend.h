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
#include <openrct2/drawing/IDrawingEngine.h>
#include <optional>
#include <span>
#include <string>
#include <vector>

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

    enum class FrameAcquireMode : uint8_t
    {
        Wait,
        SkipIfBusy,
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

        bool operator==(const Extent&) const = default;
    };

    struct BackendCapabilities
    {
        BackendApi api = BackendApi::Vulkan;
        uint32_t framesInFlight = 1;
        uint32_t maxTextureDimension = 0;
        uint32_t maxTextureArrayLayers = 0;
        uint64_t deviceLocalMemory = 0;
        uint64_t uploadRingCapacity = 0;
        bool supportsNonBlockingFrameAcquire = false;
        bool supportsLineCommands = false;
        bool supportsOpaqueRectCommands = false;
        bool supportsTransparencyCommands = false;
        bool supportsWeatherCommands = false;
        bool supportsLightFxComposition = false;
        bool supportsGpuLightFxRasterization = false;
        bool supportsAsyncReadback = false;
        bool supportsCanvasUpload = false;
        bool supportsGpuTimestamps = false;
        bool supportsHdrMetadata = false;
        bool supportsHdr10Output = false;
        bool hdr10OutputActive = false;
        bool supportsIndexedDrawCommands = false;
    };

    struct BackendConfig
    {
        void* nativeWindow = nullptr;
        Extent logicalExtent{};
        Extent drawableExtent{};
        PresentMode presentMode = PresentMode::VSync;
        FrameAcquireMode frameAcquireMode = FrameAcquireMode::Wait;
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

    // The backend and drawing-engine benchmark surfaces share one record.
    using FrameTimings = Drawing::FrameTimings;

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

        // Logical extent sizes indexed render targets. Drawable extent is the
        // physical surface size sampled by the UI thread; zero means the
        // surface is minimized or temporarily unavailable.
        virtual void Resize(Extent logicalExtent, Extent drawableExtent) = 0;
        // Re-query presentation formats at the next normal frame boundary.
        virtual void RequestSurfaceFormatRefresh() = 0;
        virtual void SetPresentMode(PresentMode mode) = 0;

        [[nodiscard]] virtual std::optional<FrameHandle> BeginFrame(uint64_t frameNumber) = 0;
        [[nodiscard]] virtual UploadSlice AllocateUpload(uint64_t size, uint64_t alignment) = 0;
        virtual void SetPalette(std::span<const std::byte> rgba) = 0;
        virtual void SetRemapPalette(std::span<const std::byte> indices) = 0;
        virtual void SetBlendPalette(std::span<const std::byte> indices) = 0;
        virtual void SetLightFxFalloffs(std::span<const std::byte> layers) = 0;
        virtual void Submit(const FrameHandle& frame, const FrameCommandStream& commands) = 0;
        virtual void Present(const FrameHandle& frame) = 0;
        // Releases an acquired frame after recording or upload failure. The
        // backend must restore semaphore, frame-slot and swapchain invariants.
        virtual void AbandonFrame(const FrameHandle& frame) = 0;

        // CPU submission, presentation, and isolated presentation-call timings
        // are available during bring-up. GPU timestamps are reported only when
        // the backend can measure them without a synchronising readback.
        [[nodiscard]] virtual std::optional<FrameTimings> GetLatestTimings() const = 0;
        // Completed samples are copied into caller-owned reusable storage
        // without touching the graphics API; WaitIdle is reserved for explicit
        // benchmark phase boundaries.
        virtual void TakeCompletedTimings(std::vector<FrameTimings>& samples) = 0;

        // Readback requests are recorded after Submit and before Present. They
        // complete with the owning frame fence; polling never waits for GPU
        // work, and backends preserve unread results across frame-slot reuse.
        virtual void RequestReadback(const FrameHandle& frame, ReadbackRequest request) = 0;
        [[nodiscard]] virtual bool TryTakeReadback(uint64_t requestId, std::span<std::byte> destination) = 0;
        // Explicit blocking capture of the latest presented indexed canvas.
        // This is reserved for synchronous consumers such as screenshots.
        [[nodiscard]] virtual bool ReadbackLatestIndexedCanvas(Extent extent, std::span<std::byte> destination) = 0;
        virtual void WaitIdle() = 0;
    };

} // namespace OpenRCT2::Ui::Gpu
