#version 450

layout(location = 0) flat in uint fColour;
layout(location = 0) out uint oColour;

void main()
{
    oColour = fColour;
}
