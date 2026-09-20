/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#pragma once

// Diagnostic-only: included exclusively in Vulkan capture builds.
#include <openrct2-renderer/RenderServiceFactory.h>
#include <openrct2-renderer/vulkan/VulkanDeviceContext.h>
#include <openrct2-ui/drawing/engines/DrawingEngineFactory.hpp>
#include <openrct2/drawing/IDrawingContext.h>
#include <openrct2/drawing/RenderService.h>
#include <array>

namespace UiParitySharedService
{
    using namespace OpenRCT2;
    using namespace OpenRCT2::Drawing;
    inline constexpr std::array<const char*, 6> kPhases{
        "lifecycle-software-initial", "lifecycle-vulkan-first", "lifecycle-vulkan-after-aux",
        "lifecycle-vulkan-recreated", "lifecycle-software-return", "lifecycle-vulkan-return"
    };

    class Lifecycle final
    {
        std::shared_ptr<Ui::Vulkan::DeviceContextOwner> _owner = std::make_shared<Ui::Vulkan::DeviceContextOwner>(true);
        std::weak_ptr<Ui::Vulkan::DeviceContextOwner> _weakOwner = _owner;
        std::weak_ptr<Ui::Vulkan::DeviceContext> _weakDevice;
        std::unique_ptr<IRenderSession> _shutdownRecorder;
        uintptr_t _contextIdentity{}, _deviceIdentity{};
        uint32_t _previousWindow{};
        json_t _report{ { "version", 1 }, { "status", "incomplete" }, { "phases", json_t::array() } };

        static void Require(bool success, const char* message)
        {
            if (!success)
                throw std::runtime_error(message);
        }
        static OffscreenRenderRequest Request(std::string name)
        {
            OffscreenRenderRequest request;
            request.name = std::move(name);
            request.logicalExtent = request.outputExtent = { 64, 64 };
            request.clearIndex = 17;
            request.rgbaOutput = true;
            request.alphaPolicy = RenderAlphaPolicy::opaque;
            for (size_t i = 0; i < request.palette.size(); ++i)
            {
                const auto value = static_cast<uint8_t>(i);
                request.palette[i] = { value, value, value, 255 };
            }
            return request;
        }
        void ObserveDevice(json_t& phase)
        {
            // This observation must never keep the device alive through shutdown.
            const auto device = _owner->AcquireOffscreen();
            const auto contextIdentity = reinterpret_cast<uintptr_t>(device.get());
            const auto deviceIdentity = reinterpret_cast<uintptr_t>(device->GetDevice());
            Require(contextIdentity != 0 && deviceIdentity != 0, "Missing shared device identity");
            if (_contextIdentity == 0)
            {
                _contextIdentity = contextIdentity;
                _deviceIdentity = deviceIdentity;
                _weakDevice = device;
            }
            Require(contextIdentity == _contextIdentity && deviceIdentity == _deviceIdentity,
                    "Main/auxiliary/recreated presentation did not retain the same device");
            phase["contextIdentity"] = contextIdentity;
            phase["deviceIdentity"] = deviceIdentity;
        }
        void RenderAuxiliary(IContext& context, const std::filesystem::path& output)
        {
            auto& service = context.GetRenderService();
            auto session = service.BeginOffscreen(Request("ui-lifecycle-pattern"));
            session->GetDrawingContext().FillRect(
                session->GetRenderTarget(), static_cast<PaletteIndex>(37), 7, 11, 29, 43);
            auto completion = session->Submit();
            session.reset();
            const auto outcome = completion->Wait(std::chrono::seconds(30));
            Require(!outcome.error && outcome.result != nullptr, "Shared auxiliary request failed");
            const auto& result = *outcome.result;
            Require(result.logicalExtent == RenderExtent{ 64, 64 } && result.outputExtent == RenderExtent{ 64, 64 }
                        && result.indexed.size() == 4096 && result.rgba.size() == 16384,
                    "Shared auxiliary result has invalid extents");
            Require(result.identity == outcome.identity && result.identity == completion->GetIdentity()
                        && result.identity.submissionId != 0 && result.identity.targetId != 0
                        && result.identity.targetGeneration != 0, "Shared auxiliary result identity is invalid");
            const auto write = [&](const char* name, const std::vector<std::byte>& bytes) {
                std::ofstream stream(output / name, std::ios::binary);
                stream.exceptions(std::ios::badbit | std::ios::failbit);
                stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            };
            // Save both buffers before semantic checks, including a failed output.
            write("lifecycle-aux.indexed", result.indexed);
            write("lifecycle-aux.rgba", result.rgba);
            for (size_t y = 0; y < 64; ++y)
                for (size_t x = 0; x < 64; ++x)
                {
                    const auto expected = std::byte(x >= 7 && x <= 29 && y >= 11 && y <= 43 ? 37 : 17);
                    const auto offset = y * 64 + x;
                    Require(result.indexed[offset] == expected, "Shared auxiliary indexed pattern differs");
                    for (size_t c = 0; c < 4; ++c)
                        Require(result.rgba[offset * 4 + c] == (c == 3 ? std::byte{ 255 } : expected),
                                "Shared auxiliary RGBA pattern differs");
                }
            _report["auxiliary"] = { { "submissionId", result.identity.submissionId },
                { "targetId", result.identity.targetId }, { "targetGeneration", result.identity.targetGeneration },
                { "indexedBytes", result.indexed.size() }, { "rgbaBytes", result.rgba.size() }, { "exactPattern", true } };
            // Retained deliberately across switches and Context destruction, never submitted before shutdown.
            _shutdownRecorder = service.BeginOffscreen(Request("ui-lifecycle-unsubmitted-at-shutdown"));
        }

    public:
        const auto& Owner() const { return _owner; }
        void BeforePhase(size_t phase, IContext& context, const std::filesystem::path& output)
        {
            Require(phase < kPhases.size(), "Invalid lifecycle phase");
            if (phase == 0)
            {
                Require(!_owner->IsCreated(), "Software initialization unexpectedly created the Vulkan owner");
                _report["ownerUncreatedAfterSoftwareStartup"] = true;
            }
            if (phase == 2)
                RenderAuxiliary(context, output);
            if (phase == 1 || phase == 3 || phase == 4 || phase == 5)
            {
                const auto selected = phase == 4 ? DrawingEngine::softwareWithHardwareDisplay : DrawingEngine::vulkan;
                Config::Get().general.drawingEngine = selected;
                gIntegratedBenchmark.drawingEngine = selected;
                ContextRecreateWindow();
                Require(context.GetDrawingEngineType() == selected, "Lifecycle renderer switch selected the wrong engine");
            }
        }
        void AfterPhase(size_t phase, IContext& context, uint32_t tick, const std::filesystem::path& output)
        {
            auto* window = static_cast<SDL_Window*>(context.GetUiContext().GetWindow());
            const auto windowId = SDL_GetWindowID(window);
            Require(windowId != 0, "Lifecycle phase has no real SDL window");
            if (phase == 1 || phase == 3 || phase == 4 || phase == 5)
                Require(windowId != _previousWindow, "Requested lifecycle window recreation did not replace the SDL window");
            if (phase == 2)
                Require(windowId == _previousWindow, "Auxiliary rendering unexpectedly recreated the main window");
            _previousWindow = windowId;
            const bool vulkan = context.GetDrawingEngineType() == DrawingEngine::vulkan;
            Require(vulkan == (phase != 0 && phase != 4), "Unexpected lifecycle renderer");
            json_t observation{ { "name", kPhases[phase] }, { "windowId", windowId }, { "simulationTicks", tick },
                                { "renderer", vulkan ? "vulkan" : "softwareWithHardwareDisplay" } };
            if (phase != 0)
                ObserveDevice(observation);
            _report["phases"].push_back(std::move(observation));
            Json::WriteToFile((output / "shared-service-lifecycle.json").string(), _report);
        }
        void Shutdown(std::unique_ptr<IContext>& context, const std::filesystem::path& output)
        {
            Require(_report["phases"].size() == kPhases.size() && _shutdownRecorder != nullptr,
                    "Lifecycle did not complete all captures and retain its shutdown recorder");
            _owner.reset(); // Only production composition retains owner/device from this point onward.
            context.reset(); // Explicitly exercise service drain, main teardown, factory release, SDL shutdown.
            const bool ownerExpired = _weakOwner.expired();
            const bool deviceExpired = _weakDevice.expired();
            const bool videoStopped = SDL_WasInit(SDL_INIT_VIDEO) == 0;
            bool rejected = false;
            std::string lateError;
            try { static_cast<void>(_shutdownRecorder->Submit()); }
            catch (const RenderServiceException& error)
            {
                rejected = error.GetCode() == RenderErrorCode::shuttingDown || error.GetCode() == RenderErrorCode::invalidState;
                lateError = error.what();
            }
            catch (const std::exception& error) { lateError = error.what(); }
            _report["shutdown"] = { { "ownerExpiredWithRecorderAlive", ownerExpired },
                { "deviceExpiredWithRecorderAlive", deviceExpired }, { "sdlVideoStopped", videoStopped },
                { "lateSubmitRejected", rejected }, { "lateSubmitError", lateError }, { "strongDeviceObservationsReleased", true } };
            _shutdownRecorder.reset();
            const bool passed = ownerExpired && deviceExpired && videoStopped && rejected;
            _report["status"] = passed ? "pass" : "fail";
            Json::WriteToFile((output / "shared-service-lifecycle.json").string(), _report);
            Require(passed, "Shared lifecycle shutdown retained device/owner or accepted late submission");
        }
    };
}
