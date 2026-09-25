// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Scalar rules from Paint.Path.cpp and Paint.PathAddition.cpp. This file is
// compiled as both GLSL and C++; production callers execute these rules on GPU.
#ifndef OPENRCT2_WORLD_PATH_RULES
#define OPENRCT2_WORLD_PATH_RULES
#include "world_foreground_depth.glsl"
#ifdef __cplusplus
#define PATH_FN constexpr
#else
#define PATH_FN
#endif

struct WorldPathPart
{
    int imageOffset;
    int x; int y; int z;
    int boundsX; int boundsY; int boundsZ;
    int sizeX; int sizeY; int sizeZ;
};
struct WorldPathParts
{
    int count;
    WorldPathPart parts[4];
};

PATH_FN WorldPathPart worldPathPart(int image, int x, int y, int z, int bx, int by, int bz, int sx, int sy, int sz)
{
    WorldPathPart p;
    p.imageOffset=image; p.x=x; p.y=y; p.z=z;
    p.boundsX=bx; p.boundsY=by; p.boundsZ=bz; p.sizeX=sx; p.sizeY=sy; p.sizeZ=sz;
    return p;
}
PATH_FN WorldPathParts worldPathEmptyParts()
{
    WorldPathParts p;
    p.count=0;
    for (int i=0;i<4;i++) p.parts[i]=worldPathPart(-1,0,0,0,0,0,0,0,0,0);
    return p;
}
PATH_FN int worldPathRotateMask(int mask, int rotation)
{
    return ((mask << rotation) | (mask >> (4-rotation))) & 15;
}

// Queue material base images already select the queue surface allocation.
PATH_FN int worldPathSurfaceOffset(int edges, int corners, int slopeDirection, bool sloped, bool queue, int rotation)
{
    if (sloped) return 16+((slopeDirection+rotation)&3);
    int e=worldPathRotateMask(edges,rotation);
    int key=e | (queue ? 0 : worldPathRotateMask(corners,rotation)<<4);
    switch (key)
    {
        case 19:return 20; case 23:return 22; case 27:return 26; case 31:return 36;
        case 38:return 21; case 39:return 23; case 46:return 33; case 47:return 37;
        case 55:return 24; case 63:return 38;
        case 76:return 29; case 77:return 30; case 78:return 34; case 79:return 39;
        case 95:return 40; case 110:return 35; case 111:return 41; case 127:return 42;
        case 137:return 25; case 139:return 27; case 141:return 31; case 143:return 43;
        case 155:return 28; case 159:return 44; case 175:return 45; case 191:return 46;
        case 205:return 32; case 207:return 47; case 223:return 48; case 239:return 49; case 255:return 50;
    }
    return e;
}
// Exact raw path rise direction, matching Paint.Path.cpp kPathSlopeToLandSlope.
PATH_FN int worldPathLandSlope(int slopeDirection)
{
    return slopeDirection==0 ? 12 : (slopeDirection==1 ? 9 : (slopeDirection==2 ? 3 : 6));
}
PATH_FN bool worldPathNeedsSupports(int pathBase, bool sloped, int slopeDirection, bool terrainPresent,
    int terrainBase, int terrainSlope, bool noSlopeRailings)
{
    if (!terrainPresent) return true;
    if (terrainBase!=pathBase) return terrainBase<pathBase || !noSlopeRailings;
    int matchingSlope=worldPathLandSlope(slopeDirection);
    return terrainSlope!=(sloped ? matchingSlope : 0);
}
// Camera-relative bounds, BEFORE the ordinary painter's bounds rotation.
// Tile traversal supplies passedSurface and flat-track-at-same-height facts.
PATH_FN WorldPathPart worldPathSurfacePart(int imageOffset, int rotatedEdges, bool passedSurface, bool flatTrack)
{
    int e=passedSurface ? rotatedEdges : 0;
    int x=(e&1)!=0 ? 0 : 3;
    int y=(e&8)!=0 ? 0 : 3;
    int sx=26+((e&1)!=0 ? 3 : 0)+((e&4)!=0 ? 3 : 0);
    int sy=26+((e&2)!=0 ? 3 : 0)+((e&8)!=0 ? 3 : 0);
    return worldPathPart(imageOffset,0,0,0,x,y,flatTrack ? 2 : 1,sx,sy,0);
}
PATH_FN int worldPathBoxOrientation(int rotatedEdges)
{
    return rotatedEdges==2 || rotatedEdges==6 || rotatedEdges==8 || rotatedEdges==9
        || rotatedEdges==10 || rotatedEdges==11 || rotatedEdges==14 ? 1 : 0;
}
PATH_FN int worldPathBridgeOffset(int rotatedEdges, int rotatedSlope, bool sloped, bool pole)
{
    if (pole) return sloped ? 16+rotatedSlope : rotatedEdges;
    return sloped ? 51+rotatedSlope : 49+worldPathBoxOrientation(rotatedEdges);
}

// A compact vocabulary of the original flat railing parent recipes.
PATH_FN WorldPathPart worldPathFenceKind(int kind)
{
    switch (kind)
    {
        case 0:return worldPathPart(3,0,4,0,0,4,2,27,1,7);
        case 1:return worldPathPart(3,0,28,0,0,27,2,27,1,7);
        case 2:return worldPathPart(4,4,0,0,4,0,2,1,27,7);
        case 3:return worldPathPart(4,28,0,0,27,0,2,1,27,7);
        case 4:return worldPathPart(5,0,4,0,0,4,2,27,1,7);
        case 5:return worldPathPart(5,0,28,0,0,27,2,27,1,7);
        case 6:return worldPathPart(1,0,4,0,0,4,2,32,1,7);
        case 7:return worldPathPart(1,0,28,0,0,27,2,32,1,7);
        case 8:return worldPathPart(2,4,0,0,4,0,2,1,27,7);
        case 9:return worldPathPart(2,28,0,0,27,0,2,1,27,7);
        case 10:return worldPathPart(0,4,0,0,4,0,2,1,32,7);
        case 11:return worldPathPart(0,28,0,0,27,0,2,1,32,7);
        case 12:return worldPathPart(3,0,4,0,0,4,2,26,1,7);
        case 13:return worldPathPart(4,28,0,0,27,4,2,1,27,7);
        case 14:return worldPathPart(2,4,0,0,4,0,2,1,26,7);
        case 15:return worldPathPart(5,0,28,0,4,27,2,27,1,7);
        case 16:return worldPathPart(11,0,0,0,0,27,2,4,4,7);
        case 17:return worldPathPart(12,0,0,0,27,27,2,4,4,7);
        case 18:return worldPathPart(13,0,0,0,27,0,2,4,4,7);
        case 19:return worldPathPart(10,0,0,0,0,0,2,4,4,7);
    }
    return worldPathPart(-1,0,0,0,0,0,0,0,0,0);
}
// Fences only: banners are separate parents, emitted AFTER this list. The
// caller omits both fences and additions at zoom>1, matching Paint.Path.cpp.
PATH_FN WorldPathParts worldPathFences(int edges, int corners, int slopeDirection, int rotation,
    bool sloped, bool queue, bool hasSupports, bool noSlopeRailings, bool drawPathOverSupports, bool junctionRailings)
{
    WorldPathParts result=worldPathEmptyParts();
    if (!queue && !hasSupports && (!sloped || noSlopeRailings)) return result;
    int q=queue ? 14 : 0;
    if (sloped)
    {
        int d=(slopeDirection+rotation)&3;
        int image=d==0 ? 8 : (d==1 ? 7 : (d==2 ? 9 : 6));
        result.count=2;
        if ((d&1)==0)
        {
            result.parts[0]=worldPathPart(image+q,0,4,0,0,4,2,32,1,23);
            result.parts[1]=worldPathPart(image+q,0,28,0,0,27,2,32,1,23);
        }
        else
        {
            result.parts[0]=worldPathPart(image+q,4,0,0,4,0,2,1,32,23);
            result.parts[1]=worldPathPart(image+q,28,0,0,27,0,2,1,32,23);
        }
        return result;
    }
    int e=worldPathRotateMask(edges,rotation);
    if (queue && !junctionRailings && (e==7 || e==11 || e==13 || e==14 || e==15)) return result;
    int a=-1; int b=-1; int c=-1; int d=-1;
    switch (e)
    {
        case 1:a=0;b=1;break; case 2:a=2;b=3;break; case 3:a=12;b=13;c=16;break;
        case 4:a=4;b=5;break; case 5:a=6;b=7;break; case 6:a=2;b=4;c=17;break;
        case 7:a=6;b=16;c=17;break; case 8:a=8;b=9;break; case 9:a=9;b=1;c=19;break;
        case 10:a=10;b=11;break;
        case 11:a=11;b=queue ? 19 : 16;c=queue ? 16 : 19;break;
        case 12:a=14;b=15;c=18;break;
        case 13:a=7;b=queue ? 19 : 18;c=queue ? 18 : 19;break;
        case 14:a=10;b=17;c=18;break;
        case 15:a=queue ? 19 : 16;b=queue ? 16 : 17;c=queue ? 17 : 18;d=queue ? 18 : 19;break;
    }
    int hiddenCorners=!queue && drawPathOverSupports ? worldPathRotateMask(corners,rotation) : 0;
    for (int i=0;i<4;i++)
    {
        int kind=i==0 ? a : (i==1 ? b : (i==2 ? c : d));
        if (kind<0 || (kind>=16 && (hiddenCorners&(1<<(kind-16)))!=0)) continue;
        WorldPathPart p=worldPathFenceKind(kind);
        p.imageOffset+=q;
        result.parts[result.count++]=p;
    }
    return result;
}
PATH_FN bool worldPathBinFull(int status, int rotatedEdge, int rotation)
{
    int rawEdge=(rotatedEdge-rotation+4)&3;
    return (status & (3<<(2*rawEdge)))==0;
}
// Each normal/broken/full addition bank contains the same four camera-facing
// edge variants. Fountain sprites are independent effects, not edge fixtures.
PATH_FN bool worldPathAdditionHasFrontContact(int drawType,int imageOffset)
{
    int edge=(imageOffset-1)&3;
    return drawType>=0 && drawType<3 && (edge==1 || edge==2);
}
// drawType: PathAdditionDrawType {light=0,bin=1,bench=2,jumpingFountain=3}.
// All offsets are relative to the addition allocation; z is relative to path.
PATH_FN WorldPathParts worldPathAdditions(int edges, int rotation, bool sloped, int drawType,
    bool broken, int status, bool highlightIssues, int zoom)
{
    WorldPathParts result=worldPathEmptyParts();
    if (zoom>1 || drawType<0 || drawType>3 || (drawType==3 && zoom>0)
        || (highlightIssues && !broken && drawType!=1)) return result;
    int openEdges=worldPathRotateMask(edges^15,rotation);
    for (int i=0;i<4;i++)
    {
        if (drawType!=3 && (openEdges&(1<<i))==0) continue;
        bool full=worldPathBinFull(status,i,rotation);
        if (drawType==1 && highlightIssues && !broken && !full) continue;
        int image=i+1+(drawType==3 ? 0 : (broken ? 4 : (drawType==1 && full ? 8 : 0)));
        int z=sloped && drawType<2 ? 8 : 0;
        WorldPathPart p;
        if (drawType==0)
        {
            if (i==0) p=worldPathPart(image,2,16,z,3,8,z+2,0,16,23);
            else if (i==1) p=worldPathPart(image,16,30,z,2,29,z+2,22,0,23);
            else if (i==2) p=worldPathPart(image,30,16,z,29,2,z+2,0,22,23);
            else p=worldPathPart(image,16,2,z,8,3,z+2,16,0,23);
        }
        else
        {
            if (i==0) p=worldPathPart(image,7,16,z,6,8,z+2,0,16,7);
            else if (i==1) p=worldPathPart(image,16,25,z,8,23,z+2,16,0,7);
            else if (i==2) p=worldPathPart(image,25,16,z,23,8,z+2,0,16,7);
            else p=worldPathPart(image,16,7,z,8,6,z+2,16,0,7);
            if (drawType==3) { p.x=0;p.y=0;p.sizeX=1;p.sizeY=1;p.sizeZ=2; }
        }
        result.parts[result.count++]=p;
    }
    return result;
}
#undef PATH_FN
#endif
