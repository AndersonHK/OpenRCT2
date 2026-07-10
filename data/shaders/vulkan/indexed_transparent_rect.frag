#version 450

const int MASK_REMAP_COUNT = 3;
const int FLAG_NO_TEXTURE = (1 << 2);
const int FLAG_MASK = (1 << 3);
const int FLAG_CROSS_HATCH = (1 << 4);
const int FLAG_TTF_TEXT = (1 << 5);

layout(set = 0, binding = 0) uniform usampler2DArray uSpriteAtlas;
layout(set = 0, binding = 1) uniform usampler2D uRemapPalette;
layout(set = 0, binding = 2) uniform sampler2D uPreviousDepth;

layout(push_constant) uniform TransparencyConstants
{
    ivec2 size;
    int peeling;
} uTransparency;

layout(location = 0) flat in ivec2 fPosition;
layout(location = 1) flat in int fFlags;
layout(location = 2) flat in uint fColour;
layout(location = 3) flat in vec4 fTexColour;
layout(location = 4) flat in vec4 fTexMask;
layout(location = 5) flat in ivec3 fPalettes;
layout(location = 6) flat in float fZoom;
layout(location = 7) flat in int fTexColourAtlas;
layout(location = 8) flat in int fTexMaskAtlas;

layout(location = 0) out uint oColour;

uint atlasTexel(vec4 bounds, int layer, ivec2 position)
{
    return texelFetch(uSpriteAtlas, ivec3(ivec2(bounds.xy) + position, layer), 0).r;
}

void main()
{
    if (uTransparency.peeling != 0)
    {
        float previousDepth = texelFetch(uPreviousDepth, ivec2(gl_FragCoord.xy), 0).r;
        if (previousDepth == 0.0 || gl_FragCoord.z >= previousDepth)
            discard;
    }

    ivec2 fragment = ivec2(floor(gl_FragCoord.xy));
    ivec2 position = ivec2((vec2(fragment) - vec2(fPosition)) * fZoom);
    uint texel;
    if ((fFlags & FLAG_NO_TEXTURE) == 0)
    {
        texel = atlasTexel(fTexColour, fTexColourAtlas, position);
        if (texel == 0u)
            discard;

        if ((fFlags & FLAG_TTF_TEXT) == 0)
        {
            texel += fColour;
        }
        else
        {
            uint hintThreshold = uint(fFlags & 0xff00) >> 8;
            if (hintThreshold > 0u)
            {
                bool solidColour = texel > 180u;
                texel = texel > hintThreshold ? fColour : 0u;
                texel = (texel << 8) + (solidColour ? 1u : 0u);
            }
            else
            {
                texel = fColour;
            }
        }
    }
    else
    {
        texel = fColour;
    }

    int paletteCount = fFlags & MASK_REMAP_COUNT;
    if (paletteCount >= 3 && texel >= 0x2eu && texel < 0x3au)
        texel = texelFetch(uRemapPalette, ivec2(int(texel + 0xc5u), fPalettes.z), 0).r;
    else if (paletteCount >= 2 && texel >= 0xcau && texel < 0xd6u)
        texel = texelFetch(uRemapPalette, ivec2(int(texel + 0x29u), fPalettes.y), 0).r;
    else if (paletteCount >= 1)
        texel = texelFetch(uRemapPalette, ivec2(int(texel), fPalettes.x), 0).r;

    if (texel == 0u)
        discard;
    if ((fFlags & FLAG_CROSS_HATCH) != 0 && ((position.x + position.y) & 1) != 0)
        discard;
    if ((fFlags & FLAG_MASK) != 0 && atlasTexel(fTexMask, fTexMaskAtlas, position) == 0u)
        discard;

    oColour = texel;
}
