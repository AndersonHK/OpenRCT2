// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#pragma once
#include "GpuWorldFlatRideCatalog.h"

#include <unordered_set>

namespace OpenRCT2::Ui::Gpu
{
    // Artwork is owned by shared families, never by the identity, position or
    // colour of a placed ride. Removing a ghost cannot evict its admitted art.
    class WorldRideArtCoverage
    {
        std::unordered_set<uint16_t> _trackTypes, _stations;
        std::unordered_set<uint32_t> _flatObjects;
        static uint32_t FlatKey(const WorldRidePresentationRecord& ride)
        {
            return (uint32_t(ride.regularStyle) << 16) | ride.objectSlot;
        }

    public:
        WorldRideArtCoverage(const WorldRidePresentationMaterials& source, const WorldObjectPresentationUsage* usage)
        {
            for (uint32_t id = 0; id < source.rides.size(); ++id)
            {
                const auto& ride = source.rides[id];
                if (!ride.present)
                    continue;
                _trackTypes.insert(ride.rideType);
                if (!usage || usage->ContainsRide(id))
                    _stations.insert(ride.stationStyle);
                if ((!usage || usage->Contains(4, id)) && WorldFlatRideFamily(static_cast<TrackStyle>(ride.regularStyle)) != 0)
                    _flatObjects.insert(FlatKey(ride));
            }
        }
        [[nodiscard]] bool Contains(
            const WorldRidePresentationMaterials& source, const WorldObjectPresentationUsage* usage) const
        {
            for (uint32_t id = 0; id < source.rides.size(); ++id)
            {
                const auto& ride = source.rides[id];
                if (!ride.present)
                    continue;
                if (!_trackTypes.contains(ride.rideType))
                    return false;
                if ((!usage || usage->ContainsRide(id)) && !_stations.contains(ride.stationStyle))
                    return false;
                if ((!usage || usage->Contains(4, id)) && WorldFlatRideFamily(static_cast<TrackStyle>(ride.regularStyle)) != 0
                    && !_flatObjects.contains(FlatKey(ride)))
                    return false;
            }
            return true;
        }
    };

    inline bool WorldRideUsageMatches(const WorldObjectPresentationUsage* left, const WorldObjectPresentationUsage* right)
    {
        return left == right || (left && right && left->slots[4] == right->slots[4] && left->slots[6] == right->slots[6]);
    }

    inline bool WorldNonRideArtworkCovers(
        const WorldObjectPresentationUsage* admitted, const WorldObjectPresentationUsage* required)
    {
        if (!admitted)
            return true;
        if (!required)
            return false;
        for (size_t family : { 0u, 1u, 2u, 3u, 5u })
            if ((required->slots[family] & ~admitted->slots[family]).any())
                return false;
        return true;
    }
} // namespace OpenRCT2::Ui::Gpu
