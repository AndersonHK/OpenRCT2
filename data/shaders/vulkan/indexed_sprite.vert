#version 450

const float DEPTH_INCREMENT = 1.0 / float(1u << 22u);
const float ATLAS_DIMENSION = 2048.0;

layout(push_constant) uniform ScreenConstants
{
    ivec2 size;
} uScreen;

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

layout(location = 0) in ivec4 vClip;
layout(location = 1) in ivec4 vBounds;
layout(location = 2) in ivec2 vTexelOffset;
layout(location = 3) in uint vAsset;
layout(location = 4) in uint vPalettes;
layout(location = 5) in uint vEffects;
layout(location = 6) in int vDepth;
layout(location = 7) in float vZoom;

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
    const vec2 corners[4] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0));
    vec2 position = mix(vec2(vBounds.xy), vec2(vBounds.zw), corners[gl_VertexIndex]);
    position = clamp(position, vec2(vClip.xy), vec2(vClip.zw));
    vec2 ndc = (position * (2.0 / vec2(uScreen.size))) - 1.0;
    float depth = 1.0 - (float(vDepth) + 1.0) * DEPTH_INCREMENT;
    gl_Position = vec4(ndc, depth, 1.0);

    SpriteAssetDescriptor asset = uSpriteAssets.assets[vAsset];
    vec4 texture = vec4(vec2(asset.atlasOrigin + vTexelOffset), ATLAS_DIMENSION, ATLAS_DIMENSION);
    fPosition = vBounds.xy;
    fFlags = int(vEffects & 0xffffu);
    fColour = (vEffects >> 16u) & 0xffu;
    fTexColour = texture;
    fTexMask = texture;
    fPalettes = ivec3(
        int(vPalettes & 0xffu), int((vPalettes >> 8u) & 0xffu), int((vPalettes >> 16u) & 0xffu));
    fZoom = vZoom;
    fTexColourAtlas = asset.atlasLayer;
    fTexMaskAtlas = asset.atlasLayer;
}
