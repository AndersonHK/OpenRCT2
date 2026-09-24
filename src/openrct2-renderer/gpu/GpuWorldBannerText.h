// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include <limits>
#include <openrct2/world/WorldBannerPresentation.h>
#include <stdexcept>
#include <unordered_map>

namespace OpenRCT2::Ui::Gpu
{
    struct WorldBannerTextData
    {
        std::shared_ptr<const WorldBannerPresentation> source;
        std::vector<uint32_t> words;
    };

    // Pack only on a text-generation change. Tick/scroll phase is a scene constant;
    // every sign shares these columns and the original mode's immutable geometry.
    inline std::shared_ptr<const WorldBannerTextData> BuildWorldBannerTextData(
        std::shared_ptr<const WorldBannerPresentation> source)
    {
        auto result = std::make_shared<WorldBannerTextData>();
        result->source = std::move(source);
        auto& words = result->words;
        words.resize(16);
        words[0] = 0x57425458;
        words[1] = 1;
        words[8] = 16;
        if (!result->source)
            return result;
        const auto& texts = *result->source;
        const auto offset = [&]() {
            if (words.size() > std::numeric_limits<uint32_t>::max())
                throw std::overflow_error("World banner text data exceeds address space");
            return static_cast<uint32_t>(words.size());
        };
        words[2] = offset();
        words[3] = static_cast<uint32_t>(texts.banners.size());
        words.resize(words.size() + texts.banners.size() * 4);
        words[4] = offset();
        words[5] = static_cast<uint32_t>(texts.queueNames.size());
        words.resize(words.size() + texts.queueNames.size() * 4);
        words[6] = offset();
        words.resize(words.size() + 4);
        words[7] = offset();
        for (const auto& mode : Drawing::ScrollingText::getModeColumns())
            for (const auto& column : mode)
                words.push_back(uint32_t(column.sourceColumn) | (uint32_t(column.y) << 16));
        std::unordered_map<const Drawing::ScrollingText::TextColumns*, uint32_t> sharedColumns;
        const auto append = [&](uint32_t descriptor, const auto& text) {
            if (!text)
                return;
            auto [entry, inserted] = sharedColumns.try_emplace(text.get(), offset());
            words[descriptor] = entry->second;
            words[descriptor + 1] = text->phaseWidth;
            words[descriptor + 2] = static_cast<uint32_t>(text->columns.size());
            words[descriptor + 3] = text->repeat ? 1u : 0u;
            if (inserted)
                for (const auto& column : text->columns)
                    for (uint32_t half = 0; half < 2; half++)
                        words.push_back(
                            uint32_t(column[half * 4]) | (uint32_t(column[half * 4 + 1]) << 8)
                            | (uint32_t(column[half * 4 + 2]) << 16) | (uint32_t(column[half * 4 + 3]) << 24));
        };
        for (uint32_t i = 0; i < texts.banners.size(); i++)
            append(words[2] + i * 4, texts.banners[i]);
        for (uint32_t i = 0; i < texts.queueNames.size(); i++)
            append(words[4] + i * 4, texts.queueNames[i]);
        append(words[6], texts.queueClosed);
        words[8] = offset();
        return result;
    }
} // namespace OpenRCT2::Ui::Gpu
