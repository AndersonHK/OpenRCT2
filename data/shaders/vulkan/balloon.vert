#version 450
#extension GL_GOOGLE_include_directive : require
#include "indexed_depth.glsl"


const float ATLAS_DIMENSION = 2048.0;
layout(push_constant) uniform BalloonConstants
{
    ivec2 screen; ivec2 view; ivec4 clip; uint depthBase;
} uCamera;
struct SpriteAssetDescriptor { ivec2 atlasOrigin; int atlasLayer; int reserved; };
layout(std430, set = 0, binding = 2) readonly buffer SpriteAssets { SpriteAssetDescriptor assets[]; } uSpriteAssets;
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
layout(location = 10) in ivec2 vColumnClip;
layout(location = 0) flat out ivec2 fPosition;
layout(location = 1) flat out int fFlags;
layout(location = 2) flat out uint fColour;
layout(location = 3) flat out vec4 fTexColour;
layout(location = 4) flat out vec4 fTexMask;
layout(location = 5) flat out ivec3 fPalettes;
layout(location = 6) flat out float fZoom;
layout(location = 7) flat out int fTexColourAtlas;
layout(location = 8) flat out int fTexMaskAtlas;

void main()
{
    // Entity projection has no terrain tile-corner rotation adjustment. B1 admits only rotation/zoom zero.
    ivec2 projected = ivec2(vWorld.y - vWorld.x, ((vWorld.x + vWorld.y) >> 1) - vWorld.z);
    ivec2 topLeft = projected + vSpriteOffset + uCamera.clip.xy - uCamera.view;
    ivec4 clip = ivec4(max(uCamera.clip.x, vColumnClip.x), uCamera.clip.y,
        min(uCamera.clip.z, vColumnClip.y), uCamera.clip.w);
    ivec2 corners[4] = ivec2[](ivec2(0,0), ivec2(1,0), ivec2(0,1), ivec2(1,1));
    ivec2 position = clamp(topLeft + vSpriteSize * corners[gl_VertexIndex], clip.xy, clip.zw);
    vec2 ndc = vec2(position) * (2.0 / vec2(uCamera.screen)) - 1.0;
    gl_Position = vec4(ndc, indexedDepth(uint(vDepth),0u), 1.0);
    SpriteAssetDescriptor asset = uSpriteAssets.assets[vAsset];
    fPosition = topLeft;
    fFlags = int(vEffects);
    fColour = 0u;
    fTexColour = vec4(vec2(asset.atlasOrigin), ATLAS_DIMENSION, ATLAS_DIMENSION);
    fTexMask = fTexColour;
    fPalettes = ivec3(int(vPalettes), 0, 0);
    fZoom = 1.0;
    fTexColourAtlas = asset.atlasLayer;
    fTexMaskAtlas = asset.atlasLayer;
}
