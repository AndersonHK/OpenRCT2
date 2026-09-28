/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UiParity
{
    struct SdlCapture
    {
        std::string name;
        std::string rendererName;
        uint32_t rendererFlags{};
        uint32_t width{};
        uint32_t height{};
        uint64_t presentOrdinal{};
        std::vector<uint8_t> rgba;
    };

    void ArmSdlCapture(std::string name);
    SdlCapture TakeSdlCapture();
} // namespace UiParity
