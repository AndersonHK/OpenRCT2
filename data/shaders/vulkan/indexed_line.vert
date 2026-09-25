#version 450
#extension GL_GOOGLE_include_directive : require
#include "indexed_depth.glsl"



layout(push_constant) uniform ScreenConstants
{
    ivec2 size;
} uScreen;

layout(location = 0) in ivec4 vBounds;
layout(location = 1) in uint vColour;
layout(location = 2) in int vDepth;

layout(location = 0) flat out uint fColour;

void main()
{
    vec2 position = gl_VertexIndex == 0 ? vec2(vBounds.xy) : vec2(vBounds.zw);
    vec2 ndc = (position * (2.0 / vec2(uScreen.size))) - 1.0;
    float depth = indexedDepth(uint(vDepth),0u);
    gl_Position = vec4(ndc, depth, 1.0);
    fColour = vColour;
}
