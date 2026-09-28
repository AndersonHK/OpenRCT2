// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include "../../src/openrct2-renderer/gpu/GpuSelectedVehiclePaint.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/PlatformEnvironment.h>
#include <openrct2/SpriteIds.h>
#include <openrct2/config/Config.h>
#include <openrct2/core/JobPool.h>
#include <openrct2/drawing/Colour.h>
#include <openrct2/drawing/PresentationScene.h>
#include <openrct2/drawing/SelectedVehicleSnapshot.h>
#include <openrct2/entity/EntityPresentationSnapshot.h>
#include <openrct2/interface/Viewport.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/ride/Ride.h>
#include <openrct2/ride/RideEntry.h>
#include <openrct2/ride/Vehicle.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapPresentationSnapshot.h>
#include <openrct2/world/tile_element/SurfaceElement.h>
#include <string_view>

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;

namespace
{
    class SelectedVehicleSnapshotTest : public testing::Test
    {
    protected:
        const bool oldHeadless = gOpenRCT2Headless, oldNoGraphics = gOpenRCT2NoGraphics;
        const std::string oldRct2 = Config::Get().general.rct2Path;
        const std::string oldRct1 = Config::Get().general.rct1Path;
        std::unique_ptr<IContext> context;
        void SetUp() override
        {
            const auto* rct2 = std::getenv("OPENRCT2_TEST_RCT2_PATH");
            if (!rct2 || !*rct2)
            {
                const auto* required = std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
                if (required && std::string_view(required) == "1")
                    FAIL() << "Selected-vehicle authoring requires pinned original sprite metadata";
                GTEST_SKIP() << "Set OPENRCT2_TEST_RCT2_PATH for original-art authoring";
            }
            gOpenRCT2Headless = true;
            gOpenRCT2NoGraphics = false;
            context = CreateContext();
            auto& env = context->GetPlatformEnvironment();
            Config::Get().general.rct2Path = rct2;
            env.SetBasePath(DirBase::rct2, rct2);
            if (const auto* rct1 = std::getenv("OPENRCT2_TEST_RCT1_PATH"))
            {
                Config::Get().general.rct1Path = rct1;
                env.SetBasePath(DirBase::rct1, rct1);
            }
            auto data = std::filesystem::current_path() / "data";
            if (const auto* shaders = std::getenv("OPENRCT2_VULKAN_SHADER_DIRECTORY"))
                data = std::filesystem::path(shaders).parent_path().parent_path();
            else if (!std::filesystem::is_regular_file(data / "g2.dat"))
                data = std::filesystem::current_path() / "bin/data";
            env.SetBasePath(DirBase::openrct2, data.string());
            ASSERT_TRUE(context->Initialise());
            gameStateInitAll(getGameState(), { 16, 16 });
        }
        void TearDown() override
        {
            context.reset();
            Config::Get().general.rct2Path = oldRct2;
            Config::Get().general.rct1Path = oldRct1;
            gOpenRCT2Headless = oldHeadless;
            gOpenRCT2NoGraphics = oldNoGraphics;
        }
        Vehicle* Car(CoordsXYZ location, uint8_t frame)
        {
            auto* vehicle = getGameState().entities.createEntity<Vehicle>();
            if (vehicle)
            {
                vehicle->flags.set(VehicleFlag::crashed);
                vehicle->animation_frame = frame;
                vehicle->next_vehicle_on_train = EntityId::GetNull();
                vehicle->spriteData = { 12, 18, 9 };
                vehicle->moveTo(location);
            }
            return vehicle;
        }
        SelectedVehicleRequest Request(const Vehicle& car, uint8_t rotation = 0)
        {
            return { .viewport = 17, .entity = getGameState().entities.GetEntityVisualHandle(car.id), .rotation = rotation };
        }
    };
} // namespace

TEST_F(SelectedVehicleSnapshotTest, OriginalCarRecipesOwnBoundsArtAndStableIdentityAcrossAllRotations)
{
    auto& state = getGameState();
    auto* first = Car({ 96, 128, 48 }, 1);
    auto* second = Car({ 129, 129, 64 }, 2);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    // Deliberately reverse linkage: original spatial buckets still paint stable EntityIds first.
    second->next_vehicle_on_train = first->id;
    state.currentTicks = 600;
    std::array<SelectedVehicleRequest, 4> requests;
    for (uint8_t r = 0; r < 4; ++r)
        requests[r] = Request(*second, r);
    const auto held = CaptureSelectedVehicleSnapshot(requests);
    ASSERT_NE(held, nullptr);
    ASSERT_EQ(held->views.size(), 4u);
    for (uint8_t r = 0; r < 4; ++r)
    {
        SCOPED_TRACE(r);
        const auto& view = held->views[r];
        ASSERT_EQ(view.cars.size(), 2u);
        ASSERT_EQ(view.components.size(), 2u);
        EXPECT_EQ(view.cars[0].entity.id, first->id);
        EXPECT_EQ(view.cars[1].entity.id, second->id);
        EXPECT_EQ(view.cars[0].position, (CoordsXYZ{ 96, 128, 48 }));
        EXPECT_EQ(view.cars[1].position, (CoordsXYZ{ 129, 129, 64 }));
        EXPECT_EQ(view.components[0].originalImage.GetIndex(), SPR_WATER_PARTICLES_DENSE_0 + 1u);
        EXPECT_EQ(view.components[0].screen, Translate3DTo2DWithZ(r, first->getLocation()));
        EXPECT_EQ(view.components[0].bounds[2], first->z + 2);
        EXPECT_EQ(view.components[0].bounds[5], first->z + 2);
        EXPECT_EQ(view.components[0].relation, SelectedVehicleRelation::parent);
        EXPECT_EQ(view.components[0].parent, 0u);
        EXPECT_EQ(view.components[1].parent, 1u);
        EXPECT_EQ(view.cars[1].tile, (CoordsXY{ 4, 4 }));
    }
    const auto id = second->id;
    const auto oldHandle = requests[0].entity;
    first->moveTo({ 256, 256, 96 });
    state.entities.entityRemove(second);
    second = state.entities.createEntityAt<Vehicle>(id);
    ASSERT_NE(second, nullptr);
    ++state.currentTicks;
    EXPECT_NE(state.entities.GetEntityVisualHandle(id), oldHandle);
    const auto rejected = CaptureSelectedVehicleSnapshot(requests);
    for (const auto& view : rejected->views)
        EXPECT_TRUE(view.components.empty());
    EXPECT_EQ(held->sourceTick, 600u);
    EXPECT_EQ(held->views[0].components[0].screen, Translate3DTo2DWithZ(0, { 96, 128, 48 }));
    EXPECT_EQ(held->views[0].cars[0].position, (CoordsXYZ{ 96, 128, 48 }));
    requests[0] = Request(*first);
    const auto moved = CaptureSelectedVehicleSnapshot(std::span(requests).first(1));
    ASSERT_EQ(moved->views[0].cars.size(), 1u);
    EXPECT_EQ(moved->views[0].cars[0].position, (CoordsXYZ{ 256, 256, 96 }));
    requests[0].viewFlags = VIEWPORT_FLAG_HIGHLIGHT_PATH_ISSUES;
    EXPECT_TRUE(CaptureSelectedVehicleSnapshot(std::span(requests).first(1))->views[0].components.empty());
}

TEST_F(SelectedVehicleSnapshotTest, PendingPublicationKeepsVehicleAndMapOnOneTickThenRecoversAfterWorldReset)
{
    auto& state = getGameState();
    auto& jobs = context->GetJobPool();
    auto* vehicle = Car({ 96, 96, 48 }, 1);
    ASSERT_NE(vehicle, nullptr);
    PresentationScene scene;
    scene.SetSelectedVehicleRequests({ Request(*vehicle) });
    state.currentTicks = 700;
    ASSERT_TRUE(scene.BeginFrame(jobs, state.entities, 1, false, EntityPublicationProfile::gpuTerrainOnly));
    const auto old = scene.GetGeneration();
    vehicle->animation_frame = 2;
    vehicle->moveTo({ 160, 160, 64 });
    auto* surface = MapGetSurfaceElementAt(TileCoordsXY{ 2, 2 });
    ASSERT_NE(surface, nullptr);
    surface->setBaseZ(80);
    surface->setClearanceZ(80);
    MapInvalidateTileFull({ 64, 64 });
    state.currentTicks = 701;
    scene.ScheduleNext(jobs, state.entities);
    vehicle->animation_frame = 3;
    vehicle->moveTo({ 256, 256, 96 });
    state.currentTicks = 702;
    jobs.Join();
    ASSERT_TRUE(scene.BeginFrame(jobs, state.entities, 2, false, EntityPublicationProfile::gpuTerrainOnly));
    const auto captured = scene.GetGeneration();
    ASSERT_NE(captured->selectedVehicles, nullptr);
    EXPECT_EQ(captured->sourceTick, 701u);
    EXPECT_EQ(captured->selectedVehicles->sourceTick, captured->sourceTick);
    EXPECT_EQ(captured->selectedVehicles->worldEpoch, captured->map->GetEpoch());
    EXPECT_EQ(captured->selectedVehicles->entityEpoch, captured->sourceEntityEpoch);
    ASSERT_EQ(captured->selectedVehicles->views[0].components.size(), 1u);
    EXPECT_EQ(captured->selectedVehicles->views[0].components[0].originalImage.GetIndex(), SPR_WATER_PARTICLES_DENSE_0 + 2u);
    EXPECT_EQ(captured->selectedVehicles->views[0].components[0].screen, Translate3DTo2DWithZ(0, { 160, 160, 64 }));
    EXPECT_EQ(captured->map->GetSurfaceChunks()[0]->records[34].terrain.baseZ, 80);
    EXPECT_EQ(old->selectedVehicles->views[0].components[0].originalImage.GetIndex(), SPR_WATER_PARTICLES_DENSE_0 + 1u);
    scene.ScheduleNext(jobs, state.entities);
    MapInit({ 16, 16 });
    state.currentTicks = 703;
    ASSERT_TRUE(scene.BeginFrame(jobs, state.entities, 3, false, EntityPublicationProfile::gpuTerrainOnly));
    EXPECT_EQ(scene.GetGeneration()->selectedVehicles->sourceTick, 703u);
    EXPECT_EQ(scene.GetGeneration()->selectedVehicles->worldEpoch, scene.GetGeneration()->map->GetEpoch());
    scene.Reset(jobs);
}

TEST_F(SelectedVehicleSnapshotTest, RealVehicleBodyOwnsItsColourRecipeWithoutLaterLiveRideReads)
{
    auto& objects = context->GetObjectManager();
    ASSERT_NE(objects.LoadObject("rct2.ride.spboat"), nullptr);
    auto* ride = RideAllocateAtIndex(RideId::FromUnderlying(0));
    ASSERT_NE(ride, nullptr);
    ride->type = RIDE_TYPE_SPLASH_BOATS;
    ride->subtype = objects.GetLoadedObjectEntryIndex("rct2.ride.spboat");
    ride->numCarsPerTrain = 3;
    auto* vehicle = Car({ 128, 128, 64 }, 0);
    ASSERT_NE(vehicle, nullptr);
    vehicle->flags = {};
    vehicle->ride = ride->id;
    vehicle->ride_subtype = ride->subtype;
    vehicle->vehicle_type = 0;
    vehicle->colours.Body = Colour::brightRed;
    vehicle->colours.Trim = Colour::yellow;
    const std::array requests{ Request(*vehicle) };
    const auto held = CaptureSelectedVehicleSnapshot(requests);
    ASSERT_NE(held, nullptr);
    ASSERT_EQ(held->views.size(), 1u);
    ASSERT_FALSE(held->views[0].components.empty());
    const auto image = held->views[0].components[0].image;
    EXPECT_TRUE(image.HasSecondary());
    EXPECT_EQ(image.GetPrimary(), Colour::brightRed);
    EXPECT_EQ(image.GetSecondary(), Colour::yellow);
    vehicle->colours.Body = Colour::lightBlue;
    ride->numCarsPerTrain = 0;
    const auto changed = CaptureSelectedVehicleSnapshot(requests);
    ASSERT_FALSE(changed->views[0].components.empty());
    EXPECT_EQ(changed->views[0].components[0].image.GetPrimary(), Colour::lightBlue);
    EXPECT_EQ(held->views[0].components[0].image, image);
}

TEST(SelectedVehiclePacketTest, RejectsCrossCarOrDiscontiguousGroupsAndTruncatedOrMixedEpochPackets)
{
    using namespace OpenRCT2::Ui::Gpu;
    SelectedVehiclePaintPacket valid;
    auto source = std::make_shared<SelectedVehicleSnapshot>();
    source->sourceTick = 7;
    source->worldEpoch = 9;
    source->entityEpoch = 11;
    valid.source = source;
    valid.words.resize(16 + 12 + 3 * 28);
    auto& w = valid.words;
    const std::array<uint32_t, 16> header{
        kSelectedVehiclePaintMagic, kSelectedVehiclePaintVersion, 1, 3, 16, 28, 64, 112, 7, 9, 0, 11, 0
    };
    std::copy(header.begin(), header.end(), w.begin());
    w[16] = 2;
    w[17] = 1;
    w[18] = 0;
    w[19] = 3;
    w[20] = 34;
    for (uint32_t i = 0; i < 3; ++i)
    {
        w[28 + i * 12 + 6] = i == 1 ? 0 : i;
        w[28 + i * 12 + 8] = i == 1 ? 257 : 256;
        w[28 + i * 12 + 9] = 34;
    }
    w[21] = 96;
    w[22] = 128;
    w[23] = 48;
    EXPECT_NO_THROW(ValidateSelectedVehiclePaintPacket(valid));
    auto oldProjectionOnly = valid;
    oldProjectionOnly.words[1] = 1;
    EXPECT_THROW(ValidateSelectedVehiclePaintPacket(oldProjectionOnly), std::invalid_argument);
    auto invalidPosition = valid;
    invalidPosition.words[21] = 0xffffffffu;
    EXPECT_THROW(ValidateSelectedVehiclePaintPacket(invalidPosition), std::invalid_argument);
    for (const auto [word, value] :
         std::array<std::pair<size_t, uint32_t>, 6>{ { { 9, 10 }, { 19, 4 }, { 34, 1 }, { 49, 35 }, { 58, 0 }, { 60, 259 } } })
    {
        auto bad = valid;
        bad.words[word] = value;
        EXPECT_THROW(ValidateSelectedVehiclePaintPacket(bad), std::invalid_argument) << word;
    }
    auto truncated = valid;
    truncated.words.pop_back();
    EXPECT_THROW(ValidateSelectedVehiclePaintPacket(truncated), std::invalid_argument);
}

TEST(SelectedVehiclePacketTest, LocalDepthCapacityIsPerParentAndNeverTruncatesAValidCar)
{
    using namespace OpenRCT2::Ui::Gpu;
    SelectedVehiclePaintPacket packet;
    packet.source = std::make_shared<SelectedVehicleSnapshot>();
    constexpr uint32_t count = 30; // Two fifteen-component parent groups in one car.
    packet.words.resize(28 + count * 28);
    auto& w = packet.words;
    w[0] = kSelectedVehiclePaintMagic;
    w[1] = kSelectedVehiclePaintVersion;
    w[2] = 1;
    w[3] = count;
    w[4] = 16;
    w[5] = 28;
    w[6] = 28 + count * 12;
    w[7] = static_cast<uint32_t>(w.size());
    w[16] = 2;
    w[17] = 1;
    w[19] = count;
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto root = i < 15 ? 0u : 15u;
        w[28 + i * 12 + 6] = root;
        w[28 + i * 12 + 8] = i == root ? 256u : 257u;
    }
    EXPECT_NO_THROW(ValidateSelectedVehiclePaintPacket(packet));
    // Move the group boundary one component forward: same total car capacity,
    // but the first group no longer fits its scalar's finite overlay interval.
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto root = i < 16 ? 0u : 16u;
        w[28 + i * 12 + 6] = root;
        w[28 + i * 12 + 8] = i == root ? 256u : 257u;
    }
    EXPECT_THROW(ValidateSelectedVehiclePaintPacket(packet), std::invalid_argument);
}
