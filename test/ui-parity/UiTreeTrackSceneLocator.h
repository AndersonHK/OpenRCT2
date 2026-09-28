/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <openrct2/GameState.h>
#include <openrct2/core/Json.hpp>
#include <openrct2/interface/Viewport.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/object/SmallSceneryObject.h>
#include <openrct2/ride/RideData.h>
#include <openrct2/world/TileElementsView.h>
#include <openrct2/world/tile_element/SmallSceneryElement.h>
#include <openrct2/world/tile_element/TrackElement.h>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace UiParity
{
    // Diagnostic census only: call on the game owner thread immediately after
    // a paused load. Does not change the park, viewport, renderer or saved file.
    inline json_t LocateTreeTrackScenes(OpenRCT2::IObjectManager& objects, int32_t viewportWidth, int32_t viewportHeight)
    {
        using namespace OpenRCT2;
        if (viewportWidth <= 0 || viewportHeight <= 0 || viewportWidth > 16384 || viewportHeight > 16384)
            throw std::invalid_argument("Invalid scene locator viewport dimensions");
        const auto mapSize = getGameState().mapSize;
        if (mapSize.x <= 0 || mapSize.y <= 0 || mapSize.x > 1024 || mapSize.y > 1024)
            throw std::invalid_argument("Scene locator supports loaded maps up to 1024 x 1024 tiles");
        struct Tree
        {
            int32_t x, y, z, height;
            uint32_t slot;
            std::string id, name;
            bool cherry;
        };
        struct Track
        {
            int32_t x, y, z;
            uint32_t rideId, rideType, trackType, style;
        };
        struct Pair
        {
            size_t tree;
            Track track;
            int32_t distanceSquared;
        };
        const auto isWooden = [](TrackStyle style) {
            return style == TrackStyle::woodenRollerCoaster || style == TrackStyle::classicWoodenRollerCoaster
                || style == TrackStyle::classicWoodenTwisterRollerCoaster || style == TrackStyle::woodenWildMouse
                || style == TrackStyle::sideFrictionRollerCoaster || style == TrackStyle::virginiaReel;
        };
        std::vector<Tree> trees;
        std::map<std::pair<int32_t, int32_t>, std::vector<Track>> tracks;
        std::map<std::string, uint64_t> treeCounts;
        std::map<uint32_t, uint64_t> woodenStyleCounts;
        uint64_t unresolvedScenery = 0;
        uint64_t woodenElements = 0;
        for (int32_t y = 0; y < mapSize.y; ++y)
        {
            for (int32_t x = 0; x < mapSize.x; ++x)
            {
                const TileCoordsXY tile{ x, y };
                for (const auto* element : TileElementsView<SmallSceneryElement>(tile))
                {
                    if (element->isGhost())
                        continue;
                    const auto* entry = element->getEntry();
                    const auto* object = objects.GetLoadedObject<SmallSceneryObject>(element->getEntryIndex());
                    if (entry == nullptr || object == nullptr)
                    {
                        ++unresolvedScenery;
                        continue;
                    }
                    if (!entry->flags.has(SmallSceneryFlag::isTree))
                        continue;
                    const std::string id(object->GetIdentifier());
                    ++treeCounts[id];
                    const bool cherry = id == "rct2ww.scenery_small.jachtree" || id == "rct2ww.scenery_small.japchblo";
                    trees.push_back({ x, y, element->getBaseZ(), entry->height, element->getEntryIndex(), id,
                        object->GetName(), cherry });
                }
                for (const auto* element : TileElementsView<TrackElement>(tile))
                {
                    if (element->isGhost())
                        continue;
                    const auto style = GetRideTypeDescriptor(element->getRideType()).TrackPaintFunctions.Regular.trackStyle;
                    if (!isWooden(style))
                        continue;
                    ++woodenElements;
                    ++woodenStyleCounts[static_cast<uint32_t>(style)];
                    tracks[{ x, y }].push_back({ x, y, element->getBaseZ(), element->getRideIndex().ToUnderlying(),
                        element->getRideType(), static_cast<uint32_t>(element->getTrackType()), static_cast<uint32_t>(style) });
                }
            }
        }
        // The bounded local search avoids a global tree x track cross product.
        constexpr int32_t radius = 8;
        std::vector<Pair> pairs;
        for (size_t i = 0; i < trees.size(); ++i)
        {
            const auto& tree = trees[i];
            std::optional<Pair> nearest;
            for (int32_t y = std::max(0, tree.y - radius); y <= std::min(mapSize.y - 1, tree.y + radius); ++y)
            {
                for (int32_t x = std::max(0, tree.x - radius); x <= std::min(mapSize.x - 1, tree.x + radius); ++x)
                {
                    const auto found = tracks.find({ x, y });
                    if (found == tracks.end())
                        continue;
                    for (const auto& track : found->second)
                    {
                        const auto distance = (tree.x - x) * (tree.x - x) + (tree.y - y) * (tree.y - y);
                        if (!nearest || distance < nearest->distanceSquared)
                            nearest = Pair{ i, track, distance };
                    }
                }
            }
            if (nearest)
                pairs.push_back(*nearest);
        }
        std::stable_sort(pairs.begin(), pairs.end(), [&](const Pair& left, const Pair& right) {
            return std::tuple{ !trees[left.tree].cherry, left.distanceSquared, left.tree }
                < std::tuple{ !trees[right.tree].cherry, right.distanceSquared, right.tree };
        });
        const size_t matchedTrees = pairs.size();
        if (pairs.size() > 64)
            pairs.resize(64);
        json_t candidates = json_t::array();
        for (const auto& pair : pairs)
        {
            const auto& tree = trees[pair.tree];
            const auto& track = pair.track;
            json_t cameras = json_t::array();
            for (int32_t rotation = 0; rotation < 4; ++rotation)
            {
                const CoordsXYZ centre{ (tree.x + track.x) * 16 + 16, (tree.y + track.y) * 16 + 16,
                    (tree.z + tree.height / 2 + track.z) / 2 };
                const auto projected = Translate3DTo2DWithZ(rotation, centre);
                for (int32_t zoom = 0; zoom <= 1; ++zoom)
                    cameras.push_back({ { "rotation", rotation }, { "zoom", zoom },
                        { "viewPosition", { projected.x - (viewportWidth << zoom) / 2,
                                              projected.y - (viewportHeight << zoom) / 2 } } });
            }
            candidates.push_back({ { "tree", { { "tile", { tree.x, tree.y } }, { "baseZ", tree.z },
                        { "height", tree.height }, { "slot", tree.slot }, { "id", tree.id }, { "name", tree.name },
                        { "exactCherryObject", tree.cherry } } },
                { "track", { { "tile", { track.x, track.y } }, { "baseZ", track.z }, { "rideId", track.rideId },
                        { "rideType", track.rideType }, { "trackType", track.trackType }, { "trackStyle", track.style } } },
                { "tileDistanceSquared", pair.distanceSquared }, { "cameras", cameras } });
        }
        return { { "schemaVersion", 1 }, { "kind", "loaded-tree-wooden-track-scene-locator" },
            { "mutatedWorld", false }, { "mapSize", { mapSize.x, mapSize.y } },
            { "viewportExtent", { viewportWidth, viewportHeight } }, { "searchRadiusTiles", radius },
            { "treeCounts", treeCounts }, { "woodenStyleCounts", woodenStyleCounts },
            { "woodenTrackElements", woodenElements }, { "unresolvedScenery", unresolvedScenery },
            { "matchedTrees", matchedTrees }, { "candidateLimit", 64 }, { "candidates", candidates },
            { "selectionProof", "Nearby semantic objects only; projected overlap, lattice visibility and reproduction require manual scene inspection." } };
    }
} // namespace UiParity
