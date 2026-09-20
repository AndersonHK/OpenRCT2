/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#include "RenderServiceFactory.h"

#include <openrct2/OpenRCT2.h>
#include <openrct2/config/Config.h>
#include <openrct2/drawing/IDrawingEngine.h>
#include <openrct2/drawing/RenderService.h>
#include <utility>
#ifdef ENABLE_VULKAN
    #include "gpu/GpuGraphicsLookupTables.h"
    #include "vulkan/VulkanRenderService.h"

    #include <openrct2/Context.h>
    #include <openrct2/PlatformEnvironment.h>
    #include <openrct2/core/Path.hpp>
    #include <openrct2/drawing/Drawing.Sprite.h>
#endif

namespace OpenRCT2::Renderer
{
    namespace
    {
        class ConfiguredFactory final : public Drawing::IRenderServiceFactory
        {
#ifdef ENABLE_VULKAN
            std::shared_ptr<Ui::Vulkan::DeviceContextOwner> _owner;
#endif
        public:
            explicit ConfiguredFactory(std::shared_ptr<Ui::Vulkan::DeviceContextOwner> owner)
#ifdef ENABLE_VULKAN
                : _owner(owner ? std::move(owner) : std::make_shared<Ui::Vulkan::DeviceContextOwner>(false))
#endif
            {
#ifndef ENABLE_VULKAN
                static_cast<void>(owner);
#endif
            }
            bool IsEnabled() const override
            {
                return gIntegratedBenchmark.drawingEngine.value_or(Config::Get().general.drawingEngine)
                    == DrawingEngine::vulkan;
            }
            std::unique_ptr<Drawing::IRenderService> Create() override
            {
                if (!IsEnabled())
                    throw Drawing::RenderServiceException(
                        { Drawing::RenderErrorCode::unavailable, "Vulkan rendering is not selected in the loaded configuration" });
#ifdef ENABLE_VULKAN
                // LazyRenderService calls this on the Context owner thread only
                // after the caller has loaded its graphics. No worker reads G1.
                if (GetContext() == nullptr || gOpenRCT2NoGraphics)
                    throw Drawing::RenderServiceException(
                        { Drawing::RenderErrorCode::unavailable, "Vulkan image rendering requires a graphics-enabled Context" });
                const auto* baseGraphics = GfxGetG1Element(0);
                if (baseGraphics == nullptr || baseGraphics->offset == nullptr)
                    throw Drawing::RenderServiceException(
                        { Drawing::RenderErrorCode::unavailable, "Vulkan image rendering requires loaded base graphics" });
                auto& environment = GetContext()->GetPlatformEnvironment();
                Ui::Vulkan::RenderServiceOptions options;
                options.shaderDirectory = Path::Combine(
                    environment.GetDirectoryPath(DirBase::openrct2, DirId::shaders), "vulkan");
                const auto lookup = Ui::Gpu::CaptureGraphicsLookupTables();
                options.remapPalette.assign(lookup.remap.begin(), lookup.remap.end());
                if (lookup.hasBlend)
                    options.blendPalette.assign(lookup.blend.begin(), lookup.blend.end());
                const auto owner = _owner;
                return Ui::Vulkan::CreateRenderServiceFactory(
                           std::move(options), [owner] { return owner->AcquireOffscreen(); })
                    ->Create();
#else
                throw Drawing::RenderServiceException(
                    { Drawing::RenderErrorCode::unavailable, "This build does not include the Vulkan render service" });
#endif
            }
        };
    }

    std::shared_ptr<Drawing::IRenderServiceFactory> CreateConfiguredRenderServiceFactory(
        std::shared_ptr<Ui::Vulkan::DeviceContextOwner> owner)
    {
        return std::make_shared<ConfiguredFactory>(std::move(owner));
    }
}
