// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <array>
#include <gtest/gtest.h>
#include <iterator>
#include <openrct2-renderer/gpu/GpuWorldVehicleCatalog.h>
#include <openrct2/GameState.h>
#include <openrct2/SpriteIds.h>
#include <openrct2/core/Speed.hpp>
#include <openrct2/entity/EntityPresentationSnapshot.h>
#include <openrct2/entity/EntityRegistry.h>
#include <openrct2/entity/Yaw.hpp>
#include <openrct2/paint/Paint.h>
#include <openrct2/paint/vehicle/VehiclePaint.h>
#include <openrct2/ride/CarEntry.h>
#include <openrct2/ride/Vehicle.h>
#include <openrct2/ride/ted/TrackElemType.h>

namespace VehicleRuleTest
{
    const CarEntry* cars;
#define VEHICLE_RULE_WORD(i) OpenRCT2::Ui::Gpu::kWorldVehicleSelector[i]
#define VEHICLE_CAR_PRESENT(c) ((c) >= 0 && (c) < 4)
#define VEHICLE_GROUP_PRECISION(c, g) int(cars[c].spriteGroups[g].spritePrecision)
#define VEHICLE_TRACK_DOWN90 127
#define VEHICLE_TRACK_DOWN90_TO_DOWN60 129
#define VEHICLE_TRACK_DOWN60_TO_DOWN90 131
#include "../../data/shaders/vulkan/world_vehicle_rules.glsl"
#undef VEHICLE_RULE_WORD
#undef VEHICLE_CAR_PRESENT
#undef VEHICLE_GROUP_PRECISION
#undef VEHICLE_TRACK_DOWN90
#undef VEHICLE_TRACK_DOWN90_TO_DOWN60
#undef VEHICLE_TRACK_DOWN60_TO_DOWN90
} // namespace VehicleRuleTest
namespace VehicleOriginalOracle
{
    void OracleSplash(PaintSession&, int32_t, const OpenRCT2::Vehicle*, const CarEntry*);
    std::vector<uint32_t> images;
    PaintStruct* CaptureParent(PaintSession&, ::ImageId image, const CoordsXYZ&, const BoundBoxXYZ&)
    {
        images.push_back(image.GetIndex());
        return nullptr;
    }
    PaintStruct* CaptureChild(PaintSession&, ::ImageId image, const CoordsXYZ&, const BoundBoxXYZ&)
    {
        images.push_back(image.GetIndex());
        return nullptr;
    }
#define PaintAddImageAsParent CaptureParent
#define PaintAddImageAsChild CaptureChild
#define VehicleVisualSplashEffect OracleSplash
#include "../../src/openrct2/paint/vehicle/VehiclePaint.cpp"
#undef VehicleVisualSplashEffect
#undef PaintAddImageAsParent
#undef PaintAddImageAsChild
} // namespace VehicleOriginalOracle

TEST(WorldVehicleRulesTest, ImmutableSelectorMatchesOriginalPitchRollFallbacksAndRestraints)
{
    using namespace OpenRCT2;
    static_assert(EnumValue(TrackElemType::down90) == 127);
    static_assert(EnumValue(TrackElemType::down90ToDown60) == 129);
    static_assert(EnumValue(TrackElemType::down60ToDown90) == 131);
    std::array<CarEntry, 4> cars{};
    VehicleRuleTest::cars = cars.data();
    PaintSession session{};
    Vehicle vehicle{};
    vehicle.SwingSprite = 3;
    vehicle.TrackTypeAndDirection = 0;
    // All groups, flat-only fallback, and alternate missing groups exercise the
    // original fallback graph rather than checking a mirror image-index table.
    for (int availability = 0; availability < 3; availability++)
    {
        for (uint32_t c = 0; c < cars.size(); c++)
        {
            cars[c].drawOrder = 0;
            cars[c].baseNumFrames = 16;
            cars[c].effectVisual = EffectVisual::unknown1;
            for (uint32_t g = 0; g < EnumValue(SpriteGroupType::count); g++)
            {
                cars[c].spriteGroups[g].imageId = 1000 + c * 100000 + g * 2000;
                const bool enabled = g == 0 || availability == 0 || (availability == 2 && (g & 1) != 0);
                cars[c].spriteGroups[g].spritePrecision = enabled ? Entity::Yaw::SpritePrecision::sprites32
                                                                  : Entity::Yaw::SpritePrecision::none;
            }
        }
        for (int pitch = 0; pitch < EnumValue(VehiclePitch::pitchCount); pitch++)
            for (int roll = 0; roll < EnumValue(VehicleRoll::rollCount); roll++)
                for (int direction = 0; direction < 32; direction += 4)
                    for (int flags = 0; flags < 4; flags++)
                    {
                        SCOPED_TRACE(
                            ::testing::Message()
                            << availability << '/' << pitch << '/' << roll << '/' << direction << '/' << flags);
                        vehicle.pitch = static_cast<VehiclePitch>(pitch);
                        vehicle.roll = static_cast<VehicleRoll>(roll);
                        vehicle.flags.clearAll();
                        if (flags & 1)
                            vehicle.flags.set(VehicleFlag::carIsInverted);
                        if (flags & 2)
                            vehicle.flags.set(VehicleFlag::carIsReversed);
                        vehicle.restraints_position = (direction & 8) ? 192 : 0;
                        VehicleOriginalOracle::images.clear();
                        VehicleOriginalOracle::VehicleVisualDefault(session, direction, 64, &vehicle, &cars[2]);
                        ASSERT_EQ(VehicleOriginalOracle::images.size(), 1u);
                        const auto selected = VehicleRuleTest::worldVehicleSelect(
                            2, pitch, roll, direction, static_cast<int>(vehicle.flags.holder), 0, vehicle.restraints_position);
                        ASSERT_GE(selected.car, 0);
                        uint32_t image = cars[selected.car].getSpriteOffset(
                            static_cast<SpriteGroupType>(selected.group), selected.yaw, static_cast<uint8_t>(selected.rank));
                        if (selected.swing)
                            image += vehicle.SwingSprite;
                        EXPECT_EQ(image, VehicleOriginalOracle::images[0]);
                    }
    }
}

TEST(WorldVehicleRulesTest, CatalogResolvesOnlyUsedCarBanksAndPreservesSelectorIdentity)
{
    using namespace OpenRCT2::Drawing;
    VehiclePresentationCatalog source;
    source.cars.resize(8);
    for (uint32_t i = 0; i < 8; i++)
    {
        auto& car = source.cars[i];
        car.present = true;
        car.imageBase = car.baseImage = 100000 + i * 100;
        car.imageCount = 100;
        car.carImages = 8;
        car.seatingRows = 2;
    }
    std::vector<uint32_t> appended;
    const std::array<uint32_t, 1> used{ 3 };
    const auto catalog = OpenRCT2::Ui::Gpu::BuildWorldVehicleCatalog(source, used, 1, [&](uint32_t image) {
        appended.push_back(image);
        return static_cast<uint32_t>(appended.size() - 1);
    });
    for (uint32_t image = 100300; image < 100324; image++)
        EXPECT_NE(std::find(appended.begin(), appended.end(), image), appended.end());
    for (uint32_t image : appended)
        EXPECT_TRUE(image < 100000 || (image >= 100300 && image < 100324));
    EXPECT_NO_THROW(OpenRCT2::Ui::Gpu::ValidateWorldVehicleCatalog(catalog.words));
    auto broken = catalog.words;
    broken[broken[4] + 9] ^= 1;
    EXPECT_THROW(OpenRCT2::Ui::Gpu::ValidateWorldVehicleCatalog(broken), std::invalid_argument);
    source.cars[3].imageCount = 23;
    EXPECT_THROW(
        OpenRCT2::Ui::Gpu::BuildWorldVehicleCatalog(source, used, 1, [](uint32_t i) { return i; }), std::invalid_argument);
}

TEST(WorldVehicleRulesTest, ResidencyFollowsFamilyOwnershipRatherThanHotVehiclePose)
{
    using namespace OpenRCT2::Drawing;
    auto catalog = std::make_shared<VehiclePresentationCatalog>();
    catalog->cars.resize(13); // Three loaded objects and the separate original G1 cable car.
    catalog->cableCar = 12;
    for (auto& car : catalog->cars)
        car.present = true;
    catalog->cars[3].present = false;
    auto records = std::make_shared<std::vector<VehiclePresentationRecord>>(2);
    (*records)[0].carSlot = 1;
    (*records)[1].carSlot = 2;
    VehiclePresentationSnapshot first;
    first.catalog = catalog;
    first.records = records;
    first.worldEpoch = 1;
    first.entityEpoch = 2;
    UpdateVehiclePresentationResidency(first, nullptr);
    EXPECT_EQ(*first.usedCars, (std::vector<uint32_t>{ 0, 1, 2 }));

    auto next = first;
    for (uint32_t pose = 0; pose < 4; ++pose)
    {
        auto moving = std::make_shared<std::vector<VehiclePresentationRecord>>(*records);
        (*moving)[0].flags = pose & 1 ? 1u << 11 : 0;
        (*moving)[0].pitch = pose & 2 ? 56 : 0;
        (*moving)[0].roll = pose & 2 ? 15 : 0;
        (*moving)[0].orientation = pose * 8;
        (*moving)[0].animation = pose;
        (*moving)[0].x = pose * 32;
        next.records = moving;
        UpdateVehiclePresentationResidency(next, &first);
        EXPECT_EQ(next.usedCars, first.usedCars);
    }
    // Removing one owner, or changing its car variant within the object, keeps
    // the complete family bank alive. Only the final family owner evicts it.
    auto remaining = std::make_shared<std::vector<VehiclePresentationRecord>>(1);
    remaining->front().carSlot = 0;
    next.records = remaining;
    UpdateVehiclePresentationResidency(next, &first);
    EXPECT_EQ(next.usedCars, first.usedCars);
    remaining = std::make_shared<std::vector<VehiclePresentationRecord>>(*remaining);
    remaining->front().carSlot = 5;
    next.records = remaining;
    UpdateVehiclePresentationResidency(next, &first);
    EXPECT_NE(next.usedCars, first.usedCars);
    EXPECT_EQ(*next.usedCars, (std::vector<uint32_t>{ 4, 5, 6, 7 }));
    // The third loaded family is never admitted; a cable car does not admit it.
    remaining = std::make_shared<std::vector<VehiclePresentationRecord>>(*remaining);
    remaining->front().carSlot = 12;
    next.records = remaining;
    UpdateVehiclePresentationResidency(next, &first);
    EXPECT_EQ(*next.usedCars, (std::vector<uint32_t>{ 12 }));
    next.records = std::make_shared<const std::vector<VehiclePresentationRecord>>();
    UpdateVehiclePresentationResidency(next, &first);
    EXPECT_TRUE(next.usedCars->empty());

    next = first;
    next.catalog = std::make_shared<VehiclePresentationCatalog>(*catalog);
    UpdateVehiclePresentationResidency(next, &first);
    EXPECT_EQ(*next.usedCars, *first.usedCars);
    EXPECT_NE(next.usedCars, first.usedCars);
    next = first;
    ++next.worldEpoch;
    UpdateVehiclePresentationResidency(next, &first);
    EXPECT_NE(next.usedCars, first.usedCars);
    next = first;
    ++next.entityEpoch;
    UpdateVehiclePresentationResidency(next, &first);
    EXPECT_NE(next.usedCars, first.usedCars);
}

TEST(WorldVehicleRulesTest, SpecializedArtworkAdmissionCoversOriginalPainterOffsetsBeyondGenericBanks)
{
    using namespace OpenRCT2::Drawing;
    using namespace OpenRCT2::Ui::Gpu;
    for (const auto [style, required] : std::array<std::pair<uint32_t, uint32_t>, 6>{
             { { 2, 21 }, { 3, 36 }, { 4, 360 }, { 9, 136 }, { 15, 360 }, { 17, 549 } } })
    {
        SCOPED_TRACE(style);
        VehiclePresentationCatalog source;
        source.cars.resize(1);
        auto& car = source.cars.front();
        car.present = true;
        car.paintStyle = style;
        car.imageBase = car.baseImage = 100000;
        car.carImages = 8;
        car.imageCount = required;
        std::vector<uint32_t> images;
        const std::array<uint32_t, 1> used{ 0 };
        BuildWorldVehicleCatalog(source, used, 1, [&](uint32_t image) {
            images.push_back(image);
            return image;
        });
        for (uint32_t offset = 0; offset < required; ++offset)
            EXPECT_TRUE(std::binary_search(images.begin(), images.end(), car.baseImage + offset));
        EXPECT_FALSE(std::binary_search(images.begin(), images.end(), car.baseImage + required));
        car.imageCount = required - 1;
        EXPECT_THROW(BuildWorldVehicleCatalog(source, used, 1, [](uint32_t image) { return image; }), std::invalid_argument);
    }
    VehiclePresentationCatalog golf;
    golf.cars.resize(2);
    golf.cars[0].imageBase = golf.cars[0].baseImage = 100000;
    golf.cars[0].imageCount = 149;
    golf.cars[1] = golf.cars[0];
    golf.cars[1].present = true;
    golf.cars[1].paintStyle = 5;
    golf.cars[1].baseImage += 32;
    golf.cars[1].carImages = 32;
    std::vector<uint32_t> images;
    BuildWorldVehicleCatalog(golf, std::array<uint32_t, 1>{ 1 }, 1, [&](uint32_t image) {
        images.push_back(image);
        return image;
    });
    EXPECT_TRUE(std::binary_search(images.begin(), images.end(), 100000u));
    EXPECT_TRUE(std::binary_search(images.begin(), images.end(), 100148u));
    EXPECT_FALSE(std::binary_search(images.begin(), images.end(), 100149u));

    VehiclePresentationCatalog cable;
    cable.cableCar = 0;
    cable.cars.resize(1);
    cable.cars[0].present = true;
    cable.cars[0].imageBase = cable.cars[0].baseImage = 29110;
    cable.cars[0].imageCount = 184;
    images.clear();
    const auto cableCatalog = BuildWorldVehicleCatalog(cable, std::array<uint32_t, 1>{ 0 }, 1, [&](uint32_t image) {
        images.push_back(image);
        return image;
    });
    EXPECT_EQ(cableCatalog.words[16 + 4], 0u); // Preserve raw numCarImages, not a fabricated row stride.
    EXPECT_TRUE(std::binary_search(images.begin(), images.end(), 29110u));
    EXPECT_TRUE(std::binary_search(images.begin(), images.end(), 29293u));
    EXPECT_FALSE(std::binary_search(images.begin(), images.end(), 29294u));
}

TEST(WorldVehicleRulesTest, RotoDropInverseSeatLookupMatchesOriginalRingAndLayersStayBounded)
{
    for (int animation = 0; animation < 256; animation += 4)
        for (int yaw = 0; yaw < 32; yaw += 8)
            for (int passengers = 0; passengers <= 32; ++passengers)
            {
                // Original painter fills 64 angular slots, then visits 0,48,1,47,...,24.
                // The shader instead inverts the mapping without a private array.
                std::array<int, 64> seats;
                seats.fill(-1);
                for (int passenger = 0; passenger < passengers; ++passenger)
                {
                    int slot = (passenger & 3) * 16 + (passenger & 0xFC);
                    slot = (slot + animation / 4 + (yaw / 8) * 16) & 63;
                    seats[slot] = passenger;
                }
                for (int ordinal = 0; ordinal <= 48; ++ordinal)
                {
                    const int slot = ordinal % 2 ? 48 - ordinal / 2 : ordinal / 2;
                    ASSERT_EQ(VehicleRuleTest::worldVehicleRotoVisibleSlot(ordinal), slot);
                    ASSERT_EQ(VehicleRuleTest::worldVehicleRotoPassenger(slot, animation, yaw, passengers), seats[slot]);
                }
            }
    for (int ordinal = 0; ordinal <= 48; ++ordinal)
    {
        const int slot = VehicleRuleTest::worldVehicleRotoVisibleSlot(ordinal);
        EXPECT_LE(std::abs(VehicleRuleTest::worldVehicleRotoDepthX(slot)), 11);
        EXPECT_LE(std::abs(VehicleRuleTest::worldVehicleRotoDepthY(slot)), 11);
        EXPECT_GE(VehicleRuleTest::worldVehicleRotoLayer(ordinal), 3);
        EXPECT_LE(VehicleRuleTest::worldVehicleRotoLayer(ordinal), 15);
    }
}

TEST(WorldVehicleRulesTest, ClassicSpinnerDoesNotSilentlyTruncateLargeRiderGroups)
{
    EXPECT_TRUE(VehicleRuleTest::worldVehicleClassicLayersValid(16));
    EXPECT_TRUE(VehicleRuleTest::worldVehicleClassicLayersValid(28));
    EXPECT_TRUE(VehicleRuleTest::worldVehicleClassicLayersValid(29));
    EXPECT_FALSE(VehicleRuleTest::worldVehicleClassicLayersValid(30));
    EXPECT_FALSE(VehicleRuleTest::worldVehicleClassicLayersValid(32));
    EXPECT_FALSE(VehicleRuleTest::worldVehicleClassicLayersValid(33));
}
