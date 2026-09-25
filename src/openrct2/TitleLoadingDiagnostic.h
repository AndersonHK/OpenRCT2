// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once

#include <cstdlib>
#include <string_view>

namespace OpenRCT2
{
    // The isolated title-loading runner must never acquire audio hardware or
    // expose a desktop window. Stage logging alone leaves normal play unchanged.
    inline bool IsTitleLoadingDiagnostic()
    {
        static const bool enabled = [] {
            const auto* report = std::getenv("OPENRCT2_LOADING_REPORT");
            const auto* finish = std::getenv("OPENRCT2_TITLE_LOADING_EXIT_AT_END");
            return report != nullptr && std::string_view(report) == "1" && finish != nullptr
                && std::string_view(finish) == "1";
        }();
        return enabled;
    }
}
