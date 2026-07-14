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

    #include <algorithm>
    #include <array>
    #include <cstddef>
    #include <cstdint>
    #include <mutex>
    #include <optional>
    #include <span>
    #include <vector>

struct SDL_Window;

namespace OpenRCT2::Ui::Vulkan
{
    constexpr uint32_t kFramesInFlight = 3;
    constexpr VkDeviceSize kDefaultUploadRingSize = 96 * 1024 * 1024;

    /**
     * Selects the presentation policy that best matches the renderer's
     * newest-frame handoff. MAILBOX remains synchronized to the display, but
     * replaces an older queued image when the producer gets ahead. FIFO is
     * the universally-supported synchronized fallback.
     */
    [[nodiscard]] inline VkPresentModeKHR SelectPresentMode(
        std::span<const VkPresentModeKHR> modes, bool vsync) noexcept
    {
        const auto supports = [modes](VkPresentModeKHR mode) {
            return std::find(modes.begin(), modes.end(), mode) != modes.end();
        };
        if (!vsync && supports(VK_PRESENT_MODE_IMMEDIATE_KHR))
        {
            return VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
        if (supports(VK_PRESENT_MODE_MAILBOX_KHR))
        {
            return VK_PRESENT_MODE_MAILBOX_KHR;
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    enum class GpuTimestampPoint : uint32_t
    {
        frameStart,
        uploadsComplete,
        indexedDrawComplete,
        lightFxComplete,
        frameComplete,
        count,
    };

    constexpr size_t kGpuTimestampCount = static_cast<size_t>(GpuTimestampPoint::count);

    struct GpuTimestampDurations
    {
        double totalMicroseconds = 0.0;
        double uploadMicroseconds = 0.0;
        double drawMicroseconds = 0.0;
        double lightFxMicroseconds = 0.0;
        double compositeMicroseconds = 0.0;
    };

    [[nodiscard]] constexpr uint64_t CalculateGpuTimestampDelta(
        uint64_t start, uint64_t end, uint32_t validBits) noexcept
    {
        if (validBits == 0)
        {
            return 0;
        }
        if (validBits >= 64)
        {
            return end - start;
        }
        return (end - start) & ((uint64_t{ 1 } << validBits) - 1);
    }

    [[nodiscard]] inline std::optional<GpuTimestampDurations> CalculateGpuTimestampDurations(
        const std::array<uint64_t, kGpuTimestampCount>& timestamps, uint32_t validBits,
        double timestampPeriodNanoseconds) noexcept
    {
        if (validBits == 0 || !(timestampPeriodNanoseconds > 0.0))
        {
            return std::nullopt;
        }
        const auto duration = [&](GpuTimestampPoint start, GpuTimestampPoint end) {
            const auto startIndex = static_cast<size_t>(start);
            const auto endIndex = static_cast<size_t>(end);
            return static_cast<double>(CalculateGpuTimestampDelta(
                       timestamps[startIndex], timestamps[endIndex], validBits))
                * timestampPeriodNanoseconds / 1000.0;
        };
        return GpuTimestampDurations{
            .totalMicroseconds = duration(GpuTimestampPoint::frameStart, GpuTimestampPoint::frameComplete),
            .uploadMicroseconds = duration(GpuTimestampPoint::frameStart, GpuTimestampPoint::uploadsComplete),
            .drawMicroseconds = duration(GpuTimestampPoint::uploadsComplete, GpuTimestampPoint::indexedDrawComplete),
            .lightFxMicroseconds = duration(GpuTimestampPoint::indexedDrawComplete, GpuTimestampPoint::lightFxComplete),
            .compositeMicroseconds = duration(GpuTimestampPoint::lightFxComplete, GpuTimestampPoint::frameComplete),
        };
    }

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
        VkDevice _device = VK_NULL_HANDLE;
        VkBuffer _buffer = VK_NULL_HANDLE;
        VkDeviceMemory _memory = VK_NULL_HANDLE;
        std::byte* _mapped = nullptr;
        VkDeviceSize _capacity = 0;
        VkDeviceSize _allocationSize = 0;
        VkDeviceSize _nonCoherentAtomSize = 1;
        VkDeviceSize _cursor = 0;
        bool _hostCoherent = false;

    public:
        UploadRing() = default;
        ~UploadRing();

        UploadRing(const UploadRing&) = delete;
        UploadRing& operator=(const UploadRing&) = delete;

        void Initialise(VkPhysicalDevice physicalDevice, VkDevice device, VkDeviceSize capacity);
        void Dispose();
        void Reset() noexcept;
        void FlushWritten();
        void Invalidate(VkDeviceSize offset, VkDeviceSize size);

        [[nodiscard]] UploadAllocation Allocate(VkDeviceSize size, VkDeviceSize alignment);
        [[nodiscard]] VkBuffer GetBuffer() const noexcept
        {
            return _buffer;
        }
        [[nodiscard]] VkDeviceSize GetUsed() const noexcept
        {
            return _cursor;
        }
    };

    struct FrameToken
    {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkExtent2D extent{};
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
            VkQueryPool timestampQueryPool = VK_NULL_HANDLE;
            bool timestampPending = false;
            std::optional<GpuTimestampDurations> completedGpuTimings;
            UploadRing upload;
        };

        SDL_Window* _window = nullptr;
        bool _loaderLoaded = false;
        bool _vsync = true;
        bool _preferHdr10 = false;
        float _hdrPaperWhiteNits = 203.0f;
        bool _hdr10Active = false;
        bool _hdrMetadataAvailable = false;
        bool _swapchainInvalid = false;
        VkExtent2D _drawableExtent{};
#ifdef VK_EXT_HDR_METADATA_EXTENSION_NAME
        PFN_vkSetHdrMetadataEXT _setHdrMetadata = nullptr;
#endif

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
        std::vector<VkImageView> _swapchainImageViews;

        VkCommandPool _commandPool = VK_NULL_HANDLE;
        std::array<FrameResources, kFramesInFlight> _frames;
        uint32_t _currentFrame = 0;
        uint64_t _swapchainGeneration = 0;
        uint32_t _timestampValidBits = 0;
        double _timestampPeriodNanoseconds = 0.0;
        bool _gpuTimestampsSupported = false;
        mutable std::mutex _hostMutex;

    public:
        Device() = default;
        ~Device();

        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;

        void Initialise(
            SDL_Window* window, VkExtent2D drawableExtent, bool vsync, VkDeviceSize uploadRingCapacity = kDefaultUploadRingSize,
            bool preferHdr10 = false, float hdrPaperWhiteNits = 203.0f);
        void Dispose();
        void WaitIdle() const;

        void SetVSync(bool enabled);
        void SetDrawableExtent(VkExtent2D drawableExtent) noexcept;
        void RequestSwapchainRecreate() noexcept;
        [[nodiscard]] bool IsSwapchainInvalid() const noexcept
        {
            return _swapchainInvalid;
        }
        bool RecreateSwapchain();
        [[nodiscard]] std::optional<FrameToken> BeginFrame(bool waitForAvailability);
        [[nodiscard]] double EndFrame(const FrameToken& frame);
        void AbandonFrame(const FrameToken& frame);
        void RecordGpuTimestamp(const FrameToken& frame, GpuTimestampPoint point) const;
        [[nodiscard]] std::optional<GpuTimestampDurations> TakeCompletedGpuTimings(uint32_t frameIndex);
        [[nodiscard]] bool SupportsGpuTimestamps() const noexcept
        {
            return _gpuTimestampsSupported;
        }

        [[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const noexcept
        {
            return _physicalDevice;
        }
        [[nodiscard]] VkDevice GetDevice() const noexcept
        {
            return _device;
        }
        [[nodiscard]] VkPipelineCache GetPipelineCache() const noexcept
        {
            return _pipelineCache;
        }
        [[nodiscard]] VkFormat GetSwapchainFormat() const noexcept
        {
            return _swapchainFormat;
        }
        [[nodiscard]] bool IsHdr10Active() const noexcept
        {
            return _hdr10Active;
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
        [[nodiscard]] uint32_t GetCurrentFrameIndex() const noexcept
        {
            return _currentFrame;
        }
        void ReadbackImage(
            uint32_t frameIndex, VkImage image, VkImageLayout layout, VkExtent2D extent,
            std::span<std::byte> destination);

    private:
        void CreateInstance();
        void CreateSurface();
        void SelectPhysicalDevice();
        void CreateLogicalDevice();
        void CreateCommandResources(VkDeviceSize uploadRingCapacity);
        [[nodiscard]] bool CreateSwapchain();
        void DestroySwapchain();
        void PublishHdrMetadata() const noexcept;
        void HarvestGpuTimestamps(FrameResources& frame);

        [[nodiscard]] QueueFamilies FindQueueFamilies(VkPhysicalDevice device) const;
        [[nodiscard]] SwapchainSupport QuerySwapchainSupport(VkPhysicalDevice device) const;
        [[nodiscard]] bool SupportsDeviceExtensions(VkPhysicalDevice device) const;
        [[nodiscard]] int32_t ScorePhysicalDevice(VkPhysicalDevice device) const;
        [[nodiscard]] static bool SupportsRequiredRenderingFormats(VkPhysicalDevice device);
        [[nodiscard]] std::vector<const char*> GetDeviceExtensions(VkPhysicalDevice device) const;

        [[nodiscard]] VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
    };
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
