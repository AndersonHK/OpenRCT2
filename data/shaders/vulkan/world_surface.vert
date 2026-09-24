#version 450
#extension GL_GOOGLE_include_directive : require
#include "terrain_sprite_geometry.glsl"

const float DEPTH_INCREMENT = 1.0 / float(1u << 22u);
const float ATLAS_DIMENSION = 2048.0;

layout(push_constant) uniform WorldSurfaceConstants
{
    ivec2 screen;
    ivec2 view;
    ivec4 clip;
    uint width;
    uint height;
    uint recordCount;
    int zoom;
    uint rotation;
    uint spriteSetCount;
    int depthBase;
    uint phase;
    uint transparentWater; uint outputCapacity;
    uint sourceTick; uint clockMinute; uint clockHour;
    uint viewFlags;
} uCamera;

struct SpriteAssetDescriptor
{
    ivec2 atlasOrigin;
    int atlasLayer;
    int reserved;
};

layout(std430, set = 0, binding = 2) readonly buffer SpriteAssets
{
    SpriteAssetDescriptor assets[];
} uSpriteAssets;

layout(location = 0) in ivec3 vWorld;
layout(location = 1) in int vValid;
layout(location = 2) in ivec2 vSpriteSize;
layout(location = 3) in ivec2 vSpriteOffset;
layout(location = 4) in uint vAsset;
layout(location = 5) in uint vPalettes;
layout(location = 6) in uint vEffects;
layout(location = 7) in int vDepth;
layout(location = 8) in int vZoom;
layout(location = 9) in int vCoordinateShift;
// Constant component depth; local child counter (not consumed here).
layout(location = 10) in ivec2 vComponent;

layout(location = 0) flat out ivec2 fPosition;
layout(location = 1) flat out int fFlags;
layout(location = 2) flat out uint fColour;
layout(location = 3) flat out vec4 fTexColour;
layout(location = 4) flat out vec4 fTexMask;
layout(location = 5) flat out ivec3 fPalettes;
layout(location = 6) flat out float fZoom;
layout(location = 7) flat out int fTexColourAtlas;
layout(location = 8) flat out int fTexMaskAtlas;

int euclideanRemainder(int value, int divisor)
{
    int result = value % divisor;
    return result < 0 ? result + divisor : result;
}

int inverseZoom(int value, int zoom)
{
    return zoom < 0 ? value << -zoom : value >> zoom;
}


void main()
{
    ivec2 adjusted = vWorld.xy;
    if (uCamera.rotation == 1)
        adjusted.x += 32;
    else if (uCamera.rotation == 2)
        adjusted += ivec2(32);
    else if (uCamera.rotation == 3)
        adjusted.y += 32;

    ivec2 rotated = adjusted;
    if (uCamera.rotation == 1)
        rotated = ivec2(adjusted.y, -adjusted.x);
    else if (uCamera.rotation == 2)
        rotated = -adjusted;
    else if (uCamera.rotation == 3)
        rotated = ivec2(-adjusted.y, adjusted.x);
    ivec2 projected = ivec2(rotated.y - rotated.x, ((rotated.x + rotated.y) >> 1) - vWorld.z);
    if((vValid&16)!=0 && uCamera.zoom>=1) {
        int mask=uCamera.zoom>=2?3:1;
        projected &= ivec2(~mask);
    }
    ivec2 fullProjected = projected;
    for (int i = 0; i < vCoordinateShift; i++)
        projected /= 2;

    ivec2 topLeft = projected + vSpriteOffset;
    int xModifier = 0;
    int yModifier = 0;
    int widthModifier = 0;
    if (vZoom > 0)
    {
        int interval = 1 << vZoom;
        xModifier = euclideanRemainder(topLeft.x, interval);
        xModifier = xModifier != 0 ? interval - xModifier : 0;
        yModifier = euclideanRemainder(topLeft.y, interval);
        widthModifier = euclideanRemainder(topLeft.x + vSpriteSize.x, interval);
        widthModifier = widthModifier != 0 ? interval - widthModifier : 0;
    }

    ivec4 bounds = ivec4(
        inverseZoom(topLeft.x + xModifier, vZoom), inverseZoom(topLeft.y, vZoom),
        inverseZoom(topLeft.x + xModifier, vZoom) + inverseZoom(vSpriteSize.x + widthModifier, vZoom),
        inverseZoom(topLeft.y, vZoom) + inverseZoom(vSpriteSize.y + yModifier, vZoom));
    bounds += ivec4(uCamera.clip.xy - uCamera.view, uCamera.clip.xy - uCamera.view);
    bool originalPathGeometry = (vValid & 4) != 0 && uCamera.zoom >= 0 && uCamera.zoom <= 1;
    TerrainSpriteGeometry geometry = TerrainSpriteGeometry(ivec4(0),ivec2(0),1.0,0u);
    if (originalPathGeometry) {
        geometry = terrainSpriteGeometry(fullProjected,vSpriteSize,vSpriteOffset,(vValid & 2) != 0,
            vZoom,vCoordinateShift,uCamera.zoom,ivec4(uCamera.view,uCamera.clip.zw-uCamera.clip.xy),uCamera.clip.xy);
        bounds = geometry.bounds;
    }
    bool visible = (vValid & 1) != 0 && (!originalPathGeometry || geometry.visible != 0u) && bounds.x < uCamera.clip.z && bounds.y < uCamera.clip.w
        && bounds.z > uCamera.clip.x && bounds.w > uCamera.clip.y;
    visible = visible && (((vEffects & 0x700u) != 0u) == (uCamera.phase == 4u));
    ivec2 corners[4] = ivec2[](ivec2(0, 0), ivec2(1, 0), ivec2(0, 1), ivec2(1, 1));
    vec2 position = visible
        ? mix(vec2(bounds.xy), vec2(bounds.zw), vec2(corners[gl_VertexIndex]))
        : vec2(-2.0 * vec2(uCamera.screen));
    if (visible) {
        ivec4 clip=uCamera.clip;
        position=clamp(position,vec2(clip.xy),vec2(clip.zw));
    }
    vec2 ndc = (position * (2.0 / vec2(uCamera.screen))) - 1.0;
    // All four vertices receive the SAME depth. Sprite pixels, clipped screen
    // coordinates and texture offsets never participate in depth evaluation.
    uint localLayer=(vValid&32)!=0?(uint(vDepth)>>4u)&255u:0u;
    float componentDepth=(vValid&32)!=0?float(vComponent.x):float(rotated.x+rotated.y+vWorld.z);
    // Initial whole-map bound. Host integration owns tighter precision/range qualification.
    // Reserve the existing world interval so later UI remains in front.
    float capacity=1048576.0; // Fixed reserved UI/world depth interval, not output allocation limit.
    float guard=min(128.0,capacity*0.25);
    float fraction=(componentDepth+131072.0)/262144.0;
    float priority=float(uCamera.depthBase)+guard+fraction*(capacity-2.0*guard);
    float hardwareDepth=1.0-(priority+1.0)*DEPTH_INCREMENT;
    // A local overlay advances representable D32 values, rather than adding a
    // family rank. 128 priority units leave room for all255 two-ULP local steps.
    uint depthBits=floatBitsToUint(hardwareDepth);
    hardwareDepth=uintBitsToFloat(depthBits-min(localLayer*2u,depthBits));
    gl_Position = vec4(ndc,hardwareDepth,1.0);

    SpriteAssetDescriptor asset = uSpriteAssets.assets[vAsset];
    int texelY = vZoom > 0 ? (1 << vZoom) - 1 - yModifier : 0;
    ivec2 texelOffset = originalPathGeometry ? geometry.texelOffset : ivec2(xModifier,texelY);
    vec4 texture = vec4(vec2(asset.atlasOrigin + texelOffset), ATLAS_DIMENSION, ATLAS_DIMENSION);
    fPosition = bounds.xy;
    fFlags = int(vEffects & 0xffffu);
    fColour = (vEffects >> 16u) & 0xffu;
    fTexColour = texture;
    fTexMask = texture;
    fPalettes = ivec3(
        int(vPalettes & 0xffu), int((vPalettes >> 8u) & 0xffu), int((vPalettes >> 16u) & 0xffu));
    fZoom = vZoom >= 0 ? float(1 << vZoom) : 1.0 / float(1 << -vZoom);
    fTexColourAtlas = asset.atlasLayer;
    fTexMaskAtlas = asset.atlasLayer;
}
