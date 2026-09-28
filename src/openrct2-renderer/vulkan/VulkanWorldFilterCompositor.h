/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#pragma once
#ifdef ENABLE_VULKAN
#include "VulkanDevice.h"
#include "VulkanResources.h"
#include <array>
#include <filesystem>
namespace OpenRCT2::Ui::Vulkan
{
    // Same-queue scratch, retired by the owning FrameExecutor fence. No CPU pixel work.
    class WorldFilterCompositor final
    {
        VkDevice _device = VK_NULL_HANDLE;
        VkDescriptorSetLayout _layout = VK_NULL_HANDLE;
        VkDescriptorPool _pool = VK_NULL_HANDLE;
        VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
        VkPipeline _pipeline = VK_NULL_HANDLE;
        std::array<VkDescriptorSet,kFramesInFlight> _sets{};
        std::array<VkImage,kFramesInFlight> _images{};
        Buffer _heads, _nodes, _control, _output;
        VkExtent2D _extent{};
        uint32_t _capacity{}, _pixels{}, _groups{};
    public:
        WorldFilterCompositor() = default;
        WorldFilterCompositor(const WorldFilterCompositor&) = delete;
        WorldFilterCompositor& operator=(const WorldFilterCompositor&) = delete;
        ~WorldFilterCompositor() { Dispose(); }
        void Initialise(const DeviceContext&,const IndexedResources&,const Buffer& status,const std::filesystem::path&);
        void Dispose();
        void Begin(VkCommandBuffer) const;
        void Resolve(VkCommandBuffer,uint32_t frameIndex) const;
        VkDescriptorSetLayout GetLayout() const { return _layout; }
        VkDescriptorSet GetSet(uint32_t frameIndex) const { return _sets.at(frameIndex); }
    };
}
#endif
