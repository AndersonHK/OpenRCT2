#version 450

layout(set = 0, binding = 0) uniform usampler2D uOpaqueCanvas;
layout(set = 0, binding = 1) uniform sampler2D uOpaqueDepth;
layout(set = 0, binding = 2) uniform usampler2D uTransparentCanvas;
layout(set = 0, binding = 3) uniform sampler2D uTransparentDepth;
layout(set = 0, binding = 4) uniform usampler2D uRemapPalette;
layout(set = 0, binding = 5) uniform usampler2D uBlendPalette;

layout(location = 0) out uint oColour;

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    uint opaque = texelFetch(uOpaqueCanvas, pixel, 0).r;
    float opaqueDepth = texelFetch(uOpaqueDepth, pixel, 0).r;
    uint transparent = texelFetch(uTransparentCanvas, pixel, 0).r;
    float transparentDepth = texelFetch(uTransparentDepth, pixel, 0).r;

    if (opaqueDepth <= transparentDepth)
        transparent = 0u;

    uint blendColour = (transparent & 0xff00u) >> 8;
    if (blendColour > 0u)
    {
        oColour = (transparent & 0x00ffu) != 0u
            ? blendColour
            : texelFetch(uBlendPalette, ivec2(int(opaque), int(blendColour)), 0).r;
    }
    else
    {
        oColour = texelFetch(uRemapPalette, ivec2(int(opaque), int(transparent)), 0).r;
    }
}
