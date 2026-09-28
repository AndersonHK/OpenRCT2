// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Unqualified retained-terrain drawing foundation; no entry point or admission.
#ifndef OPENRCT2_TERRAIN_SPRITE_GEOMETRY_GLSL
#define OPENRCT2_TERRAIN_SPRITE_GEOMETRY_GLSL

ivec2 terrainRotateXY(ivec2 value, uint rotation)
{
    if (rotation == 1u) return ivec2(value.y, -value.x);
    if (rotation == 2u) return -value;
    if (rotation == 3u) return ivec2(-value.y, value.x);
    return value;
}

// TileElement/BlankTilesPaint advance to the camera-facing tile corner first.
ivec2 terrainPaintTileOrigin(ivec2 tileOrigin, uint rotation)
{
    if (rotation == 1u || rotation == 2u) tileOrigin.x += 32;
    if (rotation == 2u || rotation == 3u) tileOrigin.y += 32;
    return tileOrigin;
}

// A rear attachment instead uses its parent's projected origin plus attachedY.
// Do not rotate or interpret an attachment offset as a world offset.
ivec2 terrainProjectParent(ivec2 tileOrigin, ivec3 localOffset, uint rotation)
{
    ivec2 world = terrainPaintTileOrigin(tileOrigin, rotation)
        + terrainRotateXY(localOffset.xy, (rotation * 3u) & 3u);
    ivec2 rotated = terrainRotateXY(world, rotation);
    return ivec2(rotated.y - rotated.x, ((rotated.x + rotated.y) >> 1) - localOffset.z);
}

// Preserve the frozen rotation-dependent endpoint adjustments, including z=-1
// surface bounds. These are painter bounds, not a normalized geometric AABB.
void terrainParentPaintBounds(ivec2 tileOrigin, ivec3 boxOffset, ivec3 boxSize,
    uint rotation, out ivec3 begin, out ivec3 end)
{
    ivec2 origin = terrainPaintTileOrigin(tileOrigin, rotation);
    begin = ivec3(origin + terrainRotateXY(boxOffset.xy, (rotation * 3u) & 3u), boxOffset.z);
    if (rotation == 0u) boxSize.xy -= ivec2(1);
    else if (rotation == 1u) boxSize.x--;
    else if (rotation == 3u) boxSize.y--;
    end = begin + ivec3(terrainRotateXY(boxSize.xy, (rotation * 3u) & 3u), boxSize.z);
}

int terrainSigned16(int value)
{
    return int((uint(value) & 65535u) ^ 32768u) - 32768;
}

struct TerrainSpriteGeometry
{
    ivec4 bounds;
    ivec2 texelOffset;
    float sampleStep;
    uint visible;
};

// The caller supplies the resolved linked-zoom variant and an actual paint
// column's target {x,y,width,height}, plus its framebuffer clip origin. Camera
// zoom is initially 0/1 only; effectiveZoom + coordinateShift must equal it.
// Sprite metadata is retained per asset generation, never built per tile/frame.
// Original-image painter culling and parent/attachment admission happen earlier.
TerrainSpriteGeometry terrainSpriteGeometry(ivec2 projected, ivec2 spriteSize,
    ivec2 spriteOffset, bool rle, int effectiveZoom, int coordinateShift,
    int cameraZoom, ivec4 target, ivec2 clipOrigin)
{
    TerrainSpriteGeometry result = TerrainSpriteGeometry(ivec4(0), ivec2(0), 1.0, 0u);
    if (cameraZoom < 0 || cameraZoom > 1 || effectiveZoom < 0 || effectiveZoom > 1
        || coordinateShift < 0 || coordinateShift > 1 || effectiveZoom + coordinateShift != cameraZoom
        || any(lessThanEqual(spriteSize, ivec2(0))) || any(greaterThan(spriteSize, ivec2(2048)))
        || any(lessThanEqual(target.zw, ivec2(0))) || any(greaterThan(target.zw, ivec2(16384)))
        || any(lessThan(projected, ivec2(-1048576))) || any(greaterThan(projected, ivec2(1048576)))
        || any(lessThan(target.xy, ivec2(-1048576))) || any(greaterThan(target.xy, ivec2(1048576)))
        || any(lessThan(spriteOffset, ivec2(-32768))) || any(greaterThan(spriteOffset, ivec2(32767)))
        || any(lessThan(clipOrigin, ivec2(-1048576))) || any(greaterThan(clipOrigin, ivec2(1048576))))
        return result;

    // Signed division truncates toward zero for linked zoom images. A right
    // shift would floor negative odd coordinates and alter the source phase.
    for (int i = 0; i < coordinateShift; i++) projected /= 2;
    int step = 1 << effectiveZoom;
    int lowMask = step - 1;
    if (rle) projected -= ivec2(lowMask);
    int top = terrainSigned16(projected.y + spriteOffset.y);
    int destY = terrainSigned16((rle ? top : top & ~lowMask) - (target.y << effectiveZoom));
    int sourceY = 0;
    int height = spriteSize.y;
    if (destY < 0)
    {
        height += destY;
        sourceY = -destY;
        destY = 0;
    }
    else if (rle)
    {
        sourceY -= destY & lowMask;
        height += destY & lowMask;
    }
    height = min(height, (target.w << effectiveZoom) - destY);
    destY >>= effectiveZoom;
    if (rle && sourceY < 0)
    {
        sourceY += step;
        height -= step;
        destY++;
    }
    int destX = terrainSigned16(((projected.x + spriteOffset.x + lowMask) & ~lowMask)
        - (target.x << effectiveZoom));
    int sourceX = 0;
    int width = spriteSize.x;
    if (destX < 0)
    {
        width += destX;
        sourceX = -destX;
        destX = 0;
    }
    width = min(width, (target.z << effectiveZoom) - destX);
    destX >>= effectiveZoom;
    if (width <= 0 || height <= 0) return result;
    ivec2 begin = clipOrigin + ivec2(destX, destY);
    result.bounds = ivec4(begin, begin + ivec2((width + lowMask) / step, (height + lowMask) / step));
    result.texelOffset = ivec2(sourceX, sourceY);
    result.sampleStep = float(step);
    result.visible = 1u;
    return result;
}

#endif
