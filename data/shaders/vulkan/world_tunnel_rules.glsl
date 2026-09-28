// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_TUNNEL_RULES
#define OPENRCT2_WORLD_TUNNEL_RULES
#ifdef __cplusplus
#define TUNNEL_FN constexpr
#define TUNNEL_BEGIN {
#define TUNNEL_END }
#else
#define TUNNEL_FN
#define TUNNEL_BEGIN int[](
#define TUNNEL_END )
#endif

// Paint.Surface.cpp kTunnels, in tiny-Z units. The low-clearance replacement
// controls both the opening height and art; door fallback only changes art.
TUNNEL_FN int worldTunnelHeight(int type)
{
    const int values[26] = TUNNEL_BEGIN 2,3,3,3,4,4,2,3,3,3,2,2,2,3,2,3,2,2,2,2,2,2,2,2,2,2 TUNNEL_END;
    return type >= 0 && type < 26 ? values[type] : 0;
}
TUNNEL_FN int worldTunnelBoundsLength(int type)
{
    const int values[26]=TUNNEL_BEGIN 2,3,5,3,4,7,2,3,5,3,3,3,3,4,3,4,2,2,2,2,2,3,3,3,3,3 TUNNEL_END;
    return type>=0 && type<26?values[type]:0;
}
TUNNEL_FN int worldTunnelBoundsOffset(int type)
{
    const int values[26]=TUNNEL_BEGIN 0,0,-32,0,0,-48,0,0,-32,0,-16,-16,-16,-16,-16,-16,0,0,0,0,0,-16,-16,-16,-16,-16 TUNNEL_END;
    return type>=0 && type<26?values[type]:0;
}
TUNNEL_FN int worldTunnelLowerBounds(int type)
{
    return type==2 || type==5 || type==8 || (type>=12 && type<=15) || type>=21?4:15;
}
TUNNEL_FN int worldTunnelLowType(int type)
{
    const int values[26] = TUNNEL_BEGIN 0,0,0,3,3,3,6,6,6,6,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25 TUNNEL_END;
    return type >= 0 && type < 26 ? values[type] : -1;
}
TUNNEL_FN int worldTunnelImageOffset(int type, int side, bool hasDoors)
{
    const int values[26] = TUNNEL_BEGIN 36,40,44,48,52,56,60,64,68,72,76,80,36,48,60,72,84,88,92,96,100,84,88,92,96,100 TUNNEL_END;
    if (type < 0 || type >= 26 || side < 0 || side > 1) return -1;
    if (!hasDoors && type >= 16) type = 0;
    return values[type] + side * 2;
}
TUNNEL_FN int worldTunnelResolveType(int type, int tinyZ, int corner1, int corner2)
{
    int height = worldTunnelHeight(type);
    if (height == 0) return -1;
    return tinyZ + height > corner1 || tinyZ + height > corner2 ? worldTunnelLowType(type) : type;
}
// Encoded authored Ghost Train selector: bit0 doorB, bit1 inward, bit2
// flat-to-down25 geometry. Raw door state remains a GPU input, never a row key.
TUNNEL_FN int worldTunnelDoorType(int authoredType, int doorA, int doorB)
{
    if (authoredType < 256 || authoredType >= 264) return authoredType;
    int bits = authoredType - 256;
    int frame = ((bits & 1) != 0 ? doorB : doorA) & 7;
    int base = (bits & 4) != 0 ? 21 : 16;
    if (frame == 0 || frame >= 6) return base;
    return base + ((bits & 2) != 0 ? 3 : 1) + (frame == 3 ? 1 : 0);
}
// Original uint16 world-height /16 -> uint8 TunnelEntry conversion.
TUNNEL_FN int worldTunnelTinyZ(int worldZ) { return (worldZ & 4095) / 16; }

// Returns type, or -1 when this connected path has no request on this side.
// Edges and slope direction are already camera relative. EDGE_SW=bit2,
// EDGE_SE=bit1, EDGE_NW=bit3, EDGE_NE=bit0. Preserve the original
// slope comparisons against EDGE_SE=2 and EDGE_NE=1.
TUNNEL_FN int worldPathTunnelType(int edges, int slopeDirection, bool sloped, int side)
{
    int connection = side == 0 ? 4 : 2;
    if (side < 0 || side > 1 || (edges & connection) == 0) return -1;
    if (sloped && slopeDirection == (side == 0 ? 2 : 1)) return 10;
    return (edges & (side == 0 ? 8 : 1)) != 0 ? 11 : 10;
}
TUNNEL_FN int worldPathTunnelZOffset(int slopeDirection, bool sloped, int side)
{
    return sloped && slopeDirection == (side == 0 ? 2 : 1) ? 16 : 0;
}
// Shop/Facility painters request square-flat only on their door-facing views.
TUNNEL_FN int worldStaticRideTunnelType(int family, int direction, int side)
{
    return (family==18 || family==19) && (direction==1 || direction==2)
        && side==(direction&1)?6:-1;
}
// All tower painters, including Lift, share centre sequence0 of kTrackMap3x3.
// Section sequence1 returns before any vertical-tunnel state mutation.
TUNNEL_FN int worldStaticRideVerticalTunnelOffset(int family, int sequence, bool section)
{
    if((family<20 || family>22) && family!=24) return -1;
    if(section) return sequence==1?-1:32;
    return sequence==0?96:-1;
}
#undef TUNNEL_FN
#undef TUNNEL_BEGIN
#undef TUNNEL_END
#endif
