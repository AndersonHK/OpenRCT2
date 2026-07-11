/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_VULKAN

    #if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
        #define VK_USE_PLATFORM_WIN32_KHR
    #endif

    #include "VulkanPlatform.h"

    #include <SDL.h>
    #if defined(_WIN32)
        #include <SDL_syswm.h>
    #else
        #include <SDL_vulkan.h>
    #endif

    #include <algorithm>
    #include <stdexcept>
    #include <string>

namespace OpenRCT2::Ui::Vulkan::Platform
{
    uint32_t GetRequiredSdlWindowFlags() noexcept
    {
#if defined(_WIN32)
        // The bundled static SDL is built without its Vulkan video-driver hooks.
        // Native Win32 WSI only needs the ordinary SDL window and its HWND.
        return 0;
#else
        return SDL_WINDOW_VULKAN;
#endif
    }

    void LoadVulkanLibrary()
    {
#if !defined(_WIN32)
        if (SDL_Vulkan_LoadLibrary(nullptr) != 0)
        {
            throw std::runtime_error(std::string("SDL could not load Vulkan: ") + SDL_GetError());
        }
#endif
    }

    void UnloadVulkanLibrary() noexcept
    {
#if !defined(_WIN32)
        SDL_Vulkan_UnloadLibrary();
#endif
    }

    std::vector<const char*> GetInstanceExtensions(SDL_Window* window)
    {
#if defined(_WIN32)
        (void)window;
        return { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
#else
        uint32_t count = 0;
        if (SDL_Vulkan_GetInstanceExtensions(window, &count, nullptr) != SDL_TRUE)
        {
            throw std::runtime_error(std::string("SDL could not query Vulkan extensions: ") + SDL_GetError());
        }
        std::vector<const char*> result(count);
        if (SDL_Vulkan_GetInstanceExtensions(window, &count, result.data()) != SDL_TRUE)
        {
            throw std::runtime_error(std::string("SDL could not query Vulkan extensions: ") + SDL_GetError());
        }
        return result;
#endif
    }

    VkSurfaceKHR CreateSurface(SDL_Window* window, VkInstance instance)
    {
#if defined(_WIN32)
        SDL_SysWMinfo windowInfo{};
        SDL_VERSION(&windowInfo.version);
        if (SDL_GetWindowWMInfo(window, &windowInfo) != SDL_TRUE)
        {
            throw std::runtime_error(std::string("SDL could not expose the Win32 window handle: ") + SDL_GetError());
        }
        if (windowInfo.subsystem != SDL_SYSWM_WINDOWS)
        {
            throw std::runtime_error("SDL window is not backed by the Win32 video driver");
        }

        const auto createSurface = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
            vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR"));
        if (createSurface == nullptr)
        {
            throw std::runtime_error("Vulkan loader does not expose vkCreateWin32SurfaceKHR");
        }
        const VkWin32SurfaceCreateInfoKHR createInfo = {
            .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
            .hinstance = windowInfo.info.win.hinstance,
            .hwnd = windowInfo.info.win.window,
        };
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        const auto result = createSurface(instance, &createInfo, nullptr, &surface);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error(
                "vkCreateWin32SurfaceKHR failed with Vulkan result " + std::to_string(result));
        }
        return surface;
#else
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (SDL_Vulkan_CreateSurface(window, instance, &surface) != SDL_TRUE)
        {
            throw std::runtime_error(std::string("SDL could not create Vulkan surface: ") + SDL_GetError());
        }
        return surface;
#endif
    }

    VkExtent2D GetDrawableExtent(SDL_Window* window) noexcept
    {
        int32_t width = 0;
        int32_t height = 0;
#if defined(_WIN32)
        SDL_SysWMinfo windowInfo{};
        SDL_VERSION(&windowInfo.version);
        RECT clientRect{};
        if (SDL_GetWindowWMInfo(window, &windowInfo) != SDL_TRUE || windowInfo.subsystem != SDL_SYSWM_WINDOWS
            || GetClientRect(windowInfo.info.win.window, &clientRect) == FALSE)
        {
            return {};
        }
        width = clientRect.right - clientRect.left;
        height = clientRect.bottom - clientRect.top;
#else
        SDL_Vulkan_GetDrawableSize(window, &width, &height);
#endif
        return {
            static_cast<uint32_t>(std::max(width, 0)),
            static_cast<uint32_t>(std::max(height, 0)),
        };
    }
} // namespace OpenRCT2::Ui::Vulkan::Platform

#endif // ENABLE_VULKAN
