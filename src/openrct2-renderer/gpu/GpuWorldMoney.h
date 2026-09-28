// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include <openrct2/drawing/MoneyPresentation.h>
#include <stdexcept>
#include <unordered_map>

namespace OpenRCT2::Ui::Gpu
{
    constexpr uint32_t kWorldMoneyCapacity = 65536;
    constexpr uint32_t kWorldMoneyRecordWords = 12;
    constexpr uint32_t kWorldMoneyCatalogOffset = 16 + kWorldMoneyCapacity * kWorldMoneyRecordWords;
    struct WorldMoneyCatalog
    {
        std::shared_ptr<const Drawing::MoneyGlyphCatalog> source;
        // Upload once at kWorldMoneyCatalogOffset words; all addresses are absolute in binding28.
        std::vector<uint32_t> words;
        std::vector<std::pair<uint32_t, uint32_t>> runs;
    };
    inline std::shared_ptr<const WorldMoneyCatalog> BuildWorldMoneyCatalog(
        std::shared_ptr<const Drawing::MoneyGlyphCatalog> source)
    {
        auto result = std::make_shared<WorldMoneyCatalog>();
        result->source = std::move(source);
        if (!result->source)
            return result;
        auto& words = result->words;
        const auto offset = [&]() {
            if (words.size() > UINT32_MAX - kWorldMoneyCatalogOffset)
                throw std::overflow_error("Money glyph address space exceeded");
            return kWorldMoneyCatalogOffset + static_cast<uint32_t>(words.size());
        };
        std::unordered_map<const Drawing::TextGlyphRun*, std::pair<uint32_t, uint32_t>> held;
        for (const auto& run : result->source->runs)
        {
            if (!run)
                throw std::invalid_argument("Missing money glyph run");
            if (auto found = held.find(run.get()); found != held.end())
            {
                result->runs.push_back(found->second);
                continue;
            }
            if (run->pieces.size() > 256)
                throw std::length_error("Money glyph run exceeds original formatted-string bound");
            const auto base = offset();
            const auto count = static_cast<uint32_t>(run->pieces.size());
            words.resize(words.size() + count * 12);
            for (uint32_t i = 0; i < count; i++)
            {
                const auto& p = run->pieces[i];
                if (p.width < 0 || p.height < 0 || uint64_t(p.width) * uint64_t(p.height) != p.pixels.size() || p.kind > 1
                    || p.ink > 255 || p.hintThreshold > 255)
                    throw std::invalid_argument("Invalid money glyph pixels");
                const auto d = base - kWorldMoneyCatalogOffset + i * 12;
                words[d] = uint32_t(p.x);
                words[d + 1] = uint32_t(p.y);
                words[d + 2] = uint32_t(p.width);
                words[d + 3] = uint32_t(p.height);
                words[d + 4] = offset();
                words[d + 5] = p.waveOrdinal;
                words[d + 6] = p.kind;
                words[d + 7] = p.ink;
                words[d + 8] = p.hintThreshold;
                for (size_t pixel = 0; pixel < p.pixels.size(); pixel += 4)
                {
                    uint32_t packed = 0;
                    for (size_t byte = 0; byte < 4 && pixel + byte < p.pixels.size(); byte++)
                        packed |= uint32_t(p.pixels[pixel + byte]) << (byte * 8);
                    words.push_back(packed);
                }
            }
            result->runs.emplace_back(base, count);
            held.emplace(run.get(), std::pair{ base, count });
        }
        return result;
    }
    // Upload at offset0 on hot-state changes. The unused capacity gap is never copied.
    inline std::vector<uint32_t> PackWorldMoneyRecords(
        const Drawing::MoneyPresentationSnapshot& source, const WorldMoneyCatalog& catalog)
    {
        if (!source.records || source.records->size() > kWorldMoneyCapacity || source.catalog != catalog.source)
            throw std::invalid_argument("Money snapshot/catalog ownership mismatch");
        std::vector<uint32_t> words(16 + source.records->size() * kWorldMoneyRecordWords);
        words[0] = 0x574D4F4Eu;
        words[1] = 1;
        words[2] = static_cast<uint32_t>(source.records->size());
        words[3] = kWorldMoneyCatalogOffset + static_cast<uint32_t>(catalog.words.size());
        for (uint32_t i = 0; i < source.records->size(); i++)
        {
            const auto& r = (*source.records)[i];
            if (r.run >= catalog.runs.size())
                throw std::invalid_argument("Money run outside held catalog");
            const auto d = 16 + i * kWorldMoneyRecordWords;
            words[d] = uint32_t(r.x);
            words[d + 1] = uint32_t(r.y);
            words[d + 2] = uint32_t(r.z);
            words[d + 3] = r.entityId;
            words[d + 4] = r.generation;
            words[d + 5] = r.wiggle;
            words[d + 6] = uint32_t(r.offsetX);
            words[d + 7] = r.flags;
            words[d + 8] = catalog.runs[r.run].first;
            words[d + 9] = catalog.runs[r.run].second;
        }
        return words;
    }
} // namespace OpenRCT2::Ui::Gpu
