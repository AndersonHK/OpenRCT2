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
    // Construction and selection never initialise graphics. A supplied owner is
    // the UI's presentation owner; omission selects a lazy graphics-only owner.
    // Selection reads the loaded configuration, preserving the transitional
    // software path. An explicitly selected Vulkan operation never falls back.
    std::shared_ptr<Drawing::IRenderServiceFactory> CreateConfiguredRenderServiceFactory(
        std::shared_ptr<Ui::Vulkan::DeviceContextOwner> owner = {});
}
