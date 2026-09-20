// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Shared scalar rule implementation, compiled as GLSL by terrain compute and
// as C++ by differential tests. No per-frame CPU caller is required.
#ifndef OPENRCT2_TERRAIN_SURFACE_RULES
#define OPENRCT2_TERRAIN_SURFACE_RULES
#ifdef __cplusplus
#define TERRAIN_FN constexpr
#else
#define TERRAIN_FN
#endif

// Prefix-sum count and check capacity before querying individual emissions.
// Scalar std430 fields: exactly 32 bytes, with no worst-case strip array.
struct TerrainEdgePlan
{
    int count;
    int startZ;
    int lowerOffset;
    int lowerCount;
    int fullCount;
    int upperOffset;
    int attachedY;
    int edge;
};

// Parent offsets/bounds are camera-relative world coordinates before the
// painter's world/bounds rotation. Attached offsets are relative screen units.
struct TerrainEdgeEmission
{
    int imageOffset;
    int x;
    int y;
    int z;
    int boundsX;
    int boundsY;
    int boundsZ;
    int attached;
};

TERRAIN_FN int terrainRelativeSlope(int rawSlope, int rotation)
{
    int shifted = (rawSlope & 15) << rotation;
    return (shifted | (shifted >> 4)) & 15;
}

TERRAIN_FN int terrainShapeImageOffset(int relativeSlope)
{
    return ((relativeSlope & 5) << 1) | ((relativeSlope & 10) >> 1);
}

// Screen-relative corners top/right/bottom/left; non-steep slopes only.
TERRAIN_FN int terrainCornerHeight(int baseZ, int relativeSlope, int corner)
{
    return baseZ / 16 + ((relativeSlope >> ((corner + 2) & 3)) & 1);
}

TERRAIN_FN int terrainMaterialSelector(int grassLength, int rotation, int tileX, int tileY, int zoom)
{
    int lengthIndex = zoom > 0 ? 8 : (grassLength & 7);
    return (lengthIndex * 4 + rotation) * 4 + (tileX & 1) + ((tileY & 1) << 1);
}

TERRAIN_FN int terrainNeighbourX(int edge, int rotation)
{
    int x = edge == 0 ? 1 : (edge == 3 ? -1 : 0);
    int y = edge == 1 ? 1 : (edge == 2 ? -1 : 0);
    return rotation == 0 ? x : (rotation == 1 ? -y : (rotation == 2 ? -x : y));
}

TERRAIN_FN int terrainNeighbourY(int edge, int rotation)
{
    int x = edge == 0 ? 1 : (edge == 3 ? -1 : 0);
    int y = edge == 1 ? 1 : (edge == 2 ? -1 : 0);
    return rotation == 0 ? y : (rotation == 1 ? x : (rotation == 2 ? -y : -x));
}

TERRAIN_FN bool terrainRuleInputValid(int baseZ, int slope, int rotation)
{
    // Frozen corner storage is uint8_t; exclude base255 + raised-corner wrap.
    return baseZ >= 16 && baseZ <= 4064 && (baseZ & 15) == 0 && slope >= 0 && slope < 16
        && rotation >= 0 && rotation < 4;
}

// Caller must reject unsupported flags, water/tunnels and invalid materials.
// Missing neighbours use the frozen minimum-land corner height (1).
TERRAIN_FN TerrainEdgePlan terrainPlanEdge(
    int edge, int baseZ, int rawSlope, int neighbourBaseZ, int neighbourRawSlope, bool neighbourValid, int rotation)
{
    TerrainEdgePlan plan;
    plan.count = 0; plan.startZ = 0; plan.lowerOffset = 0; plan.lowerCount = 0;
    plan.fullCount = 0; plan.upperOffset = 0; plan.attachedY = 0; plan.edge = edge;
    if (edge < 0 || edge > 3 || !terrainRuleInputValid(baseZ, rawSlope, rotation)
        || (neighbourValid && !terrainRuleInputValid(neighbourBaseZ, neighbourRawSlope, rotation)))
        return plan;
    int ownSlope = terrainRelativeSlope(rawSlope, rotation);
    int neighbourSlope = terrainRelativeSlope(neighbourRawSlope, rotation);
    int ownCorner1 = edge == 0 ? 3 : (edge == 1 ? 1 : 0);
    int ownCorner2 = edge < 2 ? 2 : (edge == 2 ? 3 : 1);
    int neighbourCorner1 = edge < 2 ? 0 : (edge == 2 ? 1 : 3);
    int neighbourCorner2 = edge == 0 ? 1 : (edge == 1 ? 3 : 2);
    int c1 = terrainCornerHeight(baseZ, ownSlope, ownCorner1);
    int c2 = terrainCornerHeight(baseZ, ownSlope, ownCorner2);
    int n1 = neighbourValid ? terrainCornerHeight(neighbourBaseZ, neighbourSlope, neighbourCorner1) : 1;
    int n2 = neighbourValid ? terrainCornerHeight(neighbourBaseZ, neighbourSlope, neighbourCorner2) : 1;
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

// Precondition: 0 <= index < plan.count. Front faces emit bottom-to-top;
// rear attachments remain in the base sorting unit, with prepended traversal.
TERRAIN_FN TerrainEdgeEmission terrainEdgeAt(TerrainEdgePlan plan, int index)
{
    TerrainEdgeEmission result;
    result.imageOffset = 0; result.x = 0; result.y = 0; result.z = 0;
    result.boundsX = 0; result.boundsY = 0; result.boundsZ = 0; result.attached = 0;
    if (index < 0 || index >= plan.count)
        return result;
    if (plan.edge >= 2)
    {
        result.imageOffset = plan.upperOffset;
        result.y = plan.attachedY;
        result.attached = 1;
        return result;
    }
    int localOffset = index < plan.lowerCount ? plan.lowerOffset
        : (index < plan.lowerCount + plan.fullCount ? 0 : plan.upperOffset);
    result.imageOffset = (plan.edge == 0 ? 0 : 5) + localOffset;
    result.x = plan.edge == 0 ? 30 : 0;
    result.y = plan.edge == 0 ? 0 : 30;
    result.z = plan.startZ + index * 16;
    result.boundsX = plan.edge == 0 ? 0 : 30;
    result.boundsY = plan.edge == 0 ? 30 : 0;
    result.boundsZ = 15;
    return result;
}

// Local calls: rear-left, rear-right, front-left, front-right. Rear calls
// prepend: final base-attachment traversal is rear-right then rear-left.
// Cross-tile parent arrangement is deliberately a separate unproven step.
TERRAIN_FN int terrainEdgeForCall(int ordinal)
{
    return ordinal < 2 ? ordinal + 2 : ordinal - 2;
}
TERRAIN_FN int terrainRearEdgeForTraversal(int ordinal)
{
    return 3 - ordinal;
}
#undef TERRAIN_FN
#endif
