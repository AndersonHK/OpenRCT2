/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_VULKAN

    #include "VulkanShader.h"

    #include <fstream>
    #include <stdexcept>
    #include <string>
    #include <vector>

namespace OpenRCT2::Ui::Vulkan
{
    VkShaderModule LoadShaderModule(VkDevice device, const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
        {
            throw std::runtime_error("Unable to open Vulkan shader: " + path.string());
        }

        const auto length = stream.tellg();
        if (length <= 0 || (length % static_cast<std::streamoff>(sizeof(uint32_t))) != 0)
        {
            throw std::runtime_error("Invalid Vulkan shader length: " + path.string());
        }
        std::vector<uint32_t> code(static_cast<size_t>(length) / sizeof(uint32_t));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(length));
        if (!stream)
        {
            throw std::runtime_error("Unable to read Vulkan shader: " + path.string());
        }

        const VkShaderModuleCreateInfo moduleInfo = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = static_cast<size_t>(length),
            .pCode = code.data(),
        };
        VkShaderModule result = VK_NULL_HANDLE;
        const auto createResult = vkCreateShaderModule(device, &moduleInfo, nullptr, &result);
        if (createResult != VK_SUCCESS)
        {
            throw std::runtime_error(
                "vkCreateShaderModule failed with Vulkan result " + std::to_string(createResult));
        }
        return result;
    }
} // namespace OpenRCT2::Ui::Vulkan

#endif // ENABLE_VULKAN
