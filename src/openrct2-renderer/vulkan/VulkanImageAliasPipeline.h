/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#pragma once
#ifdef ENABLE_VULKAN
    #include "VulkanDevice.h"
    #include <filesystem>
    #include <openrct2/drawing/RenderService.h>

namespace OpenRCT2::Ui::Vulkan
{
    // One worker-owned descriptor set, reused only after the preceding slot fence retires.
    class ImageAliasPipeline final
    {
        VkDevice _device = VK_NULL_HANDLE;
        VkPipeline _pipeline = VK_NULL_HANDLE;
        VkPipelineLayout _layout = VK_NULL_HANDLE;
        VkDescriptorSetLayout _setLayout = VK_NULL_HANDLE;
        VkDescriptorPool _pool = VK_NULL_HANDLE;
        VkDescriptorSet _set = VK_NULL_HANDLE;
        VkDeviceSize _alignment = 4;

    public:
        ~ImageAliasPipeline();
        ImageAliasPipeline() = default;
        ImageAliasPipeline(const ImageAliasPipeline&) = delete;
        ImageAliasPipeline& operator=(const ImageAliasPipeline&) = delete;
        void Initialise(const DeviceContext& device, const std::filesystem::path& shaders);
        void Dispose();
        UploadAllocation Record(const SubmissionToken& token, const Drawing::OffscreenRenderRequest& request);
    };
}
#endif
