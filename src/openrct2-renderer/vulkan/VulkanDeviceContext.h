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
    #include "VulkanPresentationHost.h"

    #include <filesystem>
    #include <memory>
    #include <mutex>
    #include <optional>
    #include <span>
    #include <vector>

namespace OpenRCT2::Ui::Vulkan
{
    // Process/service-owned device. Sessions retain this owner; output targets
    // and submission slots never create a second logical device implicitly.
    class DeviceContext final
    {
    public:
        struct QueueFamilies
        {
            std::optional<uint32_t> graphics;
            std::optional<uint32_t> present;
            bool IsComplete() const noexcept
            {
                return graphics.has_value() && present.has_value();
            }
        };
        struct SwapchainSupport
        {
            VkSurfaceCapabilitiesKHR capabilities{};
            std::vector<VkSurfaceFormatKHR> formats;
            std::vector<VkPresentModeKHR> presentModes;
            bool IsUsable() const noexcept
            {
                return !formats.empty() && !presentModes.empty();
            }
        };

        // The host is used only during bootstrap. The returned surface remains
        // caller-owned and must be destroyed before releasing the context.
        static std::shared_ptr<DeviceContext> CreateWindowed(PresentationHost& host, VkSurfaceKHR& surface);
        // Uses the linked Vulkan loader without touching SDL or requesting WSI.
        // A missing import library at process load is a separate packaging gate.
        static std::shared_ptr<DeviceContext> CreateGraphicsOnly();
        // Recreated windows attach a new surface to the same selected device.
        // Incompatible extension/queue/format requirements fail explicitly.
        VkSurfaceKHR CreateCompatibleSurface(PresentationHost& host);
        ~DeviceContext();
        DeviceContext(const DeviceContext&) = delete;
        DeviceContext& operator=(const DeviceContext&) = delete;

        VkInstance GetInstance() const noexcept
        {
            return _instance;
        }
        VkPhysicalDevice GetPhysicalDevice() const noexcept
        {
            return _physicalDevice;
        }
        VkDevice GetDevice() const noexcept
        {
            return _device;
        }
        VkPipelineCache GetPipelineCache() const noexcept
        {
            return _pipelineCache;
        }
        const QueueFamilies& GetQueueFamilies() const noexcept
        {
            return _queueFamilies;
        }
        bool SupportsPresentation() const noexcept
        {
            return _presentation;
        }
        bool HasHdrMetadata() const noexcept
        {
            return _hdrMetadataAvailable;
        }
    #ifdef VK_EXT_HDR_METADATA_EXTENSION_NAME
        PFN_vkSetHdrMetadataEXT GetSetHdrMetadata() const noexcept
        {
            return _setHdrMetadata;
        }
    #endif
        // All host queue operations share this synchronization domain. Fence
        // waits are outside this lock; command pools belong to separate slots.
        std::unique_lock<std::mutex> LockPipelineCache() const
        {
            return std::unique_lock(_pipelineCacheMutex);
        }
        VkResult Submit(const VkSubmitInfo& submit, VkFence fence);
        VkResult Present(const VkPresentInfoKHR& present);
        VkResult WaitIdle() const noexcept;
        // Optional performance cache. Invalid or unavailable files never prevent startup.
        void LoadPipelineCache(const std::filesystem::path& directory) noexcept;
        void SavePipelineCache() noexcept;
        static SwapchainSupport QuerySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);

    private:
        DeviceContext() = default;
        void CreateInstance(std::span<const std::string> requiredExtensions);
        void SelectPhysicalDevice();
        void CreateLogicalDevice();
        void DestroyLogicalDevice() noexcept;
        QueueFamilies FindQueueFamilies(VkPhysicalDevice device) const;
        bool SupportsDeviceExtensions(VkPhysicalDevice device) const;
        int32_t ScorePhysicalDevice(VkPhysicalDevice device) const;
        static bool SupportsRequiredRenderingFormats(VkPhysicalDevice device);
        std::vector<const char*> GetDeviceExtensions(VkPhysicalDevice device) const;

        std::unique_ptr<VulkanLibraryLease> _library;
        bool _presentation = false;
        std::vector<std::string> _instanceExtensions;
        VkSurfaceKHR _bootstrapSurface = VK_NULL_HANDLE;
        VkInstance _instance = VK_NULL_HANDLE;
        VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
        VkDevice _device = VK_NULL_HANDLE;
        VkQueue _graphicsQueue = VK_NULL_HANDLE;
        VkQueue _presentQueue = VK_NULL_HANDLE;
        VkPipelineCache _pipelineCache = VK_NULL_HANDLE;
        std::filesystem::path _pipelineCachePath;
        QueueFamilies _queueFamilies;
        bool _hdrMetadataAvailable = false;
    #ifdef VK_EXT_HDR_METADATA_EXTENSION_NAME
        PFN_vkSetHdrMetadataEXT _setHdrMetadata = nullptr;
    #endif
        mutable std::mutex _queueMutex;
        mutable std::mutex _pipelineCacheMutex;
    };
    // Application composition owns one of these for its lifetime. Construction
    // and IsCreated are CPU-only. UI mode cannot bootstrap a graphics-only
    // device before the presentation host has supplied its requirements.
    class DeviceContextOwner final
    {
        const bool _presentationRequired;
        mutable std::mutex _mutex;
        std::shared_ptr<DeviceContext> _context;

    public:
        explicit DeviceContextOwner(bool presentationRequired)
            : _presentationRequired(presentationRequired)
        {
        }
        bool IsCreated() const;
        std::shared_ptr<DeviceContext> AcquireWindowed(PresentationHost& host, VkSurfaceKHR& surface);
        std::shared_ptr<DeviceContext> AcquireOffscreen();
    };
} // namespace OpenRCT2::Ui::Vulkan
#endif
