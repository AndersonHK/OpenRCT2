// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_PATH_ORDER
#define OPENRCT2_WORLD_PATH_ORDER
#include "world_path_rules.glsl"
#ifdef __cplusplus
#define PATH_ORDER_FN constexpr
#define PATH_ORDER_LOOP
#else
#define PATH_ORDER_FN
#define PATH_ORDER_LOOP [[dont_unroll]]
#endif

struct WorldPathOrder
{
    int count;
    int indices[12];
};
struct WorldPathBounds
{
    int x; int y; int z; int ex; int ey; int ez;
};
// Exact CreateNormalPaintStruct / RotateBoundBoxSize endpoint convention.
// Translation by the common path's tile and base Z cancels in comparisons.
PATH_ORDER_FN WorldPathBounds worldPathOrderBounds(WorldPathPart part, int rotation)
{
    int x=part.boundsX; int y=part.boundsY;
    int sx=part.sizeX; int sy=part.sizeY;
    WorldPathBounds b;
    b.z=part.boundsZ; b.ez=b.z+part.sizeZ;
    if (rotation==0) { b.x=x; b.y=y; b.ex=x+sx-1; b.ey=y+sy-1; }
    else if (rotation==1) { b.x=-y; b.y=x; b.ex=-y-sy; b.ey=x+sx-1; }
    else if (rotation==2) { b.x=-x; b.y=-y; b.ex=-x-sx; b.ey=-y-sy; }
    else { b.x=y; b.y=-x; b.ex=y+sy-1; b.ey=-x-sx; }
    return b;
}
PATH_ORDER_FN bool worldPathOrderIntersects(WorldPathBounds a, WorldPathBounds b, int rotation)
{
    if (rotation==0) return a.ez>=b.z && a.ey>=b.y && a.ex>=b.x
        && !(a.z<b.ez && a.y<b.ey && a.x<b.ex);
    if (rotation==1) return a.ez>=b.z && a.ey>=b.y && a.ex<b.x
        && !(a.z<b.ez && a.y<b.ey && a.x>=b.ex);
    if (rotation==2) return a.ez>=b.z && a.ey<b.y && a.ex<b.x
        && !(a.z<b.ez && a.y>=b.ey && a.x>=b.ex);
    return a.ez>=b.z && a.ey<b.y && a.ex>=b.x
        && !(a.z<b.ez && a.y>=b.ey && a.x<b.ex);
}

// Bounded ordering of ONE path's parent recipes, not a replacement for the
// scene sorter. Preserve source emission order in parts; children stay attached
// to their parent and are not entries here. Current recipes have bounds origins
// within [0,32]; an interior tile's absolute quadrant translation cancels.
// Zero-area sprite recipes still participate: the CPU can allocate a paint
// struct for them even though the image draws no pixels.
PATH_ORDER_FN WorldPathOrder worldPathOrder(WorldPathPart parts[12], int count, int rotation)
{
    WorldPathOrder result;
    result.count=0;
    PATH_ORDER_LOOP
    for(int i=0;i<12;i++) result.indices[i]=-1;
    if (count<0 || count>12 || rotation<0 || rotation>3) return result;
    if (count==0) return result;
    // Keep linked-list traversal finite even if a future recipe exposes a cycle.
    // Valid lists need at most quadratic work per quadrant; this generous cubic
    // budget preserves their ordering and falls back to authored order on failure.
    int budget=4*count*count*count+64;
    int q[12]; int flags[12]; int next[13]; WorldPathBounds bounds[12];
    PATH_ORDER_LOOP
    for(int i=0;i<13;i++) next[i]=-1;
    int minQ=2; int maxQ=0;
    PATH_ORDER_LOOP
    for(int i=0;i<count;i++)
    {
        if (parts[i].boundsX<0 || parts[i].boundsX>32 || parts[i].boundsY<0 || parts[i].boundsY>32)
            return result;
        q[i]=(parts[i].boundsX+parts[i].boundsY)/32;
        if(q[i]<minQ) minQ=q[i];
        if(q[i]>maxQ) maxQ=q[i];
        bounds[i]=worldPathOrderBounds(parts[i],rotation);
        flags[i]=0;
    }
    // Equivalent to per-quadrant prepend followed by ascending quadrant link.
    int tail=12;
    PATH_ORDER_LOOP
    for(int quadrant=minQ;quadrant<=maxQ;quadrant++)
        PATH_ORDER_LOOP
        for(int i=count-1;i>=0;i--)
            if(q[i]==quadrant) { next[tail]=i; tail=i; }
    int first=12;
    PATH_ORDER_LOOP
    for(int quadrant=minQ;quadrant==minQ || quadrant<maxQ;quadrant++)
    {
        // PaintStructsFirstInQuadrant: first remains the preceding node.
        PATH_ORDER_LOOP
        while(next[first]!=-1 && q[next[first]]<quadrant) {
            if(--budget<=0) return result;
            first=next[first];
        }
        int node=next[first];
        PATH_ORDER_LOOP
        while(node!=-1)
        {
            if(--budget<=0) return result;
            if(q[node]>quadrant+1) { flags[node]=128; break; }
            if(q[node]==quadrant+1) flags[node]=3;
            else if(q[node]==quadrant) flags[node]=(quadrant==minQ ? 2 : 0)|1;
            node=next[node];
        }
        int search=first;
        PATH_ORDER_LOOP
        for(;;)
        {
            if(--budget<=0) return result;
            int parent=search;
            int child=next[parent];
            PATH_ORDER_LOOP
            while(child!=-1 && (flags[child]&128)==0 && (flags[child]&1)==0)
            { if(--budget<=0) return result; parent=child; child=next[parent]; }
            if(child==-1 || (flags[child]&128)!=0) break;
            flags[child]&=~1;
            WorldPathBounds initial=bounds[child];
            int scan=child;
            PATH_ORDER_LOOP
            for(;;)
            {
                if(--budget<=0) return result;
                int previous=scan;
                scan=next[scan];
                if(scan==-1 || (flags[scan]&128)!=0) break;
                if((flags[scan]&2)==0) continue;
                if(worldPathOrderIntersects(initial,bounds[scan],rotation))
                {
                    next[previous]=next[scan];
                    next[scan]=next[parent];
                    next[parent]=scan;
                    scan=previous;
                }
            }
            search=parent;
        }
    }
    int node=next[12];
    PATH_ORDER_LOOP
    while(node!=-1 && result.count<count)
    {
        result.indices[result.count++]=node;
        node=next[node];
    }
    return result;
}
#undef PATH_ORDER_LOOP
#undef PATH_ORDER_FN
#endif
