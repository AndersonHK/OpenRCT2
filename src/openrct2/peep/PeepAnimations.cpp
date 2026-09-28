/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "PeepAnimations.h"

#include "../Context.h"
#include "../drawing/Drawing.Sprite.h"
#include "../drawing/SpriteAssetDecoder.h"
#include "../entity/Peep.h"
#include "../entity/Staff.h"
#include "../object/ObjectLimits.h"
#include "../object/ObjectManager.h"
#include "../object/PeepAnimationsObject.h"

#include <algorithm>
#include <random>
#include <sstream>

namespace OpenRCT2
{
    static const EnumMap<PeepAnimationType> availableGuestAnimations(
        {
            { "walking", PeepAnimationType::walking },
            { "checkTime", PeepAnimationType::checkTime },
            { "watchRide", PeepAnimationType::watchRide },
            { "eatFood", PeepAnimationType::eatFood },
            { "shakeHead", PeepAnimationType::shakeHead },
            { "emptyPockets", PeepAnimationType::emptyPockets },
            { "holdMat", PeepAnimationType::holdMat },
            { "sittingIdle", PeepAnimationType::sittingIdle },
            { "sittingEatFood", PeepAnimationType::sittingEatFood },
            { "sittingLookAroundLeft", PeepAnimationType::sittingLookAroundLeft },
            { "sittingLookAroundRight", PeepAnimationType::sittingLookAroundRight },
            { "hanging", PeepAnimationType::hanging },
            { "wow", PeepAnimationType::wow },
            { "throwUp", PeepAnimationType::throwUp },
            { "jump", PeepAnimationType::jump },
            { "drowning", PeepAnimationType::drowning },
            { "joy", PeepAnimationType::joy },
            { "readMap", PeepAnimationType::readMap },
            { "wave", PeepAnimationType::wave },
            { "wave2", PeepAnimationType::wave2 },
            { "takePhoto", PeepAnimationType::takePhoto },
            { "clap", PeepAnimationType::clap },
            { "disgust", PeepAnimationType::disgust },
            { "drawPicture", PeepAnimationType::drawPicture },
            { "beingWatched", PeepAnimationType::beingWatched },
            { "withdrawMoney", PeepAnimationType::withdrawMoney },
        });

    static const EnumMap<PeepAnimationType> availableHandymanAnimations(
        {
            { "walking", PeepAnimationType::walking },
            { "watchRide", PeepAnimationType::watchRide },
            { "hanging", PeepAnimationType::hanging },
            { "staffMower", PeepAnimationType::staffMower },
            { "staffSweep", PeepAnimationType::staffSweep },
            { "drowning", PeepAnimationType::drowning },
            { "staffWatering", PeepAnimationType::staffWatering },
            { "staffEmptyBin", PeepAnimationType::staffEmptyBin },
        });

    static const EnumMap<PeepAnimationType> availableMechanicAnimations(
        {
            { "walking", PeepAnimationType::walking },
            { "watchRide", PeepAnimationType::watchRide },
            { "hanging", PeepAnimationType::hanging },
            { "drowning", PeepAnimationType::drowning },
            { "staffAnswerCall", PeepAnimationType::staffAnswerCall },
            { "staffAnswerCall2", PeepAnimationType::staffAnswerCall2 },
            { "staffCheckBoard", PeepAnimationType::staffCheckBoard },
            { "staffFix", PeepAnimationType::staffFix },
            { "staffFix2", PeepAnimationType::staffFix2 },
            { "staffFixGround", PeepAnimationType::staffFixGround },
            { "staffFix3", PeepAnimationType::staffFix3 },
        });

    static const EnumMap<PeepAnimationType> availableSecurityAnimations(
        {
            { "walking", PeepAnimationType::walking },
            { "watchRide", PeepAnimationType::watchRide },
            { "hanging", PeepAnimationType::hanging },
            { "drowning", PeepAnimationType::drowning },
        });

    static const EnumMap<PeepAnimationType> availableEntertainerAnimations(
        {
            { "walking", PeepAnimationType::walking },
            { "watchRide", PeepAnimationType::watchRide },
            { "hanging", PeepAnimationType::hanging },
            { "drowning", PeepAnimationType::drowning },
            { "joy", PeepAnimationType::joy },
            { "wave2", PeepAnimationType::wave2 },
        });

    const EnumMap<PeepAnimationType>& getAnimationsByPeepType(AnimationPeepType peepType)
    {
        switch (peepType)
        {
            case AnimationPeepType::guest:
                return availableGuestAnimations;
            case AnimationPeepType::handyman:
                return availableHandymanAnimations;
            case AnimationPeepType::mechanic:
                return availableMechanicAnimations;
            case AnimationPeepType::security:
                return availableSecurityAnimations;
            case AnimationPeepType::entertainer:
            default:
                return availableEntertainerAnimations;
        }
    }

    AnimationPeepType getAnimationPeepType(StaffType staffType)
    {
        switch (staffType)
        {
            case StaffType::handyman:
                return AnimationPeepType::handyman;
            case StaffType::mechanic:
                return AnimationPeepType::mechanic;
            case StaffType::security:
                return AnimationPeepType::security;
            case StaffType::entertainer:
            default:
                return AnimationPeepType::entertainer;
        }
    }

    ObjectEntryIndex findPeepAnimationsIndexForType(const AnimationPeepType type)
    {
        auto& objManager = GetContext()->GetObjectManager();
        for (auto i = 0u; i < kMaxPeepAnimationsObjects; i++)
        {
            auto* animObj = objManager.GetLoadedObject<PeepAnimationsObject>(i);
            if (animObj != nullptr && animObj->GetPeepType() == type)
                return i;
        }
        return kObjectEntryIndexNull;
    }

    PeepAnimationsObject* findPeepAnimationsObjectForType(const AnimationPeepType type)
    {
        auto& objManager = GetContext()->GetObjectManager();
        for (auto i = 0u; i < kMaxPeepAnimationsObjects; i++)
        {
            auto* animObj = objManager.GetLoadedObject<PeepAnimationsObject>(i);
            if (animObj != nullptr && animObj->GetPeepType() == type)
                return animObj;
        }
        return nullptr;
    }

    std::vector<ObjectEntryIndex> findAllPeepAnimationsIndexesForType(const AnimationPeepType type, bool randomOnly)
    {
        std::vector<ObjectEntryIndex> output{};
        auto& objManager = GetContext()->GetObjectManager();
        for (auto i = 0u; i < kMaxPeepAnimationsObjects; i++)
        {
            auto* animObj = objManager.GetLoadedObject<PeepAnimationsObject>(i);
            if (animObj == nullptr || animObj->GetPeepType() != type)
                continue;

            if (randomOnly && animObj->ShouldExcludeFromRandomPlacement())
                continue;

            output.push_back(i);
        }
        return output;
    }

    std::vector<PeepAnimationsObject*> findAllPeepAnimationsObjectForType(const AnimationPeepType type, bool randomOnly)
    {
        std::vector<PeepAnimationsObject*> output{};
        auto& objManager = GetContext()->GetObjectManager();
        for (auto i = 0u; i < kMaxPeepAnimationsObjects; i++)
        {
            auto* animObj = objManager.GetLoadedObject<PeepAnimationsObject>(i);
            if (animObj == nullptr || animObj->GetPeepType() != type)
                continue;

            if (randomOnly && animObj->ShouldExcludeFromRandomPlacement())
                continue;

            output.push_back(animObj);
        }
        return output;
    }

    ObjectEntryIndex findRandomPeepAnimationsIndexForType(const AnimationPeepType type)
    {
        // Get available costumes, excluding from random placement as requested
        auto costumes = findAllPeepAnimationsIndexesForType(type, true);

        // No costumes? Try again without respecting the random placement flag
        if (costumes.empty())
            costumes = findAllPeepAnimationsIndexesForType(type);

        // Still no costumes available? Bail out
        if (costumes.empty())
            return kObjectEntryIndexNull;

        std::vector<ObjectEntryIndex> out{};
        std::sample(costumes.begin(), costumes.end(), std::back_inserter(out), 1, std::mt19937{ std::random_device{}() });
        return !out.empty() ? out[0] : kObjectEntryIndexNull;
    }

    std::vector<AnimationGroupResult> getAnimationGroupsByPeepType(const AnimationPeepType type)
    {
        std::vector<AnimationGroupResult> groups{};

        auto& objManager = GetContext()->GetObjectManager();
        for (auto i = 0u; i < kMaxPeepAnimationsObjects; i++)
        {
            auto* animObj = objManager.GetLoadedObject<PeepAnimationsObject>(i);
            if (animObj == nullptr || animObj->GetPeepType() != type)
                continue;

            for (auto j = 0u; j < animObj->GetNumAnimationGroups(); j++)
            {
                auto group = PeepAnimationGroup(j);
                auto scriptName = animObj->GetScriptName(group);
                if (scriptName.empty())
                    continue;

                groups.push_back(
                    {
                        .objectId = ObjectEntryIndex(i),
                        .group = group,
                        .legacyPosition = animObj->GetLegacyPosition(group),
                        .rawName = animObj->GetCostumeName(),
                        .scriptName = scriptName,
                    });
            }
        }

        std::sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) { return a.rawName < b.rawName; });

        return groups;
    }

    std::vector<AvailableCostume> getAvailableCostumeStrings(const AnimationPeepType type)
    {
        auto availCostumeIndexes = findAllPeepAnimationsIndexesForType(type);
        auto availCostumeObjects = findAllPeepAnimationsObjectForType(type);

        auto availableCostumes = std::vector<AvailableCostume>{};
        for (auto i = 0u; i < availCostumeObjects.size(); i++)
        {
            auto baseName = availCostumeObjects[i]->GetCostumeName();
            auto inlineImageId = availCostumeObjects[i]->GetInlineImageId();

            // std::format doesn't appear to be available on macOS <13.3
            std::stringstream out{};
            out << "{INLINE_SPRITE}";
            for (auto b = 0; b < 32; b += 8)
                out << '{' << ((inlineImageId >> b) & 0xFF) << '}';
            out << ' ';
            out << baseName;

            availableCostumes.push_back(
                {
                    .index = availCostumeIndexes[i],
                    .object = availCostumeObjects[i],
                    .rawName = baseName,
                    .friendlyName = out.str(),
                });
        }

        std::sort(availableCostumes.begin(), availableCostumes.end(), [](const auto& a, const auto& b) {
            return a.rawName < b.rawName;
        });

        return availableCostumes;
    }

    // Adapted from CarEntry.cpp
    SpriteBounds inferMaxAnimationDimensions(const PeepAnimation& anim)
    {
        Drawing::SpriteAssetBoundsAccumulator inference;
        const auto numImages = *(std::max_element(anim.frameOffsets.begin(), anim.frameOffsets.end())) + 1;
        for (int32_t i = 0; i < numImages; ++i)
        {
            if (const auto* element = GfxGetG1Element(anim.baseImage + i); element != nullptr)
                inference.Add(*element);
        }
        const auto bounds = inference.GetBounds();

        return {
            .spriteWidth = bounds.width,
            .spriteHeightNegative = bounds.heightNegative,
            .spriteHeightPositive = bounds.heightPositive,
        };
    }
} // namespace OpenRCT2
