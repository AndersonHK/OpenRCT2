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
    #include "VulkanDevice.h"

namespace OpenRCT2::Ui::Vulkan
{
    // One externally serialized command-pool domain retaining the shared device.
    // GPU submissions retain this owner through their real fence completion;
    // timeout alone never resets a pool, upload allocation or token generation.
    class SubmissionSlots final
    {
        friend class Device;
        struct FrameResources
        {
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            VkFence available = VK_NULL_HANDLE;
            VkQueryPool timestampQueryPool = VK_NULL_HANDLE;
            bool timestampPending = false;
            std::optional<GpuTimestampDurations> completedGpuTimings;
            UploadRing upload;
            enum class State
            {
                idle,
                recording,
                submitted,
                completed,
                failed
            };
            State state = State::idle;
            uint64_t generation = 0;
        };

        std::shared_ptr<DeviceContext> _context;
        VkDevice _device = VK_NULL_HANDLE;
        VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
        DeviceContext::QueueFamilies _queueFamilies;
        uint32_t _slotCount = kFramesInFlight;
        VkCommandPool _commandPool = VK_NULL_HANDLE;
        std::array<FrameResources, kFramesInFlight> _frames;
        uint32_t _timestampValidBits = 0;
        double _timestampPeriodNanoseconds = 0;
        bool _gpuTimestampsSupported = false;
        void Initialise(VkDeviceSize capacity);
        void Dispose() noexcept;
        void HarvestGpuTimestamps(FrameResources& frame);
        FrameResources& Validate(const SubmissionToken& token);

    public:
        explicit SubmissionSlots(
            std::shared_ptr<DeviceContext> context, VkDeviceSize capacity = kDefaultUploadRingSize,
            uint32_t slotCount = kFramesInFlight);
        ~SubmissionSlots();
        SubmissionSlots(const SubmissionSlots&) = delete;
        SubmissionSlots& operator=(const SubmissionSlots&) = delete;
        const std::shared_ptr<DeviceContext>& GetContext() const noexcept
        {
            return _context;
        }
        std::optional<SubmissionToken> Begin(
            uint32_t index, bool waitForAvailability, Drawing::RenderUploadTelemetry* telemetry = nullptr);
        void Submit(const SubmissionToken& token);
        bool Wait(const SubmissionToken& token, uint64_t timeoutNanoseconds);
        void Abandon(const SubmissionToken& token);
    };
} // namespace OpenRCT2::Ui::Vulkan
#endif
