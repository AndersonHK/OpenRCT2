#version 330 core

flat in ivec2 fOrigin;
flat in ivec2 fOffset;
flat in int fPattern;
flat in int fScreenHeight;

out uint oColour;

int positiveMod(int value, int divisor)
{
    int result = value % divisor;
    return result < 0 ? result + divisor : result;
}

void main()
{
    const int patternSize = 32;
    ivec2 pixel = ivec2(int(floor(gl_FragCoord.x)), fScreenHeight - int(floor(gl_FragCoord.y)) - 1);
    int patternY = positiveMod(fOffset.y + pixel.y - fOrigin.y, patternSize);

    uint colour;
    if (fPattern == 0)
    {
        if (patternY == 0)
            colour = 12u;
        else if (patternY == 1)
            colour = 14u;
        else if (patternY == 2)
            colour = 16u;
        else
            discard;
    }
    else
    {
        if (patternY == 0 || patternY == 1)
            colour = 32u;
        else if (patternY == 2)
            colour = 16u;
        else
            discard;
    }

    // Both legacy patterns use an X offset of zero for their active rows.
    int firstPixel = positiveMod(-fOffset.x, patternSize);
    if (positiveMod(pixel.x - fOrigin.x - firstPixel, patternSize) != 0)
    {
        discard;
    }

    oColour = colour;
}
