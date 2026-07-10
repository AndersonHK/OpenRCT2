/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#ifdef ENABLE_VULKAN

    #include <filesystem>
    #include <vulkan/vulkan.h>

namespace OpenRCT2::Ui::Vulkan
{
    [[nodiscard]] VkShaderModule LoadShaderModule(VkDevice device, const std::filesystem::path& path);
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
