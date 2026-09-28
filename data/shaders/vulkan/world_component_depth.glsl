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
// Each integer anchor owns four of the reserved 2^20 world priority units.
// indexed_depth maps these to512 representable positive D32 values. Local
// layers use one ULP each and may NEVER cross that contact boundary.
const int WORLD_COMPONENT_DEPTH_MIN=-131072;
const int WORLD_COMPONENT_DEPTH_MAX=131071;
const int WORLD_COMPONENT_LAYER_MAX=255;
// A larger representable layer domain does not admit unbounded parent fanout.
const int WORLD_COMPONENT_CHILD_MAX=15;
COMPONENT_DEPTH_FN bool worldComponentDepthValid(int depth,int layer)
{
    return depth>=WORLD_COMPONENT_DEPTH_MIN && depth<=WORLD_COMPONENT_DEPTH_MAX
        && layer>=0 && layer<=WORLD_COMPONENT_LAYER_MAX;
}
COMPONENT_DEPTH_FN int worldComponentPriorityOffset(int depth)
{
    return (depth-WORLD_COMPONENT_DEPTH_MIN)*4;
}
#undef COMPONENT_DEPTH_FN
#endif
