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

    #include <cstdint>
    #include <vector>
    #include <vulkan/vulkan.h>

struct SDL_Window;

namespace OpenRCT2::Ui::Vulkan::Platform
{
    [[nodiscard]] uint32_t GetRequiredSdlWindowFlags() noexcept;
    void LoadVulkanLibrary();
    void UnloadVulkanLibrary() noexcept;
    [[nodiscard]] std::vector<const char*> GetInstanceExtensions(SDL_Window* window);
    [[nodiscard]] VkSurfaceKHR CreateSurface(SDL_Window* window, VkInstance instance);
    [[nodiscard]] VkExtent2D GetDrawableExtent(SDL_Window* window) noexcept;
} // namespace OpenRCT2::Ui::Vulkan::Platform

#endif // ENABLE_VULKAN
