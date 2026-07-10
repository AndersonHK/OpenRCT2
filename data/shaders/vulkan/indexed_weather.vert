#version 450

layout(push_constant) uniform ScreenConstants
{
    ivec2 size;
} uScreen;

layout(location = 0) in ivec4 vBounds;
layout(location = 1) in ivec2 vOffset;
layout(location = 2) in int vPattern;

layout(location = 0) flat out ivec2 fOrigin;
layout(location = 1) flat out ivec2 fOffset;
layout(location = 2) flat out int fPattern;

void main()
{
    const vec2 corners[4] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0));
    vec2 position = mix(vec2(vBounds.xy), vec2(vBounds.zw), corners[gl_VertexIndex]);
    vec2 ndc = (position * (2.0 / vec2(uScreen.size))) - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    fOrigin = vBounds.xy;
    fOffset = vOffset;
    fPattern = vPattern;
}
