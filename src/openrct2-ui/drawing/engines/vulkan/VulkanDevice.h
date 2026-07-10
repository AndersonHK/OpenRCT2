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

    #include <vulkan/vulkan.h>
    #include <SDL_vulkan.h>

    #include <array>
    #include <cstddef>
    #include <cstdint>
    #include <optional>
    #include <span>
    #include <vector>

struct SDL_Window;

namespace OpenRCT2::Ui::Vulkan
{
    constexpr uint32_t kFramesInFlight = 3;
    constexpr VkDeviceSize kDefaultUploadRingSize = 32 * 1024 * 1024;

    struct UploadAllocation
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        VkDeviceSize size = 0;
        std::byte* data = nullptr;

        explicit operator bool() const noexcept
        {
            return data != nullptr;
        }
    };

    class UploadRing final
    {
    private:
        VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
        VkDevice _device = VK_NULL_HANDLE;
        VkBuffer _buffer = VK_NULL_HANDLE;
        VkDeviceMemory _memory = VK_NULL_HANDLE;
        std::byte* _mapped = nullptr;
        VkDeviceSize _capacity = 0;
        VkDeviceSize _cursor = 0;

    public:
        UploadRing() = default;
        ~UploadRing();

        UploadRing(const UploadRing&) = delete;
        UploadRing& operator=(const UploadRing&) = delete;

        void Initialise(VkPhysicalDevice physicalDevice, VkDevice device, VkDeviceSize capacity);
        void Dispose();
        void Reset() noexcept;

        [[nodiscard]] UploadAllocation Allocate(VkDeviceSize size, VkDeviceSize alignment);
        [[nodiscard]] VkBuffer GetBuffer() const noexcept
        {
            return _buffer;
        }
        [[nodiscard]] VkDeviceSize GetCapacity() const noexcept
        {
            return _capacity;
        }
        [[nodiscard]] VkDeviceSize GetUsed() const noexcept
        {
            return _cursor;
        }
    };

    struct FrameToken
    {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkExtent2D extent{};
        VkFormat format = VK_FORMAT_UNDEFINED;
        uint32_t imageIndex = 0;
        uint32_t frameIndex = 0;
        UploadRing* upload = nullptr;
    };

    class Device final
    {
    private:
        struct QueueFamilies
        {
            std::optional<uint32_t> graphics;
            std::optional<uint32_t> present;

            [[nodiscard]] bool IsComplete() const noexcept
            {
                return graphics.has_value() && present.has_value();
            }
        };

        struct SwapchainSupport
        {
            VkSurfaceCapabilitiesKHR capabilities{};
            std::vector<VkSurfaceFormatKHR> formats;
            std::vector<VkPresentModeKHR> presentModes;

            [[nodiscard]] bool IsUsable() const noexcept
            {
                return !formats.empty() && !presentModes.empty();
            }
        };

        struct FrameResources
        {
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            VkSemaphore imageAvailable = VK_NULL_HANDLE;
            VkSemaphore renderFinished = VK_NULL_HANDLE;
            VkFence available = VK_NULL_HANDLE;
            UploadRing upload;
        };

        SDL_Window* _window = nullptr;
        bool _loaderLoaded = false;
        bool _vsync = true;
        bool _swapchainInvalid = false;

        VkInstance _instance = VK_NULL_HANDLE;
        VkSurfaceKHR _surface = VK_NULL_HANDLE;
        VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
        VkDevice _device = VK_NULL_HANDLE;
        VkQueue _graphicsQueue = VK_NULL_HANDLE;
        VkQueue _presentQueue = VK_NULL_HANDLE;
        VkPipelineCache _pipelineCache = VK_NULL_HANDLE;
        QueueFamilies _queueFamilies;

        VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
        VkFormat _swapchainFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D _swapchainExtent{};
        std::vector<VkImage> _swapchainImages;
        std::vector<VkImageView> _swapchainImageViews;

        VkCommandPool _commandPool = VK_NULL_HANDLE;
        std::array<FrameResources, kFramesInFlight> _frames;
        uint32_t _currentFrame = 0;
        uint64_t _swapchainGeneration = 0;
        VkDeviceSize _uploadRingCapacity = kDefaultUploadRingSize;

    public:
        Device() = default;
        ~Device();

        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;

        void Initialise(
            SDL_Window* window, bool vsync, VkDeviceSize uploadRingCapacity = kDefaultUploadRingSize);
        void Dispose();
        void WaitIdle() const;

        void SetVSync(bool enabled);
        void RequestSwapchainRecreate() noexcept;
        [[nodiscard]] std::optional<FrameToken> BeginFrame();
        void EndFrame(const FrameToken& frame);

        [[nodiscard]] VkInstance GetInstance() const noexcept
        {
            return _instance;
        }
        [[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const noexcept
        {
            return _physicalDevice;
        }
        [[nodiscard]] VkDevice GetDevice() const noexcept
        {
            return _device;
        }
        [[nodiscard]] VkQueue GetGraphicsQueue() const noexcept
        {
            return _graphicsQueue;
        }
        [[nodiscard]] VkPipelineCache GetPipelineCache() const noexcept
        {
            return _pipelineCache;
        }
        [[nodiscard]] uint32_t GetGraphicsQueueFamily() const noexcept
        {
            return _queueFamilies.graphics.value();
        }
        [[nodiscard]] VkFormat GetSwapchainFormat() const noexcept
        {
            return _swapchainFormat;
        }
        [[nodiscard]] VkExtent2D GetSwapchainExtent() const noexcept
        {
            return _swapchainExtent;
        }
        [[nodiscard]] std::span<const VkImageView> GetSwapchainImageViews() const noexcept
        {
            return _swapchainImageViews;
        }
        [[nodiscard]] uint64_t GetSwapchainGeneration() const noexcept
        {
            return _swapchainGeneration;
        }

    private:
        void CreateInstance();
        void CreateSurface();
        void SelectPhysicalDevice();
        void CreateLogicalDevice();
        void CreateCommandResources();
        void CreateSwapchain();
        void DestroySwapchain();
        bool RecreateSwapchain();

        [[nodiscard]] QueueFamilies FindQueueFamilies(VkPhysicalDevice device) const;
        [[nodiscard]] SwapchainSupport QuerySwapchainSupport(VkPhysicalDevice device) const;
        [[nodiscard]] bool SupportsDeviceExtensions(VkPhysicalDevice device) const;
        [[nodiscard]] int32_t ScorePhysicalDevice(VkPhysicalDevice device) const;
        [[nodiscard]] std::vector<const char*> GetInstanceExtensions() const;
        [[nodiscard]] std::vector<const char*> GetDeviceExtensions(VkPhysicalDevice device) const;

        [[nodiscard]] VkSurfaceFormatKHR ChooseSurfaceFormat(std::span<const VkSurfaceFormatKHR> formats) const;
        [[nodiscard]] VkPresentModeKHR ChoosePresentMode(std::span<const VkPresentModeKHR> modes) const;
        [[nodiscard]] VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
    };
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
