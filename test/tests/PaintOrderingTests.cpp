/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <gtest/gtest.h>

#include <openrct2/paint/Paint.h>

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace
{
    constexpr uint32_t kSupport = 1;
    constexpr uint32_t kTree = 2;
    constexpr uint32_t kVehicle = 3;

    struct ScopedPaintSort
    {
        bool previous = gPaintStableSort;

        explicit ScopedPaintSort(std::optional<bool> stable)
        {
            if (stable.has_value())
                gPaintStableSort = *stable;
        }

        ~ScopedPaintSort()
        {
            gPaintStableSort = previous;
        }
    };

    PaintStructBoundBox BoundsForRotation(PaintStructBoundBox bounds, uint8_t rotation)
    {
        // Paint bounds retain oriented endpoints, rather than normalized minima and maxima.
        // Reflect the same small scene inside its tile to exercise all four comparator specializations.
        if (rotation == 1 || rotation == 2)
        {
            bounds.x = 671 - bounds.x;
            bounds.x_end = 671 - bounds.x_end;
        }
        if (rotation == 2 || rotation == 3)
        {
            bounds.y = 671 - bounds.y;
            bounds.y_end = 671 - bounds.y_end;
        }
        return bounds;
    }

    std::vector<uint32_t> ArrangeOverlappingScene(
        uint8_t rotation, std::optional<bool> stable, std::optional<int32_t> vehicleHeight,
        bool treeBelowSupport = false, bool treeFirst = false)
    {
        const ScopedPaintSort sort(stable);
        auto session = std::make_unique<PaintSessionCore>();
        std::array<PaintStruct, 3> entries{};
        auto& support = entries[0];
        auto& tree = entries[1];
        auto& vehicle = entries[2];

        // A narrow tree volume crosses the height of a wider support segment. Neither static
        // volume is strictly behind the other, so the initial paint order resolves their overlap.
        support.Bounds = BoundsForRotation({ 320, 320, 16, 351, 351, 48 }, rotation);
        tree.Bounds = BoundsForRotation({ 330, 330, 0, 331, 331, treeBelowSupport ? 8 : 64 }, rotation);
        const auto height = vehicleHeight.value_or(80);
        vehicle.Bounds = BoundsForRotation({ 328, 328, height, 343, 343, height + 16 }, rotation);
        support.image_id = ImageId(kSupport);
        tree.image_id = ImageId(kTree);
        vehicle.image_id = ImageId(kVehicle);

        constexpr uint16_t quadrant = 42;
        for (auto& entry : entries)
            entry.QuadrantIndex = quadrant;
        session->CurrentRotation = rotation;
        session->QuadrantBackIndex = session->QuadrantFrontIndex = quadrant;

        auto* firstStatic = treeFirst ? &tree : &support;
        auto* lastStatic = treeFirst ? &support : &tree;
        firstStatic->NextQuadrantEntry = lastStatic;
        vehicle.NextQuadrantEntry = firstStatic;
        session->Quadrants[quadrant] = vehicleHeight.has_value() ? &vehicle : firstStatic;

        PaintSessionArrange(*session);

        std::vector<uint32_t> order;
        for (auto* entry = session->PaintHead; entry != nullptr; entry = entry->NextQuadrantEntry)
        {
            order.push_back(entry->image_id.GetIndex());
            // A malformed linked list should fail without hanging this CPU-only fixture.
            if (order.size() > entries.size())
                break;
        }
        return order;
    }
}

TEST(PaintOrderingTest, LegacyInsertionReversesStaticOverlapWhenVehicleMovesInFront)
{
    for (uint8_t rotation = 0; rotation < 4; ++rotation)
    {
        SCOPED_TRACE(rotation);
        EXPECT_EQ(ArrangeOverlappingScene(rotation, false, std::nullopt), (std::vector<uint32_t>{ kSupport, kTree }));
        EXPECT_EQ(
            ArrangeOverlappingScene(rotation, false, 80), (std::vector<uint32_t>{ kTree, kSupport, kVehicle }));
    }
}

TEST(PaintOrderingTest, StableInsertionPreservesStaticOverlapAcrossVehicleMovement)
{
    for (uint8_t rotation = 0; rotation < 4; ++rotation)
    {
        SCOPED_TRACE(rotation);
        EXPECT_EQ(ArrangeOverlappingScene(rotation, true, std::nullopt), (std::vector<uint32_t>{ kSupport, kTree }));
        EXPECT_EQ(
            ArrangeOverlappingScene(rotation, true, 16), (std::vector<uint32_t>{ kVehicle, kSupport, kTree }));
        for (const auto height : { 80, 88, 80 })
        {
            EXPECT_EQ(
                ArrangeOverlappingScene(rotation, true, height), (std::vector<uint32_t>{ kSupport, kTree, kVehicle }));
        }
        EXPECT_EQ(ArrangeOverlappingScene(rotation, true, std::nullopt), (std::vector<uint32_t>{ kSupport, kTree }));
    }
}

TEST(PaintOrderingTest, StableOrderingRespectsBoundsAndPreservesEitherAmbiguousInputOrder)
{
    for (uint8_t rotation = 0; rotation < 4; ++rotation)
    {
        SCOPED_TRACE(rotation);
        // Stability is not a type priority: an already-front support remains in front for an ambiguous pair.
        EXPECT_EQ(
            ArrangeOverlappingScene(rotation, true, 80, false, true),
            (std::vector<uint32_t>{ kTree, kSupport, kVehicle }));
        for (const auto treeFirst : { false, true })
        {
            // A genuinely separated tree belongs behind the support regardless of initial list order.
            EXPECT_EQ(
                ArrangeOverlappingScene(rotation, true, 80, true, treeFirst),
                (std::vector<uint32_t>{ kTree, kSupport, kVehicle }));
        }
    }
}
