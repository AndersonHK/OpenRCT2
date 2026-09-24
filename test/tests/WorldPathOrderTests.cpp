// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include <algorithm>
#include <array>
#include <gtest/gtest.h>
#include <memory>
#include <openrct2/paint/Paint.h>
#include <vector>

namespace PathOrder
{
#include "../../data/shaders/vulkan/world_path_order.glsl"
}

namespace
{
    using namespace PathOrder;

    struct LegacySortScope
    {
        bool previous = gPaintStableSort;
        LegacySortScope()
        {
            gPaintStableSort = false;
        }
        ~LegacySortScope()
        {
            gPaintStableSort = previous;
        }
    };

    void CompareWithPainter(std::array<WorldPathPart, 12> parts, int count, int rotation)
    {
        const LegacySortScope legacy;
        auto session = std::make_unique<PaintSessionCore>();
        std::array<PaintStruct, 12> entries{};
        session->CurrentRotation = static_cast<uint8_t>(rotation);
        session->QuadrantBackIndex = UINT32_MAX;
        session->QuadrantFrontIndex = 0;
        // Build the oracle independently with the CPU coordinate transform and
        // absolute quadrant mapping. No shader bounds or order feeds the oracle.
        constexpr CoordsXY tile{ 640, 672 };
        constexpr int baseZ = 64;
        for (int i = 0; i < count; ++i)
        {
            const auto& p = parts[i];
            auto origin = CoordsXY{ p.boundsX, p.boundsY }.rotate((rotation * 3) & 3) + tile;
            auto size = CoordsXY{ p.sizeX, p.sizeY };
            if (rotation == 0 || rotation == 1)
                --size.x;
            if (rotation == 0 || rotation == 3)
                --size.y;
            size = size.rotate((rotation * 3) & 3);
            auto& entry = entries[i];
            entry.Bounds = { origin.x,          origin.y,          baseZ + p.boundsZ,
                             origin.x + size.x, origin.y + size.y, baseZ + p.boundsZ + p.sizeZ };
            const auto local = worldPathOrderBounds(p, rotation);
            EXPECT_EQ(local.x + tile.x, entry.Bounds.x);
            EXPECT_EQ(local.y + tile.y, entry.Bounds.y);
            EXPECT_EQ(local.ex + tile.x, entry.Bounds.x_end);
            EXPECT_EQ(local.ey + tile.y, entry.Bounds.y_end);
            EXPECT_EQ(local.z + baseZ, entry.Bounds.z);
            EXPECT_EQ(local.ez + baseZ, entry.Bounds.z_end);
            constexpr int range = MaxPaintQuadrants * 32;
            int hash = 0;
            switch (rotation)
            {
                case 0:
                    hash = origin.x + origin.y;
                    break;
                case 1:
                    hash = origin.y - origin.x + range / 2;
                    break;
                case 2:
                    hash = -origin.y - origin.x + range;
                    break;
                case 3:
                    hash = origin.x - origin.y + range / 2;
                    break;
            }
            const auto quadrant = static_cast<uint32_t>(std::clamp(hash / 32, 0, MaxPaintQuadrants - 1));
            entry.QuadrantIndex = static_cast<uint16_t>(quadrant);
            entry.NextQuadrantEntry = session->Quadrants[quadrant];
            session->Quadrants[quadrant] = &entry;
            session->QuadrantBackIndex = std::min(session->QuadrantBackIndex, quadrant);
            session->QuadrantFrontIndex = std::max(session->QuadrantFrontIndex, quadrant);
        }
        PaintSessionArrange(*session);
        std::vector<int> expected;
        for (auto* p = session->PaintHead; p != nullptr && expected.size() <= entries.size(); p = p->NextQuadrantEntry)
            expected.push_back(static_cast<int>(p - entries.data()));
        ASSERT_EQ(expected.size(), static_cast<size_t>(count));
        const auto actual = worldPathOrder(parts.data(), count, rotation);
        ASSERT_EQ(actual.count, count);
        EXPECT_EQ((std::vector<int>(actual.indices, actual.indices + actual.count)), expected);
    }

    void Append(std::array<WorldPathPart, 12>& parts, int& count, const WorldPathParts& additions)
    {
        for (int i = 0; i < additions.count; ++i)
            parts[count++] = additions.parts[i];
    }
} // namespace

TEST(WorldPathRulesTest, LocalParentOrderMatchesLegacyPainterForFlatMasksAndAdditions)
{
    for (int rotation = 0; rotation < 4; ++rotation)
        for (int edges = 0; edges < 16; ++edges)
            for (int corners = 0; corners < 16; ++corners)
                for (bool queue : { false, true })
                {
                    SCOPED_TRACE(::testing::Message() << rotation << '/' << edges << '/' << corners << '/' << queue);
                    std::array<WorldPathPart, 12> parts{};
                    int count = 0;
                    parts[count++] = worldPathSurfacePart(0, worldPathRotateMask(edges, rotation), true, false);
                    Append(parts, count, worldPathAdditions(edges, rotation, false, edges % 3, false, 0, false, 0));
                    Append(parts, count, worldPathFences(edges, corners, 0, rotation, false, queue, true, false, true, true));
                    CompareWithPainter(parts, count, rotation);
                }
}

TEST(WorldPathRulesTest, LocalParentOrderMatchesLegacyPainterForSlopesAndZeroExtentBounds)
{
    for (int rotation = 0; rotation < 4; ++rotation)
        for (int slope = 0; slope < 4; ++slope)
            for (int edges = 0; edges < 16; ++edges)
                for (bool queue : { false, true })
                {
                    SCOPED_TRACE(::testing::Message() << rotation << '/' << slope << '/' << edges << '/' << queue);
                    std::array<WorldPathPart, 12> parts{};
                    int count = 0;
                    parts[count++] = worldPathSurfacePart(
                        16 + ((slope + rotation) & 3), worldPathRotateMask(edges, rotation), true, false);
                    Append(parts, count, worldPathAdditions(edges, rotation, true, edges % 3, true, 255, false, 0));
                    Append(parts, count, worldPathFences(edges, 0, slope, rotation, true, queue, true, false, false, true));
                    CompareWithPainter(parts, count, rotation);
                }
    // Exercise all three local quadrants, zero dimensions and exactly touching
    // endpoints. These are parent bounds, not raster dimensions or sprite culls.
    for (int rotation = 0; rotation < 4; ++rotation)
    {
        std::array<WorldPathPart, 12> parts{};
        for (int i = 0; i < 12; ++i)
            parts[i] = worldPathPart(
                i, 0, 0, 0, (i % 3) * 16, ((i / 3) % 3) * 16, (i % 4) * 8, i % 3, (i + 1) % 3, (i % 2) * 16);
        CompareWithPainter(parts, 12, rotation);
    }
}

TEST(WorldPathRulesTest, LocalParentOrderRejectsOutOfContractInputs)
{
    std::array<WorldPathPart, 12> parts{};
    EXPECT_EQ(worldPathOrder(parts.data(), 0, 0).count, 0);
    EXPECT_EQ(worldPathOrder(parts.data(), -1, 0).count, 0);
    EXPECT_EQ(worldPathOrder(parts.data(), 13, 0).count, 0);
    EXPECT_EQ(worldPathOrder(parts.data(), 1, 4).count, 0);
    parts[0].boundsX = -1;
    EXPECT_EQ(worldPathOrder(parts.data(), 1, 0).count, 0);
    parts[0].boundsX = 33;
    EXPECT_EQ(worldPathOrder(parts.data(), 1, 0).count, 0);
}
