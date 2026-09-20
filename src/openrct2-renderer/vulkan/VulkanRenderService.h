/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#pragma once
#ifdef ENABLE_VULKAN
    #include "VulkanDeviceContext.h"

    #include <filesystem>
    #include <functional>
    #include <openrct2/drawing/RenderService.h>

namespace OpenRCT2::Ui::Vulkan
{
    struct RenderServiceOptions
    {
        std::filesystem::path shaderDirectory;
        uint32_t atlasLayers = 4;
        uint64_t uploadBytes = 32 * 1024 * 1024;
        uint64_t maxTargetPixels = 4 * 1024 * 1024;
        // Empty tables select identity. Application composition supplies owned
        // graphics lookup snapshots before enabling filtered preview callers.
        std::vector<std::byte> remapPalette;
        std::vector<std::byte> blendPalette;
    };

    // A UI factory must return the main renderer's existing owner. The provider
    // is invoked on the submission worker, once, only for an actual render job.
    // No fallback device is created if a supplied owner is unavailable.
    using DeviceProvider = std::function<std::shared_ptr<DeviceContext>()>;
    std::shared_ptr<Drawing::IRenderServiceFactory> CreateRenderServiceFactory(
        RenderServiceOptions options, DeviceProvider deviceProvider);
    // Headless composition only: the same lazy service, without SDL or a surface.
    std::shared_ptr<Drawing::IRenderServiceFactory> CreateGraphicsOnlyRenderServiceFactory(RenderServiceOptions options);
} // namespace OpenRCT2::Ui::Vulkan
#endif
