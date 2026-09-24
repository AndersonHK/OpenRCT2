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
        #include <commctrl.h>
    #else
        #include <SDL_vulkan.h>
    #endif

    #include <algorithm>
    #include <chrono>
    #include <cstdio>
    #include <cwchar>
    #include <future>
    #include <openrct2-renderer/gpu/GpuBackend.h>
    #include <openrct2/core/Console.hpp>
    #include <stdexcept>
    #include <string>

namespace OpenRCT2::Ui::Vulkan::Platform
{
    void PreparePipelines(SDL_Window* window, const std::function<void()>& work)
    {
        using Clock = std::chrono::steady_clock;
        const auto started = Clock::now();
        Console::WriteLine("Vulkan startup: preparing graphics pipelines; cold driver compilation may take a while");
        std::fflush(stdout);
        struct Progress
        {
            SDL_Window* window;
            std::string title;
    #if defined(_WIN32)
            HWND panel{};
            HWND bar{};
            HMODULE controlsLibrary{};
    #endif
            ~Progress()
            {
    #if defined(_WIN32)
                if (panel != nullptr)
                    DestroyWindow(panel);
                if (controlsLibrary != nullptr)
                    FreeLibrary(controlsLibrary);
    #endif
                SDL_SetWindowTitle(window, title.c_str());
            }
        } progress{ window, SDL_GetWindowTitle(window) };
    #if defined(_WIN32)
        SDL_SysWMinfo info{};
        SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(window, &info) == SDL_TRUE)
        {
            progress.panel = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"STATIC", L"Preparing graphics...", WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 560, 120,
                info.info.win.window, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (progress.panel != nullptr)
            {
                SendMessageW(progress.panel, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
                progress.controlsLibrary = LoadLibraryExW(L"comctl32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
                if (progress.controlsLibrary != nullptr)
                {
                    const auto initialise = reinterpret_cast<decltype(&InitCommonControlsEx)>(
                        GetProcAddress(progress.controlsLibrary, "InitCommonControlsEx"));
                    const INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_PROGRESS_CLASS };
                    if (initialise != nullptr && initialise(&controls))
                    {
                        progress.bar = CreateWindowExW(
                            0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_MARQUEE, 24, 56, 512, 18, progress.panel,
                            nullptr, GetModuleHandleW(nullptr), nullptr);
                        if (progress.bar != nullptr)
                            SendMessageW(progress.bar, PBM_SETMARQUEE, TRUE, 30);
                    }
                }
            }
        }
    #endif
        // Only Vulkan-owned resources are touched by this task. SDL and its event queue stay on the UI thread.
        auto pending = std::async(std::launch::async, work);
        auto nextStatus = started;
        while (pending.wait_for(std::chrono::milliseconds(16)) != std::future_status::ready)
        {
            SDL_PumpEvents();
            const auto now = Clock::now();
            if (now < nextStatus)
                continue;
            nextStatus = now + std::chrono::milliseconds(250);
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - started).count();
            const auto caption = std::string("OpenRCT2 - Preparing graphics (") + std::to_string(elapsed) + " s)";
            SDL_SetWindowTitle(window, caption.c_str());
    #if defined(_WIN32)
            if (progress.panel != nullptr)
            {
                int width{}, height{};
                SDL_GetWindowSize(window, &width, &height);
                const auto panelWidth = std::min(560, std::max(1, width - 24));
                SetWindowPos(
                    progress.panel, HWND_TOP, (width - panelWidth) / 2, std::max(0, (height - 120) / 2), panelWidth, 120,
                    SWP_NOACTIVATE);
                if (progress.bar != nullptr)
                    SetWindowPos(
                        progress.bar, nullptr, 24, 56, std::max(1, panelWidth - 48), 18, SWP_NOZORDER | SWP_NOACTIVATE);
                const auto text = L"\nPreparing graphics\n" + std::to_wstring(elapsed)
                    + L" seconds\n\n\nFirst launch may take longer";
                SetWindowTextW(progress.panel, text.c_str());
                UpdateWindow(progress.panel);
            }
    #endif
        }
        pending.get();
        const auto seconds = std::chrono::duration<double>(Clock::now() - started).count();
        Console::WriteLine("Vulkan startup: graphics pipelines ready in %.3f seconds", seconds);
    }

    namespace
    {
        class SdlPresentationHost final : public PresentationHost
        {
        private:
            SDL_Window* _window;

        public:
            explicit SdlPresentationHost(SDL_Window* window)
                : _window(window)
            {
                if (window == nullptr)
                    throw std::invalid_argument("Vulkan presentation requires an SDL window");
            }

            std::unique_ptr<VulkanLibraryLease> AcquireVulkanLibrary() override
            {
                class SdlLibraryLease final : public VulkanLibraryLease
                {
                public:
                    SdlLibraryLease()
                    {
                        Platform::LoadVulkanLibrary();
                    }
                    ~SdlLibraryLease() override
                    {
                        Platform::UnloadVulkanLibrary();
                    }
                };
                return std::make_unique<SdlLibraryLease>();
            }

            std::vector<std::string> GetInstanceExtensions() override
            {
                const auto extensions = Platform::GetInstanceExtensions(_window);
                return { extensions.begin(), extensions.end() };
            }

            VkSurfaceKHR CreateSurface(VkInstance instance) override
            {
                return Platform::CreateSurface(_window, instance);
            }

            void DestroySurface(VkInstance instance, VkSurfaceKHR surface) noexcept override
            {
                vkDestroySurfaceKHR(instance, surface, nullptr);
            }

            VkExtent2D GetDrawableExtent() const noexcept override
            {
                return Platform::GetDrawableExtent(_window);
            }
        };
    } // namespace

    std::unique_ptr<PresentationHost> CreatePresentationHost(SDL_Window* window)
    {
        return std::make_unique<SdlPresentationHost>(window);
    }

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
            throw std::runtime_error("vkCreateWin32SurfaceKHR failed with Vulkan result " + std::to_string(result));
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

    std::optional<float> GetSdrWhiteNits(SDL_Window* window)
    {
    #if defined(_WIN32)
        if (window == nullptr)
            return std::nullopt;
        SDL_SysWMinfo windowInfo{};
        SDL_VERSION(&windowInfo.version);
        if (SDL_GetWindowWMInfo(window, &windowInfo) != SDL_TRUE || windowInfo.subsystem != SDL_SYSWM_WINDOWS)
            return std::nullopt;
        MONITORINFOEXW monitor{};
        monitor.cbSize = sizeof(monitor);
        const auto handle = MonitorFromWindow(windowInfo.info.win.window, MONITOR_DEFAULTTONEAREST);
        if (handle == nullptr || !GetMonitorInfoW(handle, reinterpret_cast<MONITORINFO*>(&monitor)))
            return std::nullopt;

        for (uint32_t attempt = 0; attempt < 4; ++attempt)
        {
            uint32_t pathCount = 0;
            uint32_t modeCount = 0;
            if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS || pathCount > 256
                || modeCount > 4096)
                return std::nullopt;
            std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
            std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
            const auto status = QueryDisplayConfig(
                QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
            if (status == ERROR_INSUFFICIENT_BUFFER)
                continue;
            if (status != ERROR_SUCCESS)
                return std::nullopt;
            std::optional<float> nits;
            for (uint32_t i = 0; i < pathCount; ++i)
            {
                const auto& path = paths[i];
                DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
                source.header = { DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source), path.sourceInfo.adapterId,
                                  path.sourceInfo.id };
                if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS)
                    return std::nullopt;
                if (std::wcscmp(source.viewGdiDeviceName, monitor.szDevice) != 0)
                    continue;
                DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
                white.header = { DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL, sizeof(white), path.targetInfo.adapterId,
                                 path.targetInfo.id };
                if (DisplayConfigGetDeviceInfo(&white.header) != ERROR_SUCCESS || white.SDRWhiteLevel == 0)
                    return std::nullopt;
                const float value = *Gpu::DecodeWindowsSdrWhiteNits(white.SDRWhiteLevel);
                // A cloned source can map to multiple physical targets. Do not
                // pick an arbitrary white point when their settings disagree.
                if (nits.has_value() && *nits != value)
                    return std::nullopt;
                nits = value;
            }
            return nits;
        }
    #else
        (void)window;
    #endif
        return std::nullopt;
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
