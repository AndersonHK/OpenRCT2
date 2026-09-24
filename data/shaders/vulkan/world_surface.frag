#version 450

const int MASK_REMAP_COUNT = 3;
const int FLAG_NO_TEXTURE = (1 << 2);
const int FLAG_MASK = (1 << 3);
const int FLAG_CROSS_HATCH = (1 << 4);
const int FLAG_TTF_TEXT = (1 << 5);
const int FLAG_ZERO_COVERAGE = (1 << 6);

layout(set = 0, binding = 0) uniform usampler2DArray uSpriteAtlas;
layout(set = 0, binding = 1) uniform usampler2D uRemapPalette;

layout(location = 0) flat in ivec2 fPosition;
layout(location = 1) flat in int fFlags;
layout(location = 2) flat in uint fColour;
layout(location = 3) flat in vec4 fTexColour;
layout(location = 4) flat in vec4 fTexMask;
layout(location = 5) flat in ivec3 fPalettes;
layout(location = 6) flat in float fZoom;
layout(location = 7) flat in int fTexColourAtlas;
layout(location = 8) flat in int fTexMaskAtlas;
layout(location = 9) flat in uint fOrder;

layout(location = 0) out uint oColour;

uint atlasTexel(vec4 bounds, int layer, ivec2 position)
{
    return texelFetch(uSpriteAtlas, ivec3(ivec2(bounds.xy) + position, layer), 0).r;
}

void main()
{
    // Keep the same vertex interface as collection, and reject invalid signed depths.
    if (fOrder >= 0x80000000u)
        discard;
    // Match the legacy rectangle shader's pixel-centre handling exactly.
    // Applying zoom to gl_FragCoord directly would introduce a half-pixel
    // offset at non-unit zoom levels.
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
                // Solid glyph coverage takes precedence over the hinting threshold.
                if (!solidColour && texel <= hintThreshold)
                    discard;
                // Filter rows occupy 0x0000..0x00ff and ordinary text uses low
                // byte 0/1. Reserve 0x0102/0x0103 for blended/solid index-zero ink.
                texel = fColour == 0u ? (solidColour ? 0x0103u : 0x0102u)
                                     : (fColour << 8) + (solidColour ? 1u : 0u);
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

    // Index zero is a valid untextured canvas write (including cropped clears).
    // TTF source coverage was checked before applying the ink index, which may be zero.
    if (texel == 0u && (fFlags & (FLAG_NO_TEXTURE | FLAG_TTF_TEXT)) == 0)
        discard;
    if ((fFlags & FLAG_CROSS_HATCH) != 0 && ((position.x + position.y) & 1) != 0)
        discard;
    if ((fFlags & FLAG_ZERO_COVERAGE) != 0 && atlasTexel(fTexMask, fTexMaskAtlas, position) == 0u)
        discard;
    if ((fFlags & FLAG_MASK) != 0)
    {
        uint mask = atlasTexel(fTexMask, fTexMaskAtlas, position);
        // Raw masked images combine the two index bytes bitwise. Solid sprites
        // use the same mask flag but only need the sprite's nonzero coverage.
        if ((fFlags & FLAG_NO_TEXTURE) == 0)
            texel &= mask;
        else if (mask == 0u)
            discard;
        if (texel == 0u)
            discard;
    }

    oColour = texel;
}
