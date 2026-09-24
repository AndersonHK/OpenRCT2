// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_TRACK_DEPTH
#define OPENRCT2_WORLD_TRACK_DEPTH
#ifdef __cplusplus
#define TRACK_DEPTH_FN constexpr
#else
#define TRACK_DEPTH_FN
#endif
// LoopingRCTrackFlat authors one rail sprite at raster origin (0,0), but
// explicitly locates its supporting column/contact socket at (16,16,height).
// Use that named contact for depth only. This is not a bounds-derived rule for
// other track styles, curved pieces, loop walls, or unrelated crossing rails.
TRACK_DEPTH_FN bool worldTrackHasCentreContactAnchor(int trackType,int sequence,int image)
{
    return trackType==0 && sequence==0 && image>=15004 && image<=15009;
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
