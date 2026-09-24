/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#ifdef ENABLE_VULKAN
#include "VulkanWorldFilterCompositor.h"
#include "VulkanShader.h"
#include <algorithm>
#include <stdexcept>
namespace OpenRCT2::Ui::Vulkan
{
    namespace
    {
        void Barrier(VkCommandBuffer cmd,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkAccessFlags reads,VkAccessFlags writes)
        {
            const VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER,nullptr,reads,writes};
            vkCmdPipelineBarrier(cmd,src,dst,0,1,&barrier,0,nullptr,0,nullptr);
        }
        void ImageBarrier(VkCommandBuffer cmd,VkImage image,VkImageLayout oldLayout,VkImageLayout newLayout,
            VkPipelineStageFlags src,VkPipelineStageFlags dst,VkAccessFlags reads,VkAccessFlags writes)
        {
            const VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,nullptr,reads,writes,oldLayout,newLayout,
                VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,image,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
            vkCmdPipelineBarrier(cmd,src,dst,0,0,nullptr,0,nullptr,1,&barrier);
        }
    }
    void WorldFilterCompositor::Initialise(const DeviceContext& device,const IndexedResources& resources,
        const Buffer& status,const std::filesystem::path& shaders)
    {
        Dispose(); _device=device.GetDevice();
        const auto physical=device.GetPhysicalDevice();
        const auto extent=resources.GetIndexedCanvas(0).GetExtent(); _extent={extent.width,extent.height};
        VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(physical,&properties);
        VkPhysicalDeviceFeatures features{}; vkGetPhysicalDeviceFeatures(physical,&features);
        const uint64_t pixels=uint64_t(extent.width)*extent.height;
        const uint64_t groups=(pixels+255)/256;
        if(!features.fragmentStoresAndAtomics || pixels==0 || pixels*4>properties.limits.maxStorageBufferRange
            || groups>properties.limits.maxComputeWorkGroupCount[0]
            || properties.limits.maxDescriptorSetStorageBuffers<25
            || properties.limits.maxPerStageDescriptorStorageBuffers<25)
            throw std::runtime_error("World filter compositor exceeds device atomics, descriptor, or canvas limits");
        _pixels=static_cast<uint32_t>(pixels); _groups=static_cast<uint32_t>(groups);
        _capacity=static_cast<uint32_t>(std::min({pixels*2,uint64_t(0x00fffffe),uint64_t(properties.limits.maxStorageBufferRange/8)}));
        if(_capacity==0) throw std::runtime_error("World filter compositor has no node capacity");
        constexpr auto storage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        _heads.Initialise(physical,_device,pixels*4,storage|VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        _nodes.Initialise(physical,_device,uint64_t(_capacity)*8,storage);
        _control.Initialise(physical,_device,20,storage|VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        _output.Initialise(physical,_device,((pixels+3)/4)*4,storage|VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        std::array<VkDescriptorSetLayoutBinding,7> bindings{};
        for(uint32_t i=0;i<7;i++) bindings[i]={i,i==4||i==5?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            1,VK_SHADER_STAGE_COMPUTE_BIT|(i<4?VK_SHADER_STAGE_FRAGMENT_BIT:0u),nullptr};
        const VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,nullptr,0,
            uint32_t(bindings.size()),bindings.data()};
        CheckVk(vkCreateDescriptorSetLayout(_device,&layout,nullptr,&_layout),"world filter descriptor layout");
        const auto frames=resources.GetFrameCount();
        const std::array poolSizes{VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,frames*5},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,frames*2}};
        const VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,nullptr,0,frames,
            uint32_t(poolSizes.size()),poolSizes.data()};
        CheckVk(vkCreateDescriptorPool(_device,&pool,nullptr,&_pool),"world filter descriptor pool");
        for(uint32_t frame=0;frame<frames;frame++)
        {
            const VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,nullptr,_pool,1,&_layout};
            CheckVk(vkAllocateDescriptorSets(_device,&allocation,&_sets[frame]),"world filter descriptor set");
            const std::array buffers{VkDescriptorBufferInfo{_heads.GetBuffer(),0,_heads.GetSize()},
                VkDescriptorBufferInfo{_nodes.GetBuffer(),0,_nodes.GetSize()},VkDescriptorBufferInfo{_control.GetBuffer(),0,20},
                VkDescriptorBufferInfo{status.GetBuffer(),0,status.GetSize()},VkDescriptorBufferInfo{_output.GetBuffer(),0,_output.GetSize()}};
            const std::array images{VkDescriptorImageInfo{resources.GetNearestSampler(),resources.GetIndexedCanvas(frame).GetView(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},VkDescriptorImageInfo{resources.GetNearestSampler(),resources.GetRemapPalette().GetView(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
            std::array<VkWriteDescriptorSet,7> writes{};
            for(uint32_t i=0;i<7;i++) {
                writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,nullptr,_sets[frame],i,0,1,bindings[i].descriptorType};
                if(i==4||i==5) writes[i].pImageInfo=&images[i-4]; else writes[i].pBufferInfo=&buffers[i==6?4:i];
            }
            vkUpdateDescriptorSets(_device,uint32_t(writes.size()),writes.data(),0,nullptr);
            _images[frame]=resources.GetIndexedCanvas(frame).GetImage();
        }
        const VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,4};
        const VkPipelineLayoutCreateInfo pipelineLayout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,nullptr,0,1,&_layout,1,&push};
        CheckVk(vkCreatePipelineLayout(_device,&pipelineLayout,nullptr,&_pipelineLayout),"world filter resolve layout");
        const auto shader=LoadShaderModule(_device,shaders/"world_filter_resolve.comp.spv");
        const VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,nullptr,0,
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,shader,"main",nullptr},_pipelineLayout};
        const auto result=vkCreateComputePipelines(_device,device.GetPipelineCache(),1,&pipeline,nullptr,&_pipeline);
        vkDestroyShaderModule(_device,shader,nullptr); CheckVk(result,"world filter resolve pipeline");
    }
    void WorldFilterCompositor::Dispose()
    {
        if(_device!=VK_NULL_HANDLE) {
            vkDestroyPipeline(_device,_pipeline,nullptr); vkDestroyPipelineLayout(_device,_pipelineLayout,nullptr);
            vkDestroyDescriptorPool(_device,_pool,nullptr); vkDestroyDescriptorSetLayout(_device,_layout,nullptr);
        }
        _heads.Dispose();_nodes.Dispose();_control.Dispose();_output.Dispose();
        _device=VK_NULL_HANDLE;_pipeline=VK_NULL_HANDLE;_pipelineLayout=VK_NULL_HANDLE;_pool=VK_NULL_HANDLE;_layout=VK_NULL_HANDLE;
        _sets={};_images={};
    }
    void WorldFilterCompositor::Begin(VkCommandBuffer cmd) const
    {
        // Scratch is shared only on the existing serial graphics queue. Retire all prior readers before reuse.
        Barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT);
        vkCmdFillBuffer(cmd,_heads.GetBuffer(),0,VK_WHOLE_SIZE,0x00ffffffu);
        const std::array<uint32_t,5> control{0,_capacity,_pixels,_extent.width,256};
        vkCmdUpdateBuffer(cmd,_control.GetBuffer(),0,sizeof(control),control.data());
        Barrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
    }
    void WorldFilterCompositor::Resolve(VkCommandBuffer cmd,uint32_t frame) const
    {
        Barrier(cmd,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,_pipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,_pipelineLayout,0,1,&_sets.at(frame),0,nullptr);
        for(uint32_t phase=0;phase<2;phase++) {
            vkCmdPushConstants(cmd,_pipelineLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&phase);
            vkCmdDispatch(cmd,_groups,1,1);
            Barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT);
        }
        ImageBarrier(cmd,_images.at(frame),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
        const VkBufferImageCopy copy{0,0,0,{VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},{0,0,0},{_extent.width,_extent.height,1}};
        vkCmdCopyBufferToImage(cmd,_output.GetBuffer(),_images.at(frame),VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        ImageBarrier(cmd,_images.at(frame),VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT);
    }
}
#endif
