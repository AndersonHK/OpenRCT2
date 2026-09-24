// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_MAZE_ORDER
#define OPENRCT2_WORLD_MAZE_ORDER
#include "world_static_tower_maze_rules.glsl"
#include "world_path_order.glsl"
#ifdef __cplusplus
#define MAZE_FN constexpr
#define MAZE_LOOP
#else
#define MAZE_FN
#define MAZE_LOOP [[dont_unroll]]
#endif
struct WorldMazeOrder { int count; int indices[26]; };
MAZE_FN int worldMazeQuadrant(int index)
{
    WorldFlatPart p=worldMazePart(index,65535,0,0);
    return (p.bx+p.by)/32;
}
MAZE_FN WorldPathBounds worldMazeBounds(int index,int rotation)
{
    WorldFlatPart p=worldMazePart(index,65535,0,0);
    return worldPathOrderBounds(worldPathPart(0,p.x,p.y,p.z,p.bx,p.by,p.bz,p.sx,p.sy,p.sz),rotation);
}
// Original per-quadrant prepend and legacy linked arrangement for this one
// maze tile. Raw wall bits only select authored parents. Scratch holds integer
// links/flags, never a26-element sprite/bounds aggregate or per-call array copies.
MAZE_FN WorldMazeOrder worldMazeOrder(int rawEntry,int direction,int rotation)
{
    WorldMazeOrder result;result.count=0;
#ifdef __cplusplus
    for(int i=0;i<26;i++) result.indices[i]=-1;
#endif
    if(rotation<0 || rotation>3 || direction<0 || direction>3) return result;
    int next[27];int flags[26];int activeMask=0;int count=0;
    int minQ=2,maxQ=0;
    MAZE_LOOP
    for(int i=0;i<27;i++) next[i]=-1;
    MAZE_LOOP
    for(int i=0;i<26;i++) {
        flags[i]=0;
        if(worldMazePart(i,rawEntry,direction,0).image<0) continue;
        activeMask|=1<<i;count++;
        int quadrant=worldMazeQuadrant(i);
        if(quadrant<minQ) minQ=quadrant;
        if(quadrant>maxQ) maxQ=quadrant;
    }
    int budget=4*count*count*count+64;
    int tail=26;
    MAZE_LOOP
    for(int quadrant=minQ;quadrant<=maxQ;quadrant++)
        MAZE_LOOP
        for(int i=25;i>=0;i--)
            if((activeMask&(1<<i))!=0 && worldMazeQuadrant(i)==quadrant) {next[tail]=i;tail=i;}
    int first=26;
    MAZE_LOOP
    for(int quadrant=minQ;quadrant==minQ || quadrant<maxQ;quadrant++) {
        MAZE_LOOP
        while(next[first]!=-1 && worldMazeQuadrant(next[first])<quadrant) {
            if(--budget<=0) return result;
            first=next[first];
        }
        int node=next[first];
        MAZE_LOOP
        while(node!=-1) {
            if(--budget<=0) return result;
            int q=worldMazeQuadrant(node);
            if(q>quadrant+1) {flags[node]=128;break;}
            if(q==quadrant+1) flags[node]=3;
            else if(q==quadrant) flags[node]=(quadrant==minQ?2:0)|1;
            node=next[node];
        }
        int search=first;
        MAZE_LOOP
        for(;;) {
            if(--budget<=0) return result;
            int parent=search,child=next[parent];
            MAZE_LOOP
            while(child!=-1 && (flags[child]&128)==0 && (flags[child]&1)==0) {
                if(--budget<=0) return result;
                parent=child;child=next[parent];
            }
            if(child==-1 || (flags[child]&128)!=0) break;
            flags[child]&=~1;
            WorldPathBounds initial=worldMazeBounds(child,rotation);
            int scan=child;
            MAZE_LOOP
            for(;;) {
                if(--budget<=0) return result;
                int previous=scan;scan=next[scan];
                if(scan==-1 || (flags[scan]&128)!=0) break;
                if((flags[scan]&2)==0) continue;
                if(worldPathOrderIntersects(initial,worldMazeBounds(scan,rotation),rotation)) {
                    next[previous]=next[scan];next[scan]=next[parent];next[parent]=scan;scan=previous;
                }
            }
            search=parent;
        }
    }
    int node=next[26];
    MAZE_LOOP
    while(node!=-1 && result.count<count) {
        result.indices[result.count++]=node;node=next[node];
    }
    if(node!=-1 || result.count!=count) result.count=0;
    return result;
}
#undef MAZE_FN
#undef MAZE_LOOP
#endif
