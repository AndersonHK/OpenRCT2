/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_VULKAN
    #include "VulkanSubmissionSlots.h"

    #include "VulkanShader.h"

    #include <algorithm>
    #include <limits>
    #include <openrct2/core/Console.hpp>
    #include <stdexcept>
    #include <utility>

namespace OpenRCT2::Ui::Vulkan
{
    SubmissionSlots::SubmissionSlots(std::shared_ptr<DeviceContext> context, VkDeviceSize capacity, uint32_t slotCount)
        : _context(std::move(context))
    {
        if (!_context || _context->GetDevice() == VK_NULL_HANDLE)
            throw std::invalid_argument("Submission slots require an initialized shared device");
        if (slotCount == 0 || slotCount > kFramesInFlight)
            throw std::invalid_argument("Invalid submission slot count");
        _slotCount = slotCount;
        _device = _context->GetDevice();
        _physicalDevice = _context->GetPhysicalDevice();
        _queueFamilies = _context->GetQueueFamilies();
        try
        {
            Initialise(std::max<VkDeviceSize>(capacity, 1024 * 1024));
        }
        catch (...)
        {
            Dispose();
            throw;
        }
    }

    SubmissionSlots::~SubmissionSlots()
    {
        Dispose();
    }

    void SubmissionSlots::Dispose() noexcept
    {
        if (_device == VK_NULL_HANDLE)
            return;
        // Also handles constructor rollback and a recording-but-unsubmitted
        // command buffer without waiting forever on an unsignaled fence.
        _context->WaitIdle();
        for (auto& frame : _frames)
        {
            frame.upload.Dispose();
            vkDestroyQueryPool(_device, frame.timestampQueryPool, nullptr);
            vkDestroyFence(_device, frame.available, nullptr);
            frame.timestampQueryPool = VK_NULL_HANDLE;
            frame.available = VK_NULL_HANDLE;
            frame.commandBuffer = VK_NULL_HANDLE;
        }
        vkDestroyCommandPool(_device, _commandPool, nullptr);
        _commandPool = VK_NULL_HANDLE;
        _device = VK_NULL_HANDLE;
    }

    SubmissionSlots::FrameResources& SubmissionSlots::Validate(const SubmissionToken& token)
    {
        if (token.owner != this || token.frameIndex >= _slotCount)
            throw std::logic_error("Vulkan submission token belongs to another slot domain");
        auto& frame = _frames[token.frameIndex];
        if (token.generation != frame.generation || token.commandBuffer != frame.commandBuffer || token.upload != &frame.upload)
            throw std::logic_error("Stale Vulkan submission slot generation");
        return frame;
    }

    std::optional<SubmissionToken> SubmissionSlots::Begin(
        uint32_t index, bool waitForAvailability, Drawing::RenderUploadTelemetry* telemetry)
    {
        if (index >= _slotCount)
            throw std::out_of_range("Vulkan submission slot index out of range");
        auto& frame = _frames[index];
        if (frame.state == FrameResources::State::recording || frame.state == FrameResources::State::failed)
            throw std::logic_error("Vulkan submission slot cannot begin in its current state");
        const auto ready = waitForAvailability ? vkWaitForFences(_device, 1, &frame.available, VK_TRUE, UINT64_MAX)
                                               : vkGetFenceStatus(_device, frame.available);
        if (ready == VK_NOT_READY)
            return std::nullopt;
        CheckVk(ready, "wait for submission slot");
        HarvestGpuTimestamps(frame);
        if (frame.generation == std::numeric_limits<uint64_t>::max())
            throw std::overflow_error("Vulkan submission generation exhausted");
        CheckVk(vkResetCommandBuffer(frame.commandBuffer, 0), "reset submission command buffer");
        frame.upload.Reset();
        frame.upload.SetTelemetry(telemetry);
        const VkCommandBufferBeginInfo begin = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        CheckVk(vkBeginCommandBuffer(frame.commandBuffer, &begin), "begin submission command buffer");
        if (frame.timestampQueryPool != VK_NULL_HANDLE)
        {
            vkCmdResetQueryPool(frame.commandBuffer, frame.timestampQueryPool, 0, static_cast<uint32_t>(kGpuTimestampCount));
            vkCmdWriteTimestamp(
                frame.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame.timestampQueryPool,
                static_cast<uint32_t>(GpuTimestampPoint::frameStart));
        }
        frame.state = FrameResources::State::recording;
        ++frame.generation;
        return SubmissionToken{ .commandBuffer = frame.commandBuffer,
                                .frameIndex = index,
                                .upload = &frame.upload,
                                .telemetry = telemetry,
                                .owner = this,
                                .generation = frame.generation };
    }

    void SubmissionSlots::Submit(const SubmissionToken& token)
    {
        auto& frame = Validate(token);
        if (frame.state != FrameResources::State::recording)
            throw std::logic_error("Vulkan submission slot is not recording");
        if (frame.timestampQueryPool != VK_NULL_HANDLE)
            vkCmdWriteTimestamp(
                frame.commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame.timestampQueryPool,
                static_cast<uint32_t>(GpuTimestampPoint::frameComplete));
        CheckVk(vkEndCommandBuffer(frame.commandBuffer), "end submission command buffer");
        frame.upload.FlushWritten();
        const VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                      .commandBufferCount = 1,
                                      .pCommandBuffers = &frame.commandBuffer };
        CheckVk(vkResetFences(_device, 1, &frame.available), "reset submission fence");
        frame.state = FrameResources::State::failed;
        CheckVk(_context->Submit(submit, frame.available), "submit shared-device commands");
        frame.state = FrameResources::State::submitted;
        // Only start/complete timestamps are populated by the generic transfer
        // API. Render executors will supply the interior points explicitly.
        frame.timestampPending = false;
        if (token.telemetry)
            token.telemetry->submitted = true;
    }

    bool SubmissionSlots::Wait(const SubmissionToken& token, uint64_t timeoutNanoseconds)
    {
        auto& frame = Validate(token);
        if (frame.state != FrameResources::State::submitted && frame.state != FrameResources::State::completed)
            throw std::logic_error("Vulkan submission has not been submitted");
        const auto result = vkWaitForFences(_device, 1, &frame.available, VK_TRUE, timeoutNanoseconds);
        if (result == VK_TIMEOUT)
            return false;
        CheckVk(result, "wait for named Vulkan submission");
        frame.state = FrameResources::State::completed;
        return true;
    }

    void SubmissionSlots::Abandon(const SubmissionToken& token)
    {
        auto& frame = Validate(token);
        if (frame.state != FrameResources::State::recording)
            throw std::logic_error("Only an unsubmitted Vulkan slot can be abandoned");
        CheckVk(vkResetCommandBuffer(frame.commandBuffer, 0), "abandon submission command buffer");
        frame.upload.SetTelemetry(nullptr);
        frame.state = FrameResources::State::idle;
    }

    void SubmissionSlots::Initialise(VkDeviceSize uploadRingCapacity)
    {
        const VkCommandPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = _queueFamilies.graphics.value(),
        };
        CheckVk(vkCreateCommandPool(_device, &poolInfo, nullptr, &_commandPool), "vkCreateCommandPool");

        std::array<VkCommandBuffer, kFramesInFlight> commandBuffers{};
        const VkCommandBufferAllocateInfo commandInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = _commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = _slotCount,
        };
        CheckVk(vkAllocateCommandBuffers(_device, &commandInfo, commandBuffers.data()), "vkAllocateCommandBuffers");

        const VkFenceCreateInfo fenceInfo = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        for (uint32_t i = 0; i < _slotCount; i++)
        {
            auto& frame = _frames[i];
            frame.commandBuffer = commandBuffers[i];
            CheckVk(vkCreateFence(_device, &fenceInfo, nullptr, &frame.available), "create frame fence");
            frame.upload.Initialise(_physicalDevice, _device, uploadRingCapacity);
        }

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(_physicalDevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(_physicalDevice, &queueFamilyCount, queueFamilies.data());
        VkPhysicalDeviceProperties deviceProperties{};
        vkGetPhysicalDeviceProperties(_physicalDevice, &deviceProperties);
        _timestampValidBits = queueFamilies[_queueFamilies.graphics.value()].timestampValidBits;
        _timestampPeriodNanoseconds = deviceProperties.limits.timestampPeriod;
        _gpuTimestampsSupported = _timestampValidBits != 0 && _timestampPeriodNanoseconds > 0.0;
        if (_gpuTimestampsSupported)
        {
            const VkQueryPoolCreateInfo queryPoolInfo = {
                .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                .queryType = VK_QUERY_TYPE_TIMESTAMP,
                .queryCount = static_cast<uint32_t>(kGpuTimestampCount),
            };
            for (uint32_t i = 0; i < _slotCount; ++i)
            {
                auto& frame = _frames[i];
                const auto result = vkCreateQueryPool(_device, &queryPoolInfo, nullptr, &frame.timestampQueryPool);
                if (result != VK_SUCCESS)
                {
                    _gpuTimestampsSupported = false;
                    break;
                }
            }
            if (!_gpuTimestampsSupported)
            {
                for (auto& frame : _frames)
                {
                    if (frame.timestampQueryPool != VK_NULL_HANDLE)
                    {
                        vkDestroyQueryPool(_device, frame.timestampQueryPool, nullptr);
                        frame.timestampQueryPool = VK_NULL_HANDLE;
                    }
                }
            }
        }
    }

    void SubmissionSlots::HarvestGpuTimestamps(FrameResources& frame)
    {
        if (!frame.timestampPending || frame.timestampQueryPool == VK_NULL_HANDLE)
        {
            return;
        }

        struct TimestampQueryResult
        {
            uint64_t value;
            uint64_t available;
        };
        std::array<TimestampQueryResult, kGpuTimestampCount> results{};
        const auto result = vkGetQueryPoolResults(
            _device, frame.timestampQueryPool, 0, static_cast<uint32_t>(results.size()), sizeof(results), results.data(),
            sizeof(TimestampQueryResult), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        frame.timestampPending = false;
        if (result == VK_NOT_READY)
        {
            return;
        }
        CheckVk(result, "vkGetQueryPoolResults(frame timestamps)");
        if (!std::all_of(results.begin(), results.end(), [](const auto& query) { return query.available != 0; }))
        {
            return;
        }
        std::array<uint64_t, kGpuTimestampCount> timestamps{};
        std::transform(results.begin(), results.end(), timestamps.begin(), [](const auto& query) { return query.value; });
        frame.completedGpuTimings = CalculateGpuTimestampDurations(
            timestamps, _timestampValidBits, _timestampPeriodNanoseconds);
    }
} // namespace OpenRCT2::Ui::Vulkan
#endif
