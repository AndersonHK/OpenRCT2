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

    #include "VulkanDeviceContext.h"

    #include <algorithm>
    #include <array>
    #include <cstddef>
    #include <cstdint>
    #include <mutex>
    #include <functional>
    #include <openrct2-renderer/vulkan/VulkanPresentationHost.h>
    #include <openrct2/drawing/RenderUploadTelemetry.h>
    #include <optional>
    #include <span>
    #include <vector>
    #include <vulkan/vulkan.h>

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
    [[nodiscard]] inline VkPresentModeKHR SelectPresentMode(std::span<const VkPresentModeKHR> modes, bool vsync) noexcept
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

    [[nodiscard]] constexpr uint64_t CalculateGpuTimestampDelta(uint64_t start, uint64_t end, uint32_t validBits) noexcept
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
            return static_cast<double>(CalculateGpuTimestampDelta(timestamps[startIndex], timestamps[endIndex], validBits))
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
        Drawing::RenderUploadTelemetry* telemetry = nullptr;
        Drawing::UploadCategory category = Drawing::UploadCategory::commands;

        void Record(Drawing::UploadMetric metric, uint64_t bytes) const noexcept
        {
            if (telemetry != nullptr)
                telemetry->Add(category, metric, bytes);
        }
        void RecordHostWrite(Drawing::UploadMetric direct = Drawing::UploadMetric::count) const noexcept
        {
            Record(Drawing::UploadMetric::hostWritten, size);
            if (direct != Drawing::UploadMetric::count)
                Record(direct, size);
        }

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
        Drawing::RenderUploadTelemetry* _telemetry = nullptr;

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

        void SetTelemetry(Drawing::RenderUploadTelemetry* telemetry) noexcept
        {
            _telemetry = telemetry;
        }
        [[nodiscard]] UploadAllocation Allocate(
            VkDeviceSize size, VkDeviceSize alignment, Drawing::UploadCategory category = Drawing::UploadCategory::commands);
        [[nodiscard]] VkBuffer GetBuffer() const noexcept
        {
            return _buffer;
        }
        [[nodiscard]] VkDeviceSize GetUsed() const noexcept
        {
            return _cursor;
        }
    };

    class SubmissionSlots;

    // Non-owning command-slot view. It carries no surface, acquired image or
    // output dimensions, and does not itself extend resource/fence lifetime.
    struct SubmissionToken
    {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        uint32_t frameIndex = 0;
        UploadRing* upload = nullptr;
        Drawing::RenderUploadTelemetry* telemetry = nullptr;
        const SubmissionSlots* owner = nullptr;
        uint64_t generation = 0;
    };

    // Windowed adapter combines a command slot with its acquired presentation
    // image. Slot retirement and presentation-image retirement remain distinct.
    struct FrameToken
    {
        SubmissionToken submission{};
        VkExtent2D extent{};
        uint32_t imageIndex = 0;
    };

    class Device final
    {
    private:
        using QueueFamilies = DeviceContext::QueueFamilies;
        using SwapchainSupport = DeviceContext::SwapchainSupport;

        // Borrowed from Backend; its owned host outlives Device.
        PresentationHost* _presentationHost = nullptr;
        std::shared_ptr<DeviceContextOwner> _owner;
        std::shared_ptr<DeviceContext> _context;
        bool _vsync = true;
        bool _preferHdr10 = false;
        float _hdrPaperWhiteNits = 203.0f;
        bool _hdr10Active = false;
        bool _hdrMetadataAvailable = false;
        bool _swapchainInvalid = false;
        bool _enableDiagnosticCapture = false;
        bool _swapchainCaptureSupported = false;
        VkExtent2D _drawableExtent{};
    #ifdef VK_EXT_HDR_METADATA_EXTENSION_NAME
        PFN_vkSetHdrMetadataEXT _setHdrMetadata = nullptr;
    #endif

        // Borrowed aliases; _context is the sole device/instance owner.
        VkInstance _instance = VK_NULL_HANDLE;
        VkSurfaceKHR _surface = VK_NULL_HANDLE;
        VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
        VkDevice _device = VK_NULL_HANDLE;
        VkPipelineCache _pipelineCache = VK_NULL_HANDLE;
        QueueFamilies _queueFamilies;

        VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
        VkFormat _swapchainFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D _swapchainExtent{};
        std::vector<VkImageView> _swapchainImageViews;
        std::vector<VkImage> _swapchainImages;
        // Reacquiring an image proves its previous presentation consumed this
        // semaphore. A command-slot fence alone does not prove that.
        std::vector<VkSemaphore> _presentReady;

        std::unique_ptr<SubmissionSlots> _slots;
        std::array<VkSemaphore, kFramesInFlight> _imageAvailable{};
        uint32_t _currentFrame = 0;
        uint64_t _swapchainGeneration = 0;
        uint32_t _timestampValidBits = 0;
        double _timestampPeriodNanoseconds = 0.0;
        bool _gpuTimestampsSupported = false;
        mutable std::mutex _hostMutex;

    public:
        explicit Device(std::shared_ptr<DeviceContextOwner> owner = {});
        ~Device();

        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;

        void Initialise(
            PresentationHost& presentationHost, VkExtent2D drawableExtent, bool vsync,
            VkDeviceSize uploadRingCapacity = kDefaultUploadRingSize, bool preferHdr10 = false,
            float hdrPaperWhiteNits = 203.0f, bool enableDiagnosticCapture = false);
        void Dispose();
        void WaitIdle() const;
        [[nodiscard]] std::shared_ptr<DeviceContext> GetContext() const noexcept
        {
            return _context;
        }

        void SetVSync(bool enabled);
        void SetDrawableExtent(VkExtent2D drawableExtent) noexcept;
        void RequestSwapchainRecreate() noexcept;
        [[nodiscard]] bool IsSwapchainInvalid() const noexcept
        {
            return _swapchainInvalid;
        }
        bool RecreateSwapchain();
        // Callback runs after the existing slot fence, before acquire/reset, under the host mutex.
        // It must not reenter Device. Exceptions abort reuse before the ring is overwritten.
        [[nodiscard]] std::optional<FrameToken> BeginFrame(
            bool waitForAvailability, const std::function<void(uint32_t)>& onSlotComplete = {});
        [[nodiscard]] double EndFrame(const FrameToken& frame);
        void AbandonFrame(const FrameToken& frame);
        void RecordGpuTimestamp(const SubmissionToken& frame, GpuTimestampPoint point) const;
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
        [[nodiscard]] bool SupportsSwapchainCapture() const noexcept
        {
            return _swapchainCaptureSupported && !_hdr10Active;
        }
        [[nodiscard]] VkImage GetSwapchainImage(uint32_t imageIndex) const
        {
            return _swapchainImages.at(imageIndex);
        }
        [[nodiscard]] uint32_t GetCurrentFrameIndex() const noexcept
        {
            return _currentFrame;
        }
        void ReadbackImage(
            uint32_t frameIndex, VkImage image, VkImageLayout layout, VkExtent2D extent, std::span<std::byte> destination,
            uint32_t bytesPerPixel = 1, const std::function<void(uint32_t)>& onSlotComplete = {});

    private:
        void CreateCommandResources(VkDeviceSize uploadRingCapacity);
        [[nodiscard]] bool CreateSwapchain();
        void DestroySwapchain();
        void PublishHdrMetadata() const noexcept;

        [[nodiscard]] SwapchainSupport QuerySwapchainSupport(VkPhysicalDevice device) const;

        [[nodiscard]] VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
    };
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
