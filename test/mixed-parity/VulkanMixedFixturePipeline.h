// Experimental mixed-scene pipeline; test-only, no runtime admission.
#pragma once
#ifdef ENABLE_VULKAN
#include <openrct2-renderer/vulkan/VulkanRectPipeline.h>
#include "MixedFixture.h"
#include <openrct2-renderer/gpu/PeepRules.h>
#include <openrct2-renderer/gpu/RetainedTerrainDrawing.h>
#include <span>
namespace OpenRCT2::Ui::Vulkan
{
    struct MixedFixtureScene
    {
        uint64_t epoch{};
        std::array<uint64_t,6> revisions{};
        std::span<const Gpu::MixedFixture::StaticInstance> statics;
        std::span<const Gpu::MixedFixture::StaticDefinition> definitions;
        std::span<const Gpu::Peeps::PeepRaw> peeps;
        std::span<const Gpu::Peeps::PeepAnimationDescriptor> descriptors;
        std::span<const Gpu::Peeps::PeepAnimationFact> facts;
        std::span<const Gpu::Terrain::DrawSpriteMetadata> sprites;
    };
    // Caller owns the shared service/device, atlas and generation lease. One
    // serialized queue; retire outstanding slots before disposing this object.
    class MixedFixturePipeline final
    {
        VkDevice _device=VK_NULL_HANDLE;
        VkDescriptorSetLayout _descriptorLayout=VK_NULL_HANDLE;
        VkDescriptorPool _pool=VK_NULL_HANDLE;
        VkDescriptorSet _set=VK_NULL_HANDLE;
        VkPipelineLayout _layout=VK_NULL_HANDLE;
        VkPipeline _pipeline=VK_NULL_HANDLE;
        RectPipeline _rect;
        std::array<Buffer,9> _buffers;
        std::array<uint64_t,6> _revisions{};
        std::array<size_t,6> _counts{};
        uint64_t _epoch{},_lastUpload{};
        VkExtent3D _extent{};
        uint32_t _frameCount{},_assetCapacity{},_assetCount{};
    public:
        ~MixedFixturePipeline();
        void Initialise(const DeviceContext&,const IndexedResources&,const std::filesystem::path& shaders,
            const std::filesystem::path& emitter);
        void Dispose();
        void Record(const SubmissionToken&,const MixedFixtureScene&,Gpu::MixedFixture::Camera);
        void DiscardPendingUploads() noexcept { _revisions.fill(0); }
        uint64_t GetLastUploadBytes() const noexcept { return _lastUpload; }
        const Buffer& GetCommandBuffer() const noexcept { return _buffers[8]; }
        const Buffer& GetStatusBuffer() const noexcept { return _buffers[7]; }
        const Buffer& GetScratchBuffer() const noexcept { return _buffers[6]; }
    };
}
#endif
