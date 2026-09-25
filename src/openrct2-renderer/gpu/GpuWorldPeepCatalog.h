// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include "PeepAssetGeneration.h"

#include <openrct2/SpriteIds.h>
#include <openrct2/drawing/Colour.h>
namespace OpenRCT2::Ui::Gpu
{
    // Compile immutable object facts once into resident sprite-set addresses. Camera,
    // action and frame selection remain in the shader, not in this catalog builder.
    template<typename Append>
    std::shared_ptr<const PeepAssetGeneration> BuildWorldPeepAssets(
        std::shared_ptr<const Drawing::RetainedPeepAnimationCatalog> catalog,
        std::shared_ptr<const std::vector<uint32_t>> usedObjects, Append&& append)
    {
        if (!catalog)
        {
            if (usedObjects && !usedObjects->empty())
                throw std::runtime_error("Live peep animation catalog is missing");
            return {};
        }
        auto result = std::make_shared<PeepAssetGeneration>();
        result->catalog = std::move(catalog);
        result->usedObjects = std::move(usedObjects);
        result->revision = NextNativeAssetRevision();
        const std::vector<uint32_t> empty;
        const auto& slots = result->usedObjects ? *result->usedObjects : empty;
        if (!std::is_sorted(slots.begin(), slots.end()) || std::adjacent_find(slots.begin(), slots.end()) != slots.end())
            throw std::invalid_argument("Peep object usage must be sorted and unique");
        for (const auto slot : slots)
        {
            const auto found = result->catalog->slots.find(slot);
            if (found == result->catalog->slots.end() || !found->second.object)
                throw std::runtime_error("Live peep animation object is missing");
            const auto& entry = found->second;
            auto descriptor = entry.object->descriptor;
            if (slot >= UINT16_MAX || descriptor.objectIndex != slot || descriptor.objectGeneration != entry.generation)
                throw std::runtime_error("Invalid world peep animation identity");
            if (result->descriptors.size() <= slot)
                result->descriptors.resize(slot + 1);
            const auto originalBase = descriptor.imageBase;
            uint32_t spriteBase = 0;
            for (uint32_t i = 0; i < descriptor.imageCount; ++i)
            {
                auto index = append(ImageId(originalBase + i).WithPrimary(Drawing::Colour::black));
                if (i == 0)
                    spriteBase = index;
                else if (index != spriteBase + i)
                    throw std::logic_error("Peep image range is not contiguous");
            }
            descriptor.imageBase = spriteBase;
            descriptor.factOffset = static_cast<uint32_t>(result->facts.size());
            for (auto fact : entry.object->facts)
            {
                if (fact.valid)
                    fact.baseImage = spriteBase + fact.baseImage - originalBase;
                result->facts.push_back(fact);
            }
            result->descriptors[slot] = descriptor;
        }
        for (auto base : { uint32_t(kPeepSpriteHatItemStart), uint32_t(kPeepSpriteBalloonItemStart),
                           uint32_t(kPeepSpriteUmbrellaItemStart) })
            for (uint32_t frame = 0; frame < 32; ++frame)
            {
                auto index = append(ImageId(base + frame).WithPrimary(Drawing::Colour::black));
                if (base == kPeepSpriteHatItemStart && frame == 0)
                    result->accessoryBase = index;
            }
        for (uint32_t frame = 0; frame < 13; ++frame)
        {
            auto index = append(ImageId(SPR_BALLOON + frame).WithPrimary(Drawing::Colour::black));
            if (frame == 0)
                result->balloonBase = index;
        }
        return result;
    }
} // namespace OpenRCT2::Ui::Gpu
