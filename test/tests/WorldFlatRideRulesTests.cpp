// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <array>
#include <gtest/gtest.h>
#include <memory>
#include <openrct2-renderer/gpu/GpuWorldFlatRideCatalog.h>
#include <openrct2/paint/Paint.h>
#include <openrct2/ride/TrackPaint.h>
#include <openrct2/ride/ted/TED.FlatRide.h>

namespace G = OpenRCT2::Ui::Gpu;
namespace F = G::FlatRideRules;

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
