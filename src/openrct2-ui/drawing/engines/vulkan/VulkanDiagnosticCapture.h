/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <openrct2-renderer/gpu/GpuBackend.h>
#include <openrct2/drawing/RetainedBalloonScene.h>
#include <openrct2/world/MapPresentationSnapshot.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace OpenRCT2::Drawing
{
    struct IDrawingEngine;
}

namespace OpenRCT2::Ui::Vulkan::Diagnostic
{
    struct Coverage
    {
        uint64_t frameNumber = 0;
        uint64_t resizeVersion = 0;
        uint64_t surfaceFormatVersion = 0;
        uint64_t paletteVersion = 0;
        uint64_t graphicsLookupTablesVersion = 0;
        size_t lineCount = 0;
        size_t opaqueRectCount = 0;
        size_t opaqueSpriteCount = 0;
        size_t transparentRectCount = 0;
        size_t weatherCount = 0;
        size_t nativeBalloonViewports = 0;
        uint64_t cpuBalloonSpriteCalls = 0;
        size_t ttfOpaqueCount = 0;
        size_t ttfTransparentCount = 0;
        std::vector<std::array<int32_t, 5>> ttfClippedBoundsAndThreshold;
        bool worldSurfaces = false;
        uint64_t worldEpoch = 0;
        uint32_t worldSurfaceRecordCount = 0;
        size_t lightFxCommandCount = 0;
        bool lightFxCpuIntensityAttached = false;
        uint64_t lightFxCommandHash = 0;
        uint64_t lightFxPaletteHash = 0;
        std::array<uint32_t, 12> lightFxTypeHistogram{};
    };

    struct BalloonPublication
    {
        bool retained = false;
        uint64_t epoch{}, sequence{};
        size_t count{}, entityCount{};
        Drawing::BalloonPublicationMetrics metrics;
        Drawing::BalloonPublicationCopyTotals producerTotals;
        // Same ordered scalar contract as the public registry census in the harness.
        std::vector<std::array<int64_t, 12>> records;
    };

    struct CaptureResult
    {
        std::string name;
        Gpu::FrameRgbaCapture output;
        std::vector<std::byte> indexed;
        Coverage coverage;
        std::optional<BalloonPublication> balloonPublication;
        std::shared_ptr<const MapPresentationSnapshot> terrainPublication;
        std::vector<Gpu::TerrainSceneCommand> terrainScenes;
        uint64_t atlasLease{};
        std::array<uint64_t, 4> terrainPreparation{}; // epoch, map copies, catalog builds, residency binds
    };

    // The request owns its result. Timeout/cancellation is terminal, so a late
    // worker completion can never turn a failed named sample into a success.
    class CaptureRequest final
    {
    public:
        explicit CaptureRequest(std::string name)
            : _name(std::move(name))
        {
            if (_name.empty())
                throw std::invalid_argument("Vulkan diagnostic capture requires a name");
        }

        void BindBalloonPublication(BalloonPublication publication)
        {
            std::scoped_lock lock(_mutex);
            if (_complete || _balloonPublication.has_value() || _frameNumber.has_value())
                throw std::logic_error("Balloon publication must bind once during the named draw before EndDraw");
            _balloonPublication = std::move(publication);
        }

        void BindTerrainPublication(std::shared_ptr<const MapPresentationSnapshot> publication)
        {
            std::scoped_lock lock(_mutex);
            if (_complete || _terrainPublication != nullptr || _frameNumber.has_value() || publication == nullptr)
                throw std::logic_error("Terrain publication must bind once before EndDraw");
            _terrainPublication = std::move(publication);
        }

        void BindFrame(uint64_t frameNumber, uint64_t atlasLease = 0,
            std::array<uint64_t, 4> terrainPreparation = {})
        {
            std::scoped_lock lock(_mutex);
            if (_complete)
                throw std::logic_error("Vulkan diagnostic request is already complete");
            if (_frameNumber.has_value())
                throw std::logic_error("Vulkan diagnostic request is already bound to a frame");
            _frameNumber = frameNumber;
            _atlasLease = atlasLease;
            _terrainPreparation = terrainPreparation; // Latch owner counters before packet publication.
        }

        void Complete(CaptureResult result)
        {
            {
                std::scoped_lock lock(_mutex);
                if (_complete)
                    return;
                if (!_frameNumber.has_value() || result.output.frameNumber != *_frameNumber
                    || result.coverage.frameNumber != *_frameNumber)
                {
                    _error = std::make_exception_ptr(std::runtime_error("Vulkan diagnostic capture frame identity mismatch"));
                }
                else
                {
                    result.name = _name;
                    result.balloonPublication = std::move(_balloonPublication);
                    result.terrainPublication = std::move(_terrainPublication);
                    result.atlasLease = _atlasLease;
                    result.terrainPreparation = _terrainPreparation;
                    _result = std::move(result);
                }
                _complete = true;
            }
            _completed.notify_all();
        }

        void Fail(std::exception_ptr error)
        {
            {
                std::scoped_lock lock(_mutex);
                if (_complete)
                    return;
                _error = error ? std::move(error)
                               : std::make_exception_ptr(std::runtime_error("Vulkan diagnostic capture cancelled"));
                _complete = true;
            }
            _completed.notify_all();
        }

        [[nodiscard]] bool IsPending() const
        {
            std::scoped_lock lock(_mutex);
            return !_complete;
        }

        [[nodiscard]] CaptureResult Wait(std::chrono::milliseconds timeout)
        {
            std::unique_lock lock(_mutex);
            if (!_completed.wait_for(lock, timeout, [this] { return _complete; }))
            {
                _error = std::make_exception_ptr(std::runtime_error("Vulkan diagnostic capture timed out: " + _name));
                _complete = true;
                _completed.notify_all();
            }
            if (_error)
                std::rethrow_exception(_error);
            if (_consumed)
                throw std::logic_error("Vulkan diagnostic capture result already consumed");
            _consumed = true;
            return std::move(_result);
        }

    private:
        const std::string _name;
        mutable std::mutex _mutex;
        std::condition_variable _completed;
        std::optional<uint64_t> _frameNumber;
        uint64_t _atlasLease{};
        std::array<uint64_t, 4> _terrainPreparation{};
        std::optional<BalloonPublication> _balloonPublication;
        std::shared_ptr<const MapPresentationSnapshot> _terrainPublication;
        std::exception_ptr _error;
        CaptureResult _result;
        bool _complete = false;
        bool _consumed = false;
    };

#ifdef OPENRCT2_VULKAN_DIAGNOSTICS
    // Harness only: enable before constructing the engine, then arm on its UI
    // thread before BeginDraw. The normal production factory has no hook.
    void EnableCaptureForTesting();
    // Publication only; does not enable GPU balloon painting. Ordinary builds remain legacy bulk.
    void SetRetainedBalloonPublicationForTesting(bool enabled);
    // Additional isolated fixture gate. Unsupported scenes keep ordinary entity painting.
    void SetNativeBalloonFixtureForTesting(bool enabled);
    void SetNativeTerrainFixtureForTesting(bool enabled);
    [[nodiscard]] std::shared_ptr<CaptureRequest> ArmNextCapture(Drawing::IDrawingEngine& engine, std::string name);
#endif
} // namespace OpenRCT2::Ui::Vulkan::Diagnostic
