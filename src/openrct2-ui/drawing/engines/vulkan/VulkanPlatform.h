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
    #include <functional>
    #include <memory>
    #include <openrct2-renderer/vulkan/VulkanPresentationHost.h>
    #include <optional>
    #include <vector>
    #include <vulkan/vulkan.h>

struct SDL_Window;

namespace OpenRCT2::Ui::Vulkan::Platform
{
    // Keeps native paint/window events responsive while a driver compiles pipelines.
    void PreparePipelines(SDL_Window* window, const std::function<void()>& work);
    // The caller owns the SDL window and must dispose the backend before destroying it.
    [[nodiscard]] std::unique_ptr<PresentationHost> CreatePresentationHost(SDL_Window* window);
    [[nodiscard]] uint32_t GetRequiredSdlWindowFlags() noexcept;
    void LoadVulkanLibrary();
    void UnloadVulkanLibrary() noexcept;
    [[nodiscard]] std::vector<const char*> GetInstanceExtensions(SDL_Window* window);
    [[nodiscard]] VkSurfaceKHR CreateSurface(SDL_Window* window, VkInstance instance);
    [[nodiscard]] VkExtent2D GetDrawableExtent(SDL_Window* window) noexcept;
    // UI-thread only. The Windows SDR-content brightness is in absolute nits;
    // unavailable/ambiguous display information leaves the caller's fallback explicit.
    [[nodiscard]] std::optional<float> GetSdrWhiteNits(SDL_Window* window);
} // namespace OpenRCT2::Ui::Vulkan::Platform

#endif // ENABLE_VULKAN
