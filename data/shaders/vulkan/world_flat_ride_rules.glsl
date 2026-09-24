// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Static/no-vehicle compositions from the corresponding track painters.
// Image bank 0 is G1; bank 1 is relative to the ride object's first car base.
#ifndef OPENRCT2_WORLD_FLAT_RIDE_RULES
#define OPENRCT2_WORLD_FLAT_RIDE_RULES
#ifdef __cplusplus
#define FLAT_FN constexpr
#define FLAT_REF(type) type&
#else
#define FLAT_FN
#define FLAT_REF(type) inout type
#endif
struct WorldFlatPart {
    int image; int bank; int colour; int child;
    int x; int y; int z;
    int bx; int by; int bz; int sx; int sy; int sz;
    int depthAnchor; // 0 raster origin; 1 nearest footprint tile; 2 named platform-fence edge.
};
const int WORLD_FLAT_PART_CAPACITY=8;
struct WorldFlatParts { int count; WorldFlatPart parts[WORLD_FLAT_PART_CAPACITY]; };
#ifdef __cplusplus
FLAT_FN void worldFlatAdd(FLAT_REF(WorldFlatParts) r,int image,int bank,int colour,int child,
    int x,int y,int z,int bx,int by,int bz,int sx,int sy,int sz)
{
    // Asset discovery and exhaustive recipe tests reject authoring overflow before GPU submission.
    if(r.count>=WORLD_FLAT_PART_CAPACITY) throw std::overflow_error("Static ride part capacity exceeded");
    WorldFlatPart p;
    p.image=image;p.bank=bank;p.colour=colour;p.child=child;
    p.x=x;p.y=y;p.z=z;p.bx=bx;p.by=by;p.bz=bz;p.sx=sx;p.sy=sy;p.sz=sz;p.depthAnchor=0;
    r.parts[r.count++]=p;
}
#else
// GLSL inout aggregate calls generate a whole-array copy-in/out temporary at
// every append site. A single assignment preserves the shared recipe and avoids
// that private-storage amplification. Arguments are side-effect-free recipe values.
#define worldFlatAdd(r,image,bank,colour,child,x,y,z,bx,by,bz,sx,sy,sz) \
    r.parts[r.count++]=WorldFlatPart(image,bank,colour,child,x,y,z,bx,by,bz,sx,sy,sz,0)
#endif
#define worldFlatAddBody(r,image,bank,colour,child,x,y,z,bx,by,bz,sx,sy,sz) \
    do { worldFlatAdd(r,image,bank,colour,child,x,y,z,bx,by,bz,sx,sy,sz); r.parts[r.count-1].depthAnchor=1; } while(false)
FLAT_FN int worldFlatSize(int family)
{
    if(family==5 || family==17) return 4;
    if(family==6 || family==7 || family==12) return 16;
    if(family==9 || family==14 || family==15) return 4;
    if(family==13) return 5;
    if(family==18 || family==19) return 1;
    if(family==23) return 1;
    return family>=1 && family<=22?9:0;
}
FLAT_FN int worldFlatSequence(int family,int sequence,int direction)
{
#ifdef __cplusplus
    const int map2[16]={0,1,2,3,1,3,0,2,3,2,1,0,2,0,3,1};
    const int map3[36]={0,1,2,3,4,5,6,7,8,0,3,5,7,2,8,1,6,4,0,7,8,6,5,4,3,1,2,0,6,4,1,8,2,7,3,5};
    const int map4[64]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,3,7,11,15,2,6,10,14,1,5,9,13,0,4,8,12,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,12,8,4,0,13,9,5,1,14,10,6,2,15,11,7,3};
#else
    const int map2[16]=int[16](0,1,2,3,1,3,0,2,3,2,1,0,2,0,3,1);
    const int map3[36]=int[36](0,1,2,3,4,5,6,7,8,0,3,5,7,2,8,1,6,4,0,7,8,6,5,4,3,1,2,0,6,4,1,8,2,7,3,5);
    const int map4[64]=int[64](0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,3,7,11,15,2,6,10,14,1,5,9,13,0,4,8,12,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,12,8,4,0,13,9,5,1,14,10,6,2,15,11,7,3);
#endif
    int n=worldFlatSize(family);
    if(sequence<0 || sequence>=n || direction<0 || direction>3) return -1;
    if(family==9 || family==14 || family==15) return direction==1 || direction==2?(sequence+2)%4:sequence;
    if(family==13) return (direction==1 || direction==2) && sequence!=0?5-sequence:sequence;
    if(n==4) return map2[direction*4+sequence];
    if(n==9) return map3[direction*9+sequence];
    if(n==16) return map4[direction*16+sequence];
    return sequence;
}
struct WorldFlatAnchor { int x; int y; };
// Camera-relative displacement from this sequence tile to the nearest tile of
// the authoritative TED footprint. A whole-body sprite must cover its own floor.
// These are tile coordinates, never inferred from image bounds or opaque pixels.
FLAT_FN WorldFlatAnchor worldFlatFrontAnchor(int family,int sequence,int direction)
{
    int s=worldFlatSequence(family,sequence,direction),n=worldFlatSize(family);
    WorldFlatAnchor result;result.x=0;result.y=0;
    if(s<0) return result;
    if(family==9 || family==13 || family==14 || family==15) {
        int axis=family==13?(s==0?64:(s<3?160-s*32:128-s*32)):
            (s==0?32:(s==1?96:(s==2?64:0)));
        if((direction&1)!=0) result.y=axis; else result.x=axis;
    } else if(n==4) {
        result.x=s>=2?0:32;result.y=(s&1)!=0?0:32;
    } else if(n==16) {
        result.x=96-(s/4)*32;result.y=96-(s%4)*32;
    } else if(n==9) {
#ifdef __cplusplus
        const int x[9]={0,-32,-32,-32,0,0,32,32,32};
        const int y[9]={0,-32,0,32,-32,32,-32,32,0};
#else
        const int x[9]=int[9](0,-32,-32,-32,0,0,32,32,32);
        const int y[9]=int[9](0,-32,0,32,-32,32,-32,32,0);
#endif
        result.x=32-x[s];result.y=32-y[s];
    }
    return result;
}
// Edge bits match TrackPaint.h: NE=1, SE=2, SW=4, NW=8.
FLAT_FN int worldFlatEdges(int family,int sequence,int direction)
{
#ifdef __cplusplus
    const int edges2[4]={9,3,12,6};
    const int edges3[9]={0,9,1,3,8,2,12,6,4};
    const int edges4[16]={9,1,1,3,8,0,0,2,8,0,0,2,12,4,4,6};
#else
    const int edges2[4]=int[4](9,3,12,6);
    const int edges3[9]=int[9](0,9,1,3,8,2,12,6,4);
    const int edges4[16]=int[16](9,1,1,3,8,0,0,2,8,0,0,2,12,4,4,6);
#endif
    if(family==9) {
        int e=(direction&1)!=0?5:10;
        if(sequence==1) e|=(direction&1)!=0?8:1;
        if(sequence==3) e|=(direction&1)!=0?2:4;
        return e;
    }
    int n=worldFlatSize(family);
    if(family==14 || family==15 || family==13 || n==1) return 0;
    return n==4?edges2[sequence]:(n==16?edges4[sequence]:edges3[sequence]);
}
#define worldFlatAddFence(r,image,bank,colour,child,x,y,z,bx,by,bz,sx,sy,sz) \
    do { worldFlatAdd(r,image,bank,colour,child,x,y,z,bx,by,bz,sx,sy,sz); r.parts[r.count-1].depthAnchor=2; } while(false)
FLAT_FN void worldFlatFences(FLAT_REF(WorldFlatParts) r,int edges,int base,int bank,int colour,int special)
{
    if((edges&8)!=0) worldFlatAddFence(r,base+3,bank,colour,1,0,0,0,0,2,2,32,1,7);
    if((edges&1)!=0) worldFlatAddFence(r,base,bank,colour,1,0,0,0,2,0,2,1,32,7);
    if(special==1 || special==3) {
        if((edges&4)!=0) worldFlatAddFence(r,base+2,bank,colour,0,0,0,0,29,0,special==3?3:2,1,28,7);
        if((edges&2)!=0) worldFlatAddFence(r,base+1,bank,colour,0,0,0,0,0,29,special==3?3:2,28,1,7);
    } else {
        if((edges&2)!=0) worldFlatAddFence(r,base+1,bank,colour,0,0,0,0,0,special==2?29:30,special==2?3:2,special==2?28:32,1,7);
        if((edges&4)!=0) worldFlatAddFence(r,base+2,bank,colour,0,0,0,0,30,0,2,1,32,7);
    }
}
// Colour roles: 0 black/station, 1 track main/additional, 2 vehicle body/trim,
// 3 track supports/additional, 4 main/supports, 5 roof darken filter, 6 main only.
FLAT_FN WorldFlatParts worldFlatParts(int family,int sequence,int direction,bool stationPresent,
    bool noPlatforms,int fenceMask,int clearance,int numStations,int numTrains)
{
    WorldFlatParts r;r.count=0;
#ifdef __cplusplus
    // GLSL consumes only entries written by worldFlatAdd; avoid clearing unused private storage.
    for(int i=0;i<WORLD_FLAT_PART_CAPACITY;i++) r.parts[i]=WorldFlatPart(0,0,0,0,0,0,0,0,0,0,0,0,0,0);
#endif
    int s=worldFlatSequence(family,sequence,direction);
    if(s<0) return r;
    int edges=worldFlatEdges(family,s,direction);
    bool platform=stationPresent && !noPlatforms;
    if(family==18 || family==19) {
        int h=clearance-3;
        if(h<0) h=0;
        if(family==18) worldFlatAdd(r,direction,1,6,0,0,0,0,2,2,0,28,28,h);
        else {
            int image=(direction+2)&3;
            bool top=direction==0 || direction==3;
            worldFlatAdd(r,image,1,1,0,0,0,0,2,2,top?h:0,direction==1?8:28,direction==1?28:(top?28:8),top?1:h);
            if(direction==1 || direction==2) worldFlatAdd(r,image+(direction==1?2:4),1,1,0,0,0,0,2,2,h,28,28,1);
        }
        return r;
    }
    if(family==6 || family==7) {
        if(platform) worldFlatAdd(r,family==6?21925:21920,0,family==6?3:1,0,0,0,0,1,1,0,30,30,1);
        if(!noPlatforms && (family==7 || platform)) worldFlatFences(r,edges&fenceMask,family==6?21934:21921,0,family==6?3:1,0);
        if(family==6 && platform) {
            int row=((direction&2)!=0?15-sequence:sequence)/4;
            int image=(direction&1)+(row%2==0?2:0);
            worldFlatAdd(r,21926+image,0,1,0,0,0,30,0,0,30,32,32,2);
            worldFlatAdd(r,21930+image,0,5,1,0,0,30,0,0,30,32,32,2);
        }
        return r;
    }
    if(family==5) {
        if(platform) worldFlatAdd(r,14+(direction&1),1,3,0,0,0,0,0,0,0,32,32,1);
        if(!noPlatforms) worldFlatFences(r,edges&fenceMask,16,1,1,0);
        if(s==1) worldFlatAddBody(r,direction*3+2,1,1,0,16,16,0,16,0,3,16,16,108);
        if(s==2) worldFlatAddBody(r,direction*3,1,1,0,16,16,0,0,16,3,16,16,108);
        if(s==3) {
            if(direction==1) worldFlatAddBody(r,12,1,1,0,16,16,0,-12,0,3,2,16,108);
            if(direction==2) worldFlatAddBody(r,13,1,1,0,16,16,0,0,-12,3,16,2,108);
            int bx=direction==1?14:(direction==3?8:0),by=direction==0?8:(direction==2?14:0);
            worldFlatAddBody(r,direction*3+1,1,1,0,16,16,0,bx,by,3,direction==3?8:(direction==1?2:16),direction==0?8:(direction==2?2:16),108);
        }
        return r;
    }
    if(family==14 || family==15 || family==13) {
        bool middle=family==13?(s!=1 && s!=4):(s==0 || s==2);
        if(platform && middle) {
            worldFlatAdd(r,family==13?22426+(direction&1):22432,0,3,0,0,0,0,0,0,0,32,32,1);
            if(family==14) {
                int x=direction==1?24:0,y=direction==0?24:0;
                worldFlatAdd(r,22362+(direction&1),0,1,direction>=2?1:0,x,y,9,
                    direction==2?-2:x,direction==3?-2:y,direction>=2?0:9,(direction&1)!=0?8:32,(direction&1)!=0?32:8,1);
            } else if(family==13) {
                int odd=direction&1;
                bool backFence=(fenceMask&(odd!=0?1:8))!=0;
                int back=(s==2?(backFence?22366:22368):(backFence?22364:22362))+odd;
                worldFlatAdd(r,back,0,1,1,0,0,9,odd!=0?0:-2,odd!=0?-2:0,9,odd!=0?8:32,odd!=0?32:8,1);
                worldFlatAdd(r,(s==2?22368:22362)+odd,0,1,0,odd!=0?24:0,odd!=0?0:24,9,odd!=0?24:0,odd!=0?0:24,9,odd!=0?8:32,odd!=0?32:8,1);
                bool frontFence=(fenceMask&(odd!=0?4:2))!=0;
                if(s==3) {
                    if(frontFence) worldFlatAdd(r,22372+odd,0,1,0,odd!=0?31:0,odd!=0?0:31,11,odd!=0?31:0,odd!=0?0:31,11,odd!=0?1:32,odd!=0?32:1,7);
                    else worldFlatAdd(r,22374+odd,0,1,0,odd!=0?23:31,odd!=0?31:23,11,odd!=0?23:31,odd!=0?31:23,11,odd!=0?8:1,odd!=0?1:8,7);
                    worldFlatAdd(r,22374+odd,0,1,0,odd!=0?0:31,odd!=0?31:0,11,odd!=0?0:31,odd!=0?31:0,11,odd!=0?8:1,odd!=0?1:8,7);
                } else if(frontFence) worldFlatAdd(r,22370+odd,0,1,0,odd!=0?31:0,odd!=0?0:31,11,odd!=0?31:0,odd!=0?0:31,11,odd!=0?1:32,odd!=0?32:1,7);
            }
        }
    } else {
        if(!noPlatforms) {
            int image=22134+((edges&4)!=0?((edges&2)!=0?0:1):((edges&2)!=0?2:3));
            worldFlatAdd(r,image,0,family==11?0:1,0,0,0,0,0,0,0,32,32,1);
            int fenceColour=(family==2 || family==17)?3:(family==12?1:0);
            worldFlatFences(r,edges&fenceMask,family==10?22146:22138,0,fenceColour,
                family==9?2:(family==10&&s==7?1:(family==11&&s==7?3:0)));
        }
    }
    int x=0,y=0;
    if(family==9 || family==13 || family==14 || family==15) {
        int axis=family==13?(s==0?0:(s<3?96-s*32:64-s*32)):(s==0?-16:(s==1?48:(s==2?16:-48)));
        x=(direction&1)!=0?0:axis;y=(direction&1)!=0?axis:0;
        int bx=(direction&1)!=0?8:(family==9 || family==13?1:0),by=(direction&1)!=0?(family==9 || family==13?1:0):8;
        int sx=(direction&1)!=0?16:(family==9 || family==13?31:32),sy=(direction&1)!=0?(family==9 || family==13?31:32):16;
        if(family==9 || family==13) {
            int base=family==9?22150:21994;
            worldFlatAddBody(r,base+(direction&1)*2,0,1,0,x,y,7,bx,by,7,sx,sy,family==9?127:80);
            worldFlatAddBody(r,family==9?direction*8:(direction&1)*9,1,2,1,x,y,7,bx,by,7,sx,sy,family==9?127:80);
            worldFlatAddBody(r,base+(direction&1)*2+1,0,1,1,x,y,7,bx,by,7,sx,sy,family==9?127:80);
        } else if(family==14) {
            if((direction&2)!=0) worldFlatAddBody(r,(direction&1)*16,1,2,0,x,y,7,bx,by,7,sx,sy,127);
            worldFlatAddBody(r,21998+direction,0,1,(direction&2)!=0?1:0,x,y,7,bx,by,7,sx,sy,127);
            if((direction&2)==0) worldFlatAddBody(r,(direction&1)*16,1,2,1,x,y,7,bx,by,7,sx,sy,127);
        } else {
            worldFlatAddBody(r,22002+(direction&1)*2,0,1,0,x,y,7,bx,by,7,sx,sy,127);
            worldFlatAddBody(r,22006+(direction&1)*32,0,1,1,x,y,7,bx,by,7,sx,sy,127);
            worldFlatAddBody(r,direction,1,2,1,x,y,5,bx,by,7,sx,sy,127);
            worldFlatAddBody(r,22070+(direction&1)*32,0,1,1,x,y,7,bx,by,7,sx,sy,127);
            worldFlatAddBody(r,22003+(direction&1)*2,0,1,1,x,y,7,bx,by,7,sx,sy,127);
        }
        return r;
    }
    if(family==10) {
        int segment=s==0?0:(s==5?1:(s==7?2:(s==8?3:-1)));
        int vehicle=(segment-direction)&3;
        if(segment>=0 && (numStations==0 || vehicle<numTrains)) worldFlatAdd(r,direction,1,2,0,0,0,3,-10,-10,3,20,20,23);
        return r;
    }
    if(family==12) {
        if(s==1 || s==2 || s==4 || s==8) return r;
        x=48-(s/4)*32;y=48-(s%4)*32;
        worldFlatAddBody(r,direction,1,2,0,x,y,7,0,0,7,24,24,48);
        return r;
    }
    if(family==17) {
        if(s==0) return r;
        x=s==1?16:-16;y=s==2?16:-16;
        if(direction==0 || direction==1) {
            worldFlatAddBody(r,direction,1,2,0,x,y,2,x,y,2,20,20,44);
            worldFlatAddBody(r,22154+direction,0,2,1,x,y,2,x,y,2,20,20,44);
            worldFlatAddBody(r,22158+direction,0,2,0,x,y,2,x+(direction==1?34:0),y+(direction==0?32:0),2,direction==1?2:20,direction==0?2:20,44);
        } else {
            worldFlatAddBody(r,22158+direction,0,2,0,x,y,2,x+(direction==3?-10:0),y+(direction==2?-10:0),2,direction==3?2:20,direction==2?2:20,44);
            worldFlatAddBody(r,22154+direction,0,2,0,x,y,2,x+(direction==3?5:0),y+(direction==2?5:0),2,20,20,44);
            worldFlatAddBody(r,direction,1,2,1,x,y,2,x+(direction==3?5:0),y+(direction==2?5:0),2,20,20,44);
        }
        return r;
    }
    if(s==0 || s==2 || s==4) return r;
    x=s==1 || s==3?32:(s==5?0:-32);
    y=s==1 || s==6?32:(s==8?0:-32);
    if(family==3 || family==4) {
        if(s!=3 && s!=6 && s!=7) return r;
        int bx=s==3?6:(s==7?-16:0),by=s==6?6:(s==7?-16:0);
        worldFlatAddBody(r,direction,1,0,0,x,y,3,bx,by,3,s==3?42:(s==7?32:24),s==6?42:(s==7?32:24),127);
    } else if(family==16) {
        worldFlatAddBody(r,572+(direction&1)*2,1,4,0,x,y,3,x+16,y+16,3,24,24,90);
        worldFlatAddBody(r,380+(direction&1)*48,1,1,1,x,y,3,x+16,y+16,3,24,24,90);
        worldFlatAddBody(r,direction*16,1,2,1,x,y,-7,x+16,y+16,3,24,24,90);
        worldFlatAddBody(r,476+(direction&1)*48,1,1,1,x,y,3,x+16,y+16,3,24,24,90);
        worldFlatAddBody(r,573+(direction&1)*2,1,4,1,x,y,3,x+16,y+16,3,24,24,90);
    } else {
        int image=family==8?0:(family==11?((direction*88)%216)%24:direction);
        int z=family==1 || family==2?3:7;
        worldFlatAddBody(r,image,1,2,0,x,y,z,x+16,y+16,z,24,24,family==1 || family==2?47:48);
    }
    return r;
}
#undef FLAT_FN
#undef FLAT_REF
#undef worldFlatAddBody
#undef worldFlatAddFence
#endif
