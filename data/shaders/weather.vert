#version 330 core

uniform ivec2 uScreenSize;

in vec2 vPosition;
in ivec4 vBounds;
in ivec2 vOffset;
in int vPattern;

flat out ivec2 fOrigin;
flat out ivec2 fOffset;
flat out int fPattern;
flat out int fScreenHeight;

void main()
{
    vec2 pos = mix(vec2(vBounds.xy), vec2(vBounds.zw), vPosition);

    fOrigin = vBounds.xy;
    fOffset = vOffset;
    fPattern = vPattern;
    fScreenHeight = uScreenSize.y;

    pos /= vec2(uScreenSize);
    pos.y = 1.0 - pos.y;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
