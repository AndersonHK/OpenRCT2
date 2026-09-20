/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#ifdef ENABLE_VULKAN

    #include <memory>
    #include <string>
    #include <vector>
    #include <vulkan/vulkan.h>

namespace OpenRCT2::Ui::Vulkan
{
    // Owns only loader access, never a native window or presentation surface.
    // Successful acquisition returns one lease; construction failure owns none.
    class VulkanLibraryLease
    {
    public:
        virtual ~VulkanLibraryLease() = default;
    };

    /**
     * Platform presentation boundary. Construction must not initialise the
     * loader, SDL or a graphics device. The backend owns this host until after
     * Device disposal, but native window lifetime remains the caller's duty.
     *
     * AcquireVulkanLibrary returns an independently owned loader lease on
     * success and none on failure. The lease outlives all instances and surfaces
     * and must not capture this host or its borrowed native window. Required extension names own their character data.
     * A successful CreateSurface transfers one surface to Device, which returns
     * it through DestroySurface before destroying its parent instance. That
     * callback must also accept VK_NULL_HANDLE after failed surface creation.
     *
     * Surface/loader operations run on the backend's existing execution thread.
     * Querying the native drawable extent remains a UI-thread operation; the
     * caller supplies sampled extents through BackendConfig/Resize as before.
     */
    class PresentationHost
    {
    public:
        virtual ~PresentationHost() = default;

        [[nodiscard]] virtual std::unique_ptr<VulkanLibraryLease> AcquireVulkanLibrary() = 0;
        [[nodiscard]] virtual std::vector<std::string> GetInstanceExtensions() = 0;
        [[nodiscard]] virtual VkSurfaceKHR CreateSurface(VkInstance instance) = 0;
        virtual void DestroySurface(VkInstance instance, VkSurfaceKHR surface) noexcept = 0;
        [[nodiscard]] virtual VkExtent2D GetDrawableExtent() const noexcept = 0;
    };
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
