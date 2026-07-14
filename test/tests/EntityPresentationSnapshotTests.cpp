/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/entity/EntityPresentationSnapshot.h>
#include <openrct2/entity/Guest.h>
#include <openrct2/entity/Litter.h>
#include <openrct2/ride/Vehicle.h>

using namespace OpenRCT2;

class EntityPresentationSnapshotTests : public testing::Test
{
protected:
    std::unique_ptr<IContext> context;

    void SetUp() override
    {
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = true;
        context = CreateContext();
        ASSERT_NE(context, nullptr);
        ASSERT_TRUE(context->Initialise());
        getGameState().entities.ResetAllEntities();
    }

    void TearDown() override
    {
        context.reset();
    }
};

TEST_F(EntityPresentationSnapshotTests, CapturesOnlyRequestedSpatialBucketsAndKeepsOwnedState)
{
    auto& entities = getGameState().entities;
    auto* visible = entities.CreateEntity<Litter>();
    auto* hidden = entities.CreateEntity<Litter>();
    ASSERT_NE(visible, nullptr);
    ASSERT_NE(hidden, nullptr);
    visible->moveTo({ 32, 64, 8 });
    hidden->moveTo({ 320, 640, 16 });
    entities.UpdateEntitiesSpatialIndex();

    const std::array tiles{ CoordsXY{ 32, 64 } };
    const auto snapshot = EntityPresentationSnapshot::Capture(entities, tiles);
    ASSERT_NE(snapshot->TryGetEntity(visible->id), nullptr);
    EXPECT_EQ(snapshot->TryGetEntity(hidden->id), nullptr);
    ASSERT_EQ(snapshot->GetEntityTileList({ 32, 64 }).size(), 1);
    EXPECT_EQ(snapshot->GetEntityTileList({ 32, 64 })[0], visible->id);
    EXPECT_TRUE(snapshot->GetEntityTileList({ 320, 640 }).empty());

    const auto originalLocation = snapshot->TryGetEntity(visible->id)->getLocation();
    visible->moveTo({ 96, 128, 24 });
    entities.UpdateEntitiesSpatialIndex();
    EXPECT_EQ(snapshot->TryGetEntity(visible->id)->getLocation(), originalLocation);
}

TEST_F(EntityPresentationSnapshotTests, CapturesVehicleAndRiderLookupClosureOutsideVisibleBuckets)
{
    auto& entities = getGameState().entities;
    auto* vehicle = entities.CreateEntity<Vehicle>();
    auto* rider = entities.CreateEntity<Guest>();
    ASSERT_NE(vehicle, nullptr);
    ASSERT_NE(rider, nullptr);
    vehicle->peep[0] = rider->id;

    const std::array<CoordsXY, 0> noTiles{};
    const auto snapshot = EntityPresentationSnapshot::Capture(entities, noTiles);
    EXPECT_NE(snapshot->TryGetEntity(vehicle->id), nullptr);
    EXPECT_NE(snapshot->TryGetEntity(rider->id), nullptr);
    EXPECT_EQ(snapshot->GetCapturedEntityCount(), 2);

    EXPECT_EQ(GetCurrentEntityPresentationSnapshot(), nullptr);
    {
        ScopedEntityPresentationSnapshot scope(snapshot.get());
        EXPECT_EQ(GetEntityForPresentation<Vehicle>(vehicle->id), snapshot->TryGetEntity(vehicle->id)->cast<Vehicle>());
        EXPECT_EQ(GetEntityForPresentation<Guest>(rider->id), snapshot->TryGetEntity(rider->id)->cast<Guest>());
    }
    EXPECT_EQ(GetCurrentEntityPresentationSnapshot(), nullptr);
}
