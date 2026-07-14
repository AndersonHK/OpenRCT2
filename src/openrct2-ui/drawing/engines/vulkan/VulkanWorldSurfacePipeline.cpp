/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_VULKAN

    #include "VulkanWorldSurfacePipeline.h"

    #include "VulkanShader.h"

    #include <array>
    #include <cstring>
    #include <ranges>
    #include <span>
    #include <stdexcept>
    #include <utility>

namespace OpenRCT2::Ui::Vulkan
{
    namespace
    {
        struct WorldSurfaceConstants
        {
            Gpu::Int2 screen;
            Gpu::Int2 view;
            Gpu::Int4 clip;
            uint32_t width;
            uint32_t height;
            uint32_t recordCount;
            int32_t zoom;
            uint32_t rotation;
            uint32_t spriteSetCount;
        };
        static_assert(sizeof(WorldSurfaceConstants) == 56);
        static_assert(offsetof(WorldSurfaceConstants, width) == 32);
        static_assert(offsetof(WorldSurfaceConstants, zoom) == 44);
        static_assert(offsetof(WorldSurfaceConstants, spriteSetCount) == 52);
        static_assert(sizeof(VkDrawIndirectCommand) == 16);

        constexpr VkVertexInputBindingDescription kBinding = {
            .binding = 0,
            .stride = sizeof(Gpu::WorldSurfaceRecord),
            .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
        };
        constexpr std::array kAttributes = {
            VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32_SINT, offsetof(Gpu::WorldSurfaceRecord, world) },
            VkVertexInputAttributeDescription{ 1, 0, VK_FORMAT_R32_SINT, offsetof(Gpu::WorldSurfaceRecord, valid) },
            VkVertexInputAttributeDescription{ 2, 0, VK_FORMAT_R32G32_SINT, offsetof(Gpu::WorldSurfaceRecord, spriteSize) },
            VkVertexInputAttributeDescription{ 3, 0, VK_FORMAT_R32G32_SINT, offsetof(Gpu::WorldSurfaceRecord, spriteOffset) },
            VkVertexInputAttributeDescription{ 4, 0, VK_FORMAT_R32_UINT, offsetof(Gpu::WorldSurfaceRecord, asset) },
            VkVertexInputAttributeDescription{ 5, 0, VK_FORMAT_R32_UINT, offsetof(Gpu::WorldSurfaceRecord, palettes) },
            VkVertexInputAttributeDescription{ 6, 0, VK_FORMAT_R32_UINT, offsetof(Gpu::WorldSurfaceRecord, effects) },
            VkVertexInputAttributeDescription{ 7, 0, VK_FORMAT_R32_SINT, offsetof(Gpu::WorldSurfaceRecord, depth) },
            VkVertexInputAttributeDescription{ 8, 0, VK_FORMAT_R32_SINT, offsetof(Gpu::WorldSurfaceRecord, zoom) },
            VkVertexInputAttributeDescription{ 9, 0, VK_FORMAT_R32_SINT, offsetof(Gpu::WorldSurfaceRecord, coordinateShift) },
        };
    } // namespace

    WorldSurfacePipeline::~WorldSurfacePipeline()
    {
        Dispose();
    }

    void WorldSurfacePipeline::Initialise(
        const Device& device, const IndexedResources& resources, std::filesystem::path shaderDirectory)
    {
        Dispose();
        _device = device.GetDevice();
        _pipelineCache = device.GetPipelineCache();
        _shaderDirectory = std::move(shaderDirectory);
        const auto extent = resources.GetIndexedCanvas(0).GetExtent();
        _extent = { extent.width, extent.height };
        constexpr VkDeviceSize sourceBytes = Gpu::kWorldSurfaceMaximumChunkCount * Gpu::kWorldSurfaceChunkWidth
            * sizeof(Gpu::WorldSurfaceSourceRecord);
        constexpr VkDeviceSize spriteBytes = Gpu::kWorldSurfaceMaximumSpriteSetCount * sizeof(Gpu::WorldSurfaceSpriteSet);
        constexpr VkDeviceSize visibleBytes = Gpu::kWorldSurfaceMaximumDrawCount * Gpu::kWorldSurfaceComputeBlockWidth
            * sizeof(Gpu::WorldSurfaceRecord);
        constexpr VkDeviceSize indirectBytes = Gpu::kWorldSurfaceMaximumDrawCount * sizeof(VkDrawIndirectCommand);
        _sourceRecords.Initialise(
            device.GetPhysicalDevice(), _device, sourceBytes,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        _spriteSets.Initialise(
            device.GetPhysicalDevice(), _device, spriteBytes,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        _visibleRecords.Initialise(
            device.GetPhysicalDevice(), _device, visibleBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        _indirectCommands.Initialise(
            device.GetPhysicalDevice(), _device, indirectBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        CreateDescriptors(resources);
        CreateRenderPass();
        CreatePipeline();
        CreateFramebuffers(resources);
    }

    void WorldSurfacePipeline::Dispose()
    {
        _indirectCommands.Dispose();
        _visibleRecords.Dispose();
        _spriteSets.Dispose();
        _sourceRecords.Dispose();
        if (_device != VK_NULL_HANDLE)
        {
            for (const auto framebuffer : _framebuffers)
                vkDestroyFramebuffer(_device, framebuffer, nullptr);
            vkDestroyPipeline(_device, _computePipeline, nullptr);
            vkDestroyPipeline(_device, _pipeline, nullptr);
            vkDestroyRenderPass(_device, _renderPass, nullptr);
            vkDestroyPipelineLayout(_device, _pipelineLayout, nullptr);
            vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
            vkDestroyDescriptorSetLayout(_device, _descriptorSetLayout, nullptr);
        }
        _device = VK_NULL_HANDLE;
        _pipelineCache = VK_NULL_HANDLE;
        _descriptorSetLayout = VK_NULL_HANDLE;
        _descriptorPool = VK_NULL_HANDLE;
        _descriptorSet = VK_NULL_HANDLE;
        _pipelineLayout = VK_NULL_HANDLE;
        _renderPass = VK_NULL_HANDLE;
        _computePipeline = VK_NULL_HANDLE;
        _pipeline = VK_NULL_HANDLE;
        _framebuffers = {};
        _uploadedRevisions.clear();
        _uploadedSpriteRevision = 0;
        _uploadedEpoch = 0;
        _uploadedWidth = 0;
        _uploadedHeight = 0;
        _extent = {};
        _shaderDirectory.clear();
    }

    void WorldSurfacePipeline::Record(const FrameToken& frame, const Gpu::WorldSurfaceSceneCommand& scene)
    {
        if (frame.frameIndex >= kFramesInFlight)
            throw std::out_of_range("Vulkan world-surface frame index is out of range");
        const uint64_t expectedRecordCount = static_cast<uint64_t>(scene.width) * scene.height;
        const size_t expectedChunkCount =
            (scene.recordCount + Gpu::kWorldSurfaceChunkWidth - 1) / Gpu::kWorldSurfaceChunkWidth;
        if (expectedRecordCount != scene.recordCount || scene.recordCount > Gpu::kWorldSurfaceMaximumRecordCount
            || scene.chunks.size() != expectedChunkCount || scene.sprites == nullptr
            || scene.sprites->records.size() > Gpu::kWorldSurfaceMaximumSpriteSetCount
            || std::ranges::any_of(scene.chunks, [](const auto& chunk) { return chunk == nullptr; }))
            throw std::invalid_argument("Vulkan world-surface scene dimensions are inconsistent");
        if (scene.recordCount == 0)
            return;
        if (_uploadedEpoch != scene.worldEpoch || _uploadedWidth != scene.width || _uploadedHeight != scene.height
            || _uploadedRevisions.size() != scene.chunks.size())
        {
            _uploadedEpoch = scene.worldEpoch;
            _uploadedWidth = scene.width;
            _uploadedHeight = scene.height;
            _uploadedRevisions.assign(scene.chunks.size(), 0);
            _uploadedSpriteRevision = 0;
        }

        constexpr VkDeviceSize chunkBytes = Gpu::kWorldSurfaceChunkWidth * sizeof(Gpu::WorldSurfaceSourceRecord);
        bool hasChangedChunks = false;
        for (size_t chunkIndex = 0; chunkIndex < scene.chunks.size(); chunkIndex++)
        {
            const auto& chunk = scene.chunks[chunkIndex];
            if (chunk != nullptr && _uploadedRevisions[chunkIndex] != chunk->revision)
            {
                hasChangedChunks = true;
                break;
            }
        }
        const bool spritesChanged = _uploadedSpriteRevision != scene.sprites->revision;
        if (hasChangedChunks || spritesChanged)
        {
            std::array<VkBufferMemoryBarrier, 2> barriers{};
            uint32_t barrierCount = 0;
            const auto append = [&barriers, &barrierCount](const VkBuffer buffer) {
                barriers[barrierCount++] = {
                    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                    .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                    .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .buffer = buffer,
                    .offset = 0,
                    .size = VK_WHOLE_SIZE,
                };
            };
            if (hasChangedChunks)
                append(_sourceRecords.GetBuffer());
            if (spritesChanged)
                append(_spriteSets.GetBuffer());
            vkCmdPipelineBarrier(
                frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                barrierCount, barriers.data(), 0, nullptr);
        }

        for (size_t chunkIndex = 0; chunkIndex < scene.chunks.size(); chunkIndex++)
        {
            const auto& chunk = scene.chunks[chunkIndex];
            if (chunk == nullptr || _uploadedRevisions[chunkIndex] == chunk->revision)
                continue;
            auto allocation = frame.upload->Allocate(chunkBytes, alignof(uint32_t));
            if (!allocation)
                throw std::runtime_error("Vulkan upload ring has no room for world-surface deltas");
            std::memcpy(allocation.data, chunk->records.data(), static_cast<size_t>(chunkBytes));
            const VkBufferCopy copy = {
                .srcOffset = allocation.offset,
                .dstOffset = static_cast<VkDeviceSize>(chunkIndex) * chunkBytes,
                .size = chunkBytes,
            };
            vkCmdCopyBuffer(frame.commandBuffer, allocation.buffer, _sourceRecords.GetBuffer(), 1, &copy);
            _uploadedRevisions[chunkIndex] = chunk->revision;
        }
        if (spritesChanged)
        {
            const VkDeviceSize spriteBytes = scene.sprites->records.size() * sizeof(Gpu::WorldSurfaceSpriteSet);
            if (spriteBytes != 0)
            {
                auto allocation = frame.upload->Allocate(spriteBytes, alignof(uint32_t));
                if (!allocation)
                    throw std::runtime_error("Vulkan upload ring has no room for world-surface sprite sets");
                std::memcpy(allocation.data, scene.sprites->records.data(), static_cast<size_t>(spriteBytes));
                const VkBufferCopy copy = { .srcOffset = allocation.offset, .dstOffset = 0, .size = spriteBytes };
                vkCmdCopyBuffer(frame.commandBuffer, allocation.buffer, _spriteSets.GetBuffer(), 1, &copy);
            }
            _uploadedSpriteRevision = scene.sprites->revision;
        }
        if (hasChangedChunks || spritesChanged)
        {
            std::array<VkBufferMemoryBarrier, 2> barriers{};
            uint32_t barrierCount = 0;
            const auto append = [&barriers, &barrierCount](const VkBuffer buffer) {
                barriers[barrierCount++] = {
                    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .buffer = buffer,
                    .offset = 0,
                    .size = VK_WHOLE_SIZE,
                };
            };
            if (hasChangedChunks)
                append(_sourceRecords.GetBuffer());
            if (spritesChanged)
                append(_spriteSets.GetBuffer());
            vkCmdPipelineBarrier(
                frame.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                barrierCount, barriers.data(), 0, nullptr);
        }

        const std::array outputToCompute = {
            VkBufferMemoryBarrier{
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = _visibleRecords.GetBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            },
            VkBufferMemoryBarrier{
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = _indirectCommands.GetBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            },
        };
        vkCmdPipelineBarrier(
            frame.commandBuffer, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, static_cast<uint32_t>(outputToCompute.size()),
            outputToCompute.data(), 0, nullptr);

        const WorldSurfaceConstants constants{
            .screen = { static_cast<int32_t>(_extent.width), static_cast<int32_t>(_extent.height) },
            .view = scene.view,
            .clip = scene.clip,
            .width = scene.width,
            .height = scene.height,
            .recordCount = scene.recordCount,
            .zoom = scene.zoom,
            .rotation = static_cast<uint32_t>(scene.rotation),
            .spriteSetCount = static_cast<uint32_t>(scene.sprites->records.size()),
        };
        const uint32_t drawCount = Gpu::GetWorldSurfaceDrawCount(scene.recordCount);
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _computePipeline);
        vkCmdBindDescriptorSets(
            frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 0, 1, &_descriptorSet, 0, nullptr);
        vkCmdPushConstants(
            frame.commandBuffer, _pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT, 0,
            sizeof(constants), &constants);
        vkCmdDispatch(frame.commandBuffer, drawCount, 1, 1);

        const std::array computeToDraw = {
            VkBufferMemoryBarrier{
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = _visibleRecords.GetBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            },
            VkBufferMemoryBarrier{
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = _indirectCommands.GetBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            },
        };
        vkCmdPipelineBarrier(
            frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 0, nullptr,
            static_cast<uint32_t>(computeToDraw.size()), computeToDraw.data(), 0, nullptr);

        const VkRenderPassBeginInfo pass = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = _renderPass,
            .framebuffer = _framebuffers[frame.frameIndex],
            .renderArea = { .offset = { 0, 0 }, .extent = _extent },
        };
        vkCmdBeginRenderPass(frame.commandBuffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
        const VkViewport viewport = {
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(_extent.width),
            .height = static_cast<float>(_extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        const VkRect2D scissor = { .offset = { 0, 0 }, .extent = _extent };
        vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);
        vkCmdBindDescriptorSets(
            frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 0, 1, &_descriptorSet, 0, nullptr);
        const VkDeviceSize offset = 0;
        const auto buffer = _visibleRecords.GetBuffer();
        vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &buffer, &offset);
        vkCmdDrawIndirect(
            frame.commandBuffer, _indirectCommands.GetBuffer(), 0, drawCount, sizeof(VkDrawIndirectCommand));
        vkCmdEndRenderPass(frame.commandBuffer);
    }

    void WorldSurfacePipeline::CreateDescriptors(const IndexedResources& resources)
    {
        constexpr std::array bindings = {
            VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT },
            VkDescriptorSetLayoutBinding{ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT },
            VkDescriptorSetLayoutBinding{ 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT },
            VkDescriptorSetLayoutBinding{ 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
        };
        const VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings = bindings.data(),
        };
        CheckVk(vkCreateDescriptorSetLayout(_device, &layoutInfo, nullptr, &_descriptorSetLayout), "world surfaces layout");
        constexpr std::array poolSizes = {
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5 },
        };
        const VkDescriptorPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1,
            .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
            .pPoolSizes = poolSizes.data(),
        };
        CheckVk(vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_descriptorPool), "world surfaces pool");
        const VkDescriptorSetAllocateInfo allocation = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = _descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &_descriptorSetLayout,
        };
        CheckVk(vkAllocateDescriptorSets(_device, &allocation, &_descriptorSet), "world surfaces descriptors");
        const VkDescriptorImageInfo atlas{ resources.GetNearestSampler(), resources.GetSpriteAtlas().GetView(),
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        const VkDescriptorImageInfo remap{ resources.GetNearestSampler(), resources.GetRemapPalette().GetView(),
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        const VkDescriptorBufferInfo sprites{ resources.GetSpriteDescriptors().GetBuffer(), 0,
                                              resources.GetSpriteDescriptors().GetSize() };
        const VkDescriptorBufferInfo sources{ _sourceRecords.GetBuffer(), 0, _sourceRecords.GetSize() };
        const VkDescriptorBufferInfo spriteSets{ _spriteSets.GetBuffer(), 0, _spriteSets.GetSize() };
        const VkDescriptorBufferInfo outputs{ _visibleRecords.GetBuffer(), 0, _visibleRecords.GetSize() };
        const VkDescriptorBufferInfo commands{ _indirectCommands.GetBuffer(), 0, _indirectCommands.GetSize() };
        const std::array writes = {
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 0, 0, 1,
                                  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &atlas },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 1, 0, 1,
                                  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &remap },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 2, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &sprites },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 3, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &sources },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 4, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &spriteSets },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 5, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &outputs },
            VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _descriptorSet, 6, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &commands },
        };
        vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    void WorldSurfacePipeline::CreateRenderPass()
    {
        const std::array attachments = {
            VkAttachmentDescription{ 0, VK_FORMAT_R8_UINT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_LOAD,
                                     VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                     VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            VkAttachmentDescription{ 0, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_LOAD,
                                     VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                     VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL },
        };
        constexpr VkAttachmentReference colour{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        constexpr VkAttachmentReference depth{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        const VkSubpassDescription subpass = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colour,
            .pDepthStencilAttachment = &depth,
        };
        constexpr std::array dependencies = {
            VkSubpassDependency{ VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                 VK_ACCESS_TRANSFER_WRITE_BIT,
                                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT },
            VkSubpassDependency{ 0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                 VK_ACCESS_SHADER_READ_BIT },
        };
        const VkRenderPassCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = static_cast<uint32_t>(attachments.size()),
            .pAttachments = attachments.data(),
            .subpassCount = 1,
            .pSubpasses = &subpass,
            .dependencyCount = static_cast<uint32_t>(dependencies.size()),
            .pDependencies = dependencies.data(),
        };
        CheckVk(vkCreateRenderPass(_device, &info, nullptr, &_renderPass), "world surfaces render pass");
    }

    void WorldSurfacePipeline::CreatePipeline()
    {
        const VkPushConstantRange push{
            VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(WorldSurfaceConstants)
        };
        const VkPipelineLayoutCreateInfo layout = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &_descriptorSetLayout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push,
        };
        CheckVk(vkCreatePipelineLayout(_device, &layout, nullptr, &_pipelineLayout), "world surfaces pipeline layout");
        const auto computeShader = LoadShaderModule(_device, _shaderDirectory / "world_surface_compact.comp.spv");
        const VkPipelineShaderStageCreateInfo computeStage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = computeShader,
            .pName = "main",
        };
        const VkComputePipelineCreateInfo computeInfo = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = computeStage,
            .layout = _pipelineLayout,
        };
        const auto computeResult =
            vkCreateComputePipelines(_device, _pipelineCache, 1, &computeInfo, nullptr, &_computePipeline);
        vkDestroyShaderModule(_device, computeShader, nullptr);
        CheckVk(computeResult, "world surfaces compute pipeline");
        constexpr VkPipelineDepthStencilStateCreateInfo depth = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f,
        };
        _pipeline = CreateGraphicsPipeline(
            _device, _pipelineCache,
            {
                .vertexShader = _shaderDirectory / "world_surface.vert.spv",
                .fragmentShader = _shaderDirectory / "indexed_rect.frag.spv",
                .vertexBindings = std::span{ &kBinding, 1 },
                .vertexAttributes = kAttributes,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                .depthStencil = &depth,
                .layout = _pipelineLayout,
                .renderPass = _renderPass,
            },
            "world surfaces graphics pipeline");
    }

    void WorldSurfacePipeline::CreateFramebuffers(const IndexedResources& resources)
    {
        for (uint32_t i = 0; i < kFramesInFlight; i++)
        {
            const std::array attachments{ resources.GetIndexedCanvas(i).GetView(), resources.GetDepthCanvas(i).GetView() };
            const VkFramebufferCreateInfo info = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = _renderPass,
                .attachmentCount = static_cast<uint32_t>(attachments.size()),
                .pAttachments = attachments.data(),
                .width = _extent.width,
                .height = _extent.height,
                .layers = 1,
            };
            CheckVk(vkCreateFramebuffer(_device, &info, nullptr, &_framebuffers[i]), "world surfaces framebuffer");
        }
    }
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
