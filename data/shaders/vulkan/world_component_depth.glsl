// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_COMPONENT_DEPTH
#define OPENRCT2_WORLD_COMPONENT_DEPTH
#ifdef __cplusplus
#define COMPONENT_DEPTH_FN constexpr
#else
#define COMPONENT_DEPTH_FN
#endif
// Camera-rotated authored XYZ, independent of sprite size, texel position,
// clipping, zoom and screen projection. This anchor scalar is an initial
// ordering heuristic, not proof that every overlapping original sprite agrees.
COMPONENT_DEPTH_FN int worldComponentDepth(int x,int y,int z,int rotation)
{
    if(rotation==1) return y-x+z;
    if(rotation==2) return -x-y+z;
    if(rotation==3) return x-y+z;
    return x+y+z;
}
#undef COMPONENT_DEPTH_FN
#endif
