// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_TRACK_DEPTH
#define OPENRCT2_WORLD_TRACK_DEPTH
#include "world_foreground_depth.glsl"
#ifdef __cplusplus
#define TRACK_DEPTH_FN constexpr
#else
#define TRACK_DEPTH_FN
#endif
// An under-rail support belongs to one authored track element. Preserve its
// original anchor when already below that element's rails; cap only its contact
// with those rails. Equal contact uses support layer1 below rail layer2;
// subtracting a world unit would bury ground-level supports beneath terrain.
// Other track elements never enter this constraint.
const int WORLD_TRACK_RAIL_LAYER=2;
const int WORLD_TRACK_FLOOR_ROLE=32;
// Photo-platform and rail are independent source parents at the same raster
// contact. The platform is a floor, not another rail; keep it beneath its rail
// without moving either component in worldXY/worldZ or changing vehicle depth.
TRACK_DEPTH_FN int worldTrackComponentLayer(int colourRole)
{
    return (colourRole&WORLD_TRACK_FLOOR_ROLE)!=0?1:WORLD_TRACK_RAIL_LAYER;
}
TRACK_DEPTH_FN int worldTrackUnderRailDepth(int authoredDepth,int ownRailDepth)
{
    return authoredDepth<ownRailDepth?authoredDepth:ownRailDepth;
}
// Internal, already-resolved station-cover recipes encode edge/variant here.
// These values are never looked up as G1 image IDs. Front covers enclose their
// platform's rail at the same near contact, using a later local shell role.
// Roof art height must not push the complete shell in front of the next tile's
// entrance. Back walls retain their separate authored anchors.
TRACK_DEPTH_FN int worldTrackStationCoverMarker(int edge,int variant)
{
    return (edge==1 || edge==2) && variant>=0 && variant<=2 ? -256+variant*4+edge : 0;
}
struct WorldTrackStationCoverAnchor { int x; int y; int z; bool valid; };
TRACK_DEPTH_FN WorldTrackStationCoverAnchor worldTrackStationCoverAnchor(int marker)
{
    WorldTrackStationCoverAnchor a;
    a.x=0;a.y=0;a.z=0;a.valid=false;
    int encoded=marker+256;
    if(encoded<0 || encoded>10) return a;
    int edge=encoded&3;
    if(edge!=1 && edge!=2) return a;
    a.x=WORLD_FOREGROUND_TILE_CORNER;a.y=WORLD_FOREGROUND_TILE_CORNER;
    a.z=0;a.valid=true;
    return a;
}
#undef TRACK_DEPTH_FN
#endif
