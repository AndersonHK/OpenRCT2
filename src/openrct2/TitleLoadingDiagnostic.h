// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace OpenRCT2
{
    // The isolated title-loading runner must never acquire audio hardware or
    // expose a desktop window. Stage logging alone leaves normal play unchanged.
    inline bool IsTitleLoadingDiagnostic()
    {
        static const bool enabled = [] {
            const auto* report = std::getenv("OPENRCT2_LOADING_REPORT");
            const auto* finish = std::getenv("OPENRCT2_TITLE_LOADING_EXIT_AT_END");
            return report != nullptr && std::string_view(report) == "1" && finish != nullptr && std::string_view(finish) == "1";
        }();
        return enabled;
    }

    namespace Detail
    {
        // Main-thread-only handoff: replacing a park must occur after the title
        // player's Update has returned, because it replaces active scene state.
        inline bool titleLoadingSavedGameRequested = false;
        inline std::optional<std::string> titleLoadingSavedGameRequest;
    } // namespace Detail

    inline bool HasTitleLoadingSavedGameRequest()
    {
        return Detail::titleLoadingSavedGameRequested;
    }

    inline bool QueueTitleLoadingSavedGameRequest()
    {
        if (!IsTitleLoadingDiagnostic())
            return false;
        const auto* path = std::getenv("OPENRCT2_TITLE_LOADING_SAVE_AT_END");
        if (path == nullptr || *path == '\0')
            return false;
        if (!Detail::titleLoadingSavedGameRequested)
        {
            Detail::titleLoadingSavedGameRequest = path;
            Detail::titleLoadingSavedGameRequested = true;
        }
        return true;
    }

    inline std::optional<std::string> TakeTitleLoadingSavedGameRequest()
    {
        return std::exchange(Detail::titleLoadingSavedGameRequest, std::nullopt);
    }
} // namespace OpenRCT2
