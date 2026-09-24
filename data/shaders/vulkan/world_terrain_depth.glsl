// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_TERRAIN_DEPTH
#define OPENRCT2_WORLD_TERRAIN_DEPTH
#ifdef __cplusplus
#define TERRAIN_DEPTH_FN constexpr
#else
#define TERRAIN_DEPTH_FN
#endif
struct WorldTerrainDepthPlane { int valid; int dx; int dy; int twiceIntercept; };
// Corners top/right/bottom/left lie at (0,0)/(0,32)/(32,32)/(32,0).
// Heights are world units. dx/dy encode exact half-unit gradients.
TERRAIN_DEPTH_FN WorldTerrainDepthPlane worldTerrainDepthPlane(
    int top,int right,int bottom,int left,int originX,int originY)
{
    WorldTerrainDepthPlane p;
    p.valid=0;p.dx=0;p.dy=0;p.twiceIntercept=0;
    if(top+bottom!=left+right || ((left-top)%16)!=0 || ((right-top)%16)!=0) return p;
    int dx=(left-top)/16,dy=(right-top)/16;
    // A zero denominator is genuinely edge-on under this camera, not a tolerance test.
    if(dx<-2 || dx>2 || dy<-2 || dy>2 || 2-dx-dy==0) return p;
    p.valid=1;p.dx=dx;p.dy=dy;p.twiceIntercept=2*top-dx*originX-dy*originY;
    return p;
}
TERRAIN_DEPTH_FN float worldTerrainDepthAt(int dx,int dy,int twiceIntercept,float u,float v)
{
    float denominator=float(2-dx-dy);
    return (1.5*float(dy-dx)*u+6.0*v+3.0*float(twiceIntercept))/denominator-v;
}
#undef TERRAIN_DEPTH_FN
#endif
