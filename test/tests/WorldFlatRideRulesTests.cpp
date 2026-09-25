// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <array>
#include <gtest/gtest.h>
#include <memory>
#include <openrct2-renderer/gpu/GpuWorldFlatRideCatalog.h>
#include <openrct2/paint/Paint.h>
#include <openrct2/ride/TrackPaint.h>
#include <openrct2/ride/ted/TED.FlatRide.h>
#include <openrct2/world/MapLimits.h>
#include <openrct2/world/tile_element/TileElementBase.h>

namespace G = OpenRCT2::Ui::Gpu;
namespace F = G::FlatRideRules;

TEST(WorldFlatRideRulesTest, SwingingShipComponentsRetainTileLocalBoundsAndAnimationOwnership)
{
    for (int direction = 0; direction < 4; ++direction)
        for (int sequence = 0; sequence < 5; ++sequence)
        {
            const auto parts = F::worldFlatParts(13, sequence, direction, true, false, 15, 128, 1, 1);
            int components = 0;
            for (int i = 0; i < parts.count; ++i)
            {
                const auto& part = parts.parts[i];
                if (part.depthAnchor != 3)
                    continue;
                // Original SwingingShipData owns a 31x16 strip, transposed for
                // odd directions. Preserve the back frame -> hull -> front frame.
                const auto contact = F::worldFlatAuthoredContact(part);
                EXPECT_EQ(contact.x, (direction & 1) ? 23 : 31);
                EXPECT_EQ(contact.y, (direction & 1) ? 31 : 23);
                EXPECT_EQ(part.bz, 7);
                EXPECT_EQ(part.child, components == 0 ? 0 : 1);
                for (int frame : { 0, 1, 8, 248, 255 })
                {
                    auto pose = F::worldFlatEmptyPose();
                    pose.present = pose.onTrack = 1;
                    pose.frame = frame;
                    const auto animated = F::worldFlatAnimatePart(part, 13, direction, 0, pose);
                    EXPECT_EQ(animated.depthAnchor, part.depthAnchor);
                    EXPECT_EQ(animated.child, part.child);
                    EXPECT_EQ(animated.x, part.x);
                    EXPECT_EQ(animated.y, part.y);
                    EXPECT_EQ(animated.bz, part.bz);
                }
                ++components;
            }
            EXPECT_EQ(components, 3);
            // The formerly broken front platform must remain below the local
            // mechanical group; its raster height must not move the whole ship.
            const auto& frame = parts.parts[parts.count - 3];
            const auto contact = F::worldFlatAuthoredContact(frame);
            for (int i = 0; i < parts.count - 3; ++i)
            {
                const auto& platform = parts.parts[i];
                if (platform.z == 9)
                    EXPECT_GT(contact.x + contact.y + frame.bz, platform.x + platform.y + platform.z);
            }
        }
}

TEST(WorldFlatRideRulesTest, LongitudinalComponentColumnsMatchOriginalTileVisitationInEveryRotation)
{
    // Independent replay of PaintSessionGenerateRotate's two tile visits per
    // diagonal. This tests ownership against the frozen painter traversal, not
    // just that the new clip helper returns its own expected arithmetic.
    for (int rotation = 0; rotation < 4; ++rotation)
    {
        CoordsXY tile{ 64, 96 };
        CoordsXY facing = tile;
        if (rotation == 1 || rotation == 2)
            facing.x += 32;
        if (rotation == 2 || rotation == 3)
            facing.y += 32;
        const auto projected = facing.rotate(rotation);
        const int left = F::worldFlatLongitudinalColumn(projected.y - projected.x) * 32;
        int accepted = 0;
        for (int column = -512; column <= 512; column += 32)
        {
            bool visited = false;
            for (int row = -512; row <= 512; row += 32)
            {
                auto point = CoordsXY{ row - column / 2, row + column / 2 }.rotate(DirectionFlipXAxis(rotation));
                if (rotation & 1)
                    point.y -= 16;
                point = point.toTileStart();
                const auto adjacent = point + CoordsXY{ 0, 32 }.rotate(DirectionFlipXAxis(rotation));
                visited |= point == tile || adjacent == tile;
            }
            EXPECT_EQ(visited, column >= left && column < left + 64) << rotation << ',' << column;
            accepted += visited;
        }
        EXPECT_EQ(accepted, 2);
        // The interval remains contiguous at both enlarged and minified zooms.
        for (int zoom = -2; zoom <= 3; ++zoom)
        {
            const auto zoomed = [zoom](int value) { return zoom < 0 ? value * (1 << -zoom) : value >> zoom; };
            EXPECT_EQ(zoomed(left + 64) - zoomed(left), zoomed(64));
        }
    }
}

TEST(WorldFlatRideRulesTest, SwingingShipLocalComponentsRespectBothSidesOfTheEverythingParkEntrances)
{
    // Ride22: track world tiles(102,234..238), Z320; entrance(103,235),
    // exit(103,237). Opposite views require opposite overlap despite equal
    // centre depth: one unrestricted common body scalar cannot satisfy both.
    const auto facing = [](int tileX, int tileY, int rotation) {
        CoordsXY point{ tileX * 32, tileY * 32 };
        if (rotation == 1 || rotation == 2)
            point.x += 32;
        if (rotation == 2 || rotation == 3)
            point.y += 32;
        return point.rotate(rotation);
    };
    for (int rotation = 0; rotation < 4; ++rotation)
        for (int entranceY : { 235, 237 })
        {
            const auto booth = facing(103, entranceY, rotation);
            const int sampleScreenX = booth.y - booth.x;
            const int boothDepth = booth.x + booth.y + 29 + 29 + 320;
            const auto parts = F::worldFlatParts(13, 0, (1 + rotation) & 3, true, false, 15, 128, 1, 1);
            const auto& frame = parts.parts[parts.count - 3];
            ASSERT_EQ(frame.depthAnchor, 3);
            const auto contact = F::worldFlatAuthoredContact(frame);
            int nearestBody = INT32_MIN;
            for (int tileY = 234; tileY <= 238; ++tileY)
            {
                const auto tile = facing(102, tileY, rotation);
                const int left = F::worldFlatLongitudinalColumn(tile.y - tile.x) * 32;
                if (sampleScreenX >= left && sampleScreenX < left + 64)
                    nearestBody = std::max(nearestBody, tile.x + tile.y + contact.x + contact.y + 320 + frame.bz);
            }
            ASSERT_NE(nearestBody, INT32_MIN);
            EXPECT_EQ(nearestBody > boothDepth, rotation == 1 || rotation == 2) << rotation << ',' << entranceY;
        }
}

TEST(WorldFlatRideRulesTest, SwingingShipForegroundFenceKeepsItsOwnEdgeAheadOfTheHull)
{
    for (int direction = 0; direction < 4; ++direction)
        for (int sequence = 0; sequence < 5; ++sequence)
            for (int fences = 0; fences < 16; ++fences)
            {
                const auto parts = F::worldFlatParts(13, sequence, direction, true, false, fences, 128, 1, 1);
                const auto& body = parts.parts[parts.count - 3];
                const auto bodyContact = F::worldFlatAuthoredContact(body);
                const int bodyDepth = bodyContact.x + bodyContact.y + body.bz;
                for (int i = 0; i < parts.count - 3; ++i)
                {
                    const auto& fence = parts.parts[i];
                    if (fence.depthAnchor != F::WORLD_FLAT_FOREGROUND_ANCHOR)
                        continue;
                    const auto contact = F::worldFlatAuthoredContact(fence);
                    EXPECT_EQ(fence.child, 0);
                    EXPECT_EQ(fence.bz, 11);
                    if (fence.sx + fence.sy == 33)
                    {
                        // Full near edge spans the corridor. Its old raster
                        // origin loses to the hull; the authored endpoint wins.
                        EXPECT_LT(fence.x + fence.y + fence.z, bodyDepth);
                        EXPECT_GT(contact.x + contact.y + fence.bz, bodyDepth);
                        EXPECT_LT(contact.x + contact.y + fence.bz, 32 + 29 + 29);
                    }
                    else
                    {
                        // Entrance-opening caps keep their actual short extent.
                        // The cap at the far end must not borrow the near corner.
                        EXPECT_EQ(fence.sx + fence.sy, 9);
                        EXPECT_EQ(contact.x + contact.y + fence.bz > bodyDepth, fence.x + fence.y > 31);
                    }
                }
            }
}

TEST(WorldFlatRideRulesTest, WholeBodiesAnchorAtNearestAuthoritativeFootprintTile)
{
    using namespace OpenRCT2::TrackMetadata;
    for (const auto [family, descriptor] :
         { std::pair{ 1, &kTEDFlatTrack3x3 }, std::pair{ 5, &kTEDFlatTrack2x2 }, std::pair{ 12, &kTEDFlatTrack4x4 } })
    {
        const int size = F::worldFlatSize(family);
        int nearestX = INT32_MIN, nearestY = INT32_MIN;
        for (int s = 0; s < size; ++s)
        {
            nearestX = std::max(nearestX, int(descriptor->sequenceData.sequences[s].clearance.x));
            nearestY = std::max(nearestY, int(descriptor->sequenceData.sequences[s].clearance.y));
        }
        for (int direction = 0; direction < 4; ++direction)
            for (int sequence = 0; sequence < size; ++sequence)
            {
                const auto& tile = descriptor->sequenceData.sequences[F::worldFlatSequence(family, sequence, direction)]
                                       .clearance;
                const auto anchor = F::worldFlatFrontAnchor(family, sequence, direction);
                EXPECT_EQ(tile.x + anchor.x, nearestX);
                EXPECT_EQ(tile.y + anchor.y, nearestY);
            }
    }
    // Original whole-body recipes opt in explicitly; flooring is still tile-local.
    for (int family : { 1, 2, 3, 4, 8, 11, 12, 16, 17 })
        for (int direction = 0; direction < 4; ++direction)
        {
            int bodies = 0;
            for (int sequence = 0; sequence < F::worldFlatSize(family); ++sequence)
            {
                const auto parts = F::worldFlatParts(family, sequence, direction, true, false, 15, 128, 1, 4);
                for (int i = 0; i < parts.count; ++i)
                {
                    bodies += parts.parts[i].depthAnchor == 1;
                    if (parts.parts[i].bank == 0 && parts.parts[i].image >= 22134 && parts.parts[i].image <= 22137)
                        EXPECT_EQ(parts.parts[i].depthAnchor, 0);
                }
            }
            EXPECT_GT(bodies, 0) << family << ',' << direction;
        }
}

TEST(WorldFlatRideRulesTest, PlatformFencesKeepIndependentPhysicalEdgesAroundWholeBody)
{
    // Carousel's four named platform edges must not collapse onto the floor's
    // raster origin. Its near fences sit in front of the nearest body tile;
    // far fences stay behind. Image offsets remain unchanged.
    for (int direction = 0; direction < 4; ++direction)
    {
        int checked = 0;
        for (int sequence = 0; sequence < 9; ++sequence)
        {
            const auto tile = OpenRCT2::TrackMetadata::kTEDFlatTrack3x3.sequenceData
                                  .sequences[F::worldFlatSequence(8, sequence, direction)]
                                  .clearance;
            const auto parts = F::worldFlatParts(8, sequence, direction, true, false, 15, 128, 1, 1);
            for (int i = 0; i < parts.count; ++i)
            {
                const auto& p = parts.parts[i];
                if (p.image < 22138 || p.image > 22141 || p.bank != 0)
                    continue;
                EXPECT_EQ(p.depthAnchor, 2);
                EXPECT_EQ(p.x, 0);
                EXPECT_EQ(p.y, 0);
                const int depth = tile.x + tile.y + p.bx + p.by + p.bz;
                if ((p.image == 22139 && tile.y == 32 && tile.x == 32) || (p.image == 22140 && tile.x == 32 && tile.y == 32))
                    EXPECT_GT(depth, 32 + 32 + 7); // Nearest whole-body anchor.
                if (p.image == 22138 || p.image == 22141)
                    EXPECT_LT(depth, 32 + 32 + 7);
                ++checked;
            }
        }
        EXPECT_EQ(checked, 12);
    }
}

TEST(WorldFlatRideRulesTest, FootprintSequencesMatchAuthoritativeTrackMaps)
{
    for (int direction = 0; direction < 4; direction++)
    {
        for (int sequence = 0; sequence < 9; sequence++)
            EXPECT_EQ(F::worldFlatSequence(1, sequence, direction), kTrackMap3x3[direction][sequence]);
        for (int sequence = 0; sequence < 16; sequence++)
            EXPECT_EQ(F::worldFlatSequence(12, sequence, direction), kTrackMap4x4[direction][sequence]);
        for (int sequence = 0; sequence < 4; sequence++)
        {
            EXPECT_EQ(F::worldFlatSequence(5, sequence, direction), kTrackMap2x2[direction][sequence]);
            EXPECT_EQ(F::worldFlatSequence(9, sequence, direction), kTrackMap1x4[direction][sequence]);
        }
    }
}
TEST(WorldFlatRideRulesTest, ParkedSeatAndRearBodyFrontOrderRemainPhysical)
{
    const auto spin = F::worldFlatParts(16, 1, 0, true, true, 15, 128, 0, 4);
    ASSERT_EQ(spin.count, 5);
    EXPECT_EQ(spin.parts[0].image, 572);
    EXPECT_EQ(spin.parts[1].image, 380);
    EXPECT_EQ(spin.parts[2].image, 0);
    EXPECT_EQ(spin.parts[2].z, -7); // Original height+3 and parked seat-height offset -10.
    EXPECT_EQ(spin.parts[3].image, 476);
    EXPECT_EQ(spin.parts[4].image, 573);
    EXPECT_EQ(spin.parts[0].child, 0);
    for (int i = 1; i < spin.count; i++)
        EXPECT_EQ(spin.parts[i].child, 1);
    const auto wheel = F::worldFlatParts(9, 0, 0, true, true, 15, 128, 0, 4);
    ASSERT_EQ(wheel.count, 3);
    EXPECT_EQ(wheel.parts[0].image, 22150);
    EXPECT_EQ(wheel.parts[1].bank, 1);
    EXPECT_EQ(wheel.parts[1].image, 0);
    EXPECT_EQ(wheel.parts[2].image, 22151);
    const auto carpet = F::worldFlatParts(15, 0, 0, true, true, 15, 128, 0, 4);
    ASSERT_EQ(carpet.count, 5);
    EXPECT_EQ(carpet.parts[2].z, 5); // Original height+7 and parked gondola offset -2.
}
TEST(WorldFlatRideRulesTest, EveryFamilyHasBoundedBodiesAndRejectedSequencesStayEmpty)
{
    for (int family = 1; family <= 19; family++)
    {
        SCOPED_TRACE(family);
        bool body = false;
        for (int direction = 0; direction < 4; direction++)
            for (int sequence = 0; sequence < F::worldFlatSize(family); sequence++)
                for (int mask = 0; mask < 16; mask++)
                {
                    const auto parts = F::worldFlatParts(family, sequence, direction, true, false, mask, 128, 0, 4);
                    ASSERT_GE(parts.count, 0);
                    ASSERT_LE(parts.count, F::WORLD_FLAT_PART_CAPACITY);
                    for (int i = 0; i < parts.count; i++)
                    {
                        body |= parts.parts[i].bank == 1;
                        EXPECT_GE(parts.parts[i].image, 0);
                    }
                }
        EXPECT_TRUE(body || family == 6 || family == 7);
        EXPECT_EQ(F::worldFlatParts(family, F::worldFlatSize(family), 0, true, false, 15, 128, 0, 4).count, 0);
    }
}
TEST(WorldFlatRideCatalogTest, SparseOwnedArtUsageAndStationRangesAreValidated)
{
    auto objects = std::make_unique<OpenRCT2::WorldObjectPresentationMaterials>();
    objects->rideObjects[0] = { 50000, 32, 50003, true };
    OpenRCT2::WorldRidePresentationMaterials rides;
    rides.rides.resize(2);
    for (auto& ride : rides.rides)
    {
        ride.present = true;
        ride.objectSlot = 0;
        ride.regularStyle = static_cast<uint16_t>(TrackStyle::_3DCinema);
    }
    rides.rides[0].stations.resize(7); // Beyond the obsolete four-station limit.
    rides.rides[0].stations[6].entranceValid = true;
    rides.rides[0].stations[6].entranceX = 300;
    rides.rides[0].stations[6].entranceY = 301;
    OpenRCT2::WorldObjectPresentationUsage usage;
    std::vector<uint32_t> images;
    const auto append = [&](uint32_t image) {
        images.push_back(image);
        return static_cast<uint32_t>(images.size() - 1);
    };
    const auto empty = G::BuildWorldFlatRideCatalog(*objects, rides, &usage, 10, append);
    EXPECT_TRUE(images.empty());
    G::ValidateWorldFlatRideCatalog(empty.words, 0);
    usage.slots[4].set(0);
    const auto catalogue = G::BuildWorldFlatRideCatalog(*objects, rides, &usage, 10, append);
    G::ValidateWorldFlatRideCatalog(catalogue.words, images.size());
    EXPECT_EQ(catalogue.words[8 + G::kWorldFlatRideWords], 0u);
    EXPECT_EQ(catalogue.words[8 + 17], 7u);
    const auto station = catalogue.words[8 + 16] + 6 * 5;
    EXPECT_EQ(catalogue.words[station + 1], 300u);
    EXPECT_EQ(catalogue.words[station + 2], 301u);
    std::vector<uint32_t> objectImages;
    for (auto image : images)
        if (image >= 50000)
            objectImages.push_back(image);
    EXPECT_EQ(objectImages, (std::vector<uint32_t>{ 50003, 50004, 50005, 50006 }));
    auto invalid = catalogue.words;
    invalid[8 + 16] = UINT32_MAX;
    EXPECT_THROW(G::ValidateWorldFlatRideCatalog(invalid, images.size()), std::invalid_argument);
    invalid = catalogue.words;
    invalid[invalid[4] + 1] = static_cast<uint32_t>(images.size());
    EXPECT_THROW(G::ValidateWorldFlatRideCatalog(invalid, images.size()), std::invalid_argument);
    objects->rideObjects[0].imageCount = 4;
    images.clear();
    EXPECT_THROW(static_cast<void>(G::BuildWorldFlatRideCatalog(*objects, rides, &usage, 10, append)), std::runtime_error);
    EXPECT_TRUE(images.empty()); // Validate all owned references before invoking the atlas callback.
}

TEST(WorldFlatRideRulesTest, TowerCapsAndMazeWallTopologyUseRawState)
{
    for (int family : { 20, 21, 22 })
    {
        const auto open = F::worldTowerParts(family, 0, 0, true, true, false, 15);
        const auto covered = F::worldTowerParts(family, 0, 0, true, false, false, 15);
        ASSERT_EQ(open.count, 2);
        ASSERT_EQ(covered.count, 1);
        EXPECT_EQ(open.parts[1].image, open.parts[0].image + 1);
        EXPECT_EQ(open.parts[1].child, 1);
        EXPECT_EQ(F::worldTowerParts(family, 1, 0, true, true, false, 15).count, 0);
        for (int direction = 0; direction < 4; direction++)
        {
            const auto base = F::worldTowerParts(family, 0, direction, false, false, true, 0);
            ASSERT_EQ(base.count, 3);
            EXPECT_EQ(base.parts[1].z, 32);
            EXPECT_EQ(base.parts[2].z, 64);
            EXPECT_EQ(base.parts[0].image, family == 20 ? 14986 : family == 21 ? 14564 : 14560 + 2 * (direction & 1));
        }
    }
    // A single quadrant wall rotates from top-left through the four source quadrants.
    for (int direction = 0; direction < 4; direction++)
        for (int quadrant = 0; quadrant < 4; quadrant++)
            EXPECT_EQ(F::worldMazePart(quadrant + 1, 1 << 3, direction, 0).image, quadrant == direction ? 21951 : -1);
    EXPECT_EQ(F::worldMazePart(0, 0, 0, 0).image, 2485);
    for (int i = 1; i < 26; i++)
        EXPECT_EQ(F::worldMazePart(i, 0, 0, 0).image, -1);
    const auto centre = F::worldMazePart(25, 1 << 6, 0, 2);
    EXPECT_EQ(centre.image, 21971);
    EXPECT_EQ(centre.sz, 8);
    EXPECT_EQ(F::worldMazePart(25, 1 << 3, 0, 2).image, -1);
}
TEST(WorldFlatRideCatalogTest, TowerAndMazeUseOnlyStaticG1ArtAndEntranceOnlyUsageDoesNotResolveBodies)
{
    auto objects = std::make_unique<OpenRCT2::WorldObjectPresentationMaterials>();
    OpenRCT2::WorldRidePresentationMaterials rides;
    rides.rides.resize(4);
    const TrackStyle styles[] = { TrackStyle::observationTower, TrackStyle::launchedFreefall, TrackStyle::rotoDrop,
                                  TrackStyle::maze };
    OpenRCT2::WorldObjectPresentationUsage usage;
    for (int i = 0; i < 4; i++)
    {
        rides.rides[i].present = true;
        rides.rides[i].regularStyle = static_cast<uint16_t>(styles[i]);
        usage.slots[6].set(i);
    }
    std::vector<uint32_t> images;
    const auto append = [&](uint32_t image) {
        images.push_back(image);
        return static_cast<uint32_t>(images.size() - 1);
    };
    const auto empty = G::BuildWorldFlatRideCatalog(*objects, rides, &usage, 10, append);
    EXPECT_TRUE(images.empty());
    G::ValidateWorldFlatRideCatalog(empty.words, 0);
    for (int i = 0; i < 4; i++)
        usage.slots[4].set(i);
    const auto catalogue = G::BuildWorldFlatRideCatalog(*objects, rides, &usage, 10, append);
    G::ValidateWorldFlatRideCatalog(catalogue.words, images.size());
    for (const uint32_t image : { 14559u, 14566u, 14988u, 2485u, 21938u, 21989u })
        EXPECT_NE(std::find(images.begin(), images.end(), image), images.end());
    EXPECT_EQ(catalogue.words[8 + 3], static_cast<uint32_t>(OpenRCT2::TrackElemType::towerSection));
    EXPECT_EQ(catalogue.words[8 + 3 * G::kWorldFlatRideWords + 2], static_cast<uint32_t>(OpenRCT2::TrackElemType::maze));
}

TEST(WorldFlatRideRulesTest, ExhaustiveReachableVariantsFitEightPartsAndOverflowIsRejected)
{
    int maximum = 0;
    for (int family = 1; family <= 24; family++)
        for (int direction = 0; direction < 4; direction++)
            for (int sequence = 0; sequence < F::worldFlatSize(family); sequence++)
                for (bool stationPresent : { false, true })
                    for (bool noPlatforms : { false, true })
                        for (int fenceMask = 0; fenceMask < 16; fenceMask++)
                            // Only Space Rings branches on train/station counts; counts >4
                            // have the same visible-body predicate as4, station counts >1 as1.
                            for (int stationCount = 0; stationCount <= (family == 10 ? 1 : 0); stationCount++)
                                for (int trains = 0; trains <= (family == 10 ? 4 : 0); trains++)
                                {
                                    const auto parts = family >= 20
                                        ? F::worldTowerParts(family, sequence, direction, false, false, noPlatforms, fenceMask)
                                        : F::worldFlatParts(
                                              family, sequence, direction, stationPresent, noPlatforms, fenceMask, 128,
                                              stationCount, trains);
                                    ASSERT_LE(parts.count, F::WORLD_FLAT_PART_CAPACITY);
                                    maximum = std::max(maximum, parts.count);
                                }
    EXPECT_EQ(maximum, 8); // Top Spin and Swinging Ship reach the bound.
    for (int family : { 20, 21, 22, 24 })
        for (int direction = 0; direction < 4; direction++)
            for (int sequence = 0; sequence < 9; sequence++)
                for (bool cap : { false, true })
                    ASSERT_LE(
                        F::worldTowerParts(family, sequence, direction, true, cap, false, 15).count,
                        F::WORLD_FLAT_PART_CAPACITY);
    F::WorldFlatParts full{};
    full.count = F::WORLD_FLAT_PART_CAPACITY;
    EXPECT_THROW(F::worldFlatAdd(full, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0), std::overflow_error);
    EXPECT_EQ(full.count, F::WORLD_FLAT_PART_CAPACITY);
}

TEST(WorldFlatRideRulesTest, EveryMazeWallMaskMatchesLegacyParentArrangement)
{
    struct RestoreSort
    {
        bool previous = gPaintStableSort;
        ~RestoreSort()
        {
            gPaintStableSort = previous;
        }
    } restore;
    gPaintStableSort = false;
    auto session = std::make_unique<PaintSessionCore>();
    std::array<PaintStruct, 26> entries{};
    constexpr CoordsXY tile{ 640, 672 };
    constexpr int baseZ = 64;
    for (int rotation = 0; rotation < 4; rotation++)
        for (int mask = 0; mask <= 65535; mask++)
        {
            session->CurrentRotation = static_cast<uint8_t>(rotation);
            session->QuadrantBackIndex = UINT32_MAX;
            session->QuadrantFrontIndex = 0;
            session->PaintHead = nullptr;
            int count = 0;
            // The bit-mask enumeration covers every rotation of raw mazeEntry as
            // well; the independent camera rotation still changes paint bounds.
            for (int i = 0; i < 26; i++)
            {
                const auto p = F::worldMazePart(i, mask, rotation, 0);
                if (p.image < 0)
                    continue;
                auto origin = CoordsXY{ p.bx, p.by }.rotate((rotation * 3) & 3) + tile;
                auto size = CoordsXY{ p.sx, p.sy };
                if (rotation == 0 || rotation == 1)
                    --size.x;
                if (rotation == 0 || rotation == 3)
                    --size.y;
                size = size.rotate((rotation * 3) & 3);
                auto& entry = entries[i];
                entry = {};
                entry.Bounds = { origin.x, origin.y, baseZ + p.bz, origin.x + size.x, origin.y + size.y, baseZ + p.bz + p.sz };
                constexpr int range = MaxPaintQuadrants * 32;
                int hash = rotation == 0 ? origin.x + origin.y
                    : rotation == 1      ? origin.y - origin.x + range / 2
                    : rotation == 2      ? -origin.y - origin.x + range
                                         : origin.x - origin.y + range / 2;
                const auto q = static_cast<uint32_t>(std::clamp(hash / 32, 0, MaxPaintQuadrants - 1));
                entry.QuadrantIndex = static_cast<uint16_t>(q);
                entry.NextQuadrantEntry = session->Quadrants[q];
                session->Quadrants[q] = &entry;
                session->QuadrantBackIndex = std::min(session->QuadrantBackIndex, q);
                session->QuadrantFrontIndex = std::max(session->QuadrantFrontIndex, q);
                count++;
            }
            PaintSessionArrange(*session);
            const auto actual = F::worldMazeOrder(mask, rotation, rotation);
            ASSERT_EQ(actual.count, count) << "mask=" << mask << " rotation=" << rotation;
            auto* parent = session->PaintHead;
            for (int i = 0; i < count; i++)
            {
                ASSERT_NE(parent, nullptr);
                ASSERT_EQ(actual.indices[i], parent - entries.data()) << "mask=" << mask << " rotation=" << rotation;
                parent = parent->NextQuadrantEntry;
            }
            ASSERT_EQ(parent, nullptr);
            // Only two populated quadrants occur for these fixed maze bounds.
            // Clear those instead of reallocating the full PaintSession each mask.
            for (auto q = session->QuadrantBackIndex; q <= session->QuadrantFrontIndex; q++)
                session->Quadrants[q] = nullptr;
        }
}

TEST(WorldFlatRideAnimationTest, SimulationFramesSelectOriginalBodySequencesAndStoppedState)
{
    auto pose = F::worldFlatEmptyPose();
    pose.present = pose.onTrack = 1;
    pose.frame = 13;
    auto wheel = F::worldFlatParts(9, 0, 3, true, true, 0, 128, 1, 1).parts[1];
    EXPECT_EQ(F::worldFlatAnimatePart(wheel, 9, 3, 0, pose).image, 29);
    auto carousel = F::worldFlatParts(8, 1, 0, true, true, 0, 128, 1, 1).parts[0];
    pose.orientation = 16;
    EXPECT_EQ(F::worldFlatAnimatePart(carousel, 8, 0, 3, pose).image, 13);
    pose.breakdownFlags = 4;
    pose.breakdownReason = 7;
    pose.breakdownModifier = 128;
    pose.currentTime = 8;
    EXPECT_EQ(F::worldFlatAnimatePart(carousel, 8, 0, 0, pose).z, carousel.z + 4);
    auto enterprise = F::worldFlatParts(12, 0, 0, true, true, 0, 128, 1, 1).parts[0];
    EXPECT_EQ(F::worldFlatAnimatePart(enterprise, 12, 0, 1, pose).image, 55);
    auto rings = F::worldFlatParts(10, 0, 0, true, true, 0, 128, 1, 4).parts[0];
    EXPECT_EQ(F::worldFlatAnimatePart(rings, 10, 0, 0, pose).image, 52);
    auto twist = F::worldFlatParts(11, 1, 0, true, true, 0, 128, 1, 1).parts[0];
    EXPECT_EQ(F::worldFlatAnimatePart(twist, 11, 0, 0, pose).image, 21); // (13 + 2*16)%24
    pose.onTrack = 0;
    EXPECT_EQ(F::worldFlatAnimatePart(enterprise, 12, 0, 1, pose).image, enterprise.image);
    EXPECT_EQ(F::worldFlatAnimatePart(carousel, 8, 0, 1, pose).z, carousel.z);
    // Original Ferris/carousel body readers still use an existing vehicle when onTrack is clear.
    EXPECT_EQ(F::worldFlatAnimatePart(wheel, 9, 3, 0, pose).image, 29);
}

TEST(WorldFlatRideAnimationTest, SignedSwingRestraintsAndMechanicalOffsetsMatchPainterConstants)
{
    auto pose = F::worldFlatEmptyPose();
    pose.present = pose.onTrack = 1;
    pose.frame = 253; // signed -3, not unsigned frame253.
    auto ship = F::worldFlatParts(13, 0, 0, true, true, 0, 128, 1, 1).parts[1];
    EXPECT_EQ(F::worldFlatAnimatePart(ship, 13, 0, 0, pose).image, 216);
    EXPECT_EQ(F::worldFlatAnimatePart(ship, 13, 2, 0, pose).image, 54);
    auto inverter = F::worldFlatParts(14, 0, 0, true, true, 0, 128, 1, 1).parts[1];
    EXPECT_EQ(F::worldFlatAnimatePart(inverter, 14, 0, 0, pose).image, 168);
    pose.frame = 8;
    auto carpet = F::worldFlatParts(15, 0, 0, true, true, 0, 128, 1, 1);
    EXPECT_EQ(F::worldFlatAnimatePart(carpet.parts[1], 15, 0, 0, pose).image, 22014);
    auto seat = F::worldFlatAnimatePart(carpet.parts[2], 15, 0, 0, pose);
    EXPECT_EQ(seat.x, carpet.parts[2].x - 32);
    EXPECT_EQ(seat.z, 44); // base height+7+oscillation37.
    pose.frame = 12;
    pose.secondary = 5;
    auto spin = F::worldFlatParts(16, 1, 0, true, true, 0, 128, 1, 1);
    seat = F::worldFlatAnimatePart(spin.parts[2], 16, 0, 0, pose);
    EXPECT_EQ(seat.image, 5);
    EXPECT_EQ(seat.x, spin.parts[2].x - 34);
    EXPECT_EQ(seat.z, 34); // base height+3+31.
    pose.restraints = 255;
    EXPECT_EQ(F::worldFlatAnimatePart(spin.parts[2], 16, 0, 0, pose).image, 66);
    EXPECT_EQ(F::worldFlatAnimatePart(spin.parts[1], 16, 2, 0, pose).image, 416);
    auto simulator = F::worldFlatParts(17, 1, 0, true, true, 0, 128, 1, 1).parts[0];
    EXPECT_EQ(F::worldFlatAnimatePart(simulator, 17, 0, 0, pose).image, 12);
    pose.restraints = 0;
    EXPECT_EQ(F::worldFlatAnimatePart(simulator, 17, 0, 0, pose).image, 48);
}

TEST(WorldFlatRideAnimationTest, HauntedAndSlideOverlaysRespectAuthoritativeProgressAndZoom)
{
    auto pose = F::worldFlatEmptyPose();
    pose.present = pose.onTrack = 1;
    pose.frame = 18;
    auto haunted = F::worldFlatParts(4, 3, 0, true, true, 0, 128, 1, 1).parts[0];
    EXPECT_EQ(F::worldFlatAnimationOverlay(haunted, 4, 0, 0, pose).image, 21);
    EXPECT_EQ(F::worldFlatAnimationOverlay(haunted, 4, 0, 1, pose).image, -1);
    pose.onTrack = 0;
    EXPECT_EQ(F::worldFlatAnimationOverlay(haunted, 4, 0, 0, pose).image, -1);
    pose.slideInUse = 1;
    pose.slideProgress = 46;
    const auto slide = F::worldFlatParts(5, 3, 0, true, true, 0, 128, 1, 1).parts[0];
    EXPECT_EQ(F::worldFlatAnimationOverlay(slide, 5, 0, 0, pose).image, 65);
    pose.slideProgress = 47;
    EXPECT_EQ(F::worldFlatAnimationOverlay(slide, 5, 0, 0, pose).image, 65);
    pose.slideProgress = 48;
    EXPECT_EQ(F::worldFlatAnimationOverlay(slide, 5, 0, 0, pose).image, -1);
}

TEST(WorldFlatRideCatalogTest, CompleteMechanismBodySequencesAreResidentBeforeAnyPoseArrives)
{
    auto objects = std::make_unique<OpenRCT2::WorldObjectPresentationMaterials>();
    auto& object = objects->rideObjects[0];
    object.present = true;
    object.imageBase = object.carBaseImage = 50000;
    object.imageCount = 576;
    OpenRCT2::WorldRidePresentationMaterials rides;
    rides.rides.resize(1);
    rides.rides[0].present = true;
    rides.rides[0].objectSlot = 0;
    rides.rides[0].regularStyle = static_cast<uint16_t>(TrackStyle::topSpin);
    std::vector<uint32_t> images;
    const auto catalogue = G::BuildWorldFlatRideCatalog(*objects, rides, nullptr, 0, [&](uint32_t image) {
        images.push_back(image);
        return static_cast<uint32_t>(images.size() - 1);
    });
    G::ValidateWorldFlatRideCatalog(catalogue.words, images.size());
    for (const auto image : { 50000u, 50075u, 50380u, 50475u, 50476u, 50575u })
        EXPECT_NE(std::find(images.begin(), images.end(), image), images.end());
    EXPECT_EQ(std::find(images.begin(), images.end(), 50076u), images.end()); // Rider overlay is a distinct stream.
    object.imageCount = 575;
    EXPECT_THROW(
        static_cast<void>(G::BuildWorldFlatRideCatalog(*objects, rides, nullptr, 0, [](uint32_t) { return 0u; })),
        std::runtime_error);
}

TEST(WorldFlatRideRulesTest, LiftCageKeepsSeparateBackFrontParentsAndAllRotationsResident)
{
    EXPECT_EQ(G::WorldFlatRideFamily(TrackStyle::lift), 24u);
    EXPECT_EQ(G::WorldFlatRideTrackType(24), OpenRCT2::TrackElemType::towerBase);
    for (int direction = 0; direction < 4; ++direction)
    {
        const auto base = F::worldTowerParts(24, 0, direction, false, false, true, 15);
        ASSERT_EQ(base.count, 6);
        for (int tier = 0; tier < 3; ++tier)
            for (int side = 0; side < 2; ++side)
            {
                const auto& part = base.parts[tier * 2 + side];
                EXPECT_EQ(part.image, (tier == 0 ? 14996 + direction * 2 : 14994) + side);
                EXPECT_EQ(part.bx, side == 0 ? 2 : 28);
                EXPECT_EQ(part.by, part.bx);
                EXPECT_EQ(part.z, tier * 32);
                EXPECT_EQ(part.bz, part.z);
                EXPECT_EQ(part.child, 0);
            }
        const auto section = F::worldTowerParts(24, 0, direction, true, true, false, 15);
        ASSERT_EQ(section.count, 2);
        EXPECT_EQ(section.parts[0].image, 14994);
        EXPECT_EQ(section.parts[1].image, 14995);
        EXPECT_EQ(F::worldTowerParts(24, 1, direction, true, true, false, 15).count, 0);
        for (int sequence = 1; sequence < 9; ++sequence)
        {
            const auto platform = F::worldTowerParts(24, sequence, direction, false, false, true, 0);
            ASSERT_EQ(platform.count, 1);
            EXPECT_EQ(platform.parts[0].image, 14989);
        }
    }
    auto objects = std::make_unique<OpenRCT2::WorldObjectPresentationMaterials>();
    OpenRCT2::WorldRidePresentationMaterials rides;
    rides.rides.resize(1);
    rides.rides[0].present = true;
    rides.rides[0].regularStyle = static_cast<uint16_t>(TrackStyle::lift);
    std::vector<uint32_t> images;
    const auto catalogue = G::BuildWorldFlatRideCatalog(*objects, rides, nullptr, 0, [&](uint32_t image) {
        images.push_back(image);
        return static_cast<uint32_t>(images.size() - 1);
    });
    G::ValidateWorldFlatRideCatalog(catalogue.words, images.size());
    EXPECT_EQ(catalogue.words[8 + 3], static_cast<uint32_t>(OpenRCT2::TrackElemType::towerSection));
    for (uint32_t image = 14989; image <= 15003; ++image)
        EXPECT_NE(std::find(images.begin(), images.end(), image), images.end()) << image;
}

TEST(WorldFlatRideRulesTest, TowerShaftTiersStayStrictlyBetweenRearAndFrontThroughLegalHeightDomain)
{
    // TileElementBase stores world height in uint8 units of kCoordsZStep.
    // All tiers retain one world contact: only their bounded fine layer changes.
    constexpr int maximumZ = OpenRCT2::kMaxTileElementHeight * kCoordsZStep;
    static_assert(maximumZ / F::WORLD_TOWER_TIER_HEIGHT + 1 == F::WORLD_TOWER_MAX_TIERS);
    int previousCap = F::WORLD_TOWER_REAR;
    for (int z = 0; z <= maximumZ; z += F::WORLD_TOWER_TIER_HEIGHT)
    {
        const auto order = F::worldTowerShaftOrder(z);
        EXPECT_GT(order.layer, previousCap) << z;
        EXPECT_LE(order.layer + 1, F::WORLD_COMPONENT_LAYER_MAX);
        EXPECT_LT(order.layer + 1, F::WORLD_TOWER_FRONT) << z;
        previousCap = order.layer + 1;
    }
    EXPECT_EQ(previousCap + 1, F::WORLD_TOWER_FRONT);
    EXPECT_EQ(F::worldTowerShaftOrder(-32).layer, F::WORLD_TOWER_SHAFT);
    const auto highest = F::worldTowerShaftOrder(maximumZ);
    EXPECT_EQ(F::worldTowerShaftOrder(maximumZ + 64).layer, highest.layer);
    // Original RotoDrop occupancy admits at most13 child sprites (exhaustively
    // covered in WorldVehicleRulesTests). Preserve their front-parent order.
    EXPECT_LE(F::WORLD_TOWER_FRONT + 13, F::WORLD_COMPONENT_LAYER_MAX);
}

TEST(WorldFlatRideRulesTest, TowerShaftOwnsOneStationContactThroughEveryVerticalSegment)
{
    // The original tower has one station at the central base piece, not one
    // station per shaft segment. A moving cabin and every segment share it.
    OpenRCT2::WorldRidePresentationRecord ride;
    ride.stations.resize(1);
    auto& station = ride.stations[0];
    station.startValid = true;
    station.startX = 113 * 32;
    station.startY = 197 * 32;
    station.startZ = 336;
    const G::WorldTowerAssemblyContact contact(ride);
    EXPECT_EQ(contact.PackedXY() & 65535u, 3632u);
    EXPECT_EQ(contact.PackedXY() >> 16, 6320u);
    EXPECT_EQ(contact.PackedHeight(), 0x80000000u | 336u);
    for (int family : { 20, 21, 22 })
        for (int direction = 0; direction < 4; ++direction)
        {
            const auto base = F::worldTowerParts(family, 0, direction, false, false, false, 15);
            int shaftCount = 0;
            for (int i = 0; i < base.count; ++i)
                shaftCount += base.parts[i].depthAnchor == F::WORLD_TOWER_SHAFT_ANCHOR;
            EXPECT_EQ(shaftCount, 3);
            const auto section = F::worldTowerParts(family, 0, direction, true, true, false, 15);
            ASSERT_EQ(section.count, 2);
            for (int i = 0; i < section.count; ++i)
                EXPECT_EQ(section.parts[i].depthAnchor, F::WORLD_TOWER_SHAFT_ANCHOR);
            // This verifies static ownership only. Sequential GPU captures,
            // not repeated constant expressions, qualify moving overlap.
            EXPECT_LT(F::WORLD_TOWER_REAR, F::WORLD_TOWER_SHAFT);
            EXPECT_LT(F::WORLD_TOWER_SHAFT, F::WORLD_TOWER_FRONT);
        }
    station.startZ = 400;
    const G::WorldTowerAssemblyContact rebuilt(ride);
    EXPECT_NE(rebuilt.PackedHeight(), contact.PackedHeight());
    station.startValid = false;
    EXPECT_EQ(G::WorldTowerAssemblyContact(ride).PackedHeight(), 0u);
}
