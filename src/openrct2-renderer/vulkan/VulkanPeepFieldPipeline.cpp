// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifdef ENABLE_VULKAN
    #include "VulkanPeepFieldPipeline.h"

    #include "VulkanShader.h"

    #include <algorithm>
    #include <cstring>
    #include <stdexcept>

namespace OpenRCT2::Ui::Vulkan
{
    namespace
    {
        void Barrier(
            VkCommandBuffer command, VkPipelineStageFlags from, VkAccessFlags read, VkPipelineStageFlags to,
            VkAccessFlags write)
        {
            VkMemoryBarrier b{ .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = read, .dstAccessMask = write };
            vkCmdPipelineBarrier(command, from, to, 0, 1, &b, 0, nullptr, 0, nullptr);
        }
    } // namespace
    void PeepFieldPipeline::Initialise(const DeviceContext& device, const std::filesystem::path& shader)
    {
        Dispose();
        _device = device.GetDevice();
        try
        {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(device.GetPhysicalDevice(), &properties);
            constexpr VkDeviceSize stateBytes = VkDeviceSize{ kCapacity } * 100;
            constexpr VkDeviceSize updateBytes = VkDeviceSize{ kCapacity } * 128;
            constexpr VkDeviceSize binBytes = VkDeviceSize{ kBinWords } * 4;
            constexpr VkDeviceSize catalogBytes = VkDeviceSize{ kCatalogWords } * 4;
            const auto& limits = properties.limits;
            if (limits.maxStorageBufferRange < std::max({ stateBytes, updateBytes, binBytes, catalogBytes })
                || limits.maxComputeWorkGroupInvocations < 128 || limits.maxComputeWorkGroupSize[0] < 128
                || limits.maxComputeWorkGroupCount[0] < (kCapacity + 127) / 128
                || limits.maxPerStageDescriptorStorageBuffers < 3 || limits.maxDescriptorSetStorageBuffers < 3)
                throw std::runtime_error("Device cannot hold native peep field/bin buffers");
            constexpr auto usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            _fields.Initialise(device.GetPhysicalDevice(), _device, stateBytes, usage);
            _updates.Initialise(device.GetPhysicalDevice(), _device, updateBytes, usage);
            _bins.Initialise(device.GetPhysicalDevice(), _device, binBytes, usage);
            _catalog.Initialise(device.GetPhysicalDevice(), _device, catalogBytes, usage);
            std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
            for (uint32_t i = 0; i < 3; i++)
                bindings[i] = { i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT };
            VkDescriptorSetLayoutCreateInfo dl{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                .bindingCount = 3,
                                                .pBindings = bindings.data() };
            CheckVk(vkCreateDescriptorSetLayout(_device, &dl, nullptr, &_descriptorLayout), "peep descriptors");
            VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 };
            VkDescriptorPoolCreateInfo pool{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &size
            };
            CheckVk(vkCreateDescriptorPool(_device, &pool, nullptr, &_pool), "peep descriptor pool");
            VkDescriptorSetAllocateInfo allocate{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                                  .descriptorPool = _pool,
                                                  .descriptorSetCount = 1,
                                                  .pSetLayouts = &_descriptorLayout };
            CheckVk(vkAllocateDescriptorSets(_device, &allocate, &_set), "peep descriptor set");
            const std::array<const Buffer*, 3> buffers{ &_fields, &_updates, &_bins };
            std::array<VkDescriptorBufferInfo, 3> info{};
            std::array<VkWriteDescriptorSet, 3> writes{};
            for (uint32_t i = 0; i < 3; i++)
            {
                info[i] = { buffers[i]->GetBuffer(), 0, buffers[i]->GetSize() };
                writes[i] = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                              .dstSet = _set,
                              .dstBinding = i,
                              .descriptorCount = 1,
                              .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                              .pBufferInfo = &info[i] };
            }
            vkUpdateDescriptorSets(_device, 3, writes.data(), 0, nullptr);
            VkPushConstantRange push{ VK_SHADER_STAGE_COMPUTE_BIT, 0, 16 };
            VkPipelineLayoutCreateInfo layout{ .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                               .setLayoutCount = 1,
                                               .pSetLayouts = &_descriptorLayout,
                                               .pushConstantRangeCount = 1,
                                               .pPushConstantRanges = &push };
            CheckVk(vkCreatePipelineLayout(_device, &layout, nullptr, &_layout), "peep pipeline layout");
            auto cacheLock = device.LockPipelineCache();
            auto module = LoadShaderModule(_device, shader);
            VkComputePipelineCreateInfo pipeline{ .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                                  .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                             .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                                                             .module = module,
                                                             .pName = "main" },
                                                  .layout = _layout };
            auto result = vkCreateComputePipelines(_device, device.GetPipelineCache(), 1, &pipeline, nullptr, &_pipeline);
            vkDestroyShaderModule(_device, module, nullptr);
            CheckVk(result, "peep field pipeline");
        }
        catch (...)
        {
            Dispose();
            throw;
        }
    }
    void PeepFieldPipeline::Dispose()
    {
        if (_device)
        {
            vkDestroyPipeline(_device, _pipeline, nullptr);
            vkDestroyPipelineLayout(_device, _layout, nullptr);
            vkDestroyDescriptorPool(_device, _pool, nullptr);
            vkDestroyDescriptorSetLayout(_device, _descriptorLayout, nullptr);
        }
        _fields.Dispose();
        _updates.Dispose();
        _bins.Dispose();
        _catalog.Dispose();
        _device = {};
        _pipeline = {};
        _layout = {};
        _pool = {};
        _set = {};
        _descriptorLayout = {};
        _consumer = {};
        _residentAssets.reset();
        Discard();
    }
    void PeepFieldPipeline::Discard() noexcept
    {
        _pending.reset();
        _pendingAssets.reset();
        _recordedOwner = nullptr;
        _recordedToken = 0;
        _uploadBytes = 0;
    }
    void PeepFieldPipeline::Commit()
    {
        if (_pending)
            _consumer.Commit(*_pending);
        if (_pendingAssets)
            _residentAssets = _pendingAssets;
        _pending.reset();
        _pendingAssets.reset();
        _recordedOwner = nullptr;
        _recordedToken = 0;
    }
    void PeepFieldPipeline::Record(
        const SubmissionToken& frame, std::shared_ptr<const Drawing::RetainedPeepSnapshot> snapshot,
        std::shared_ptr<const Gpu::PeepAssetGeneration> assets)
    {
        if (!_pipeline || !frame.commandBuffer || !frame.upload || !snapshot || !assets || !assets->catalog
            || assets->revision == 0 || assets->descriptors.size() > kDescriptorCapacity
            || assets->facts.size() > kFactCapacity)
            throw std::invalid_argument("Invalid native peep frame/catalog");
        if (_pending)
        {
            if (_recordedOwner != frame.owner || _recordedToken != frame.generation || _recordedSlot != frame.frameIndex
                || _pending->snapshot != snapshot || _pendingAssets != assets)
                throw std::logic_error("Native peep packet changed within an uncommitted recording");
            return; // Multiple camera views consume one coherent world revision and tile bins.
        }
        auto delta = _consumer.Prepare(snapshot);
        const bool catalogChanged = _residentAssets != assets;
        _pending.emplace(std::move(delta));
        _pendingAssets = assets;
        _recordedOwner = frame.owner;
        _recordedToken = frame.generation;
        _recordedSlot = frame.frameIndex;
        _uploadBytes = 0;
        Barrier(
            frame.commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        const bool rebuildBins = _pending->reset || !_pending->lifecycle.empty() || !_pending->motion.empty();
        if (rebuildBins)
            vkCmdFillBuffer(frame.commandBuffer, _bins.GetBuffer(), 0, _bins.GetSize(), 0);
        if (_pending->reset)
            vkCmdFillBuffer(frame.commandBuffer, _fields.GetBuffer(), 0, _fields.GetSize(), 0);
        const auto stage = [&](const Buffer& target, std::span<const std::byte> bytes) {
            if (bytes.empty())
                return;
            if (bytes.size() > target.GetSize())
                throw std::overflow_error("Native peep upload capacity exceeded");
            auto upload = frame.upload->Allocate(bytes.size(), 4, Drawing::UploadCategory::world);
            if (!upload)
                throw std::runtime_error("Upload ring cannot hold native peep delta/catalog");
            std::memcpy(upload.data, bytes.data(), bytes.size());
            upload.RecordHostWrite();
            VkBufferCopy copy{ upload.offset, 0, bytes.size() };
            vkCmdCopyBuffer(frame.commandBuffer, upload.buffer, target.GetBuffer(), 1, &copy);
            upload.Record(Drawing::UploadMetric::bufferTransfer, bytes.size());
            _uploadBytes += bytes.size();
        };
        // Exactly one ring allocation/copy command for all changed groups, with no temporary payload copy.
        std::array<uint32_t, 4> offsets{}, counts{};
        const auto payloadBytes = _pending->PayloadBytes();
        if (payloadBytes > _updates.GetSize())
            throw std::overflow_error("Native peep delta capacity exceeded");
        if (payloadBytes != 0)
        {
            auto upload = frame.upload->Allocate(payloadBytes, 4, Drawing::UploadCategory::world);
            if (!upload)
                throw std::runtime_error("Upload ring cannot hold native peep delta");
            size_t cursor = 0;
            const auto append = [&](const auto& source, uint32_t group) {
                offsets[group] = static_cast<uint32_t>(cursor / 4);
                counts[group] = static_cast<uint32_t>(source.size());
                auto bytes = std::as_bytes(std::span(source));
                if (!bytes.empty())
                    std::memcpy(upload.data + cursor, bytes.data(), bytes.size());
                cursor += bytes.size();
            };
            append(_pending->lifecycle, 0);
            append(_pending->motion, 1);
            append(_pending->appearance, 2);
            append(_pending->animation, 3);
            upload.RecordHostWrite();
            const VkBufferCopy copy{ upload.offset, 0, payloadBytes };
            vkCmdCopyBuffer(frame.commandBuffer, upload.buffer, _updates.GetBuffer(), 1, &copy);
            upload.Record(Drawing::UploadMetric::bufferTransfer, payloadBytes);
            _uploadBytes += payloadBytes;
        }
        if (catalogChanged)
        {
            std::vector<uint32_t> catalog(4 + assets->descriptors.size() * 8 + assets->facts.size() * 4);
            catalog[0] = static_cast<uint32_t>(assets->descriptors.size());
            catalog[1] = static_cast<uint32_t>(assets->facts.size());
            if (!assets->descriptors.empty())
                std::memcpy(
                    catalog.data() + 4, assets->descriptors.data(),
                    assets->descriptors.size() * sizeof(Drawing::RetainedPeepAnimationDescriptor));
            if (!assets->facts.empty())
                std::memcpy(
                    catalog.data() + 4 + assets->descriptors.size() * 8, assets->facts.data(),
                    assets->facts.size() * sizeof(Drawing::RetainedPeepAnimationFact));
            stage(_catalog, std::as_bytes(std::span(catalog)));
        }
        Barrier(
            frame.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline);
        vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _layout, 0, 1, &_set, 0, nullptr);
        const auto dispatch = [&](uint32_t pass, uint32_t count, uint32_t source) {
            if (count == 0)
                return;
            const std::array<uint32_t, 4> push{ pass, count, source, 0 };
            vkCmdPushConstants(frame.commandBuffer, _layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), push.data());
            vkCmdDispatch(frame.commandBuffer, (count + 127) / 128, 1, 1);
            Barrier(
                frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        };
        for (uint32_t group = 0; group < 4; group++)
            dispatch(group, counts[group], offsets[group]);
        if (rebuildBins && snapshot->count != 0)
        {
            dispatch(4, kCapacity, 0);
            dispatch(5, 1, 0);
            dispatch(6, 1024, 0);
        }
    }
} // namespace OpenRCT2::Ui::Vulkan
#endif
