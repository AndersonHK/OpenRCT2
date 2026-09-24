/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <memory>
#include <openrct2/drawing/IDrawingEngine.h>
#ifndef ENABLE_VULKAN
    #error The graphical application requires Vulkan.
#endif
#include "vulkan/VulkanDrawingEngine.h"
#include <openrct2-renderer/vulkan/VulkanDeviceContext.h>

namespace OpenRCT2::Ui
{
    struct IUiContext;

    class DrawingEngineFactory final : public Drawing::IDrawingEngineFactory
    {
        std::shared_ptr<Vulkan::DeviceContextOwner> _owner;
    public:
        explicit DrawingEngineFactory(std::shared_ptr<Vulkan::DeviceContextOwner> owner = {})
            : _owner(owner ? std::move(owner) : std::make_shared<Vulkan::DeviceContextOwner>(true))
        {
        }
        [[nodiscard]] std::unique_ptr<Drawing::IDrawingEngine> Create(IUiContext& uiContext) override
        {
            return CreateVulkanDrawingEngine(uiContext, _owner);
        }
    };
} // namespace OpenRCT2::Ui
