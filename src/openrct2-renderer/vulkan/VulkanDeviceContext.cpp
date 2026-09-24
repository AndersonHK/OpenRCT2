/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_VULKAN
    #include "VulkanDeviceContext.h"

    #include "VulkanPipelineCacheData.h"
    #include "VulkanShader.h"

    #include <algorithm>
    #include <array>
    #include <cstring>
    #include <fstream>
    #include <openrct2-renderer/gpu/GpuAtlas.h>
    #include <openrct2-renderer/gpu/GpuCommandStream.h>
    #include <openrct2/core/Console.hpp>
    #include <stdexcept>
    #include <utility>

namespace OpenRCT2::Ui::Vulkan
{
    namespace
    {
        constexpr std::array<const char*, 1> kRequiredDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        constexpr const char* kPortabilitySubsetExtension = "VK_KHR_portability_subset";

        bool HasExtension(std::span<const VkExtensionProperties> extensions, const char* name)
        {
            return std::any_of(extensions.begin(), extensions.end(), [name](const auto& extension) {
                return std::strcmp(extension.extensionName, name) == 0;
            });
        }

        void AppendExtensionIfMissing(std::vector<const char*>& extensions, const char* name)
        {
            const bool present = std::any_of(extensions.begin(), extensions.end(), [name](const char* extension) {
                return std::strcmp(extension, name) == 0;
            });
            if (!present)
            {
                extensions.push_back(name);
            }
        }

    } // namespace

    std::shared_ptr<DeviceContext> DeviceContext::CreateWindowed(PresentationHost& host, VkSurfaceKHR& surface)
    {
        if (surface != VK_NULL_HANDLE)
            throw std::invalid_argument("Windowed bootstrap requires an empty surface output");
        auto context = std::shared_ptr<DeviceContext>(new DeviceContext());
        context->_presentation = true;
        context->_library = host.AcquireVulkanLibrary();
        if (!context->_library)
            throw std::logic_error("Presentation host returned no Vulkan loader lease");
        const auto extensions = host.GetInstanceExtensions();
        context->CreateInstance(extensions);
        try
        {
            context->_bootstrapSurface = host.CreateSurface(context->_instance);
            if (context->_bootstrapSurface == VK_NULL_HANDLE)
                throw std::runtime_error("Presentation host returned a null Vulkan surface");
            context->SelectPhysicalDevice();
            context->CreateLogicalDevice();
        }
        catch (...)
        {
            context->DestroyLogicalDevice();
            host.DestroySurface(context->_instance, context->_bootstrapSurface);
            context->_bootstrapSurface = VK_NULL_HANDLE;
            throw;
        }
        surface = std::exchange(context->_bootstrapSurface, VK_NULL_HANDLE);
        return context;
    }

    std::shared_ptr<DeviceContext> DeviceContext::CreateGraphicsOnly()
    {
        auto context = std::shared_ptr<DeviceContext>(new DeviceContext());
        // Direct Vulkan linkage keeps loader code mapped for process lifetime.
        // No SDL provider, surface extension or native window is involved.
        context->_library = std::make_unique<VulkanLibraryLease>();
        context->CreateInstance({});
        context->SelectPhysicalDevice();
        context->CreateLogicalDevice();
        return context;
    }

    DeviceContext::~DeviceContext()
    {
        DestroyLogicalDevice();
        if (_instance != VK_NULL_HANDLE)
            vkDestroyInstance(_instance, nullptr);
    }

    void DeviceContext::LoadPipelineCache(const std::filesystem::path& directory) noexcept
    {
        if (directory.empty() || !_pipelineCachePath.empty())
            return;
        try
        {
            const auto lock = LockPipelineCache();
            VkPhysicalDeviceProperties device{};
            vkGetPhysicalDeviceProperties(_physicalDevice, &device);
            std::string key = std::to_string(device.vendorID) + "-" + std::to_string(device.deviceID) + "-"
                + std::to_string(device.driverVersion) + "-";
            constexpr char hex[] = "0123456789abcdef";
            for (auto byte : device.pipelineCacheUUID)
            {
                key += hex[byte >> 4];
                key += hex[byte & 15];
            }
            _pipelineCachePath = directory / (key + ".bin");
            std::ifstream input(_pipelineCachePath, std::ios::binary | std::ios::ate);
            if (!input)
            {
                Console::WriteLine("Vulkan pipeline cache: cold start (no compatible cache)");
                return;
            }
            const auto size = static_cast<std::streamoff>(input.tellg());
            if (size < 0 || static_cast<uint64_t>(size) > kMaximumPipelineCacheBytes + sizeof(PipelineCacheFileHeader))
                throw std::runtime_error("invalid cache size");
            std::vector<std::byte> file(static_cast<size_t>(size));
            input.seekg(0);
            if (!input.read(reinterpret_cast<char*>(file.data()), static_cast<std::streamsize>(file.size())))
                throw std::runtime_error("incomplete cache read");
            const auto payload = DecodePipelineCacheFile(file, device);
            if (payload.empty())
                throw std::runtime_error("incompatible or damaged cache");
            VkPipelineCache loaded{};
            const VkPipelineCacheCreateInfo info{ .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
                                                  .initialDataSize = payload.size(),
                                                  .pInitialData = payload.data() };
            const auto result = vkCreatePipelineCache(_device, &info, nullptr, &loaded);
            if (result != VK_SUCCESS)
                throw std::runtime_error("driver rejected cache");
            const auto merge = vkMergePipelineCaches(_device, _pipelineCache, 1, &loaded);
            vkDestroyPipelineCache(_device, loaded, nullptr);
            CheckVk(merge, "merge persisted pipeline cache");
            Console::WriteLine("Vulkan pipeline cache: loaded %zu bytes", payload.size());
        }
        catch (const std::exception& error)
        {
            Console::WriteLine("Vulkan pipeline cache ignored: %s", error.what());
        }
    }

    void DeviceContext::SavePipelineCache() noexcept
    {
        if (_pipelineCachePath.empty() || _device == VK_NULL_HANDLE)
            return;
        try
        {
            const auto lock = LockPipelineCache();
            size_t size{};
            CheckVk(vkGetPipelineCacheData(_device, _pipelineCache, &size, nullptr), "query pipeline cache size");
            if (size > kMaximumPipelineCacheBytes)
                throw std::runtime_error("pipeline cache exceeds file limit");
            std::vector<std::byte> payload(size);
            CheckVk(vkGetPipelineCacheData(_device, _pipelineCache, &size, payload.data()), "read pipeline cache");
            payload.resize(size);
            VkPhysicalDeviceProperties device{};
            vkGetPhysicalDeviceProperties(_physicalDevice, &device);
            const auto file = EncodePipelineCacheFile(payload, device);
            if (file.empty())
                throw std::runtime_error("invalid driver cache header");
            std::filesystem::create_directories(_pipelineCachePath.parent_path());
            // A truncated/concurrent write is safely rejected by the bounded header and digest on the next launch.
            std::ofstream output(_pipelineCachePath, std::ios::binary | std::ios::trunc);
            if (!output.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size())))
                throw std::runtime_error("cannot save pipeline cache");
            Console::WriteLine("Vulkan pipeline cache: saved %zu bytes", payload.size());
        }
        catch (const std::exception& error)
        {
            Console::WriteLine("Vulkan pipeline cache save skipped: %s", error.what());
        }
    }

    VkSurfaceKHR DeviceContext::CreateCompatibleSurface(PresentationHost& host)
    {
        if (!_presentation || !_queueFamilies.present)
            throw std::logic_error("A graphics-only Vulkan device cannot acquire a presentation surface");
        for (const auto& extension : host.GetInstanceExtensions())
        {
            if (std::find(_instanceExtensions.begin(), _instanceExtensions.end(), extension) == _instanceExtensions.end())
                throw std::runtime_error("The recreated window requires an unavailable Vulkan instance extension");
        }
        const auto surface = host.CreateSurface(_instance);
        try
        {
            if (surface == VK_NULL_HANDLE)
                throw std::runtime_error("Presentation host returned a null Vulkan surface");
            VkBool32 supported = VK_FALSE;
            CheckVk(
                vkGetPhysicalDeviceSurfaceSupportKHR(_physicalDevice, *_queueFamilies.present, surface, &supported),
                "query recreated window presentation support");
            if (supported != VK_TRUE || !QuerySwapchainSupport(_physicalDevice, surface).IsUsable())
                throw std::runtime_error("The existing Vulkan device cannot present to the recreated window");
        }
        catch (...)
        {
            host.DestroySurface(_instance, surface);
            throw;
        }
        return surface;
    }

    bool DeviceContextOwner::IsCreated() const
    {
        const std::lock_guard lock(_mutex);
        return _context != nullptr;
    }

    std::shared_ptr<DeviceContext> DeviceContextOwner::AcquireWindowed(PresentationHost& host, VkSurfaceKHR& surface)
    {
        const std::lock_guard lock(_mutex);
        if (!_presentationRequired || surface != VK_NULL_HANDLE)
            throw std::logic_error("Windowed rendering requires a presentation owner and an empty surface output");
        if (_context)
            surface = _context->CreateCompatibleSurface(host);
        else
            _context = DeviceContext::CreateWindowed(host, surface);
        return _context;
    }

    std::shared_ptr<DeviceContext> DeviceContextOwner::AcquireOffscreen()
    {
        const std::lock_guard lock(_mutex);
        if (!_context)
        {
            if (_presentationRequired)
                throw std::runtime_error("The main Vulkan presentation device is not available for auxiliary rendering");
            _context = DeviceContext::CreateGraphicsOnly();
        }
        return _context;
    }

    void DeviceContext::DestroyLogicalDevice() noexcept
    {
        if (_device == VK_NULL_HANDLE)
            return;
        WaitIdle();
        vkDestroyPipelineCache(_device, _pipelineCache, nullptr);
        vkDestroyDevice(_device, nullptr);
        _device = VK_NULL_HANDLE;
        _pipelineCache = VK_NULL_HANDLE;
    }

    VkResult DeviceContext::Submit(const VkSubmitInfo& submit, VkFence fence)
    {
        const std::lock_guard lock(_queueMutex);
        return vkQueueSubmit(_graphicsQueue, 1, &submit, fence);
    }

    VkResult DeviceContext::Present(const VkPresentInfoKHR& present)
    {
        if (!_presentation)
            throw std::logic_error("Graphics-only Vulkan context cannot present");
        const std::lock_guard lock(_queueMutex);
        return vkQueuePresentKHR(_presentQueue, &present);
    }

    VkResult DeviceContext::WaitIdle() const noexcept
    {
        const std::lock_guard lock(_queueMutex);
        return _device == VK_NULL_HANDLE ? VK_SUCCESS : vkDeviceWaitIdle(_device);
    }

    void DeviceContext::CreateInstance(std::span<const std::string> requiredExtensions)
    {
        std::vector<const char*> extensions;
        extensions.reserve(requiredExtensions.size() + 2);
        for (const auto& extension : requiredExtensions)
            extensions.push_back(extension.c_str());
        VkInstanceCreateFlags flags = 0;

        uint32_t extensionCount = 0;
        CheckVk(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr), "enumerate instance extensions");
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        CheckVk(
            vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data()),
            "enumerate instance extensions");

    #ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        if (HasExtension(availableExtensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
        {
            AppendExtensionIfMissing(extensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
    #endif
    #ifdef VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME
        if (_presentation && HasExtension(availableExtensions, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME))
        {
            AppendExtensionIfMissing(extensions, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
        }
    #endif

        _instanceExtensions.clear();
        for (const auto* extension : extensions)
            _instanceExtensions.emplace_back(extension);
        const VkApplicationInfo applicationInfo = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "OpenRCT2",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "OpenRCT2 GPU renderer",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = VK_API_VERSION_1_1,
        };
        const VkInstanceCreateInfo instanceInfo = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .flags = flags,
            .pApplicationInfo = &applicationInfo,
            .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data(),
        };
        CheckVk(vkCreateInstance(&instanceInfo, nullptr, &_instance), "vkCreateInstance");
    }

    void DeviceContext::SelectPhysicalDevice()
    {
        uint32_t deviceCount = 0;
        CheckVk(vkEnumeratePhysicalDevices(_instance, &deviceCount, nullptr), "vkEnumeratePhysicalDevices(count)");
        if (deviceCount == 0)
        {
            throw std::runtime_error("No Vulkan physical devices are available");
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        CheckVk(vkEnumeratePhysicalDevices(_instance, &deviceCount, devices.data()), "vkEnumeratePhysicalDevices");

        int32_t bestScore = -1;
        for (const auto device : devices)
        {
            const int32_t score = ScorePhysicalDevice(device);
            if (score > bestScore)
            {
                bestScore = score;
                _physicalDevice = device;
            }
        }
        if (_physicalDevice == VK_NULL_HANDLE)
        {
            throw std::runtime_error("No Vulkan 1.1 device satisfies the renderer's queue, surface, format, atlas, and "
                                     "indirect-compute requirements");
        }
        _queueFamilies = FindQueueFamilies(_physicalDevice);
    }

    void DeviceContext::CreateLogicalDevice()
    {
        constexpr float priority = 1.0f;
        const std::array families = { _queueFamilies.graphics.value(),
                                      _queueFamilies.present.value_or(_queueFamilies.graphics.value()) };
        std::array<VkDeviceQueueCreateInfo, families.size()> queueInfos{};
        for (size_t i = 0; i < queueInfos.size(); i++)
        {
            queueInfos[i] = {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = families[i],
                .queueCount = 1,
                .pQueuePriorities = &priority,
            };
        }
        const uint32_t queueCount = families[0] == families[1] ? 1 : 2;

        const auto extensions = GetDeviceExtensions(_physicalDevice);
        VkPhysicalDeviceFeatures features{};
        features.multiDrawIndirect = VK_TRUE;
        features.fragmentStoresAndAtomics = VK_TRUE;
        const VkDeviceCreateInfo deviceInfo = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount = queueCount,
            .pQueueCreateInfos = queueInfos.data(),
            .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data(),
            .pEnabledFeatures = &features,
        };
        CheckVk(vkCreateDevice(_physicalDevice, &deviceInfo, nullptr, &_device), "vkCreateDevice");
    #ifdef VK_EXT_HDR_METADATA_EXTENSION_NAME
        const bool enabledHdrMetadata = std::any_of(extensions.begin(), extensions.end(), [](const char* extension) {
            return std::strcmp(extension, VK_EXT_HDR_METADATA_EXTENSION_NAME) == 0;
        });
        if (enabledHdrMetadata)
        {
            _setHdrMetadata = reinterpret_cast<PFN_vkSetHdrMetadataEXT>(vkGetDeviceProcAddr(_device, "vkSetHdrMetadataEXT"));
            _hdrMetadataAvailable = _setHdrMetadata != nullptr;
        }
    #endif
        vkGetDeviceQueue(_device, _queueFamilies.graphics.value(), 0, &_graphicsQueue);
        if (_presentation)
            vkGetDeviceQueue(_device, _queueFamilies.present.value(), 0, &_presentQueue);

        const VkPipelineCacheCreateInfo pipelineCacheInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
        CheckVk(vkCreatePipelineCache(_device, &pipelineCacheInfo, nullptr, &_pipelineCache), "vkCreatePipelineCache");
    }

    DeviceContext::QueueFamilies DeviceContext::FindQueueFamilies(VkPhysicalDevice device) const
    {
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
        std::vector<VkQueueFamilyProperties> properties(count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());

        QueueFamilies result;
        for (uint32_t i = 0; i < count; i++)
        {
            if ((properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
            {
                result.graphics = i;
            }
            if (_presentation)
            {
                VkBool32 supportsPresent = VK_FALSE;
                CheckVk(
                    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, _bootstrapSurface, &supportsPresent),
                    "query present support");
                if (supportsPresent == VK_TRUE)
                {
                    result.present = i;
                }
            }
            if (result.graphics.has_value() && (!_presentation || result.present.has_value()))
            {
                break;
            }
        }
        return result;
    }

    DeviceContext::SwapchainSupport DeviceContext::QuerySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface)
    {
        SwapchainSupport result;
        CheckVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &result.capabilities), "query surface capabilities");

        uint32_t formatCount = 0;
        CheckVk(vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr), "query surface formats");
        result.formats.resize(formatCount);
        if (formatCount > 0)
        {
            CheckVk(
                vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, result.formats.data()),
                "query surface formats");
        }

        uint32_t modeCount = 0;
        CheckVk(vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &modeCount, nullptr), "query present modes");
        result.presentModes.resize(modeCount);
        if (modeCount > 0)
        {
            CheckVk(
                vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &modeCount, result.presentModes.data()),
                "query present modes");
        }
        return result;
    }

    bool DeviceContext::SupportsDeviceExtensions(VkPhysicalDevice device) const
    {
        if (!_presentation)
            return true;
        uint32_t count = 0;
        CheckVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr), "enumerate device extensions");
        std::vector<VkExtensionProperties> available(count);
        CheckVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data()), "enumerate device extensions");
        return std::all_of(kRequiredDeviceExtensions.begin(), kRequiredDeviceExtensions.end(), [&](const char* required) {
            return HasExtension(available, required);
        });
    }

    int32_t DeviceContext::ScorePhysicalDevice(VkPhysicalDevice device) const
    {
        const auto families = FindQueueFamilies(device);
        const auto swapchain = _presentation ? QuerySwapchainSupport(device, _bootstrapSurface) : SwapchainSupport{};
        if (!families.graphics.has_value()
            || (_presentation
                && (!families.present.has_value() || !SupportsDeviceExtensions(device) || !swapchain.IsUsable()
                    || (swapchain.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0))
            || !SupportsRequiredRenderingFormats(device))
        {
            return -1;
        }

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceFeatures(device, &features);
        if (properties.apiVersion < VK_API_VERSION_1_1 || !features.fragmentStoresAndAtomics
            || properties.limits.maxImageDimension2D < static_cast<uint32_t>(Gpu::kAtlasDimension)
            || properties.limits.maxImageArrayLayers < Gpu::kAtlasLayers
            || !Gpu::AreWorldSurfaceComputeLimitsSufficient(
                properties.limits.maxComputeWorkGroupInvocations, properties.limits.maxComputeWorkGroupSize[0],
                properties.limits.maxComputeWorkGroupCount[0], properties.limits.maxComputeSharedMemorySize,
                features.multiDrawIndirect))
        {
            return -1;
        }
        int32_t score = static_cast<int32_t>(properties.limits.maxImageDimension2D);
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            score += 100000;
        }
        else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        {
            score += 50000;
        }
        return score;
    }

    bool DeviceContext::SupportsRequiredRenderingFormats(VkPhysicalDevice device)
    {
        const auto supports = [device](VkFormat format, VkFormatFeatureFlags required) {
            VkFormatProperties2 properties{ .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 };
            vkGetPhysicalDeviceFormatProperties2(device, format, &properties);
            return (properties.formatProperties.optimalTilingFeatures & required) == required;
        };

        constexpr VkFormatFeatureFlags transferAndSampling = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
            | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        return supports(VK_FORMAT_R8_UINT, transferAndSampling | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
            && supports(VK_FORMAT_R16_UINT, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
            && supports(
                   VK_FORMAT_D32_SFLOAT,
                   VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
                       | VK_FORMAT_FEATURE_TRANSFER_DST_BIT)
            && supports(
                   VK_FORMAT_R8G8B8A8_UNORM,
                   VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT
                       | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
    }

    std::vector<const char*> DeviceContext::GetDeviceExtensions(VkPhysicalDevice device) const
    {
        std::vector<const char*> result;
        if (_presentation)
            result.assign(kRequiredDeviceExtensions.begin(), kRequiredDeviceExtensions.end());

        uint32_t count = 0;
        CheckVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr), "enumerate portability extensions");
        std::vector<VkExtensionProperties> available(count);
        CheckVk(
            vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data()),
            "enumerate portability extensions");
        if (HasExtension(available, kPortabilitySubsetExtension))
        {
            result.push_back(kPortabilitySubsetExtension);
        }
    #ifdef VK_EXT_HDR_METADATA_EXTENSION_NAME
        if (_presentation && HasExtension(available, VK_EXT_HDR_METADATA_EXTENSION_NAME))
        {
            result.push_back(VK_EXT_HDR_METADATA_EXTENSION_NAME);
        }
    #endif

        return result;
    }

} // namespace OpenRCT2::Ui::Vulkan
#endif
