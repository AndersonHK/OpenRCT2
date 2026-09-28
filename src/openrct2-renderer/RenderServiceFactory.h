/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#pragma once
#include <memory>

namespace OpenRCT2::Drawing
{
    struct IRenderServiceFactory;
}
namespace OpenRCT2::Ui::Vulkan
{
    class DeviceContextOwner;
}
namespace OpenRCT2::Renderer
{
    // Always enabled; Vulkan initialisation is deferred until an image operation needs it.
    // A supplied owner shares the UI's presentation device; omission uses a lazy graphics-only owner.
    // Creation failures are reported explicitly, without a software fallback.
    std::shared_ptr<Drawing::IRenderServiceFactory> CreateConfiguredRenderServiceFactory(
        std::shared_ptr<Ui::Vulkan::DeviceContextOwner> owner = {});
}
