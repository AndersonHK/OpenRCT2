#pragma once

#ifdef ENABLE_VULKAN

    #include "VulkanDevice.h"
    #include "VulkanResources.h"

    #include <array>
    #include <filesystem>
    #include <openrct2-renderer/gpu/GpuCommandStream.h>

namespace OpenRCT2::Ui::Vulkan
{
    class LightFxPipeline final
    {
    private:
        VkDevice _device = VK_NULL_HANDLE;
        uint32_t _frameCount = kFramesInFlight;
        VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout _descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
        std::array<VkDescriptorSet, kFramesInFlight> _descriptorSets{};
        VkPipeline _pipeline = VK_NULL_HANDLE;
        VkPipelineCache _pipelineCache = VK_NULL_HANDLE;
        VkExtent2D _extent{};
        bool _available = false;

    public:
        ~LightFxPipeline();
        LightFxPipeline() = default;
        LightFxPipeline(const LightFxPipeline&) = delete;
        LightFxPipeline& operator=(const LightFxPipeline&) = delete;
        void Initialise(
            const DeviceContext& device, const IndexedResources& resources, const std::filesystem::path& shaderDirectory,
            bool enableGpuRasterization);
        void Dispose();
        void RefreshDescriptors(const IndexedResources& resources);
        [[nodiscard]] static bool IsSupported(const DeviceContext& device, Gpu::Extent logicalExtent) noexcept;
        [[nodiscard]] bool IsAvailable() const noexcept
        {
            return _available;
        }
        [[nodiscard]] bool Record(
            const SubmissionToken& frame, const Gpu::LightFxFrameSnapshot& snapshot, IndexedResources& resources) const;
    };
} // namespace OpenRCT2::Ui::Vulkan

#endif
