// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_PROP_RULES
#define OPENRCT2_WORLD_PROP_RULES
#ifdef __cplusplus
#define PROP_FN constexpr
#else
#define PROP_FN
struct WorldObjectRecord {
    int baseZ; int clearanceZ;
    uint ordinal; uint flags; uint objectSlot; uint kind; uint direction; uint sequence;
    uint colours; uint data0; uint data1; uint reserved;
    uint trackTypeAndRideType; uint rideIdAndMazeEntry; uint trackData0; uint trackData1;
};
#endif

struct WorldPropPart {
    int imageOffset; int x; int y; int z;
    int boundsX; int boundsY; int boundsZ; int sizeX; int sizeY; int sizeZ;
    int colourMode; int child;
};
struct WorldPropParts { int count; WorldPropPart parts[6]; };
PROP_FN WorldPropPart worldPropPart(int image, int x, int y, int z, int bx, int by, int bz,
    int sx, int sy, int sz, int colourMode, int child)
{
    WorldPropPart p;
    p.imageOffset=image; p.x=x; p.y=y; p.z=z;
    p.boundsX=bx; p.boundsY=by; p.boundsZ=bz; p.sizeX=sx; p.sizeY=sy; p.sizeZ=sz;
    p.colourMode=colourMode; p.child=child;
    return p;
}
PROP_FN WorldPropParts worldEmptyPropParts()
{
    WorldPropParts result; result.count=0;
    for(int i=0;i<6;i++) result.parts[i]=worldPropPart(0,0,0,0,0,0,0,0,0,0,0,0);
    return result;
}
PROP_FN bool worldPropFlag(int flags,int bit) { return (flags & (1 << bit))!=0; }
PROP_FN int worldSmallColourMode(int flags)
{
    if(worldPropFlag(flags,29)) return 3;
    if(worldPropFlag(flags,10)) return worldPropFlag(flags,19)?2:1;
    return 0;
}
PROP_FN WorldPropParts worldSmallPropParts(int flags,int height,int direction,int quadrant,int age,
    int ticks,int phase,int frameOffset,int zoom,int minute,int hour)
{
    WorldPropParts result=worldEmptyPropParts();
    WorldPropPart body=worldPropPart(direction,0,0,0,0,0,0,2,2,0,worldSmallColourMode(flags),0);
    if(worldPropFlag(flags,0)) {
        if(worldPropFlag(flags,24)) {

#ifdef __cplusplus
    const int bx[4]={3,3,17,3};
#else
    const int bx[4]=int[4](3,3,17,3);
#endif

#ifdef __cplusplus
    const int by[4]={3,17,3,3};
#else
    const int by[4]=int[4](3,17,3,3);
#endif

            body.boundsX=bx[direction]; body.boundsY=by[direction];
            body.sizeX=(direction&1)==0?12:26; body.sizeY=(direction&1)==0?26:12;
            body.x=3; body.y=3;
        } else {
            body.x=15; body.y=15;
            if(worldPropFlag(flags,1)) {
                body.x=3; body.y=3; body.sizeX=26; body.sizeY=26;
                if(worldPropFlag(flags,18)) { body.x=1; body.y=1; body.sizeX=30; body.sizeY=30; }
            }
            body.boundsX=body.x; body.boundsY=body.y;
        }
    } else {

#ifdef __cplusplus
    const int qx[4]={7,7,23,23};
#else
    const int qx[4]=int[4](7,7,23,23);
#endif

#ifdef __cplusplus
    const int qy[4]={7,23,23,7};
#else
    const int qy[4]=int[4](7,23,23,7);
#endif

        body.x=qx[quadrant]; body.y=qy[quadrant]; body.boundsX=body.x; body.boundsY=body.y;
    }
    int length=height-4;
    if(length>128 || length<0) length=128;
    body.sizeZ=length-1;
    if(worldPropFlag(flags,5)) { if(age>=40) body.imageOffset+=4; if(age>=55) body.imageOffset+=4; }
    if(!worldPropFlag(flags,21)) result.parts[result.count++]=body;
    if(worldPropFlag(flags,9)) {
        WorldPropPart glass=body; glass.imageOffset+=4; glass.colourMode=4; glass.child=1;
        result.parts[result.count++]=glass;
    }
    if(worldPropFlag(flags,4) && (worldPropFlag(flags,21) || zoom<=1)) {
        WorldPropPart frame=body; frame.child=1; frame.colourMode=0;
        if(worldPropFlag(flags,11)) {
            frame.imageOffset=4+((ticks/2)&15); result.parts[result.count++]=frame;
        } else if(worldPropFlag(flags,12)) {
            frame.imageOffset=8+((ticks/2)&15); result.parts[result.count++]=frame;
            frame.imageOffset=direction+4; result.parts[result.count++]=frame;
            frame.imageOffset=24+((ticks/2)&15); result.parts[result.count++]=frame;
        } else if(worldPropFlag(flags,13)) {
            int time=(hour%12)*4+((minute+6)*17)/256;
            if(time>=48) time-=48;
            time+=direction*12; if(time>=48) time-=48;
            frame.imageOffset=68+time; result.parts[result.count++]=frame;
            time=minute+direction*15; if(time>=60) time-=60;
            frame.imageOffset=8+time; result.parts[result.count++]=frame;
        } else if(worldPropFlag(flags,14)) {
            frame.imageOffset=((ticks+phase)/4)%16; result.parts[result.count++]=frame;
        } else if(worldPropFlag(flags,15)) {
            frame.imageOffset=frameOffset*4+direction;
            if(worldPropFlag(flags,21) || worldPropFlag(flags,16)) frame.imageOffset+=4;
            frame.child=worldPropFlag(flags,21)?0:1; frame.colourMode=worldSmallColourMode(flags);
            result.parts[result.count++]=frame;
        }
    }
    return result;
}
PROP_FN WorldPropParts worldLargePropParts(int flags,int sequence,int direction,int clearance,int corners,int walls)
{

#ifdef __cplusplus
    const int bx[17]={3,17,17,17,3,3,3,3,3,3,3,3,3,3,3,3,1};
#else
    const int bx[17]=int[17](3,17,17,17,3,3,3,3,3,3,3,3,3,3,3,3,1);
#endif


#ifdef __cplusplus
    const int by[17]={3,17,3,3,3,3,3,3,17,17,3,3,3,3,3,3,1};
#else
    const int by[17]=int[17](3,17,3,3,3,3,3,3,17,17,3,3,3,3,3,3,1);
#endif


#ifdef __cplusplus
    const int sx[17]={26,12,12,12,12,26,28,26,12,26,26,26,12,26,26,26,30};
#else
    const int sx[17]=int[17](26,12,12,12,12,26,28,26,12,26,26,26,12,26,26,26,30);
#endif


#ifdef __cplusplus
    const int sy[17]={26,12,12,26,12,26,12,26,12,12,26,26,28,26,26,26,30};
#else
    const int sy[17]=int[17](26,12,12,26,12,26,12,26,12,12,26,26,28,26,26,26,30);
#endif

    int index=walls!=0?((corners<<direction)|(corners>>(4-direction)))&15:16;
    int colour=worldPropFlag(flags,6)?3:(worldPropFlag(flags,1)?2:(worldPropFlag(flags,0)?1:0));
    WorldPropParts result=worldEmptyPropParts(); result.count=1;
    result.parts[0]=worldPropPart(4+sequence*4+direction,0,0,0,bx[index],by[index],0,sx[index],sy[index],
        (clearance<128?clearance:128)-3,colour,0);
    return result;
}
PROP_FN WorldPropParts worldBannerPropParts(int direction,int zoom)
{

#ifdef __cplusplus
    const int bx[8]={1,1,2,29,32,32,2,29};
#else
    const int bx[8]=int[8](1,1,2,29,32,32,2,29);
#endif

#ifdef __cplusplus
    const int by[8]={2,29,32,32,2,29,1,1};
#else
    const int by[8]=int[8](2,29,32,32,2,29,1,1);
#endif

    WorldPropParts result=worldEmptyPropParts(); result.count=zoom<=1?2:0;
    for(int i=0;i<result.count;i++) result.parts[i]=worldPropPart(direction*2+i,0,0,-16,
        bx[direction*2+i],by[direction*2+i],-14,1,1,21,1,0);
    return result;
}
PROP_FN WorldPropParts worldWallPropParts(int flags,int flags2,int height,int direction,int slope,int frame,bool backwards,int ticks)
{

#ifdef __cplusplus
    const int dx[4]={0,1,31,2};
#else
    const int dx[4]=int[4](0,1,31,2);
#endif

#ifdef __cplusplus
    const int dy[4]={0,31,0,1};
#else
    const int dy[4]=int[4](0,31,0,1);
#endif


#ifdef __cplusplus
    const int bx[4]={1,2,30,1};
#else
    const int bx[4]=int[4](1,2,30,1);
#endif

#ifdef __cplusplus
    const int by[4]={1,30,2,1};
#else
    const int by[4]=int[4](1,30,2,1);
#endif


#ifdef __cplusplus
    const int sx[4]={1,29,1,28};
#else
    const int sx[4]=int[4](1,29,1,28);
#endif

#ifdef __cplusplus
    const int sy[4]={28,1,29,1};
#else
    const int sy[4]=int[4](28,1,29,1);
#endif

    int colour=worldPropFlag(flags,7)?3:(worldPropFlag(flags,6)?2:(worldPropFlag(flags,0)?1:0));
    int bh=height*8-2;
    WorldPropParts result=worldEmptyPropParts(); result.count=1;
    WorldPropPart body=worldPropPart(0,dx[direction],dy[direction],0,bx[direction],by[direction],1,sx[direction],sy[direction],bh,colour,0);
    if(worldPropFlag(flags,4)) {

#ifdef __cplusplus
    const int door[128]={
            2,2,22,26,30,34,34,34,34,34,30,26,22,2,6,2,2,2,6,10,14,18,18,18,18,18,14,10,6,2,22,2,
            0,0,4,8,12,16,16,16,16,16,12,8,4,0,20,0,0,0,20,24,28,32,32,32,32,32,28,24,20,0,4,0,
            2,2,6,10,14,18,18,18,18,18,14,10,6,2,22,2,2,2,22,26,30,34,34,34,34,34,30,26,22,2,6,2,
            0,0,20,24,28,32,32,32,32,32,28,24,20,0,4,0,0,0,4,8,12,16,16,16,16,16,12,8,4,0,20,0};
#else
    const int door[128]=int[128](
            2,2,22,26,30,34,34,34,34,34,30,26,22,2,6,2,2,2,6,10,14,18,18,18,18,18,14,10,6,2,22,2,
            0,0,4,8,12,16,16,16,16,16,12,8,4,0,20,0,0,0,20,24,28,32,32,32,32,32,28,24,20,0,4,0,
            2,2,6,10,14,18,18,18,18,18,14,10,6,2,22,2,2,2,22,26,30,34,34,34,34,34,30,26,22,2,6,2,
            0,0,20,24,28,32,32,32,32,32,28,24,20,0,4,0,0,0,4,8,12,16,16,16,16,16,12,8,4,0,20,0);
#endif

        body.imageOffset=door[direction*32+(frame&15)+(backwards?16:0)];
        WorldPropPart front=body; front.imageOffset++;
        if(worldPropFlag(flags,2)) {
            body.boundsX=direction==2?30:1; body.boundsY=direction==1?30:1;
            body.sizeX=direction==0?1:3; body.sizeY=direction==3?1:3; body.sizeZ=bh-5;
            front.boundsX=body.boundsX; front.boundsY=body.boundsY;
            front.boundsZ=bh-((direction==0 || direction==3)?4:3);
            front.sizeX=direction==0?1:(direction==2?3:(direction==1?29:28));
            front.sizeY=direction==3?1:(direction==1?3:(direction==2?29:28));
            front.sizeZ=(direction==0 || direction==3)?3:2;
        } else front.child=1;
        result.parts[0]=body; result.parts[1]=front; result.count=2;
    } else {
        body.sizeZ=(height*8-2)&255;

#ifdef __cplusplus
    const int images[12]={1,5,3,0,4,2,1,3,5,0,2,4};
#else
    const int images[12]=int[12](1,5,3,0,4,2,1,3,5,0,2,4);
#endif

        body.imageOffset=images[direction*3+(slope<=2?slope:0)];
        if(worldPropFlag(flags,3)) {
            if(direction==1) body.imageOffset+=worldPropFlag(flags,1)?12:6;
            else if(direction==2) body.imageOffset+=6;
        }
        if(worldPropFlag(flags2,4)) body.imageOffset+=(ticks&7)*2;
        result.parts[0]=body;
        if(worldPropFlag(flags,1)) {
            body.imageOffset+=6; body.child=1; body.colourMode=4;
            result.parts[result.count++]=body;
        }
    }
    return result;
}
#undef PROP_FN
#endif
