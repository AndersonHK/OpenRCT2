#version 450

// Palette and LightFX have already been resolved into display-encoded RGBA8.
// Linear filtering must operate on these bytes, never on palette indices.
layout(set = 0, binding = 0) uniform sampler2D uRgbaCanvas;
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
    return mix(pow((colour + 0.055) / 1.055, vec3(2.4)), colour / 12.92, useLinear);
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
    vec3 powered = pow(clamp(linearBt2020 * (paperWhiteNits / 10000.0), 0.0, 1.0), vec3(m1));
    return pow((c1 + c2 * powered) / (1.0 + c3 * powered), vec3(m2));
}

void main()
{
    vec4 colour = texture(uRgbaCanvas, fTextureCoordinate);
    if (uOutput.encoding == 1)
        colour.rgb = srgbToLinear(colour.rgb);
    else if (uOutput.encoding == 2)
        colour.rgb = encodePq(linearSrgbToBt2020(srgbToLinear(colour.rgb)), uOutput.paperWhiteNits);
    oColour = colour;
}
