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
#include "GpuResourceLimits.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <openrct2/drawing/IDrawingEngine.h>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace OpenRCT2::Ui::Gpu
{
    // Keep unavailable/invalid display data distinct from a valid dim setting.
    [[nodiscard]] inline float NormaliseHdrPaperWhiteNits(float value) noexcept
    {
        return std::isfinite(value) && value > 0 ? std::clamp(value, 80.0f, 1000.0f) : 203.0f;
    }

    [[nodiscard]] inline std::optional<float> DecodeWindowsSdrWhiteNits(uint32_t level) noexcept
    {
        if (level == 0)
            return std::nullopt;
        // DISPLAYCONFIG_SDR_WHITE_LEVEL: 1000 means an 80-nit SDR white.
        return static_cast<float>(static_cast<double>(level) * 80.0 / 1000.0);
    }

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

    enum class ScaleMode : uint8_t
    {
        Nearest,
        Linear,
        SmoothNearest,
    };

    struct ScaleSettings
    {
        ScaleMode mode = ScaleMode::Nearest;
        // Smooth nearest first expands by ceil(windowScale), independently of
        // drawable/HiDPI scaling, matching the software display contract.
        uint32_t integerScale = 1;
        bool operator==(const ScaleSettings&) const = default;
    };

    struct BackendConfig
    {
        Extent logicalExtent{};
        Extent drawableExtent{};
        PresentMode presentMode = PresentMode::VSync;
        FrameAcquireMode frameAcquireMode = FrameAcquireMode::Wait;
        OutputColorMode outputColorMode = OutputColorMode::Sdr;
        float hdrPaperWhiteNits = 203.0f;
        uint64_t uploadRingBytesPerFrame = kDefaultUploadRingBytes;
        std::string shaderDirectory;
        std::string pipelineCacheDirectory;
        // UI hosts may execute driver compilation off-thread while pumping native messages.
        // Device/window creation precedes this callback; it must finish the work before returning.
        std::function<void(const std::function<void()>&)> preparePipelines;
        // Diagnostic-only swapchain transfer support. Ordinary frames never
        // copy or read back final output, even when this capability is enabled.
        bool enableDiagnosticCapture = false;
        bool enableUploadTelemetry = false;
        ScaleSettings scaleSettings{};
    };

    struct FrameHandle
    {
        uint64_t frameNumber = 0;
        uint32_t frameSlot = 0;
        uint32_t imageIndex = 0;
        Extent drawableExtent{};
    };

    struct FrameRgbaCapture
    {
        uint64_t frameNumber = 0;
        Extent logicalExtent{};
        Extent drawableExtent{};
        uint64_t paletteVersion = 0;
        uint64_t swapchainGeneration = 0;
        uint32_t sourceFormat = 0; // Native output format (VkFormat for Vulkan).
        std::string deviceName;
        uint32_t vendorId = 0;
        uint32_t deviceId = 0;
        uint32_t driverVersion = 0; // Native driver version; encoding is vendor-specific.
        bool lightFxEnabled = false;
        ScaleSettings scaleSettings{};
        BalloonUploadStats balloonUploads{};
        TerrainUploadStats terrainUploads{};
        // Actual SDR attachment bytes, top-down, tightly packed RGBA8, straight
        // alpha. Includes palette/lighting/output encoding and physical scaling;
        // excludes the OS compositor, display colour management and cursor.
        std::vector<std::byte> rgba;
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
        [[nodiscard]] virtual bool SupportsGpuLightFxRasterization() const noexcept = 0;

        // Logical extent sizes indexed render targets. Drawable extent is the
        // physical surface size sampled by the UI thread; zero means the
        // surface is minimized or temporarily unavailable.
        virtual void Resize(Extent logicalExtent, Extent drawableExtent) = 0;
        // Re-query presentation formats at the next normal frame boundary.
        virtual void RequestSurfaceFormatRefresh() = 0;
        virtual void SetPresentMode(PresentMode mode) = 0;
        virtual void SetScaleSettings(ScaleSettings)
        {
        }
        virtual void SetHdrPaperWhiteNits(float)
        {
        }

        [[nodiscard]] virtual std::optional<FrameHandle> BeginFrame(uint64_t frameNumber) = 0;
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
        [[nodiscard]] virtual Drawing::FramePresentationCounters GetFramePresentationCounters() const
        {
            return {};
        }

        // Explicit blocking capture of the latest presented indexed canvas.
        // This is reserved for synchronous consumers such as screenshots.
        [[nodiscard]] virtual bool ReadbackLatestIndexedCanvas(Extent extent, std::span<std::byte> destination) = 0;
        // Request before Submit; false means capture is disabled/unsupported.
        // Readback blocks for the requested presented frame. Captures expire on
        // slot reuse or resize; absent/expired IDs return nullopt, never a newer
        // image. These diagnostic methods share the backend's execution thread.
        [[nodiscard]] virtual bool RequestFrameCapture(const FrameHandle&)
        {
            return false;
        }
        [[nodiscard]] virtual std::optional<FrameRgbaCapture> ReadbackFrameRgba(uint64_t)
        {
            return std::nullopt;
        }
        virtual void WaitIdle() = 0;
    };

} // namespace OpenRCT2::Ui::Gpu
