#version 450

layout(set = 0, binding = 0) uniform usampler2D uIndexedCanvas;
layout(set = 0, binding = 1) uniform sampler2D uPalette;

layout(location = 0) in vec2 fTextureCoordinate;
layout(location = 0) out vec4 oColour;

void main()
{
    uint paletteIndex = texture(uIndexedCanvas, fTextureCoordinate).r;
    oColour = texelFetch(uPalette, ivec2(int(paletteIndex), 0), 0);
}
