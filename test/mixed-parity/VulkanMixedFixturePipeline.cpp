// Experimental mixed-scene qualification component; compiled only into diagnostic tests.
#ifdef ENABLE_VULKAN
#include "VulkanMixedFixturePipeline.h"
#include <openrct2-renderer/vulkan/VulkanShader.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace OpenRCT2::Ui::Vulkan
{
    namespace Mixed=Gpu::MixedFixture;
    namespace
    {
        constexpr std::array<uint32_t,6> kCapacities={256,1024,256,256,65536,4096};
        constexpr std::array<VkDeviceSize,9> kBytes={
            256*sizeof(Mixed::StaticInstance),1024*sizeof(Mixed::StaticDefinition),256*sizeof(Gpu::Peeps::PeepRaw),
            256*sizeof(Gpu::Peeps::PeepAnimationDescriptor),65536*sizeof(Gpu::Peeps::PeepAnimationFact),
            4096*sizeof(Gpu::Terrain::DrawSpriteMetadata),1024*sizeof(Gpu::SpriteCommand),
            Mixed::kStatusWords*sizeof(uint32_t),1024*sizeof(Gpu::SpriteCommand)};
        void Require(bool condition,const char* message) { if(!condition) throw std::invalid_argument(message); }
        void Barrier(VkCommandBuffer command,VkPipelineStageFlags from,VkAccessFlags source,VkPipelineStageFlags to,VkAccessFlags dest)
        {
            const VkMemoryBarrier barrier{.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=source,.dstAccessMask=dest};
            vkCmdPipelineBarrier(command,from,to,0,1,&barrier,0,nullptr,0,nullptr);
        }
        void ValidateDefinitions(std::span<const Mixed::StaticDefinition> rules)
        {
            Require(rules.size()%4==0,"Static definitions need all four rotations");
            for(size_t i=0;i<rules.size();++i)
            {
                const auto& rule=rules[i]; const auto& first=rules[i&~size_t{3}];
                Require(rule.rotation==i%4 && rule.partCount<=4 && rule.objectGeneration!=0
                    && rule.objectKey==first.objectKey && rule.objectGeneration==first.objectGeneration,"Bad static definition identity");
                for(uint32_t part=0;part<rule.partCount;++part)
                {
                    const auto& p=rule.parts[part];
                    Require(p.image!=UINT32_MAX && p.effects<=2 && p.reserved0==0 && p.reserved1==0,"Unsupported static component");
                    for(int32_t value:{p.offsetX,p.offsetY,p.offsetZ,p.attachedY,p.boundsX,p.boundsY,p.boundsZ,p.sizeX,p.sizeY,p.sizeZ})
                        Require(value>=-512 && value<=512,"Static component exceeds fixture coordinates");
                    Require(p.parentPart==UINT32_MAX || (p.parentPart<part && rule.parts[p.parentPart].parentPart==UINT32_MAX),
                        "Static child must reference an earlier independent parent");
                }
            }
        }
        void ValidateSprites(std::span<const Gpu::Terrain::DrawSpriteMetadata> sprites,uint32_t assets)
        {
            uint32_t last=0; bool first=true;
            for(const auto& sprite:sprites)
            {
                Require((first || sprite.imageIndex>last) && sprite.flags==0 && sprite.reserved0==0 && sprite.reserved1==0,
                    "Sprite table must have unique sorted supported images");
                first=false; last=sprite.imageIndex;
                const auto& v=sprite.variants[0];
                Require(sprite.width>0 && sprite.width<=2048 && sprite.height>0 && sprite.height<=2048
                    && v.width>0 && v.width<=2048 && v.height>0 && v.height<=2048 && v.asset<assets
                    && v.flags<=1 && v.effectiveZoom==0 && v.coordinateShift==0,"Bad zoom0 sprite metadata");
                for(int32_t offset:{sprite.xOffset,sprite.yOffset,v.xOffset,v.yOffset})
                    Require(offset>=-32768 && offset<=32767,"Sprite offset is outside G1 range");
            }
        }
    }
    MixedFixturePipeline::~MixedFixturePipeline() { Dispose(); }
    void MixedFixturePipeline::Initialise(const DeviceContext& device,const IndexedResources& resources,
        const std::filesystem::path& shaders,const std::filesystem::path& emitter)
    {
        Dispose(); _device=device.GetDevice(); _extent=resources.GetIndexedCanvas(0).GetExtent();
        _frameCount=resources.GetFrameCount(); _assetCapacity=resources.GetAtlasLayers()*Gpu::kAtlasSlotsPerLayer;
        try
        {
            VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(device.GetPhysicalDevice(),&properties);
            const auto& l=properties.limits;
            Require(l.maxPerStageDescriptorStorageBuffers>=9 && l.maxDescriptorSetStorageBuffers>=9
                && l.maxComputeWorkGroupInvocations>=64 && l.maxComputeWorkGroupSize[0]>=64
                && l.maxStorageBufferRange>=*std::max_element(kBytes.begin(),kBytes.end())
                && l.maxPushConstantsSize>=sizeof(Mixed::Camera),"Mixed fixture device limits unsupported");
            for(size_t i=0;i<_buffers.size();++i)
            {
                VkBufferUsageFlags usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                if(i==8) usage|=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
                if(i==7) usage|=VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
                _buffers[i].Initialise(device.GetPhysicalDevice(),_device,kBytes[i],usage);
            }
            std::array<VkDescriptorSetLayoutBinding,9> bindings{};
            for(uint32_t i=0;i<bindings.size();++i) bindings[i]={i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT};
            const VkDescriptorSetLayoutCreateInfo descriptorInfo{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount=static_cast<uint32_t>(bindings.size()),.pBindings=bindings.data()};
            CheckVk(vkCreateDescriptorSetLayout(_device,&descriptorInfo,nullptr,&_descriptorLayout),"mixed descriptor layout");
            const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,9};
            const VkDescriptorPoolCreateInfo poolInfo{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&poolSize};
            CheckVk(vkCreateDescriptorPool(_device,&poolInfo,nullptr,&_pool),"mixed descriptor pool");
            const VkDescriptorSetAllocateInfo allocateInfo{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool=_pool,.descriptorSetCount=1,.pSetLayouts=&_descriptorLayout};
            CheckVk(vkAllocateDescriptorSets(_device,&allocateInfo,&_set),"mixed descriptor set");
            std::array<VkDescriptorBufferInfo,9> infos{}; std::array<VkWriteDescriptorSet,9> writes{};
            for(uint32_t i=0;i<infos.size();++i)
            {
                infos[i]={_buffers[i].GetBuffer(),0,_buffers[i].GetSize()};
                writes[i]={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=_set,.dstBinding=i,.descriptorCount=1,
                    .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&infos[i]};
            }
            vkUpdateDescriptorSets(_device,static_cast<uint32_t>(writes.size()),writes.data(),0,nullptr);
            const VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Mixed::Camera)};
            const VkPipelineLayoutCreateInfo layout{.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount=1,.pSetLayouts=&_descriptorLayout,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
            CheckVk(vkCreatePipelineLayout(_device,&layout,nullptr,&_layout),"mixed pipeline layout");
            auto lock=device.LockPipelineCache(); const auto module=LoadShaderModule(_device,emitter);
            const VkComputePipelineCreateInfo pipeline{.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"},.layout=_layout};
            const auto result=vkCreateComputePipelines(_device,device.GetPipelineCache(),1,&pipeline,nullptr,&_pipeline);
            vkDestroyShaderModule(_device,module,nullptr); lock.unlock(); CheckVk(result,"mixed emitter");
            _rect.Initialise(device,resources,shaders);
        }
        catch(...) { Dispose(); throw; }
    }
    void MixedFixturePipeline::Dispose()
    {
        _rect.Dispose();
        if(_device!=VK_NULL_HANDLE)
        {
            vkDestroyPipeline(_device,_pipeline,nullptr); vkDestroyPipelineLayout(_device,_layout,nullptr);
            vkDestroyDescriptorPool(_device,_pool,nullptr); vkDestroyDescriptorSetLayout(_device,_descriptorLayout,nullptr);
        }
        for(auto& buffer:_buffers) buffer.Dispose();
        _device=VK_NULL_HANDLE; _pipeline=VK_NULL_HANDLE; _layout=VK_NULL_HANDLE; _pool=VK_NULL_HANDLE;
        _descriptorLayout=VK_NULL_HANDLE; _set=VK_NULL_HANDLE; _epoch=0; _lastUpload=0;
        _extent={}; _frameCount=0; _assetCapacity=0; _assetCount=0; _counts.fill(0); DiscardPendingUploads();
    }
    void MixedFixturePipeline::Record(const SubmissionToken& frame,const MixedFixtureScene& scene,Mixed::Camera camera)
    {
        Require(_pipeline!=VK_NULL_HANDLE && frame.commandBuffer!=VK_NULL_HANDLE && frame.upload && frame.frameIndex<_frameCount,
            "Mixed fixture requires active existing submission");
        Require(scene.epoch!=0 && camera.rotation<4 && camera.width>0 && camera.width<=4096 && camera.height>0 && camera.height<=4096
            && camera.x>=-1048576 && camera.x<=1048576 && camera.y>=-1048576 && camera.y<=1048576
            && camera.clipX>=0 && camera.clipY>=0 && uint64_t(camera.clipX)+camera.width<=_extent.width
            && uint64_t(camera.clipY)+camera.height<=_extent.height && camera.assetCount>0 && camera.assetCount<=_assetCapacity
            && camera.outputCapacity<=Mixed::kCommandCapacity,"Mixed fixture camera/epoch invalid");
        const std::array sizes={scene.statics.size(),scene.definitions.size(),scene.peeps.size(),scene.descriptors.size(),scene.facts.size(),scene.sprites.size()};
        Require(sizes[0]+sizes[2]<=Mixed::kOwnerCapacity,"Too many mixed fixture owners");
        for(size_t i=0;i<sizes.size();++i) Require(sizes[i]<=kCapacities[i] && scene.revisions[i]!=0,"Mixed fixture input capacity/revision invalid");
        if(_epoch!=scene.epoch) { _epoch=scene.epoch; DiscardPendingUploads(); }
        for(size_t i=0;i<sizes.size();++i)
            Require(_revisions[i]!=scene.revisions[i] || _counts[i]==sizes[i],"Input size changed without revision");
        if(_revisions[1]!=scene.revisions[1]) ValidateDefinitions(scene.definitions);
        if(_revisions[5]!=scene.revisions[5]) ValidateSprites(scene.sprites,camera.assetCount);
        else Require(_assetCount==camera.assetCount,"Asset capacity changed without sprite revision");
        if(_revisions[3]!=scene.revisions[3]) for(size_t i=1;i<scene.descriptors.size();++i)
            Require(scene.descriptors[i-1].objectIndex<scene.descriptors[i].objectIndex,"Peep descriptors must be unique sorted object indices");
        camera.staticCount=static_cast<uint32_t>(sizes[0]); camera.definitionCount=static_cast<uint32_t>(sizes[1]);
        camera.peepCount=static_cast<uint32_t>(sizes[2]); camera.descriptorCount=static_cast<uint32_t>(sizes[3]);
        camera.factCount=static_cast<uint32_t>(sizes[4]); camera.spriteCount=static_cast<uint32_t>(sizes[5]);
        _lastUpload=0;
        try
        {
            Barrier(frame.commandBuffer,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
            const std::array data={std::as_bytes(scene.statics),std::as_bytes(scene.definitions),std::as_bytes(scene.peeps),
                std::as_bytes(scene.descriptors),std::as_bytes(scene.facts),std::as_bytes(scene.sprites)};
            for(size_t i=0;i<data.size();++i) if(_revisions[i]!=scene.revisions[i])
            {
                if(!data[i].empty())
                {
                    auto upload=frame.upload->Allocate(data[i].size(),4,Drawing::UploadCategory::world);
                    if(!upload) throw std::runtime_error("Mixed fixture upload ring exhausted");
                    std::memcpy(upload.data,data[i].data(),data[i].size()); upload.RecordHostWrite();
                    const VkBufferCopy copy{upload.offset,0,data[i].size()};
                    vkCmdCopyBuffer(frame.commandBuffer,upload.buffer,_buffers[i].GetBuffer(),1,&copy);
                    upload.Record(Drawing::UploadMetric::bufferTransfer,copy.size); _lastUpload+=copy.size;
                }
                _revisions[i]=scene.revisions[i]; _counts[i]=sizes[i];
            }
            _assetCount=camera.assetCount;
            vkCmdFillBuffer(frame.commandBuffer,_buffers[7].GetBuffer(),0,_buffers[7].GetSize(),0);
            Barrier(frame.commandBuffer,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
            vkCmdBindPipeline(frame.commandBuffer,VK_PIPELINE_BIND_POINT_COMPUTE,_pipeline);
            vkCmdBindDescriptorSets(frame.commandBuffer,VK_PIPELINE_BIND_POINT_COMPUTE,_layout,0,1,&_set,0,nullptr);
            for(uint32_t pass=0;pass<2;++pass)
            {
                camera.pass=pass; vkCmdPushConstants(frame.commandBuffer,_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(camera),&camera);
                vkCmdDispatch(frame.commandBuffer,pass==1?1:std::max(1u,(camera.staticCount+camera.peepCount+63)/64),1,1);
                Barrier(frame.commandBuffer,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
            }
            Barrier(frame.commandBuffer,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT|VK_ACCESS_INDIRECT_COMMAND_READ_BIT|VK_ACCESS_TRANSFER_READ_BIT);
            _rect.RecordSpritesIndirect(frame,_buffers[8],_buffers[7],1,kBytes[8],Mixed::kIndirectByteOffset,sizeof(VkDrawIndirectCommand));
        }
        catch(...) { DiscardPendingUploads(); throw; }
    }
}
#endif
