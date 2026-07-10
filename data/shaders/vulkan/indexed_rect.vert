#version 450

const float DEPTH_INCREMENT = 1.0 / float(1u << 22u);

layout(push_constant) uniform ScreenConstants
{
    ivec2 size;
} uScreen;

layout(location = 0) in ivec4 vClip;
layout(location = 1) in int vTexColourAtlas;
layout(location = 2) in vec4 vTexColourBounds;
layout(location = 3) in int vTexMaskAtlas;
layout(location = 4) in vec4 vTexMaskBounds;
layout(location = 5) in ivec3 vPalettes;
layout(location = 6) in int vFlags;
layout(location = 7) in uint vColour;
layout(location = 8) in ivec4 vBounds;
layout(location = 9) in int vDepth;
layout(location = 10) in float vZoom;

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

    fPosition = vBounds.xy;
    fFlags = vFlags;
    fColour = vColour;
    fTexColour = vTexColourBounds;
    fTexMask = vTexMaskBounds;
    fPalettes = vPalettes;
    fZoom = vZoom;
    fTexColourAtlas = vTexColourAtlas;
    fTexMaskAtlas = vTexMaskAtlas;
}
