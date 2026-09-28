/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/
#include "../peep-parity/PeepCaptureFixture.h"
#include "RetainedPeepTestHelpers.h"

#include <gtest/gtest.h>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/PlatformEnvironment.h>
#include <openrct2/actions/peep/StaffSetColourAction.h>
#include <openrct2/config/Config.h>
#ifdef ENABLE_SCRIPTING
    #include <openrct2/scripting/ScriptEngine.h>
#endif
#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <openrct2/core/JobPool.h>
#include <openrct2/drawing/Colour.h>
#include <openrct2/drawing/PresentationScene.h>
#include <openrct2/drawing/RetainedPeepState.h>
#include <openrct2/entity/Balloon.h>
#include <openrct2/entity/EntityPresentationSnapshot.h>
#include <openrct2/entity/Guest.h>
#include <openrct2/entity/Litter.h>
#include <openrct2/entity/Staff.h>
#include <openrct2/object/ObjectList.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/object/ObjectRepository.h>
#include <openrct2/object/PeepAnimationsObject.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapPresentationSnapshot.h>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace OpenRCT2;
using namespace OpenRCT2::Drawing;
using namespace OpenRCT2::Drawing::Test;

namespace
{
    RetainedPeepRecord MakeRecord(uint32_t id, uint32_t generation = 1)
    {
        RetainedPeepRecord record{};
        record.id = id;
        record.generation = generation;
        record.x = record.previousX = 64;
        record.y = record.previousY = 96;
        record.z = record.previousZ = 16;
        record.sourceTick = record.previousTick = 10;
        record.objectGeneration = 1;
        record.flags = kRetainedPeepPresent;
        return record;
    }

    RetainedPeepRecord Tombstone(uint32_t id, uint32_t generation)
    {
        RetainedPeepRecord record{};
        record.id = id;
        record.generation = generation;
        return record;
    }

    std::shared_ptr<const RetainedPeepAnimationObject> MakeObject(uint32_t generation = 1)
    {
        const std::array sources{ RetainedPeepAnimationSource{ 0, 0, 100 }, RetainedPeepAnimationSource{ 1, 2, 116 } };
        return BuildRetainedPeepAnimationObject({ 0, generation, 2, 0, 100, 32, 0, 0 }, sources);
    }
} // namespace

TEST(RetainedPeepStateTest, SkippedSnapshotsRetainDeletionAndUnchangedChunks)
{
    RetainedPeepScene scene;
    RetainedPeepBatch initial = FullPeepBatch(7, true, { MakeRecord(0), MakeRecord(64), MakeRecord(128) });
    ASSERT_TRUE(scene.Apply(initial, 1));
    const auto before = scene.GetSnapshot();
    ASSERT_TRUE(scene.Apply(FullPeepBatch(7, false, { Tombstone(0, 1) }), 2));
    auto changed = MakeRecord(64);
    changed.frameOffset = 9;
    ASSERT_TRUE(scene.Apply(FullPeepBatch(7, false, { changed }), 3));
    const auto latest = scene.GetSnapshot();
    EXPECT_EQ(latest->count, 2u);
    EXPECT_EQ(latest->TryGet(EntityId::FromUnderlying(0)), std::nullopt);
    ASSERT_NE(before->TryGet(EntityId::FromUnderlying(0)), std::nullopt);
    EXPECT_EQ(latest->chunks[0]->revision, 2u);
    EXPECT_EQ(latest->chunks[1]->revision, 3u);
    EXPECT_EQ(latest->chunks[2], before->chunks[2]);
    initial = {};
    EXPECT_EQ(before->TryGet(EntityId::FromUnderlying(64))->frameOffset, 0u);
    ASSERT_TRUE(scene.Apply(FullPeepBatch(7, false, { changed }), 4));
    EXPECT_EQ(scene.GetSnapshot()->chunks[1], latest->chunks[1]);
    EXPECT_EQ(scene.GetLastApplyMetrics().changedRecords, 0u);
    EXPECT_EQ(scene.GetLastApplyMetrics().copiedRecordBytes, 0u);
    EXPECT_EQ(scene.GetSnapshot()->TryGet(EntityId::GetNull()), std::nullopt);
}

TEST(RetainedPeepStateTest, ObjectBankUsageTracksOwnersWithoutRebuildingForPoseOrColourChanges)
{
    RetainedPeepScene scene;
    auto first = MakeRecord(1);
    auto second = MakeRecord(2);
    first.objectIndex = second.objectIndex = 7;
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, true, { first, second }), 1));
    const auto initial = scene.GetSnapshot();
    ASSERT_NE(initial->usedObjects, nullptr);
    EXPECT_EQ(*initial->usedObjects, (std::vector<uint32_t>{ 7 }));
    EXPECT_EQ(initial->objectUseCounts->at(7), 2u);

    first.x = first.previousX = 128;
    first.colours = 3;
    first.frameOffset = 4;
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, false, { first }), 2));
    EXPECT_EQ(scene.GetSnapshot()->usedObjects, initial->usedObjects);
    EXPECT_EQ(scene.GetSnapshot()->objectUseCounts, initial->objectUseCounts);
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, false, { Tombstone(2, 1) }), 3));
    EXPECT_EQ(scene.GetSnapshot()->usedObjects, initial->usedObjects);
    EXPECT_EQ(scene.GetSnapshot()->objectUseCounts->at(7), 1u);
    EXPECT_EQ(initial->objectUseCounts->at(7), 2u);

    first.objectIndex = 9;
    const auto beforeInvalid = scene.GetSnapshot();
    auto invalid = FullPeepBatch(1, false, { first });
    invalid.appearance.front().value.objectGeneration = 0;
    EXPECT_THROW(scene.Apply(invalid, 4), std::invalid_argument);
    EXPECT_EQ(scene.GetSnapshot(), beforeInvalid);
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, false, { first }), 4));
    EXPECT_EQ(*scene.GetSnapshot()->usedObjects, (std::vector<uint32_t>{ 9 }));
    EXPECT_EQ(*initial->usedObjects, (std::vector<uint32_t>{ 7 }));
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, false, { Tombstone(1, 1) }), 5));
    EXPECT_TRUE(scene.GetSnapshot()->usedObjects->empty());
    EXPECT_TRUE(scene.GetSnapshot()->objectUseCounts->empty());
    ASSERT_TRUE(scene.Apply(FullPeepBatch(2, true, { second }), 6));
    EXPECT_EQ(*scene.GetSnapshot()->usedObjects, (std::vector<uint32_t>{ 7 }));
    EXPECT_EQ(scene.GetSnapshot()->objectUseCounts->at(7), 1u);
}

TEST(RetainedPeepStateTest, SameEpochResetCannotForgetTombstonesOrRecoverAnInputGap)
{
    RetainedPeepScene scene;
    ASSERT_TRUE(scene.Apply(FullPeepBatch(4, true, { MakeRecord(3) }), 1));
    ASSERT_TRUE(scene.Apply(FullPeepBatch(4, false, { Tombstone(3, 1) }), 2));
    const auto removed = scene.GetSnapshot();
    const auto copied = scene.GetLastApplyMetrics().copiedRecordBytes;
    RetainedPeepBatch reset{ .epoch = 4, .reset = true };
    EXPECT_THROW(scene.Apply(reset, 3), std::invalid_argument);
    EXPECT_THROW(scene.Apply(reset, 20), std::invalid_argument);
    AppendFullPeep(reset, MakeRecord(3));
    EXPECT_THROW(scene.Apply(reset, 3), std::invalid_argument);
    EXPECT_EQ(scene.GetSnapshot(), removed);
    EXPECT_EQ(scene.GetLastApplyMetrics().copiedRecordBytes, copied);
    EXPECT_THROW(scene.Apply(FullPeepBatch(4, false, { MakeRecord(3) }), 3), std::invalid_argument);
    EXPECT_THROW(scene.Apply({ .epoch = 4 }, 4), std::invalid_argument);
    EXPECT_THROW(scene.Apply({ .epoch = 5 }, 3), std::invalid_argument);
    ASSERT_TRUE(scene.Apply(FullPeepBatch(5, true, { MakeRecord(3) }), 20));
    EXPECT_THROW(scene.Apply({ .epoch = 4, .reset = true }, 21), std::invalid_argument);
    EXPECT_EQ(scene.GetSnapshot()->epoch, 5u);
    EXPECT_EQ(removed->TryGet(EntityId::FromUnderlying(3)), std::nullopt);
}

TEST(RetainedPeepStateTest, CrossFamilyReuseAndWrappedGenerationRejectDelayedLiveState)
{
    RetainedPeepScene scene;
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, true, { MakeRecord(2, UINT32_MAX) }), 1));
    // Generation 1 is a different entity family; generation zero is never issued by the registry.
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, false, { Tombstone(2, 1) }), 2));
    EXPECT_THROW(scene.Apply(FullPeepBatch(1, false, { MakeRecord(2, UINT32_MAX) }), 3), std::invalid_argument);
    EXPECT_THROW(scene.Apply(FullPeepBatch(1, false, { MakeRecord(2, 1) }), 3), std::invalid_argument);
    auto staff = MakeRecord(2, 2);
    staff.flags |= kRetainedPeepStaff;
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, false, { staff }), 3));
    auto wrongFamily = staff;
    wrongFamily.flags = kRetainedPeepPresent;
    EXPECT_THROW(scene.Apply(FullPeepBatch(1, false, { wrongFamily }), 4), std::invalid_argument);
    EXPECT_THROW(scene.Apply(FullPeepBatch(1, false, { Tombstone(2, 1) }), 4), std::invalid_argument);
    EXPECT_EQ(scene.GetSnapshot()->count, 1u);
}

TEST(RetainedPeepStateTest, InvalidTailCannotPartiallyPublishAndSequenceZeroIsInvalid)
{
    RetainedPeepScene scene;
    ASSERT_TRUE(scene.Apply(FullPeepBatch(2, true, { MakeRecord(0) }), 1));
    const auto before = scene.GetSnapshot();
    auto changed = MakeRecord(0);
    changed.frameOffset = 7;
    auto invalid = MakeRecord(65);
    invalid.objectGeneration = 0;
    EXPECT_THROW(scene.Apply(FullPeepBatch(2, false, { changed, invalid }), 2), std::invalid_argument);
    EXPECT_EQ(scene.GetSnapshot(), before);
    EXPECT_THROW(scene.Apply(FullPeepBatch(2, false, { changed, changed }), 2), std::invalid_argument);
    auto badTombstone = FullPeepBatch(2, false, { Tombstone(0, 1) });
    badTombstone.motion.push_back({ 0, 1, SplitRetainedPeepRecord(changed).motion });
    EXPECT_THROW(scene.Apply(badTombstone, 2), std::invalid_argument);
    EXPECT_THROW(scene.Apply({ .epoch = 2 }, 0), std::invalid_argument);
    EXPECT_FALSE(scene.Apply({ .epoch = 2 }, 1));
    EXPECT_EQ(scene.GetSnapshot(), before);
}

TEST(RetainedPeepStateTest, CapturesRawGuestAndStaffFieldsAndRequiresAdjacentSameIdentityHistory)
{
    Guest guest{};
    guest.type = EntityType::guest;
    guest.id = EntityId::FromUnderlying(8);
    guest.x = 64;
    guest.y = 96;
    guest.z = 24;
    guest.orientation = 24;
    guest.animationObjectIndex = 3;
    guest.action = PeepActionType::idle;
    guest.animationType = PeepAnimationType::sittingIdle;
    guest.nextAnimationType = PeepAnimationType::watchRide;
    guest.animationImageIdOffset = 28;
    guest.setTShirtColour(Colour::brightRed);
    guest.setTrousersColour(Colour::darkBlue);
    guest.setHatColour(Colour::yellow);
    guest.setBalloonColour(Colour::white);
    guest.setUmbrellaColour(Colour::black);
    guest.spriteData.width = 11;
    guest.spriteData.heightMin = 20;
    guest.spriteData.heightMax = 5;
    const EntityVisualHandle handle{ 2, guest.id, 7 };
    const auto raw = CaptureRetainedPeepRecord(guest, handle, 9, 0);
    EXPECT_EQ(raw.objectIndex, 3u);
    EXPECT_EQ(raw.objectGeneration, 9u);
    EXPECT_EQ(raw.action, 254u);
    EXPECT_EQ(raw.animationType, static_cast<uint32_t>(guest.animationType));
    EXPECT_EQ(raw.nextAnimationType, static_cast<uint32_t>(guest.nextAnimationType));
    EXPECT_EQ(raw.frameOffset, 28u);
    EXPECT_EQ(raw.orientation, 24u);
    EXPECT_EQ(raw.colours, static_cast<uint32_t>(Colour::brightRed) | (static_cast<uint32_t>(Colour::darkBlue) << 8));
    EXPECT_EQ(
        raw.accessoryColours,
        static_cast<uint32_t>(Colour::yellow) | (static_cast<uint32_t>(Colour::white) << 8)
            | (static_cast<uint32_t>(Colour::black) << 16));
    EXPECT_EQ(raw.flags, kRetainedPeepPresent);
    EXPECT_EQ(raw.previousX, raw.x);
    const RetainedPeepPreviousPosition prior{ handle, UINT32_MAX, { 63, 96, 24 } };
    const auto interpolated = CaptureRetainedPeepRecord(guest, handle, 9, 0, prior);
    EXPECT_EQ(interpolated.previousX, 63);
    EXPECT_EQ(interpolated.flags, kRetainedPeepPresent | kRetainedPeepInterpolate);
    auto wrong = prior;
    wrong.handle.generation++;
    EXPECT_THROW(CaptureRetainedPeepRecord(guest, handle, 9, 0, wrong), std::invalid_argument);
    wrong = prior;
    wrong.tick--;
    EXPECT_THROW(CaptureRetainedPeepRecord(guest, handle, 9, 0, wrong), std::invalid_argument);
    EXPECT_THROW(CaptureRetainedPeepRecord(guest, handle, 0, 0), std::invalid_argument);

    Staff staff{};
    static_cast<Peep&>(staff) = static_cast<const Peep&>(guest);
    staff.type = EntityType::staff;
    staff.assignedStaffType = StaffType::mechanic;
    const auto staffRaw = CaptureRetainedPeepRecord(staff, handle, 9, 0);
    EXPECT_EQ(staffRaw.flags, kRetainedPeepPresent | kRetainedPeepStaff);
    EXPECT_EQ(staffRaw.accessoryColours, static_cast<uint32_t>(StaffType::mechanic) << 24);
}

TEST(RetainedPeepCatalogTest, UnloadRetainsLeaseAndCannotResurrectThroughSameEpochReset)
{
    auto mutableObject = std::make_shared<RetainedPeepAnimationObject>(*MakeObject());
    const std::array initialChanges{ RetainedPeepAnimationChange{ 0, 1, mutableObject } };
    const auto first = PublishRetainedPeepAnimationCatalog(nullptr, 3, 1, true, initialChanges);
    mutableObject->facts[0].baseImage = 120;
    EXPECT_EQ(first->slots.at(0).object->facts[0].baseImage, 100u);
    const std::array unload{ RetainedPeepAnimationChange{ 0, 1, nullptr } };
    const auto deleted = PublishRetainedPeepAnimationCatalog(first, 3, 2, false, unload);
    EXPECT_EQ(deleted->slots.at(0).object, nullptr);
    EXPECT_EQ(first->slots.at(0).object->facts[0].baseImage, 100u);
    EXPECT_THROW(PublishRetainedPeepAnimationCatalog(deleted, 3, 3, true, {}), std::invalid_argument);
    EXPECT_THROW(PublishRetainedPeepAnimationCatalog(deleted, 3, 9, true, {}), std::invalid_argument);
    EXPECT_THROW(PublishRetainedPeepAnimationCatalog(deleted, 3, 3, false, initialChanges), std::invalid_argument);
    const std::array reload{ RetainedPeepAnimationChange{ 0, 2, MakeObject(2) } };
    const auto latest = PublishRetainedPeepAnimationCatalog(deleted, 3, 3, false, reload);
    EXPECT_EQ(latest->slots.at(0).generation, 2u);
    EXPECT_EQ(deleted->slots.at(0).object, nullptr);
    const auto idle = PublishRetainedPeepAnimationCatalog(latest, 3, 4, false, {});
    EXPECT_EQ(idle->slots.at(0).object, latest->slots.at(0).object);
    EXPECT_THROW(PublishRetainedPeepAnimationCatalog(idle, 2, 5, true, {}), std::invalid_argument);
    EXPECT_EQ(PublishRetainedPeepAnimationCatalog(idle, 4, 5, true, {})->slots.size(), 0u);
}

TEST(RetainedPeepCatalogTest, RejectsMalformedFactsDuplicatesAndImageExtentOverflow)
{
    const auto valid = MakeObject();
    ASSERT_EQ(valid->facts.size(), 2u * kRetainedPeepAnimationTypes);
    EXPECT_EQ(valid->facts[1].valid, 0u);
    EXPECT_EQ(valid->facts[kRetainedPeepAnimationTypes + 2].baseImage, 116u);
    const std::array duplicate{ RetainedPeepAnimationSource{ 0, 0, 100 }, RetainedPeepAnimationSource{ 0, 0, 101 } };
    EXPECT_THROW(BuildRetainedPeepAnimationObject(valid->descriptor, duplicate), std::invalid_argument);
    const std::array outOfObject{ RetainedPeepAnimationSource{ 0, 0, 132 } };
    EXPECT_THROW(BuildRetainedPeepAnimationObject(valid->descriptor, outOfObject), std::invalid_argument);
    auto descriptor = valid->descriptor;
    descriptor.imageBase = UINT32_MAX - 2;
    EXPECT_THROW(BuildRetainedPeepAnimationObject(descriptor, {}), std::invalid_argument);
    auto malformed = std::make_shared<RetainedPeepAnimationObject>(*valid);
    malformed->facts.pop_back();
    const std::array changes{ RetainedPeepAnimationChange{ 0, 1, malformed } };
    EXPECT_THROW(PublishRetainedPeepAnimationCatalog(nullptr, 1, 1, true, changes), std::invalid_argument);
    *malformed = *valid;
    malformed->facts[1].baseImage = 100; // Invalid holes must remain canonical zeros.
    EXPECT_THROW(PublishRetainedPeepAnimationCatalog(nullptr, 1, 1, true, changes), std::invalid_argument);
}

class RetainedPeepPublicationTest : public testing::Test
{
protected:
    bool oldHeadless = gOpenRCT2Headless;
    bool oldNoGraphics = gOpenRCT2NoGraphics;
    std::unique_ptr<IContext> context;
    std::unique_ptr<PresentationScene> publication;
    const std::array<uint32_t, 1> generations{ 4 };
    bool contextReady{};
    std::string oldRct1 = Config::Get().general.rct1Path;
    std::string oldRct2 = Config::Get().general.rct2Path;
    virtual bool NeedsSpriteAssets() const
    {
        return false;
    }

    void SetUp() override
    {
        const auto* rct2 = std::getenv("OPENRCT2_TEST_RCT2_PATH");
        if (NeedsSpriteAssets() && (rct2 == nullptr || *rct2 == '\0'))
        {
            const auto* required = std::getenv("OPENRCT2_REQUIRE_VULKAN_TESTS");
            if (required != nullptr && std::string_view(required) == "1")
                FAIL() << "Peep object lifecycle requires pinned OPENRCT2_TEST_RCT2_PATH";
            GTEST_SKIP() << "Peep object lifecycle requires OPENRCT2_TEST_RCT2_PATH";
        }
        gOpenRCT2Headless = true; // No window, renderer or GPU is needed to load/decode CPU sprites.
        gOpenRCT2NoGraphics = !NeedsSpriteAssets();
        context = CreateContext();
        ASSERT_NE(context, nullptr);
        if (NeedsSpriteAssets())
        {
            auto& environment = context->GetPlatformEnvironment();
            Config::Get().general.rct2Path = rct2;
            environment.SetBasePath(DirBase::rct2, rct2);
            if (const auto* rct1 = std::getenv("OPENRCT2_TEST_RCT1_PATH"))
            {
                Config::Get().general.rct1Path = rct1;
                environment.SetBasePath(DirBase::rct1, rct1);
            }
            std::filesystem::path data = std::filesystem::current_path() / "data";
            if (const auto* shaders = std::getenv("OPENRCT2_VULKAN_SHADER_DIRECTORY"))
                data = std::filesystem::path(shaders).parent_path().parent_path();
            else if (!std::filesystem::is_regular_file(data / "g2.dat"))
                data = std::filesystem::current_path() / "bin/data";
            environment.SetBasePath(DirBase::openrct2, data.string());
        }
        ASSERT_TRUE(context->Initialise());
        contextReady = true;
        MapInit({ 16, 16 });
        getGameState().entities.resetAllEntities();
        publication = std::make_unique<PresentationScene>();
    }
    void TearDown() override
    {
        if (publication != nullptr)
            publication->Reset(context->GetJobPool());
        publication.reset();
#ifdef ENABLE_SCRIPTING
        if (context != nullptr && !contextReady)
        {
            // Failed graphics startup can precede per-context script registration.
            try
            {
                context->GetScriptEngine().Initialise();
            }
            catch (const std::runtime_error& error)
            {
                EXPECT_STREQ(error.what(), "Script engine already initialised.");
            }
        }
#endif
        context.reset();
        gOpenRCT2Headless = oldHeadless;
        gOpenRCT2NoGraphics = oldNoGraphics;
        Config::Get().general.rct1Path = oldRct1;
        Config::Get().general.rct2Path = oldRct2;
    }
};

class RetainedPeepCatalogGraphicsTest : public RetainedPeepPublicationTest
{
protected:
    bool NeedsSpriteAssets() const override
    {
        return true;
    }
};

TEST_F(RetainedPeepPublicationTest, StationaryUniformActionPublishesOnceAndPreservesHeldSnapshot)
{
    auto& gameState = getGameState();
    auto& registry = gameState.entities;
    auto* staff = registry.createEntity<Staff>();
    auto* unaffected = registry.createEntity<Staff>();
    ASSERT_NE(staff, nullptr);
    ASSERT_NE(unaffected, nullptr);
    staff->assignedStaffType = StaffType::handyman;
    unaffected->assignedStaffType = StaffType::mechanic;
    staff->animationObjectIndex = unaffected->animationObjectIndex = 0;
    staff->setClothingColours(Colour::black, Colour::black);
    staff->moveTo({ 64, 64, 16 });
    RetainedPeepScene scene;
    {
        const auto input = registry.CaptureRetainedEntityPublication(1, generations, false);
        ASSERT_TRUE(scene.Apply(input.peeps, 1));
        registry.AcknowledgeRetainedEntityPublication();
    }
    const auto held = scene.GetSnapshot();
    const auto oldColours = held->TryGet(staff->id)->colours;
    EXPECT_EQ(held->TryGet(staff->id)->accessoryColours >> 24, static_cast<uint32_t>(StaffType::handyman));
    EXPECT_EQ(held->TryGet(unaffected->id)->accessoryColours >> 24, static_cast<uint32_t>(StaffType::mechanic));

    // These paused-capable actions must publish without movement or a simulation tick.
    EXPECT_EQ(
        GameActions::StaffSetColourAction(StaffType::handyman, Colour::white).Execute(gameState, gameState.park).error,
        GameActions::Status::ok);
    EXPECT_EQ(
        GameActions::StaffSetColourAction(StaffType::handyman, Colour::yellow).Execute(gameState, gameState.park).error,
        GameActions::Status::ok);
    const auto changesInput = registry.CaptureRetainedEntityPublication(2, generations, false);
    const auto& changes = changesInput.peeps;
    ASSERT_EQ(changes.appearance.size(), 1u);
    EXPECT_EQ(changes.appearance[0].id, staff->id.ToUnderlying());
    EXPECT_TRUE(changes.motion.empty());
    EXPECT_EQ(
        changes.appearance[0].value.colours,
        static_cast<uint32_t>(Colour::yellow) | (static_cast<uint32_t>(Colour::yellow) << 8));
    ASSERT_TRUE(scene.Apply(changes, 2));
    registry.AcknowledgeRetainedEntityPublication();
    EXPECT_EQ(held->TryGet(staff->id)->colours, oldColours);
    EXPECT_NE(scene.GetSnapshot()->TryGet(staff->id)->colours, oldColours);
    EXPECT_EQ(registry.CaptureRetainedEntityPublication(3, generations, false).peeps.PayloadBytes(), 0u);
}

TEST_F(RetainedPeepPublicationTest, StationaryStateTransitionPublishesRawStateWithoutMovement)
{
    auto& registry = getGameState().entities;
    auto* guest = registry.createEntity<Guest>();
    ASSERT_NE(guest, nullptr);
    guest->animationObjectIndex = 0;
    guest->state = PeepState::walking;
    guest->moveTo({ 64, 64, 16 });
    RetainedPeepScene scene;
    {
        const auto input = registry.CaptureRetainedEntityPublication(1, generations, false);
        ASSERT_TRUE(scene.Apply(input.peeps, 1));
        registry.AcknowledgeRetainedEntityPublication();
    }
    const auto held = scene.GetSnapshot();
    guest->setState(PeepState::sitting);
    const auto changesInput = registry.CaptureRetainedEntityPublication(2, generations, false);
    const auto& changes = changesInput.peeps;
    ASSERT_EQ(changes.animation.size(), 1u);
    EXPECT_EQ(
        changes.animation[0].value.flags & kRetainedPeepStateMask,
        static_cast<uint32_t>(PeepState::sitting) << kRetainedPeepStateShift);
    ASSERT_EQ(changes.motion.size(), 1u);
    EXPECT_EQ(changes.motion[0].value.x, held->TryGet(guest->id)->x);
    ASSERT_TRUE(scene.Apply(changes, 2));
    registry.AcknowledgeRetainedEntityPublication();
    EXPECT_EQ(
        held->TryGet(guest->id)->flags & kRetainedPeepStateMask,
        static_cast<uint32_t>(PeepState::walking) << kRetainedPeepStateShift);
}

TEST_F(RetainedPeepCatalogGraphicsTest, StationaryAnimationMethodsPublishLoadedObjectFrames)
{
    auto& manager = context->GetObjectManager();
    manager.UnloadAll();
    auto* object = dynamic_cast<PeepAnimationsObject*>(manager.LoadObject("rct2.peep_animations.guest"));
    ASSERT_NE(object, nullptr);
    const auto slot = manager.GetLoadedObjectEntryIndex(object);
    const auto catalog = manager.GetPeepAnimationCatalog();
    ASSERT_NE(catalog, nullptr);
    std::vector<uint32_t> objectGenerations(static_cast<size_t>(slot) + 1);
    objectGenerations[slot] = catalog->slots.at(slot).generation;
    auto& registry = getGameState().entities;
    RetainedPeepScene scene;
    uint64_t sequence = 0;
    uint32_t tick = 0;

    for (const auto animation : { PeepAnimationType::walking, PeepAnimationType::wave })
    {
        SCOPED_TRACE(static_cast<uint32_t>(animation));
        const auto& frames = object->GetPeepAnimation(PeepAnimationGroup::normal, animation).frameOffsets;
        ASSERT_FALSE(frames.empty());
        const auto next = std::find_if(frames.begin() + 1, frames.end(), [&](auto frame) { return frame != frames[0]; });
        ASSERT_NE(next, frames.end());
        auto* guest = registry.createEntity<Guest>();
        ASSERT_NE(guest, nullptr);
        guest->animationObjectIndex = slot;
        guest->animationGroup = PeepAnimationGroup::normal;
        guest->animationType = guest->nextAnimationType = animation;
        guest->animationImageIdOffset = frames[0];
        guest->walkingAnimationFrameNum = guest->animationFrameNum = static_cast<uint8_t>(next - frames.begin() - 1);
        guest->moveTo({ 64, 64, 16 });
        // The previous guest remains resident as an unchanged control on the second iteration.
        {
            const auto input = registry.CaptureRetainedEntityPublication(++tick, objectGenerations, false);
            ASSERT_TRUE(scene.Apply(input.peeps, ++sequence));
            registry.AcknowledgeRetainedEntityPublication();
        }
        const auto held = scene.GetSnapshot();
        if (animation == PeepAnimationType::walking)
            guest->updateWalkingAnimation();
        else
            ASSERT_TRUE(guest->updateActionAnimation());
        const auto changesInput = registry.CaptureRetainedEntityPublication(++tick, objectGenerations, false);
        const auto& changes = changesInput.peeps;
        ASSERT_EQ(changes.animation.size(), 1u);
        EXPECT_EQ(changes.animation[0].id, guest->id.ToUnderlying());
        EXPECT_EQ(changes.animation[0].value.frameOffset, *next);
        EXPECT_TRUE(changes.motion.empty());
        ASSERT_TRUE(scene.Apply(changes, ++sequence));
        registry.AcknowledgeRetainedEntityPublication();
        EXPECT_EQ(held->TryGet(guest->id)->frameOffset, frames[0]);
        EXPECT_EQ(registry.CaptureRetainedEntityPublication(++tick, objectGenerations, false).peeps.PayloadBytes(), 0u);
    }
}

TEST_F(RetainedPeepPublicationTest, StationaryQueueArrivalPublishesIdlePoseWithoutAdvancingAnimation)
{
    auto& registry = getGameState().entities;
    auto* guest = registry.createEntity<Guest>();
    auto* ahead = registry.createEntity<Guest>();
    ASSERT_NE(guest, nullptr);
    ASSERT_NE(ahead, nullptr);
    guest->animationObjectIndex = ahead->animationObjectIndex = 0;
    guest->state = PeepState::queuing;
    guest->action = PeepActionType::walking;
    guest->nextAnimationType = PeepAnimationType::walking;
    guest->guestNextInQueue = ahead->id;
    guest->moveTo({ 64, 64, 16 });
    ahead->moveTo({ 65, 64, 16 });
    RetainedPeepScene scene;
    {
        const auto input = registry.CaptureRetainedEntityPublication(1, generations, false);
        ASSERT_TRUE(scene.Apply(input.peeps, 1));
        registry.AcknowledgeRetainedEntityPublication();
    }
    const auto held = scene.GetSnapshot();

    ASSERT_TRUE(guest->updateQueuePosition(PeepActionType::walking));
    const auto changesInput = registry.CaptureRetainedEntityPublication(2, generations, false);
    const auto& changes = changesInput.peeps;
    ASSERT_EQ(changes.animation.size(), 1u);
    EXPECT_EQ(changes.animation[0].id, guest->id.ToUnderlying());
    EXPECT_EQ(changes.animation[0].value.action, static_cast<uint32_t>(PeepActionType::idle));
    EXPECT_EQ(changes.animation[0].value.nextAnimationType, static_cast<uint32_t>(PeepAnimationType::watchRide));
    EXPECT_EQ(changes.animation[0].value.frameOffset, held->TryGet(guest->id)->frameOffset);
    ASSERT_EQ(changes.motion.size(), 1u);
    EXPECT_EQ(changes.motion[0].value.x, held->TryGet(guest->id)->x);
    ASSERT_TRUE(scene.Apply(changes, 2));
    registry.AcknowledgeRetainedEntityPublication();
    EXPECT_EQ(held->TryGet(guest->id)->action, static_cast<uint32_t>(PeepActionType::walking));
    EXPECT_EQ(held->TryGet(guest->id)->nextAnimationType, static_cast<uint32_t>(PeepAnimationType::walking));
    (void)guest->performNextAction(); // Continuing to wait must not publish the temporary idle -> walking -> idle change.
    EXPECT_EQ(registry.CaptureRetainedEntityPublication(3, generations, false).peeps.PayloadBytes(), 0u);
}

TEST_F(RetainedPeepPublicationTest, CoalescesAuthoritativeMovementAndExplicitAppearanceWithoutTweenNotifications)
{
    auto& registry = getGameState().entities;
    auto* guest = registry.createEntity<Guest>();
    ASSERT_NE(guest, nullptr);
    guest->animationObjectIndex = 0;
    guest->moveTo({ 64, 64, 16 });
    guest->moveTo({ 65, 64, 16 });
    guest->animationImageIdOffset = 12;
    registry.PublishEntityVisualState(*guest);
    registry.PublishEntityVisualState(*guest);
    const auto initialInput = registry.CaptureRetainedEntityPublication(10, generations, false);
    const auto& initial = initialInput.peeps;
    ASSERT_TRUE(initial.reset);
    ASSERT_EQ(initial.lifecycle.size(), 1u);
    ASSERT_EQ(initial.motion.size(), 1u);
    ASSERT_EQ(initial.appearance.size(), 1u);
    ASSERT_EQ(initial.animation.size(), 1u);
    EXPECT_EQ(initial.motion[0].value.x, 65);
    EXPECT_EQ(initial.animation[0].value.frameOffset, 12u);
    EXPECT_EQ(initial.appearance[0].value.objectGeneration, 4u);
    EXPECT_EQ(initial.motion[0].value.sourceTick, 10u);
    EXPECT_EQ(initial.motion[0].value.flags & kRetainedPeepInterpolate, 0u);
    RetainedPeepScene scene;
    ASSERT_TRUE(scene.Apply(initial, 1));
    registry.AcknowledgeRetainedEntityPublication();
    // A presentation move never queues authoritative state. Restore before any further capture.
    guest->moveToForTween({ 64, 64, 16 });
    guest->moveToForTween({ 65, 64, 16 });
    EXPECT_EQ(registry.CaptureRetainedEntityPublication(11, generations, false).peeps.PayloadBytes(), 0u);
    // Explicit appearance publication works even when no spatial movement occurred.
    guest->setHatColour(Colour::yellow);
    registry.PublishEntityVisualState(*guest);
    const auto appearanceInput = registry.CaptureRetainedEntityPublication(11, generations, false);
    const auto& appearance = appearanceInput.peeps;
    ASSERT_EQ(appearance.appearance.size(), 1u);
    EXPECT_EQ(appearance.appearance[0].value.accessoryColours & 0xff, static_cast<uint32_t>(Colour::yellow));
    ASSERT_TRUE(scene.Apply(appearance, 2));
    registry.AcknowledgeRetainedEntityPublication();
    EXPECT_TRUE(registry.ConsumeEntityVisualChanges().changes.empty()); // One shared stream, not two queues.
}

TEST_F(RetainedPeepPublicationTest, MissingCatalogDoesNotConsumeResetOrDirtyTail)
{
    auto& registry = getGameState().entities;
    auto* guest = registry.createEntity<Guest>();
    auto* staff = registry.createEntity<Staff>();
    ASSERT_NE(guest, nullptr);
    ASSERT_NE(staff, nullptr);
    guest->animationObjectIndex = 0;
    staff->animationObjectIndex = 1;
    EXPECT_THROW(static_cast<void>(registry.CaptureRetainedEntityPublication(1, generations, false)), std::invalid_argument);
    const std::array<uint32_t, 2> incomplete{ 4, 0 };
    EXPECT_THROW(static_cast<void>(registry.CaptureRetainedEntityPublication(1, incomplete, false)), std::invalid_argument);
    const std::array<uint32_t, 2> complete{ 4, 7 };
    const auto retryInput = registry.CaptureRetainedEntityPublication(1, complete, false);
    const auto& retry = retryInput.peeps;
    ASSERT_TRUE(retry.reset);
    ASSERT_EQ(retry.lifecycle.size(), 2u);
    ASSERT_EQ(retry.appearance.size(), 2u);
    EXPECT_EQ(retry.appearance[0].value.objectGeneration, 4u);
    EXPECT_EQ(retry.appearance[1].value.objectGeneration, 7u);
    EXPECT_NE(retry.lifecycle[1].value.flags & kRetainedPeepStaff, 0u);
    RetainedPeepScene scene;
    ASSERT_TRUE(scene.Apply(retry, 1));
    registry.AcknowledgeRetainedEntityPublication();
}

TEST_F(RetainedPeepPublicationTest, SharedLifecycleCapturesRemovalOtherFamilyReuseAndNewEpoch)
{
    auto& registry = getGameState().entities;
    auto* guest = registry.createEntity<Guest>();
    ASSERT_NE(guest, nullptr);
    guest->animationObjectIndex = 0;
    const auto id = guest->id;
    RetainedPeepScene scene;
    {
        const auto input = registry.CaptureRetainedEntityPublication(1, generations, false);
        ASSERT_TRUE(scene.Apply(input.peeps, 1));
        registry.AcknowledgeRetainedEntityPublication();
    }
    const auto before = scene.GetSnapshot();
    const auto generation = registry.GetEntityVisualHandle(id).generation;
    registry.entityRemove(guest);
    auto* replacement = registry.createEntity<Litter>();
    ASSERT_NE(replacement, nullptr);
    ASSERT_EQ(replacement->id, id);
    const auto deletionInput = registry.CaptureRetainedEntityPublication(2, generations, false);
    const auto& deletion = deletionInput.peeps;
    ASSERT_EQ(deletion.lifecycle.size(), 1u);
    EXPECT_EQ(deletion.lifecycle[0].value.flags, 0u);
    EXPECT_GT(deletion.lifecycle[0].value.generation, generation);
    ASSERT_TRUE(scene.Apply(deletion, 2));
    registry.AcknowledgeRetainedEntityPublication();
    {
        const auto input = registry.CaptureRetainedEntityPublication(3, generations, false);
        ASSERT_TRUE(scene.Apply(input.peeps, 3));
        registry.AcknowledgeRetainedEntityPublication();
    }
    EXPECT_EQ(scene.GetSnapshot()->TryGet(id), std::nullopt);
    EXPECT_NE(before->TryGet(id), std::nullopt);
    registry.resetAllEntities();
    auto* staff = registry.createEntity<Staff>();
    ASSERT_NE(staff, nullptr);
    staff->animationObjectIndex = 0;
    const auto resetInput = registry.CaptureRetainedEntityPublication(4, generations, false);
    const auto& reset = resetInput.peeps;
    EXPECT_TRUE(reset.reset);
    EXPECT_GT(reset.epoch, before->epoch);
    ASSERT_TRUE(scene.Apply(reset, 4));
    registry.AcknowledgeRetainedEntityPublication();
    EXPECT_EQ(scene.GetSnapshot()->count, 1u);
}

TEST_F(RetainedPeepPublicationTest, CombinedOwnerBootstrapsPeepsWithoutLegacyCopiesAndKeepsRiderFactsOwned)
{
    auto& registry = getGameState().entities;
    auto& jobs = context->GetJobPool();
    auto* guest = registry.createEntity<Guest>();
    auto* staff = registry.createEntity<Staff>();
    ASSERT_NE(guest, nullptr);
    ASSERT_NE(staff, nullptr);
    guest->animationObjectIndex = staff->animationObjectIndex = 0;
    guest->state = PeepState::onRide;
    guest->setTShirtColour(Colour::brightRed);
    guest->setTrousersColour(Colour::darkBlue);
    const auto guestId = guest->id;
    const auto staffId = staff->id;
    Balloon::create({ 64, 64, 200 }, Colour::yellow, false);
    const auto balloonId = registry.GetEntityExecutionList(EntityType::balloon).front()->id;
    const std::array changes{ RetainedPeepAnimationChange{ 0, 1, MakeObject() } };
    const auto catalog = PublishRetainedPeepAnimationCatalog(nullptr, 1, 1, true, changes);
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 1, true));
    EXPECT_NE(publication->GetGeneration()->entities->TryGetEntity(guestId), nullptr);
    (void)registry.ConsumeEntityVisualChanges(); // Bootstrap must not depend on dirty entries still being pending.
    getGameState().currentTicks = 19;
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 2, false, EntityPublicationProfile::retainedPeepsAndBalloons, catalog));
    const auto retained = publication->GetGeneration();
    ASSERT_NE(retained->peeps, nullptr);
    EXPECT_EQ(retained->sourceTick, 19u);
    EXPECT_EQ(retained->sourceEntityEpoch, registry.GetEntityVisualEpoch());
    EXPECT_EQ(retained->peeps->count, 2u);
    EXPECT_EQ(retained->peepAnimations, catalog);
    EXPECT_EQ(retained->entities->TryGetEntity(guestId), nullptr);
    EXPECT_EQ(retained->entities->TryGetEntity(staffId), nullptr);
    EXPECT_NE(retained->entities->TryGetEntity(balloonId), nullptr);
    EXPECT_EQ(retained->entities->GetCapturedEntityCount(), 3u);
    EXPECT_EQ(retained->entities->GetBalloonMetrics().bootstrapVisits, 3u);
    EXPECT_EQ(retained->entities->GetBalloonMetrics().worklistVisits, 0u);
    EXPECT_TRUE(registry.ConsumeEntityVisualChanges().changes.empty()); // Bootstrap already acknowledged its input.
    {
        ScopedEntityPresentationSnapshot scope(retained->entities.get());
        guest->setState(PeepState::walking);
        guest->setTShirtColour(Colour::black);
        const auto facts = GetGuestPresentationFacts(guestId);
        ASSERT_TRUE(facts.has_value());
        EXPECT_EQ(facts->state, static_cast<uint8_t>(PeepState::onRide));
        EXPECT_EQ(facts->colours & 0xff, static_cast<uint32_t>(Colour::brightRed));
        EXPECT_FALSE(GetGuestPresentationFacts(staffId).has_value());
        EXPECT_FALSE(GetGuestPresentationFacts(balloonId).has_value());
    }
    const auto pending = registry.CaptureRetainedEntityPublication(20, generations, false);
    EXPECT_EQ(pending.dirtyVisits, 1u);
    ASSERT_EQ(pending.peeps.appearance.size(), 1u);
    EXPECT_EQ(pending.peeps.appearance[0].id, guestId.ToUnderlying());
    EXPECT_EQ(pending.peeps.appearance[0].value.colours & 0xff, static_cast<uint32_t>(Colour::black));
    // Inspect without acknowledging: the owner must consume this same mutation in its next publication.
    getGameState().currentTicks = 20;
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 3, true, EntityPublicationProfile::retainedPeepsAndBalloons, catalog));
    const auto changed = publication->GetGeneration();
    EXPECT_EQ(changed->sourceTick, 20u);
    {
        ScopedEntityPresentationSnapshot scope(changed->entities.get());
        const auto facts = GetGuestPresentationFacts(guestId);
        ASSERT_TRUE(facts.has_value());
        EXPECT_EQ(facts->state, static_cast<uint8_t>(PeepState::walking));
        EXPECT_EQ(facts->colours & 0xff, static_cast<uint32_t>(Colour::black));
    }
    {
        ScopedEntityPresentationSnapshot scope(retained->entities.get());
        const auto facts = GetGuestPresentationFacts(guestId);
        ASSERT_TRUE(facts.has_value());
        EXPECT_EQ(facts->state, static_cast<uint8_t>(PeepState::onRide));
        EXPECT_EQ(facts->colours & 0xff, static_cast<uint32_t>(Colour::brightRed));
    }
    EXPECT_TRUE(registry.ConsumeEntityVisualChanges().changes.empty());
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 4, true));
    EXPECT_EQ(publication->GetGeneration()->peeps, nullptr);
    EXPECT_NE(publication->GetGeneration()->entities->TryGetEntity(guestId), nullptr);
    EXPECT_NE(retained->peeps->TryGet(guestId), std::nullopt);
}

TEST_F(RetainedPeepPublicationTest, CombinedScheduledCaptureRetainsDeletionAndConsumesEachDirtyIdOnce)
{
    auto& registry = getGameState().entities;
    auto& jobs = context->GetJobPool();
    auto* first = registry.createEntity<Guest>();
    auto* second = registry.createEntity<Guest>();
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    first->animationObjectIndex = second->animationObjectIndex = 0;
    const auto firstId = first->id;
    const auto secondId = second->id;
    const std::array changes{ RetainedPeepAnimationChange{ 0, 1, MakeObject() } };
    const auto catalog = PublishRetainedPeepAnimationCatalog(nullptr, 1, 1, true, changes);
    constexpr auto profile = EntityPublicationProfile::retainedPeepsAndBalloons;
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 1, true, profile, catalog));
    const auto before = publication->GetGeneration();
    second->animationImageIdOffset = 9;
    registry.PublishEntityVisualState(*second); // Deliberately reverse entity-id order.
    registry.entityRemove(first);
    auto* replacement = registry.createEntity<Litter>();
    ASSERT_NE(replacement, nullptr);
    ASSERT_EQ(replacement->id, firstId);
    getGameState().currentTicks = 22;
    publication->ScheduleNext(jobs, registry, catalog);
    EXPECT_TRUE(registry.ConsumeEntityVisualChanges().changes.empty());
    // Synchronous acquisition waits for prepared storage and captures any later changes at this boundary.
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 2, true, profile, catalog));
    const auto current = publication->GetGeneration();
    EXPECT_EQ(current->peeps->TryGet(firstId), std::nullopt);
    ASSERT_NE(current->peeps->TryGet(secondId), std::nullopt);
    EXPECT_EQ(current->peeps->TryGet(secondId)->frameOffset, 9u);
    EXPECT_NE(current->entities->TryGetEntity(firstId), nullptr);
    EXPECT_NE(before->peeps->TryGet(firstId), std::nullopt);
    EXPECT_EQ(before->peeps->TryGet(secondId)->frameOffset, 0u);
    EXPECT_EQ(current->sourceTick, 22u);
    EXPECT_EQ(current->entities->GetBalloonMetrics().bootstrapVisits, 0u);
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 3, true, profile, catalog));
    EXPECT_EQ(publication->GetGeneration()->peeps->chunks, current->peeps->chunks);
}

TEST_F(RetainedPeepPublicationTest, CatalogTransitionBootstrapsAndFailedCaptureCanRetrySameDraw)
{
    auto& registry = getGameState().entities;
    auto& jobs = context->GetJobPool();
    auto* guest = registry.createEntity<Guest>();
    ASSERT_NE(guest, nullptr);
    guest->animationObjectIndex = 0;
    const auto id = guest->id;
    const std::array initial{ RetainedPeepAnimationChange{ 0, 1, MakeObject() } };
    const auto catalog = PublishRetainedPeepAnimationCatalog(nullptr, 1, 1, true, initial);
    constexpr auto profile = EntityPublicationProfile::retainedPeepsAndBalloons;
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 1, true, profile, catalog));
    const auto before = publication->GetGeneration();
    const std::array replacement{ RetainedPeepAnimationChange{ 0, 2, MakeObject(2) } };
    const auto reloaded = PublishRetainedPeepAnimationCatalog(catalog, 1, 2, false, replacement);
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 2, false, profile, reloaded));
    const auto after = publication->GetGeneration();
    EXPECT_GT(after->peeps->epoch, before->peeps->epoch);
    EXPECT_EQ(after->peeps->TryGet(id)->objectGeneration, 2u);
    EXPECT_EQ(before->peeps->TryGet(id)->objectGeneration, 1u);
    EXPECT_EQ(after->entities->GetBalloonMetrics().bootstrapVisits, 1u);
    guest->animationObjectIndex = 1; // Invalid catalog dependency: capture must leave dirty input pending.
    registry.PublishEntityVisualState(*guest);
    EXPECT_THROW(publication->BeginFrame(jobs, registry, 3, true, profile, reloaded), std::invalid_argument);
    EXPECT_EQ(publication->GetGeneration(), after);
    guest->animationObjectIndex = 0;
    guest->animationImageIdOffset = 7;
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 3, true, profile, reloaded));
    EXPECT_EQ(publication->GetGeneration()->peeps->TryGet(id)->frameOffset, 7u);
    EXPECT_EQ(publication->GetGeneration()->entities->GetBalloonMetrics().bootstrapVisits, 1u);
    EXPECT_TRUE(registry.ConsumeEntityVisualChanges().changes.empty());
}

TEST_F(RetainedPeepPublicationTest, EmptyProfileReentryAdvancesPublicationEpochDespiteSameRegistryEpoch)
{
    auto& registry = getGameState().entities;
    auto& jobs = context->GetJobPool();
    auto* guest = registry.createEntity<Guest>();
    ASSERT_NE(guest, nullptr);
    guest->animationObjectIndex = 0;
    const auto id = guest->id;
    const std::array changes{ RetainedPeepAnimationChange{ 0, 1, MakeObject() } };
    const auto catalog = PublishRetainedPeepAnimationCatalog(nullptr, 1, 1, true, changes);
    constexpr auto profile = EntityPublicationProfile::retainedPeepsAndBalloons;
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 1, true, profile, catalog));
    const auto held = publication->GetGeneration();
    const auto registryEpoch = registry.GetEntityVisualEpoch();
    registry.entityRemove(guest);
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 2, true));
    (void)registry.ConsumeEntityVisualChanges();
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 3, false, profile, catalog));
    const auto empty = publication->GetGeneration();
    EXPECT_EQ(empty->sourceEntityEpoch, registryEpoch);
    ASSERT_NE(empty->peeps, nullptr);
    EXPECT_GT(empty->peeps->epoch, held->peeps->epoch);
    EXPECT_GT(empty->peeps->sequence, held->peeps->sequence);
    EXPECT_EQ(empty->peeps->count, 0u);
    EXPECT_EQ(empty->peeps->TryGet(id), std::nullopt);
    EXPECT_EQ(empty->entities->GetCapturedEntityCount(), 0u);
    EXPECT_NE(held->peeps->TryGet(id), std::nullopt);
    // GPU consumers must clear prior residency on this publication-epoch change, even with no uploaded chunks.
    for (const auto& chunk : empty->peeps->chunks)
        EXPECT_EQ(chunk, nullptr);
}

// These integration tests use the shipped peep-animation object, like existing shipped-object metadata tests.
// They exercise real ObjectManager boundaries; synthetic raw/fact validation remains covered above.
namespace
{
    // Deliberately zero-image object and a throwing parser exercise failure paths without adding test hooks to production.
    class CatalogFailureRepository final : public IObjectRepository
    {
        ObjectRepositoryItem _item{};

    public:
        IObjectManager* manager{};
        bool throwOnLoad{};
        bool observedUnavailableDuringLoad{};
        CatalogFailureRepository()
        {
            _item.Type = ObjectType::peepAnimations;
            _item.Identifier = "test.peep_animations.empty";
        }
        void LoadOrConstruct(int32_t) override
        {
        }
        void Construct(int32_t) override
        {
        }
        size_t GetNumObjects() const override
        {
            return 1;
        }
        const ObjectRepositoryItem* GetObjects() const override
        {
            return &_item;
        }
        const ObjectRepositoryItem* FindObjectLegacy(std::string_view id) const override
        {
            return FindObject(id);
        }
        const ObjectRepositoryItem* FindObject(std::string_view id) const override
        {
            return id == _item.Identifier ? &_item : nullptr;
        }
        const ObjectRepositoryItem* FindObject(const RCTObjectEntry*) const override
        {
            return nullptr;
        }
        const ObjectRepositoryItem* FindObject(const ObjectEntryDescriptor& entry) const override
        {
            return FindObject(entry.Identifier);
        }
        std::unique_ptr<Object> LoadObject(const ObjectRepositoryItem* item) override
        {
            return LoadObject(item, true);
        }
        std::unique_ptr<Object> LoadObject(const ObjectRepositoryItem*, bool) override
        {
            observedUnavailableDuringLoad = manager != nullptr && manager->GetPeepAnimationCatalog() == nullptr;
            if (throwOnLoad)
                throw std::runtime_error("Injected catalog lifecycle parse failure");
            auto object = std::make_unique<PeepAnimationsObject>();
            object->SetIdentifier(_item.Identifier);
            object->SetDescriptor(ObjectEntryDescriptor(ObjectType::peepAnimations, _item.Identifier));
            return object;
        }
        void RegisterLoadedObject(const ObjectRepositoryItem*, std::unique_ptr<Object>&& object) override
        {
            _item.LoadedObject = std::move(object);
        }
        void UnregisterLoadedObject(const ObjectRepositoryItem*, Object*) override
        {
            _item.LoadedObject.reset();
        }
        void AddObject(const RCTObjectEntry*, const void*, size_t) override
        {
        }
        void AddObjectFromFile(ObjectGeneration, std::string_view, const void*, size_t) override
        {
        }
        void ExportPackedObject(IStream*) override
        {
        }
    };
} // namespace

TEST(RetainedPeepCatalogTest, ObjectManagerClosesCatalogDuringMutationAndAfterParseOrCaptureFailure)
{
    // This synthetic object has no image data. Enable catalog validation without loading/rendering any sprites.
    struct RestoreNoGraphics
    {
        bool previous = gOpenRCT2NoGraphics;
        ~RestoreNoGraphics()
        {
            gOpenRCT2NoGraphics = previous;
        }
    } restore;
    gOpenRCT2NoGraphics = false;
    CatalogFailureRepository repository;
    auto manager = CreateObjectManager(repository);
    repository.manager = manager.get();
    const auto initial = manager->GetPeepAnimationCatalog();
    ASSERT_NE(initial, nullptr);
    repository.throwOnLoad = true;
    EXPECT_THROW(manager->LoadObject("test.peep_animations.empty"), std::runtime_error);
    EXPECT_TRUE(repository.observedUnavailableDuringLoad);
    EXPECT_EQ(manager->GetPeepAnimationCatalog(), nullptr);
    repository.throwOnLoad = false;
    // Legacy installation succeeds, but a zero-image object cannot produce usable retained facts.
    const auto* empty = manager->LoadObject("test.peep_animations.empty");
    ASSERT_NE(empty, nullptr);
    EXPECT_EQ(manager->GetPeepAnimationCatalog(), nullptr);
    const auto descriptor = empty->GetDescriptor();
    manager->UnloadObjects({ descriptor });
    const auto recovered = manager->GetPeepAnimationCatalog();
    ASSERT_NE(recovered, nullptr);
    EXPECT_TRUE(recovered->slots.empty());
    EXPECT_GT(recovered->epoch, initial->epoch);
    EXPECT_TRUE(initial->slots.empty());
}

TEST_F(RetainedPeepCatalogGraphicsTest, ObjectManagerCatalogLoadUnloadAndReloadOwnsOldFacts)
{
    auto& manager = context->GetObjectManager();
    manager.UnloadAll();
    const auto empty = manager.GetPeepAnimationCatalog();
    ASSERT_NE(empty, nullptr);
    EXPECT_TRUE(empty->slots.empty());
    auto* object = manager.LoadObject("rct2.peep_animations.guest");
    ASSERT_NE(object, nullptr);
    const auto slot = manager.GetLoadedObjectEntryIndex(object);
    const auto first = manager.GetPeepAnimationCatalog();
    ASSERT_NE(first, nullptr);
    ASSERT_TRUE(first->slots.contains(slot));
    ASSERT_NE(first->slots.at(slot).object, nullptr);
    EXPECT_GT(first->epoch, empty->epoch);
    const auto oldFacts = first->slots.at(slot).object;
    const auto oldGeneration = first->slots.at(slot).generation;
    const auto oldFactBytes = oldFacts->facts;
    const auto descriptor = object->GetDescriptor();
    EXPECT_EQ(manager.LoadObject("rct2.peep_animations.guest"), object);
    EXPECT_EQ(manager.GetPeepAnimationCatalog(), first); // An existing-object lookup publishes nothing.
    manager.UnloadObjects({ descriptor });
    const auto removed = manager.GetPeepAnimationCatalog();
    ASSERT_NE(removed, nullptr);
    EXPECT_GT(removed->epoch, first->epoch);
    EXPECT_FALSE(removed->slots.contains(slot));
    EXPECT_EQ(oldFacts->facts, oldFactBytes);
    ASSERT_NE(manager.LoadObject(descriptor, slot), nullptr);
    const auto reloaded = manager.GetPeepAnimationCatalog();
    ASSERT_NE(reloaded, nullptr);
    EXPECT_GT(reloaded->epoch, removed->epoch);
    EXPECT_GT(reloaded->slots.at(slot).generation, oldGeneration);
    EXPECT_EQ(first->slots.at(slot).object, oldFacts);
    EXPECT_EQ(oldFacts->facts, oldFactBytes);
}

TEST_F(RetainedPeepCatalogGraphicsTest, ObjectManagerCatalogTracksAliasRebindAndSamePointerReset)
{
    auto& manager = context->GetObjectManager();
    manager.UnloadAll();
    const ObjectEntryDescriptor descriptor(ObjectType::peepAnimations, "rct2.peep_animations.guest");
    ObjectList aliases;
    aliases.SetObject(0, descriptor);
    aliases.SetObject(1, descriptor);
    manager.LoadObjects(aliases);
    const auto first = manager.GetPeepAnimationCatalog();
    ASSERT_NE(first, nullptr);
    ASSERT_EQ(first->slots.size(), 2u);
    const auto* loaded = manager.GetLoadedObject(ObjectType::peepAnimations, 0);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(manager.GetLoadedObject(ObjectType::peepAnimations, 1), loaded);
    EXPECT_EQ(first->slots.at(1).object->descriptor.objectIndex, 1u);
    manager.LoadObjects(aliases);
    EXPECT_EQ(manager.GetPeepAnimationCatalog(), first);
    EXPECT_EQ(manager.GetLoadedObject(ObjectType::peepAnimations, 0), loaded);
    EXPECT_EQ(manager.GetLoadedObject(ObjectType::peepAnimations, 1), loaded);
    manager.ResetObjects();
    const auto reset = manager.GetPeepAnimationCatalog();
    ASSERT_NE(reset, nullptr);
    EXPECT_EQ(manager.GetLoadedObject(ObjectType::peepAnimations, 0), loaded);
    EXPECT_GT(reset->epoch, first->epoch);
    EXPECT_GT(reset->slots.at(0).generation, first->slots.at(0).generation);
    EXPECT_GT(reset->slots.at(1).generation, first->slots.at(1).generation);
    ObjectList moved;
    moved.SetObject(3, descriptor);
    manager.LoadObjects(moved); // Rebinds an already-loaded pointer without calling its Load.
    const auto rebound = manager.GetPeepAnimationCatalog();
    ASSERT_NE(rebound, nullptr);
    ASSERT_EQ(rebound->slots.size(), 1u);
    EXPECT_TRUE(rebound->slots.contains(3));
    EXPECT_FALSE(rebound->slots.contains(0));
    EXPECT_FALSE(rebound->slots.contains(1));
    EXPECT_GT(rebound->epoch, reset->epoch);
    EXPECT_EQ(manager.GetLoadedObject(ObjectType::peepAnimations, 3), loaded);
    manager.LoadObjects(aliases);
    const auto bothAgain = manager.GetPeepAnimationCatalog();
    manager.UnloadObjects({ descriptor });
    ASSERT_NE(manager.GetPeepAnimationCatalog(), nullptr);
    EXPECT_TRUE(manager.GetPeepAnimationCatalog()->slots.empty());
    EXPECT_EQ(manager.GetLoadedObject(ObjectType::peepAnimations, 0), nullptr);
    EXPECT_EQ(manager.GetLoadedObject(ObjectType::peepAnimations, 1), nullptr);
    EXPECT_EQ(bothAgain->slots.size(), 2u);
}

TEST_F(RetainedPeepCatalogGraphicsTest, CatalogCaptureRejectsUnloadedObjectAndManagerEpochsNeverAlias)
{
    auto& manager = context->GetObjectManager();
    auto secondManager = CreateObjectManager(context->GetObjectRepository());
    ASSERT_NE(secondManager->GetPeepAnimationCatalog(), nullptr);
    ASSERT_NE(manager.GetPeepAnimationCatalog(), nullptr);
    EXPECT_NE(secondManager->GetPeepAnimationCatalog()->epoch, manager.GetPeepAnimationCatalog()->epoch);
    auto object = manager.LoadTempObject("rct2.peep_animations.guest", true);
    ASSERT_NE(object, nullptr);
    auto* peepObject = dynamic_cast<PeepAnimationsObject*>(object.get());
    ASSERT_NE(peepObject, nullptr);
    EXPECT_THROW(CaptureRetainedPeepAnimationObject(*peepObject, 0, 1), std::invalid_argument);
    struct UnloadTemporaryObject
    {
        PeepAnimationsObject& object;
        ~UnloadTemporaryObject()
        {
            object.Unload();
        }
    } unload{ *peepObject };
    peepObject->Load();
    const auto owned = CaptureRetainedPeepAnimationObject(*peepObject, 0, 1);
    peepObject->Unload();
    EXPECT_THROW(CaptureRetainedPeepAnimationObject(*peepObject, 0, 2), std::invalid_argument);
    EXPECT_FALSE(owned->facts.empty());
}

TEST_F(RetainedPeepCatalogGraphicsTest, MissingObjectPreflightPreservesCommittedCatalog)
{
    auto& manager = context->GetObjectManager();
    ASSERT_NE(manager.LoadObject("rct2.peep_animations.guest"), nullptr);
    const auto before = manager.GetPeepAnimationCatalog();
    ASSERT_NE(before, nullptr);
    ObjectList missing;
    missing.SetObject(ObjectType::peepAnimations, 0, "openrct2.peep_animations.missing_catalog_lifecycle_test");
    EXPECT_THROW(manager.LoadObjects(missing), std::exception);
    EXPECT_EQ(manager.GetPeepAnimationCatalog(), before);
    EXPECT_EQ(manager.LoadObject("openrct2.peep_animations.missing_catalog_lifecycle_test"), nullptr);
    EXPECT_EQ(manager.GetPeepAnimationCatalog(), before);
}

TEST_F(RetainedPeepPublicationTest, RecreatedCombinedOwnerCannotAliasHeldRawPublication)
{
    auto& registry = getGameState().entities;
    auto& jobs = context->GetJobPool();
    auto* guest = registry.createEntity<Guest>();
    ASSERT_NE(guest, nullptr);
    guest->animationObjectIndex = 0;
    const auto id = guest->id;
    const std::array changes{ RetainedPeepAnimationChange{ 0, 1, MakeObject() } };
    const auto catalog = PublishRetainedPeepAnimationCatalog(nullptr, 1, 1, true, changes);
    constexpr auto profile = EntityPublicationProfile::retainedPeepsAndBalloons;
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 1, true, profile, catalog));
    const auto held = publication->GetGeneration();
    const auto registryEpoch = registry.GetEntityVisualEpoch();
    ASSERT_NE(held->peeps->TryGet(id), std::nullopt);
    const auto originalFrame = held->peeps->TryGet(id)->frameOffset;
    publication->Reset(jobs);
    publication = std::make_unique<PresentationScene>();
    guest->animationImageIdOffset = 7;
    registry.PublishEntityVisualState(*guest);
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 1, true, profile, catalog));
    const auto changed = publication->GetGeneration();
    EXPECT_GT(changed->peeps->epoch, held->peeps->epoch);
    EXPECT_EQ(changed->peeps->epoch, changed->balloons->epoch);
    EXPECT_EQ(changed->sourceEntityEpoch, registryEpoch);
    EXPECT_EQ(changed->peepAnimations, held->peepAnimations);
    EXPECT_EQ(changed->peeps->TryGet(id)->frameOffset, 7u);
    EXPECT_EQ(held->peeps->TryGet(id)->frameOffset, originalFrame);
    // A normal publication increments only sequence and keeps unchanged chunks shared.
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 2, true, profile, catalog));
    EXPECT_EQ(publication->GetGeneration()->peeps->epoch, changed->peeps->epoch);
    EXPECT_EQ(publication->GetGeneration()->peeps->chunks, changed->peeps->chunks);
    EXPECT_GT(publication->GetGeneration()->peeps->sequence, changed->peeps->sequence);
    publication->Reset(jobs);
    publication = std::make_unique<PresentationScene>();
    registry.entityRemove(guest);
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 1, true, profile, catalog));
    const auto empty = publication->GetGeneration();
    EXPECT_GT(empty->peeps->epoch, changed->peeps->epoch);
    EXPECT_EQ(empty->peeps->count, 0u);
    EXPECT_EQ(empty->entities->GetCapturedEntityCount(), 0u);
    EXPECT_EQ(empty->peeps->TryGet(id), std::nullopt);
    EXPECT_NE(held->peeps->TryGet(id), std::nullopt);
    EXPECT_NE(changed->peeps->TryGet(id), std::nullopt);
}

TEST_F(RetainedPeepPublicationTest, FailedScheduledRawCaptureRetriesSameDrawWithCompleteCoherentWorld)
{
    auto& registry = getGameState().entities;
    auto& jobs = context->GetJobPool();
    auto* guest = registry.createEntity<Guest>();
    ASSERT_NE(guest, nullptr);
    guest->animationObjectIndex = 0;
    const auto id = guest->id;
    const std::array changes{ RetainedPeepAnimationChange{ 0, 1, MakeObject() } };
    const auto catalog = PublishRetainedPeepAnimationCatalog(nullptr, 1, 1, true, changes);
    constexpr auto profile = EntityPublicationProfile::retainedPeepsAndBalloons;
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 1, true, profile, catalog));
    const auto held = publication->GetGeneration();
    const auto oldHeight = held->map->GetFirstElementAt({ 2, 2 })->getBaseZ();
    auto tile = *MapGetFirstElementAt(TileCoordsXY{ 2, 2 });
    tile.setBaseZ(48);
    ASSERT_EQ(ReplaceTileElementsAt({ 2, 2 }, { tile }), TileMutationStatus::ok);
    guest->animationObjectIndex = 1; // Valid raw scalar, absent from the held catalog.
    registry.PublishEntityVisualState(*guest);
    EXPECT_THROW(publication->ScheduleNext(jobs, registry, catalog), std::invalid_argument);
    EXPECT_EQ(publication->GetGeneration(), held);
    jobs.Join(); // The map job is now complete, but the entity preparation failed.
    guest->animationObjectIndex = 0;
    guest->animationImageIdOffset = 7;
    registry.PublishEntityVisualState(*guest);
    getGameState().currentTicks = 32;
    publication->ScheduleNext(jobs, registry, catalog); // Recovery is deferred to the coordinated BeginFrame.
    EXPECT_EQ(publication->GetGeneration(), held);
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 1, false, profile, catalog));
    const auto recovered = publication->GetGeneration();
    EXPECT_EQ(recovered->map->GetFirstElementAt({ 2, 2 })->getBaseZ(), 48);
    ASSERT_NE(recovered->map->GetFirstElementAt({ 15, 15 }), nullptr);
    ASSERT_NE(recovered->peeps->TryGet(id), std::nullopt);
    EXPECT_EQ(recovered->peeps->TryGet(id)->frameOffset, 7u);
    EXPECT_EQ(recovered->sourceTick, 32u);
    EXPECT_EQ(held->map->GetFirstElementAt({ 2, 2 })->getBaseZ(), oldHeight);
    EXPECT_EQ(held->peeps->TryGet(id)->frameOffset, 0u);
    EXPECT_FALSE(publication->BeginFrame(jobs, registry, 1, false, profile, catalog));
}

TEST_F(RetainedPeepPublicationTest, FailedSynchronousRawCaptureCanRecoverOnAsynchronousRetry)
{
    auto& registry = getGameState().entities;
    auto& jobs = context->GetJobPool();
    auto* guest = registry.createEntity<Guest>();
    ASSERT_NE(guest, nullptr);
    guest->animationObjectIndex = 0;
    const auto id = guest->id;
    const std::array changes{ RetainedPeepAnimationChange{ 0, 1, MakeObject() } };
    const auto catalog = PublishRetainedPeepAnimationCatalog(nullptr, 1, 1, true, changes);
    constexpr auto profile = EntityPublicationProfile::retainedPeepsAndBalloons;
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 1, true, profile, catalog));
    const auto held = publication->GetGeneration();
    auto tile = *MapGetFirstElementAt(TileCoordsXY{ 2, 2 });
    tile.setBaseZ(64);
    ASSERT_EQ(ReplaceTileElementsAt({ 2, 2 }, { tile }), TileMutationStatus::ok);
    guest->animationObjectIndex = 1;
    registry.PublishEntityVisualState(*guest);
    EXPECT_THROW(publication->BeginFrame(jobs, registry, 2, true, profile, catalog), std::invalid_argument);
    EXPECT_EQ(publication->GetGeneration(), held);
    // Still-invalid retry must throw again and preserve the same exposed generation.
    EXPECT_THROW(publication->BeginFrame(jobs, registry, 2, false, profile, catalog), std::invalid_argument);
    EXPECT_EQ(publication->GetGeneration(), held);
    guest->animationObjectIndex = 0;
    guest->animationImageIdOffset = 9;
    registry.PublishEntityVisualState(*guest);
    getGameState().currentTicks = 41;
    ASSERT_TRUE(publication->BeginFrame(jobs, registry, 2, false, profile, catalog));
    const auto recovered = publication->GetGeneration();
    EXPECT_EQ(recovered->map->GetFirstElementAt({ 2, 2 })->getBaseZ(), 64);
    ASSERT_NE(recovered->map->GetFirstElementAt({ 15, 15 }), nullptr);
    ASSERT_NE(recovered->peeps->TryGet(id), std::nullopt);
    EXPECT_EQ(recovered->peeps->TryGet(id)->frameOffset, 9u);
    EXPECT_EQ(recovered->sourceTick, 41u);
    EXPECT_TRUE(registry.ConsumeEntityVisualChanges().changes.empty());
    EXPECT_EQ(held->peeps->TryGet(id)->frameOffset, 0u);
}

TEST_F(RetainedPeepPublicationTest, NoGraphicsObjectLifecycleKeepsOptionalCatalogDisabled)
{
    ASSERT_TRUE(gOpenRCT2NoGraphics);
    auto& manager = context->GetObjectManager();
    EXPECT_EQ(manager.GetPeepAnimationCatalog(), nullptr);
    const auto* object = manager.LoadObject("rct2.peep_animations.guest");
    ASSERT_NE(object, nullptr);
    EXPECT_EQ(object->GetNumImages(), 0u);
    EXPECT_EQ(manager.GetPeepAnimationCatalog(), nullptr);
    manager.ResetObjects();
    EXPECT_EQ(manager.GetPeepAnimationCatalog(), nullptr);
    manager.UnloadObjects({ object->GetDescriptor() });
    EXPECT_EQ(manager.GetPeepAnimationCatalog(), nullptr);
}

TEST(RetainedPeepStateTest, FieldDeltasPreserveAppearanceAndDoNotConsumeFailedPreparations)
{
    RetainedPeepScene scene;
    auto raw = MakeRecord(7);
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, true, { raw }), 1));
    const auto held = scene.GetSnapshot();
    RetainedPeepFieldConsumer consumer;
    const auto initial = consumer.Prepare(held);
    EXPECT_TRUE(initial.reset);
    EXPECT_EQ(initial.PayloadBytes(), 128u);
    consumer.Commit(initial);
    raw.x = raw.previousX = 66;
    raw.sourceTick = raw.previousTick = 11;
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, false, { raw }), 2));
    const auto uncommitted = consumer.Prepare(scene.GetSnapshot());
    EXPECT_EQ(uncommitted.PayloadBytes(), 48u);
    EXPECT_TRUE(uncommitted.lifecycle.empty());
    EXPECT_TRUE(uncommitted.appearance.empty());
    EXPECT_TRUE(uncommitted.animation.empty());
    raw.colours = 123;
    ASSERT_TRUE(
        scene.Apply({ .epoch = 1, .appearance = { { raw.id, raw.generation, SplitRetainedPeepRecord(raw).appearance } } }, 3));
    const auto latest = consumer.Prepare(scene.GetSnapshot());
    EXPECT_FALSE(latest.reset);
    EXPECT_EQ(latest.PayloadBytes(), 72u);
    ASSERT_EQ(latest.motion.size(), 1u);
    ASSERT_EQ(latest.appearance.size(), 1u);
    EXPECT_EQ(latest.motion[0].value.sourceTick, 11u); // Colour-only recapture does not rewrite motion time.
    EXPECT_EQ(latest.appearance[0].value.colours, 123u);
    EXPECT_EQ(scene.GetSnapshot()->chunks[0]->animation, held->chunks[0]->animation);
    EXPECT_EQ(held->TryGet(EntityId::FromUnderlying(7))->x, 64);
    consumer.Commit(latest);
    EXPECT_THROW(consumer.Commit(uncommitted), std::invalid_argument);
    EXPECT_EQ(consumer.Prepare(scene.GetSnapshot()).PayloadBytes(), 0u);
    raw.frameOffset = 9;
    ASSERT_TRUE(
        scene.Apply({ .epoch = 1, .animation = { { raw.id, raw.generation, SplitRetainedPeepRecord(raw).animation } } }, 4));
    const auto phase = consumer.Prepare(scene.GetSnapshot());
    EXPECT_EQ(phase.PayloadBytes(), 44u);
    EXPECT_TRUE(phase.motion.empty());
    EXPECT_TRUE(phase.appearance.empty());
}

TEST(RetainedPeepStateTest, SkippedFieldUpdatesCarryLatestAdjacentHistoryAndForceReusedIdentity)
{
    RetainedPeepScene scene;
    auto raw = MakeRecord(2);
    raw.flags |= kRetainedPeepInterpolate;
    raw.sourceTick = 0;
    raw.previousTick = UINT32_MAX;
    raw.previousX = 62;
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, true, { raw }), 1));
    RetainedPeepFieldConsumer consumer;
    consumer.Commit(consumer.Prepare(scene.GetSnapshot()));
    raw.previousX = 64;
    raw.x = 66;
    raw.previousTick = 0;
    raw.sourceTick = 1;
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, false, { raw }), 2));
    raw.previousX = 66;
    raw.x = 68;
    raw.previousTick = 1;
    raw.sourceTick = 2;
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, false, { raw }), 3));
    auto latest = consumer.Prepare(scene.GetSnapshot());
    ASSERT_EQ(latest.motion.size(), 1u);
    EXPECT_EQ(latest.motion[0].value.previousX, 66);
    EXPECT_EQ(latest.motion[0].value.x, 68);
    EXPECT_EQ(latest.motion[0].value.previousTick, 1u);
    EXPECT_EQ(latest.motion[0].value.sourceTick, 2u);
    consumer.Commit(latest);
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, false, { Tombstone(2, 1) }), 4));
    raw.generation = 2; // All payload groups must cross even if values equal the deleted identity.
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, false, { raw }), 5));
    latest = consumer.Prepare(scene.GetSnapshot());
    EXPECT_EQ(latest.PayloadBytes(), 128u);
    ASSERT_EQ(latest.lifecycle.size(), 1u);
    ASSERT_EQ(latest.appearance.size(), 1u);
    EXPECT_EQ(latest.lifecycle[0].value.generation, 2u);
    EXPECT_EQ(latest.appearance[0].generation, 2u);
    consumer.Commit(latest);
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, false, { Tombstone(2, 2) }), 6));
    const auto removed = consumer.Prepare(scene.GetSnapshot());
    EXPECT_EQ(removed.PayloadBytes(), 12u);
    EXPECT_TRUE(removed.motion.empty());
    EXPECT_TRUE(removed.appearance.empty());
    EXPECT_TRUE(removed.animation.empty());
    consumer.Commit(removed);
    ASSERT_TRUE(scene.Apply({ .epoch = 2, .reset = true }, 7));
    const auto empty = consumer.Prepare(scene.GetSnapshot());
    EXPECT_TRUE(empty.reset);
    EXPECT_EQ(empty.PayloadBytes(), 0u); // Reset still clears all resident groups, including when no records follow.
    consumer.Commit(empty);
    EXPECT_EQ(consumer.GetEpoch(), 2u);
}

TEST(RetainedPeepStateTest, SurvivingFieldConsumerAcceptsRecreatedOwnerSequenceForLiveAndEmptyEpochs)
{
    for (const bool empty : { false, true })
    {
        SCOPED_TRACE(empty ? "empty recreated owner" : "populated recreated owner");
        RetainedPeepScene original;
        auto raw = MakeRecord(7);
        ASSERT_TRUE(original.Apply(FullPeepBatch(20, true, { raw }), 1));
        RetainedPeepFieldConsumer consumer;
        consumer.Commit(consumer.Prepare(original.GetSnapshot()));
        raw.colours = 123;
        ASSERT_TRUE(original.Apply(FullPeepBatch(20, false, { raw }), 2));
        consumer.Commit(consumer.Prepare(original.GetSnapshot()));
        const auto held = original.GetSnapshot();
        const auto oldPrepared = consumer.Prepare(held);
        ASSERT_GT(consumer.GetSequence(), 1u);

        // A new publisher owns a globally newer epoch, but starts its own sequence at one.
        RetainedPeepScene recreated;
        RetainedPeepBatch bootstrap{ .epoch = 21, .reset = true };
        if (!empty)
        {
            raw.colours = 456;
            AppendFullPeep(bootstrap, raw);
        }
        ASSERT_TRUE(recreated.Apply(bootstrap, 1));
        const auto replacement = consumer.Prepare(recreated.GetSnapshot());
        EXPECT_TRUE(replacement.reset);
        EXPECT_EQ(replacement.baseEpoch, 20u);
        EXPECT_EQ(replacement.baseSequence, 2u);
        EXPECT_EQ(replacement.PayloadBytes(), empty ? 0u : 128u);
        if (!empty)
        {
            ASSERT_EQ(replacement.lifecycle.size(), 1u);
            ASSERT_EQ(replacement.motion.size(), 1u);
            ASSERT_EQ(replacement.appearance.size(), 1u);
            ASSERT_EQ(replacement.animation.size(), 1u);
            EXPECT_EQ(replacement.appearance[0].value.colours, 456u);
        }
        consumer.Commit(replacement);
        EXPECT_EQ(consumer.GetEpoch(), 21u);
        EXPECT_EQ(consumer.GetSequence(), 1u);
        EXPECT_EQ(consumer.Prepare(recreated.GetSnapshot()).PayloadBytes(), 0u);
        EXPECT_THROW(consumer.Prepare(held), std::invalid_argument);
        EXPECT_THROW(consumer.Commit(oldPrepared), std::invalid_argument);
        // A higher sequence must not make a delayed older epoch current again.
        ASSERT_TRUE(original.Apply({ .epoch = 20 }, 3));
        EXPECT_THROW(consumer.Prepare(original.GetSnapshot()), std::invalid_argument);
        EXPECT_EQ(held->TryGet(EntityId::FromUnderlying(7))->colours, 123u);
    }
}

TEST(RetainedPeepStateTest, DenseMovingPopulationSharesAllUnchangedFieldGroups)
{
    RetainedPeepScene scene;
    RetainedPeepBatch batch{ .epoch = 1, .reset = true };
    for (uint32_t id = 0; id < 256; ++id)
        AppendFullPeep(batch, MakeRecord(id));
    ASSERT_TRUE(scene.Apply(batch, 1));
    const auto initial = scene.GetSnapshot();
    RetainedPeepFieldConsumer consumer;
    consumer.Commit(consumer.Prepare(initial));
    batch.reset = false;
    batch.lifecycle.clear();
    batch.appearance.clear();
    batch.animation.clear();
    for (auto& update : batch.motion)
    {
        auto& raw = update.value;
        raw.x = raw.previousX = 66;
        raw.sourceTick = raw.previousTick = 11;
    }
    ASSERT_TRUE(scene.Apply(batch, 2));
    const auto delta = consumer.Prepare(scene.GetSnapshot());
    EXPECT_EQ(delta.motion.size(), 256u);
    EXPECT_EQ(delta.PayloadBytes(), 256u * 48u);
    EXPECT_TRUE(delta.lifecycle.empty());
    EXPECT_TRUE(delta.appearance.empty());
    EXPECT_TRUE(delta.animation.empty());
    for (size_t index = 0; index < 4; ++index)
    {
        const auto& before = initial->chunks[index];
        const auto& after = scene.GetSnapshot()->chunks[index];
        EXPECT_EQ(before->lifecycle, after->lifecycle);
        EXPECT_EQ(before->appearance, after->appearance);
        EXPECT_EQ(before->animation, after->animation);
        EXPECT_NE(before->motion, after->motion);
    }
    EXPECT_EQ(scene.GetLastApplyMetrics().clonedChunks, 4u);
}

TEST_F(RetainedPeepPublicationTest, StationaryClothingMutatorPublishesOnlyChangedAppearance)
{
    auto& registry = getGameState().entities;
    auto* guest = registry.createEntity<Guest>();
    ASSERT_NE(guest, nullptr);
    guest->animationObjectIndex = 0;
    guest->setClothingColours(Colour::black, Colour::darkBlue);
    guest->moveTo({ 64, 64, 16 });
    RetainedPeepScene scene;
    {
        const auto input = registry.CaptureRetainedEntityPublication(1, generations, false);
        ASSERT_TRUE(scene.Apply(input.peeps, 1));
        registry.AcknowledgeRetainedEntityPublication();
    }
    const auto held = scene.GetSnapshot();
    const auto originalColours = held->TryGet(guest->id)->colours;
    RetainedPeepFieldConsumer consumer;
    consumer.Commit(consumer.Prepare(held));

    guest->setClothingColours(Colour::white, Colour::yellow);
    const auto changed = registry.CaptureRetainedEntityPublication(2, generations, false);
    EXPECT_EQ(changed.dirtyVisits, 1u);
    ASSERT_EQ(changed.peeps.appearance.size(), 1u);
    ASSERT_TRUE(scene.Apply(changed.peeps, 2));
    registry.AcknowledgeRetainedEntityPublication();
    const auto delta = consumer.Prepare(scene.GetSnapshot());
    EXPECT_EQ(delta.PayloadBytes(), 24u);
    EXPECT_TRUE(delta.lifecycle.empty());
    EXPECT_TRUE(delta.motion.empty());
    EXPECT_TRUE(delta.animation.empty());
    ASSERT_EQ(delta.appearance.size(), 1u);
    EXPECT_EQ(
        delta.appearance[0].value.colours, static_cast<uint32_t>(Colour::white) | (static_cast<uint32_t>(Colour::yellow) << 8));
    EXPECT_EQ(held->TryGet(guest->id)->colours, originalColours);
    consumer.Commit(delta);

    guest->setClothingColours(Colour::white, Colour::yellow);
    const auto same = registry.CaptureRetainedEntityPublication(3, generations, false);
    EXPECT_EQ(same.dirtyVisits, 0u);
    EXPECT_EQ(same.peeps.PayloadBytes(), 0u);
    ASSERT_TRUE(scene.Apply(same.peeps, 3));
    registry.AcknowledgeRetainedEntityPublication();
    EXPECT_EQ(consumer.Prepare(scene.GetSnapshot()).PayloadBytes(), 0u);
    EXPECT_EQ(held->TryGet(guest->id)->colours, originalColours);
}

TEST(RetainedPeepStateTest, PartialGroupsShareUnchangedStateAndEmptyBatchesDoNotClone)
{
    RetainedPeepScene scene;
    auto raw = MakeRecord(7);
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, true, { raw }), 1));
    const auto held = scene.GetSnapshot();
    auto appearance = SplitRetainedPeepRecord(raw).appearance;
    appearance.colours = 123;
    const RetainedPeepBatch partial{ .epoch = 1, .appearance = { { 7, 1, appearance } } };
    EXPECT_EQ(partial.PayloadBytes(), 24u);
    ASSERT_TRUE(scene.Apply(partial, 2));
    const auto changed = scene.GetSnapshot();
    EXPECT_EQ(changed->count, 1u);
    EXPECT_EQ(changed->TryGet(EntityId::FromUnderlying(7))->colours, 123u);
    EXPECT_EQ(held->TryGet(EntityId::FromUnderlying(7))->colours, 0u);
    EXPECT_EQ(changed->chunks[0]->lifecycle, held->chunks[0]->lifecycle);
    EXPECT_EQ(changed->chunks[0]->motion, held->chunks[0]->motion);
    EXPECT_EQ(changed->chunks[0]->animation, held->chunks[0]->animation);
    EXPECT_NE(changed->chunks[0]->appearance, held->chunks[0]->appearance);
    ASSERT_TRUE(scene.Apply(partial, 3));
    EXPECT_EQ(scene.GetSnapshot()->chunks[0], changed->chunks[0]);
    EXPECT_EQ(scene.GetLastApplyMetrics().changedRecords, 0u);
    auto redundantMotion = SplitRetainedPeepRecord(raw).motion;
    redundantMotion.sourceTick = redundantMotion.previousTick = 99;
    ASSERT_TRUE(scene.Apply({ .epoch = 1, .motion = { { 7, 1, redundantMotion } } }, 4));
    EXPECT_EQ(scene.GetSnapshot()->chunks[0], changed->chunks[0]);
    EXPECT_EQ(scene.GetSnapshot()->TryGet(EntityId::FromUnderlying(7))->sourceTick, raw.sourceTick);
    ASSERT_TRUE(scene.Apply({ .epoch = 1 }, 5));
    EXPECT_EQ(scene.GetSnapshot()->chunks[0], changed->chunks[0]);
    EXPECT_EQ(scene.GetLastApplyMetrics().copiedRecordBytes, 0u);
    EXPECT_EQ(scene.GetLastApplyMetrics().copiedRevisionBytes, 0u);
    EXPECT_EQ(scene.GetLastApplyMetrics().copiedChunkMetadataBytes, 0u);
}

TEST(RetainedPeepStateTest, PartialValidationAndIncompleteReusePreservePublishedStateAndMetrics)
{
    RetainedPeepScene scene;
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, true, { MakeRecord(7), MakeRecord(8) }), 1));
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, false, { Tombstone(8, 1) }), 2));
    const auto held = scene.GetSnapshot();
    const auto metrics = scene.GetLastApplyMetrics();
    const auto valid = FullPeepBatch(1, false, { MakeRecord(7), MakeRecord(8, 2) });
    auto rejected = [&](RetainedPeepBatch batch) {
        EXPECT_THROW(scene.Apply(batch, 3), std::invalid_argument);
        EXPECT_EQ(scene.GetSnapshot(), held);
        EXPECT_EQ(scene.GetLastApplyMetrics().changedRecords, metrics.changedRecords);
        EXPECT_EQ(scene.GetLastApplyMetrics().copiedRecordBytes, metrics.copiedRecordBytes);
        EXPECT_EQ(scene.GetLastApplyMetrics().copiedRevisionBytes, metrics.copiedRevisionBytes);
        EXPECT_EQ(scene.GetLastApplyMetrics().copiedChunkMetadataBytes, metrics.copiedChunkMetadataBytes);
    };
    // Every missing group on a reincarnated slot is invalid, including omitted lifecycle.
    auto batch = valid;
    batch.lifecycle.pop_back();
    rejected(batch);
    batch = valid;
    batch.motion.pop_back();
    rejected(batch);
    batch = valid;
    batch.appearance.pop_back();
    rejected(batch);
    batch = valid;
    batch.animation.pop_back();
    rejected(batch);
    // The same requirement applies to a never-initialized slot in a new epoch.
    batch = FullPeepBatch(2, true, { MakeRecord(9) });
    batch.animation.clear();
    rejected(batch);
    batch = valid;
    batch.lifecycle.push_back(batch.lifecycle.front());
    rejected(batch);
    batch = valid;
    batch.motion.push_back(batch.motion.front());
    rejected(batch);
    batch = valid;
    batch.appearance.push_back(batch.appearance.front());
    rejected(batch);
    batch = valid;
    batch.animation.push_back(batch.animation.front());
    rejected(batch);
    batch = valid;
    batch.motion.back().generation = 1;
    rejected(batch);
    batch = valid;
    batch.appearance.back().generation = 0;
    rejected(batch);
    batch = valid;
    batch.animation.back().generation = 3;
    rejected(batch);
    batch = valid;
    batch.lifecycle.back().id = kMaxEntities;
    rejected(batch);
    batch = valid;
    batch.motion.back().id = kMaxEntities;
    rejected(batch);
    batch = valid;
    batch.appearance.back().id = kMaxEntities;
    rejected(batch);
    batch = valid;
    batch.animation.back().id = kMaxEntities;
    rejected(batch);
    batch = valid;
    batch.lifecycle.back().value.flags |= kRetainedPeepInterpolate;
    rejected(batch);
    batch = valid;
    batch.motion.back().value.flags |= kRetainedPeepPresent;
    rejected(batch);
    batch = valid;
    batch.animation.back().value.flags |= kRetainedPeepStaff;
    rejected(batch);
    batch = valid;
    batch.motion.back().value.orientation = 32;
    rejected(batch);
    batch = valid;
    batch.motion.back().value.previousTick--;
    rejected(batch);
    batch = valid;
    batch.motion.back().value.flags = kRetainedPeepInterpolate;
    rejected(batch);
    batch = valid;
    batch.appearance.back().value.objectGeneration = 0;
    rejected(batch);
    batch = valid;
    batch.appearance.back().value.objectIndex = UINT16_MAX;
    rejected(batch);
    batch = valid;
    batch.appearance.back().value.colours = UINT16_MAX + 1u;
    rejected(batch);
    batch = valid;
    batch.animation.back().value.frameOffset = UINT8_MAX + 1u;
    rejected(batch);
    batch = valid;
    batch.animation.back().value.width = UINT8_MAX + 1u;
    rejected(batch);
    batch = valid;
    batch.lifecycle.back().value.flags = 0;
    rejected(batch); // Tombstone with payload.
    // A valid update still succeeds at the same sequence after all rejected transactions.
    ASSERT_TRUE(scene.Apply(valid, 3));
    EXPECT_EQ(scene.GetSnapshot()->count, 2u);
    EXPECT_EQ(held->TryGet(EntityId::FromUnderlying(8)), std::nullopt);
    RetainedPeepFieldConsumer consumer;
    const auto delta = consumer.Prepare(scene.GetSnapshot());
    ASSERT_EQ(delta.appearance.size(), 2u);
    EXPECT_EQ(delta.appearance[1].generation, 2u);
}

TEST(RetainedPeepStateTest, AuthoritativeHistoryAcceptsPositiveSpansAndRejectsAmbiguousTime)
{
    RetainedPeepScene scene;
    auto raw = MakeRecord(7);
    raw.flags |= kRetainedPeepInterpolate;
    raw.previousX = raw.x - 8;
    raw.previousTick = 7;
    raw.sourceTick = 11;
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, true, { raw }), 1));
    raw.previousTick = UINT32_MAX - 2;
    raw.sourceTick = 2; // Positive span five across unsigned tick wrap.
    ASSERT_TRUE(scene.Apply(FullPeepBatch(1, false, { raw }), 2));
    const auto held = scene.GetSnapshot();
    for (const uint32_t span : { 0u, 0x80000000u, UINT32_MAX })
    {
        raw.previousTick = 9;
        raw.sourceTick = raw.previousTick + span;
        EXPECT_THROW(scene.Apply(FullPeepBatch(1, false, { raw }), 3), std::invalid_argument);
        EXPECT_EQ(scene.GetSnapshot(), held);
    }
    raw.previousTick = 9;
    raw.sourceTick = 0x80000008u; // Largest unambiguous positive span.
    EXPECT_TRUE(scene.Apply(FullPeepBatch(1, false, { raw }), 3));
}
