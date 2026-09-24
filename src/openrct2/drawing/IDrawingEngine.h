/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#define OPENRCT2_VULKAN_ONLY 1

#include "../core/FlagHolder.hpp"
#include "PaletteType.h"
#include "PresentationGeneration.h"
#include "RenderUploadTelemetry.h"
#include "WeatherDrawer.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

enum DrawingEngineFlag
{
    /**
     * Whether or not the engine will only draw changed blocks of the screen each frame.
     */
    dirtyOptimisations,

    /**
     * The drawing engine is capable of processing the drawing in parallel.
     */
    parallelDrawing = 1 << 1,
};
using DrawingEngineFlags = FlagHolder<uint8_t, DrawingEngineFlag>;

namespace OpenRCT2::Ui
{
    struct IUiContext;
} // namespace OpenRCT2::Ui

namespace OpenRCT2::Drawing
{
    // Lifetime counters, independent of the bounded timing-sample queue. Read
    // before/after an explicit benchmark drain to distinguish late work from
    // skipped draw attempts. Accepted presentation is not proof of scanout:
    // MAILBOX/the compositor may replace an accepted image before display.
    struct FramePresentationCounters
    {
        uint64_t publishedVisualPackets = 0;
        uint64_t supersededVisualPackets = 0;
        uint64_t unavailableVisualPackets = 0;
        uint64_t discardedVisualPackets = 0;
        uint64_t visualFrameSubmissions = 0;
        uint64_t presentRequests = 0;
        uint64_t presentAccepted = 0;
        uint64_t presentOutOfDate = 0;
        uint64_t fenceCompletedFrames = 0;
        uint64_t lostTimingSamples = 0;
    };

    struct FrameTimings
    {
        uint64_t frameNumber = 0;
        std::optional<RenderUploadTelemetry> uploadTelemetry;
        bool telemetryOnly = false;
        double cpuSubmitMicroseconds = 0.0;
        double cpuPresentMicroseconds = 0.0;
        double gpuMicroseconds = 0.0;
        double gpuUploadMicroseconds = 0.0;
        double gpuDrawMicroseconds = 0.0;
        double gpuLightFxMicroseconds = 0.0;
        double gpuCompositeMicroseconds = 0.0;
        double presentCallMicroseconds = 0.0;
        // steady_clock epoch, sampled immediately after an accepted queue
        // present. Transported with the existing fence-complete timing sample.
        std::optional<uint64_t> acceptedPresentNanoseconds;
        bool hasGpuTimestamp = false;
        bool hasGpuPassTimestamps = false;
        bool hasPresentCallMeasurement = false;
    };

    struct IDrawingContext;
    struct RenderTarget;

    struct IDrawingEngine
    {
        virtual ~IDrawingEngine()
        {
        }

        // Retained publication is separate from native GPU paint admission; the default preserves bulk software capture.
        virtual EntityPublicationProfile GetEntityPublicationProfile() const
        {
            return EntityPublicationProfile::legacyBulk;
        }
        virtual void Initialise() = 0;
        virtual void Resize(uint32_t width, uint32_t height) = 0;
        // Display identity changed independently of the logical canvas size.
        virtual void NotifyDisplayChanged()
        {
        }
        virtual void SetPalette(const GamePalette& colours) = 0;

        virtual void SetVSync(bool vsync) = 0;

        // Non-blocking frame admission. Asynchronous backends return false while a newer visual packet is already queued;
        // callers can keep simulating instead of constructing work that the backend would immediately supersede.
        [[nodiscard]] virtual bool CanBeginFrame()
        {
            return true;
        }

        virtual void Invalidate(int32_t left, int32_t top, int32_t right, int32_t bottom) = 0;
        // Complete-frame backends do not consume projected dirty rectangles. Callers may skip that invalidation work while
        // simulation-side presentation records continue to track the actual world changes.
        virtual bool CanSkipViewportInvalidation() const
        {
            return false;
        }
        virtual void BeginDraw() = 0;
        virtual void EndDraw() = 0;
        virtual void PaintWindows() = 0;
        virtual void PaintWeather() = 0;
        virtual void CopyRect(int32_t x, int32_t y, int32_t width, int32_t height, int32_t dx, int32_t dy) = 0;
        virtual std::string Screenshot() = 0;

        virtual IDrawingContext* GetDrawingContext() = 0;
        virtual RenderTarget* getRT() = 0;

        virtual DrawingEngineFlags GetFlags() = 0;

        // Renderers publish only fence-complete samples. Unsupported backends
        // retain the default unavailable result.
        [[nodiscard]] virtual std::optional<FrameTimings> GetLatestFrameTimings() const
        {
            return std::nullopt;
        }

        // Non-blocking consumption of every completed renderer sample since
        // the previous call. Unsupported renderers retain the empty default.
        virtual void TakeCompletedFrameTimings(std::vector<FrameTimings>& samples)
        {
            samples.clear();
        }

        // Explicit benchmark boundary. Implementations may wait for already
        // submitted rendering work, then return every remaining completed
        // sample. This is never called from the routine frame hot path.
        virtual void DrainFrameTimings(std::vector<FrameTimings>& samples)
        {
            TakeCompletedFrameTimings(samples);
        }

        [[nodiscard]] virtual std::optional<FramePresentationCounters> GetFramePresentationCounters() const
        {
            return std::nullopt;
        }

        virtual void InvalidateImage(uint32_t image) = 0;
    };

    struct IDrawingEngineFactory
    {
        virtual ~IDrawingEngineFactory()
        {
        }
        [[nodiscard]] virtual std::unique_ptr<IDrawingEngine> Create(Ui::IUiContext& uiContext) = 0;
    };

    struct IWeatherDrawer
    {
        virtual ~IWeatherDrawer() = default;
        virtual void Draw(
            RenderTarget& rt, int32_t x, int32_t y, int32_t width, int32_t height, int32_t xStart, int32_t yStart,
            const uint8_t* weatherpattern)
            = 0;
    };
} // namespace OpenRCT2::Drawing
