#version 450

const float DEPTH_INCREMENT = 1.0 / float(1u << 22u);

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
    float depth = 1.0 - (float(vDepth) + 1.0) * DEPTH_INCREMENT;
    gl_Position = vec4(ndc, depth, 1.0);
    fColour = vColour;
}
