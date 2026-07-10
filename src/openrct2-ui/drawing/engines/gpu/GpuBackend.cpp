/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "GpuBackend.h"

#include <SDL_video.h>

namespace OpenRCT2::Ui::Gpu
{
    uint32_t GetRequiredSdlWindowFlags(BackendApi api) noexcept
    {
        switch (api)
        {
            case BackendApi::Vulkan:
                return SDL_WINDOW_VULKAN;
            case BackendApi::OpenGLLegacy:
                return SDL_WINDOW_OPENGL;
        }
        return 0;
    }
} // namespace OpenRCT2::Ui::Gpu
