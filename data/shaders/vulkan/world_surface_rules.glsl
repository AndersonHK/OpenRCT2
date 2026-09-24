// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_SURFACE_RULES
#define OPENRCT2_WORLD_SURFACE_RULES
#include "terrain_surface_rules.glsl"
#ifdef __cplusplus
#define TERRAIN_FN constexpr
#else
#define TERRAIN_FN
#endif
// Exact Slope.cpp corner table, including its four double-height slopes.
TERRAIN_FN int worldSurfaceCornerHeight(int baseZ, int relativeSlope, int corner)
{
    int height = terrainCornerHeight(baseZ, relativeSlope, corner);
    if ((relativeSlope == 23 && corner == 3) || (relativeSlope == 27 && corner == 2)
        || (relativeSlope == 29 && corner == 1) || (relativeSlope == 30 && corner == 0)) height++;
    return height;
}
TERRAIN_FN bool worldSurfaceInputValid(int baseZ, int slope, int rotation)
{
    return baseZ >= 16 && baseZ <= 4048 && (baseZ & 15) == 0 && slope >= 0 && slope < 32
        && rotation >= 0 && rotation < 4;
}
TERRAIN_FN TerrainEdgePlan worldSurfacePlanEdge(
    int edge, int baseZ, int rawSlope, int neighbourBaseZ, int neighbourRawSlope, bool neighbourValid, int rotation)
{
    TerrainEdgePlan plan;
    plan.count = 0; plan.startZ = 0; plan.lowerOffset = 0; plan.lowerCount = 0;
    plan.fullCount = 0; plan.upperOffset = 0; plan.attachedY = 0; plan.edge = edge;
    if (edge < 0 || edge > 3 || !worldSurfaceInputValid(baseZ, rawSlope, rotation)
        || (neighbourValid && !worldSurfaceInputValid(neighbourBaseZ, neighbourRawSlope, rotation)))
        return plan;
    int ownSlope = (terrainRelativeSlope(rawSlope, rotation) | (rawSlope & 16));
    int neighbourSlope = (terrainRelativeSlope(neighbourRawSlope, rotation) | (neighbourRawSlope & 16));
    int ownCorner1 = edge == 0 ? 3 : (edge == 1 ? 1 : 0);
    int ownCorner2 = edge < 2 ? 2 : (edge == 2 ? 3 : 1);
    int neighbourCorner1 = edge < 2 ? 0 : (edge == 2 ? 1 : 3);
    int neighbourCorner2 = edge == 0 ? 1 : (edge == 1 ? 3 : 2);
    int c1 = worldSurfaceCornerHeight(baseZ, ownSlope, ownCorner1);
    int c2 = worldSurfaceCornerHeight(baseZ, ownSlope, ownCorner2);
    int n1 = neighbourValid ? worldSurfaceCornerHeight(neighbourBaseZ, neighbourSlope, neighbourCorner1) : 1;
    int n2 = neighbourValid ? worldSurfaceCornerHeight(neighbourBaseZ, neighbourSlope, neighbourCorner2) : 1;
    if (c1 <= n1 && c2 <= n2)
        return plan;
    if (edge >= 2)
    {
        plan.count = 1;
        plan.upperOffset = (edge == 2 ? 33 : 30) + c2 - c1 + 1;
        plan.attachedY = baseZ - c1 * 16;
        return plan;
    }
    int start = n1 < n2 ? n1 : n2;
    plan.startZ = start * 16;
    plan.lowerCount = n1 != n2 && start != c1 && start != c2 ? 1 : 0;
    plan.lowerOffset = n2 >= n1 ? 4 : 3;
    int current = start + plan.lowerCount;
    int lowest = c1 < c2 ? c1 : c2;
    plan.fullCount = lowest > current ? lowest - current : 0;
    current += plan.fullCount;
    int upperCount = current < c1 || current < c2 ? 1 : 0;
    plan.upperOffset = current >= c1 ? 2 : 1;
    plan.count = plan.lowerCount + plan.fullCount + upperCount;
    return plan;
}

#undef TERRAIN_FN
#endif
