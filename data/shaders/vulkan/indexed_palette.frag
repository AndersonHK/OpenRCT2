#version 450

layout(set = 0, binding = 0) uniform usampler2D uIndexedCanvas;
layout(set = 0, binding = 1) uniform sampler2D uPalette;
layout(set = 0, binding = 2) uniform usampler2D uLightMap;
layout(set = 0, binding = 3) uniform sampler2D uLightPalette;

layout(push_constant) uniform OutputConstants
{
    int encoding;
    float paperWhiteNits;
    int lightFxEnabled;
} uOutput;

layout(location = 0) in vec2 fTextureCoordinate;
layout(location = 0) out vec4 oColour;

vec3 srgbToLinear(vec3 colour)
{
    bvec3 useLinear = lessThanEqual(colour, vec3(0.04045));
    vec3 low = colour / 12.92;
    vec3 high = pow((colour + 0.055) / 1.055, vec3(2.4));
    return mix(high, low, useLinear);
}

vec3 linearSrgbToBt2020(vec3 colour)
{
    return mat3(
        0.6274040, 0.0690970, 0.0163916,
        0.3292820, 0.9195400, 0.0880132,
        0.0433136, 0.0113612, 0.8955950) * colour;
}

vec3 encodePq(vec3 linearBt2020, float paperWhiteNits)
{
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 128.0;
    const float c3 = 2392.0 / 128.0;
    vec3 luminance = clamp(linearBt2020 * (paperWhiteNits / 10000.0), 0.0, 1.0);
    vec3 powered = pow(luminance, vec3(m1));
    return pow((c1 + c2 * powered) / (1.0 + c3 * powered), vec3(m2));
}

void main()
{
    uint paletteIndex = texture(uIndexedCanvas, fTextureCoordinate).r;
    vec4 colour = texelFetch(uPalette, ivec2(int(paletteIndex), 0), 0);
    if (uOutput.lightFxEnabled != 0)
    {
        uint intensity = min(texture(uLightMap, fTextureCoordinate).r, 255u);
        if (intensity != 0)
        {
            uvec4 darkBytes = uvec4(round(colour * 255.0));
            uvec4 lightBytes = uvec4(round(texelFetch(uLightPalette, ivec2(int(paletteIndex), 0), 0) * 255.0));
            uvec4 mixedBytes = min(uvec4(255), darkBytes + (lightBytes * (intensity * 6u)) / 256u);
            colour = vec4(mixedBytes) / 255.0;
        }
    }
    if (uOutput.encoding == 1)
    {
        // sRGB attachments encode shader-linear values. Decode the palette
        // first so the final swapchain bytes match the legacy palette.
        colour.rgb = srgbToLinear(colour.rgb);
    }
    else if (uOutput.encoding == 2)
    {
        colour.rgb = encodePq(linearSrgbToBt2020(srgbToLinear(colour.rgb)), uOutput.paperWhiteNits);
    }
    oColour = colour;
}
