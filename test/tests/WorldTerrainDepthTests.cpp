// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#include "../../src/openrct2/paint/tile_element/Paint.TileElement.h"

#include <array>
#include <gtest/gtest.h>
namespace TerrainDepthRules
{
#include "../../data/shaders/vulkan/world_path_rules.glsl"
#include "../../data/shaders/vulkan/world_surface_rules.glsl"
#include "../../data/shaders/vulkan/world_terrain_depth.glsl"
} // namespace TerrainDepthRules
using namespace TerrainDepthRules;

TEST(WorldTerrainDepthTest, EveryPlanarRawSlopeMatchesWorldCoordinatesAcrossRotations)
{
    for (int rotation = 0; rotation < 4; ++rotation)
    {
        for (int slope = 0; slope < 32; ++slope)
        {
            SCOPED_TRACE(rotation);
            SCOPED_TRACE(slope);
            const int relative = terrainRelativeSlope(slope, rotation) | (slope & 16);
            std::array<int, 4> h{};
            for (int i = 0; i < 4; ++i)
                h[i] = 16 * worldSurfaceCornerHeight(320, relative, i);
            const bool planar = h[0] + h[2] == h[1] + h[3];
            const int dx = (h[3] - h[0]) / 16, dy = (h[1] - h[0]) / 16;
            const bool supported = planar && 2 - dx - dy != 0;
            for (const auto origin : { std::array<int, 2>{ 0, 0 }, { -32000, 31968 }, { 31968, 31968 } })
            {
                const auto p = worldTerrainDepthPlane(h[0], h[1], h[2], h[3], origin[0], origin[1]);
                ASSERT_EQ(p.valid != 0, supported);
                if (!supported)
                    continue;
                // Direct physical XYZ oracle; include corner, edge and interior points.
                for (int x = 0; x <= 32; x += 4)
                {
                    for (int y = 0; y <= 32; y += 4)
                    {
                        const float worldX = static_cast<float>(origin[0] + x);
                        const float worldY = static_cast<float>(origin[1] + y);
                        const float z = h[0] + dx * x * 0.5f + dy * y * 0.5f;
                        const float u = worldY - worldX, v = (worldX + worldY) * 0.5f - z;
                        EXPECT_FLOAT_EQ(worldTerrainDepthAt(p.dx, p.dy, p.twiceIntercept, u, v), worldX + worldY + z);
                    }
                }
            }
        }
    }
}

TEST(WorldTerrainDepthTest, NonplanarAndEdgeOnCornersRemainExplicitlyUnsupported)
{
    EXPECT_EQ(worldTerrainDepthPlane(0, 0, 16, 0, 0, 0).valid, 0);
    EXPECT_EQ(worldTerrainDepthPlane(0, 16, 32, 16, 0, 0).valid, 0);
    EXPECT_EQ(worldTerrainDepthPlane(0, 7, 7, 0, 0, 0).valid, 0);
    const auto descending = worldTerrainDepthPlane(32, 16, 0, 16, 0, 0);
    ASSERT_EQ(descending.valid, 1);
    EXPECT_FLOAT_EQ(worldTerrainDepthAt(descending.dx, descending.dy, descending.twiceIntercept, 0.0f, 32.0f), 64.0f);
}

TEST(WorldTerrainDepthTest, PathRampPlanesMatchOriginalRiseDirectionsAcrossRotations)
{
    for (int direction = 0; direction < 4; ++direction)
    {
        ASSERT_EQ(worldPathLandSlope(direction), kPathSlopeToLandSlope[direction]);
        for (int rotation = 0; rotation < 4; ++rotation)
        {
            SCOPED_TRACE(direction);
            SCOPED_TRACE(rotation);
            const int relative = terrainRelativeSlope(worldPathLandSlope(direction), rotation);
            std::array<int, 4> h{};
            for (int i = 0; i < 4; ++i)
                h[i] = 16 * worldSurfaceCornerHeight(320, relative, i);
            const auto p = worldTerrainDepthPlane(h[0], h[1], h[2], h[3], -320, 640);
            ASSERT_EQ(p.valid, 1);
            // All path ramps rise sixteen units over one tile; unlike edge-on
            // terrain, none of their four rotated physical planes is singular.
            EXPECT_EQ(p.dx * p.dx + p.dy * p.dy, 1);
            for (int x = 0; x <= 32; x += 4)
                for (int y = 0; y <= 32; y += 4)
                {
                    const float worldX = -320.0f + x, worldY = 640.0f + y;
                    const float z = h[0] + p.dx * x * 0.5f + p.dy * y * 0.5f;
                    EXPECT_FLOAT_EQ(
                        worldTerrainDepthAt(p.dx, p.dy, p.twiceIntercept, worldY - worldX, (worldX + worldY) * 0.5f - z),
                        worldX + worldY + z);
                }
        }
    }
}
