// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_TRACK_DEPTH
#define OPENRCT2_WORLD_TRACK_DEPTH
#ifdef __cplusplus
#define TRACK_DEPTH_FN constexpr
#else
#define TRACK_DEPTH_FN
#endif
// An under-rail support belongs to one authored track element. Preserve its
// original anchor when already below that element's rails; cap only its contact
// with those rails. Other track elements never enter this constraint.
TRACK_DEPTH_FN int worldTrackUnderRailDepth(int authoredDepth,int ownRailDepth)
{
    return authoredDepth<ownRailDepth?authoredDepth:ownRailDepth-1;
}
// Internal, already-resolved station-cover recipes encode edge/variant here.
// These values are never looked up as G1 image IDs. Front covers contain the
// shelter roof as well as its fascia. Use the front edge's authored origin
// (the same edge as the front fence), lifted to the explicit shelter height.
// Do not add a midpoint along that edge: it pushes the whole roof in front of
// a neighboring entrance. Back wall covers do not opt into this anchor.
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
    int edge=encoded&3,variant=encoded/4;
    if(edge!=1 && edge!=2) return a;
    a.x=edge==1?0:31;a.y=edge==1?31:0;
    a.z=1+(variant==0?22:(variant==1?30:46));a.valid=true;
    return a;
}
#undef TRACK_DEPTH_FN
#endif
