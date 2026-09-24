/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
 *****************************************************************************/
#pragma once

#include "../drawing/ScrollingText.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace OpenRCT2
{
    struct WorldRidePresentationMaterials;
    struct WorldBannerPresentation
    {
        uint64_t revision{};
        // Indexed by BannerIndex and RideId respectively. Null entries are absent.
        std::vector<std::shared_ptr<const Drawing::ScrollingText::TextColumns>> banners;
        std::vector<std::shared_ptr<const Drawing::ScrollingText::TextColumns>> queueNames;
        std::shared_ptr<const Drawing::ScrollingText::TextColumns> queueClosed;
    };

    // Called on the authoritative publication boundary. Returned generations own all font
    // pixels and remain valid across banner deletion, ride reuse, language and font changes.
    std::shared_ptr<const WorldBannerPresentation> CaptureWorldBannerTexts(
        const std::shared_ptr<const WorldRidePresentationMaterials>& rides);
} // namespace OpenRCT2
