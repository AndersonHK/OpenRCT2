// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include "GpuWorldObject.h"

#include <algorithm>
#include <limits>
#include <openrct2/world/WorldObjectPresentation.h>
#include <stdexcept>

namespace OpenRCT2::Ui::Gpu
{
    // Resolves each material's complete reachable sprite range once per resident asset generation.
    // The callback returns consecutive table indices and owns atlas dependency/lease construction.
    template<typename AppendImage>
    WorldPropCatalog BuildWorldPropCatalog(
        const WorldObjectPresentationMaterials& source, uint32_t glassPaletteRow, AppendImage&& appendImage,
        const WorldObjectPresentationUsage* usage = nullptr)
    {
        WorldPropCatalog result;
        const uint32_t sizes[] = { static_cast<uint32_t>(source.smallScenery.size()),
                                   static_cast<uint32_t>(source.largeScenery.size()),
                                   static_cast<uint32_t>(source.walls.size()), static_cast<uint32_t>(source.banners.size()) };
        uint32_t words = kWorldPropCatalogHeaderWords;
        for (auto count : sizes)
            words += count * kWorldPropMaterialWords;
        result.words.resize(words);
        uint32_t cursor = kWorldPropCatalogHeaderWords;
        for (uint32_t family = 0; family < 4; family++)
        {
            result.words[family] = cursor;
            result.words[4 + family] = sizes[family];
            cursor += sizes[family] * kWorldPropMaterialWords;
        }
        result.words[8] = glassPaletteRow;
        const auto start = [&](uint32_t family, uint32_t slot) {
            return result.words[family] + slot * kWorldPropMaterialWords;
        };
        const auto range = [&](uint32_t entry, const auto& material, uint64_t required) {
            if (material.image < material.imageBase
                || uint64_t(material.image) >= uint64_t(material.imageBase) + material.imageCount)
                throw std::runtime_error("GPU prop material image exceeds its owning allocation");
            const auto available = uint64_t(material.imageBase) + material.imageCount - material.image;
            const auto count = static_cast<uint32_t>(std::min(required, available));
            uint32_t base = 0;
            for (uint32_t i = 0; i < count; i++)
            {
                const auto index = appendImage(material.image + i);
                if (i == 0)
                    base = index;
                else if (uint64_t(base) + i != index)
                    throw std::runtime_error("GPU prop image table must be contiguous");
            }
            result.words[entry] = base;
            result.words[entry + 1] = count;
            result.words[entry + 2] = material.flags;
        };
        for (uint32_t slot = 0; slot < sizes[0]; slot++)
        {
            const auto& item = source.smallScenery[slot];
            if (!item.present || (usage && !usage->Contains(0, slot)))
                continue;
            const auto e = start(0, slot);
            // Retain all finite frame references without a per-object or per-tile frame cap.
            uint64_t count = 4;
            if (item.flags & (1u << 5))
                count = std::max<uint64_t>(count, 12);
            if (item.flags & (1u << 9))
                count += 4;
            if (item.flags & (1u << 11))
                count = std::max<uint64_t>(count, 20);
            if (item.flags & (1u << 12))
                count = std::max<uint64_t>(count, 40);
            if (item.flags & (1u << 13))
                count = std::max<uint64_t>(count, 116);
            if (item.flags & (1u << 14))
                count = std::max<uint64_t>(count, 16);
            if (item.flags & (1u << 15))
            {
                uint32_t largest = 0;
                for (auto offset : item.frameOffsets)
                    largest = std::max<uint32_t>(largest, offset);
                count = std::max<uint64_t>(
                    count, uint64_t(largest) * 4 + 4 + ((item.flags & ((1u << 21) | (1u << 16))) != 0 ? 4 : 0));
            }
            range(e, item, count);
            result.words[e + 4] = item.height;
            result.words[e + 5] = item.animationDelay;
            result.words[e + 6] = item.animationMask;
            result.words[e + 7] = static_cast<uint32_t>(result.words.size());
            result.words[e + 8] = static_cast<uint32_t>(item.frameOffsets.size());
            result.words.insert(result.words.end(), item.frameOffsets.begin(), item.frameOffsets.end());
        }
        for (uint32_t slot = 0; slot < sizes[1]; slot++)
        {
            const auto& item = source.largeScenery[slot];
            if (!item.present || (usage && !usage->Contains(1, slot)))
                continue;
            const auto e = start(1, slot);
            range(e, item, 4 + uint64_t(item.tiles.size()) * 4);
            result.words[e + 9] = static_cast<uint32_t>(result.words.size());
            result.words[e + 10] = static_cast<uint32_t>(item.tiles.size());
            result.words[e + 11] = item.scrollingMode;
            for (const auto& tile : item.tiles)
            {
                result.words.push_back(static_cast<uint32_t>(tile.zClearance));
                result.words.push_back(tile.corners);
                result.words.push_back(tile.walls);
                result.words.push_back(uint32_t(tile.hasSupports) | (uint32_t(tile.allowSupportsAbove) << 1));
            }
            if (item.font)
            {
                const auto& font = *item.font;
                const auto imageCount = uint32_t(font.numImages) * ((font.flags & 1u) != 0 ? 2u : 4u);
                if (font.image < item.imageBase
                    || uint64_t(font.image) + imageCount > uint64_t(item.imageBase) + item.imageCount)
                    throw std::runtime_error("GPU object font exceeds its held allocation");
                uint32_t imageBase = 0;
                for (uint32_t i = 0; i < imageCount; i++)
                {
                    const auto image = appendImage(font.image + i);
                    if (i == 0)
                        imageBase = image;
                    else if (image != imageBase + i)
                        throw std::runtime_error("GPU object font images must be contiguous");
                }
                result.words[e + 12] = static_cast<uint32_t>(result.words.size());
                result.words.insert(
                    result.words.end(),
                    { imageBase, imageCount, font.flags, font.maxWidth, uint32_t(font.offsets[0]), uint32_t(font.offsets[1]),
                      uint32_t(font.offsets[2]), uint32_t(font.offsets[3]) });
                result.words.insert(result.words.end(), font.glyphs.begin(), font.glyphs.end());
            }
        }
        for (uint32_t slot = 0; slot < sizes[2]; slot++)
        {
            const auto& item = source.walls[slot];
            if (!item.present || (usage && !usage->Contains(2, slot)))
                continue;
            const auto e = start(2, slot);
            range(e, item, (item.flags & (1u << 4)) != 0 ? 36 : 37);
            result.words[e + 3] = item.flags2;
            result.words[e + 4] = item.height;
            result.words[e + 11] = item.scrollingMode;
        }
        for (uint32_t slot = 0; slot < sizes[3]; slot++)
        {
            const auto& item = source.banners[slot];
            if (!item.present || (usage && !usage->Contains(3, slot)))
                continue;
            range(start(3, slot), item, 8);
            result.words[start(3, slot) + 11] = item.scrollingMode;
        }
        return result;
    }
} // namespace OpenRCT2::Ui::Gpu
