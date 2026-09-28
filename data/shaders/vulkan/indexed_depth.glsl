// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_INDEXED_DEPTH
#define OPENRCT2_INDEXED_DEPTH
#ifdef __cplusplus
#define INDEXED_DEPTH_FN constexpr
#define INDEXED_DEPTH_UINT uint32_t
#else
#define INDEXED_DEPTH_FN
#define INDEXED_DEPTH_UINT uint
#endif
const INDEXED_DEPTH_UINT INDEXED_DEPTH_PRIORITY_MAX=(1u<<22u)-1u;
// Integer priority advances by128 representable positive D32 values. World
// contacts reserve four priorities each, so their255 local layers cannot reach
// the next contact. Ordinary UI and annotation commands use local layer zero.
INDEXED_DEPTH_FN INDEXED_DEPTH_UINT indexedDepthBits(INDEXED_DEPTH_UINT priority,INDEXED_DEPTH_UINT localLayer)
{
    return 0x3f800000u-((priority+1u)<<7u)-localLayer;
}
#ifndef __cplusplus
float indexedDepth(uint priority,uint localLayer)
{
    return uintBitsToFloat(indexedDepthBits(priority,localLayer));
}
#endif
#undef INDEXED_DEPTH_FN
#undef INDEXED_DEPTH_UINT
#endif
