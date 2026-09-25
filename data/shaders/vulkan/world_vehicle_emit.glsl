// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_VEHICLE_EMIT
#define OPENRCT2_WORLD_VEHICLE_EMIT
layout(std430,set=0,binding=23) readonly buffer WorldVehicles { uint words[]; } uVehicles;
layout(std430,set=0,binding=24) readonly buffer WorldVehicleCatalog { uint words[]; } uVehicleCatalog;
// Reset for every work item before any emission. Cutaway uses owned entity XYZ,
// independently of inverted-body/reverser raster placement and seat depth anchors.
ivec3 worldVehicleClipPosition;
uint worldVehicleCarWord(int car,int field) { return uVehicleCatalog.words[16u+uint(car)*96u+uint(field)]; }
#define VEHICLE_RULE_WORD(i) uVehicleCatalog.words[uVehicleCatalog.words[4]+uint(i)]
#define VEHICLE_CAR_PRESENT(c) (uint(c)<uVehicleCatalog.words[2] && worldVehicleCarWord(c,0)!=0u)
#define VEHICLE_GROUP_PRECISION(c,g) int(worldVehicleCarWord(c,17+(g)*2))
#define VEHICLE_TRACK_DOWN90 127
#define VEHICLE_TRACK_DOWN90_TO_DOWN60 129
#define VEHICLE_TRACK_DOWN60_TO_DOWN90 131
#include "world_vehicle_rules.glsl"
#undef VEHICLE_RULE_WORD
#undef VEHICLE_CAR_PRESENT
#undef VEHICLE_GROUP_PRECISION
#undef VEHICLE_TRACK_DOWN90
#undef VEHICLE_TRACK_DOWN90_TO_DOWN60
#undef VEHICLE_TRACK_DOWN60_TO_DOWN90
uint worldVehicleCount()
{
    return uVehicles.words[0]==0x56535231u && uVehicles.words[1]==1u ? uVehicles.words[2] : 0u;
}
uint worldVehicleImage(uint image)
{
    uint begin=0u,end=uVehicleCatalog.words[7],base=uVehicleCatalog.words[6];
    [[dont_unroll]] while(begin<end) {
        uint mid=begin+(end-begin)/2u;
        if(uVehicleCatalog.words[base+mid*2u]<image) begin=mid+1u;else end=mid;
    }
    return begin<uVehicleCatalog.words[7] && uVehicleCatalog.words[base+begin*2u]==image
        ? uVehicleCatalog.words[base+begin*2u+1u] : 0xffffffffu;
}
uint worldVehicleFind(uint id)
{
    uint begin=0u,end=worldVehicleCount();
    [[dont_unroll]] while(begin<end) {
        uint mid=begin+(end-begin)/2u;
        if(uVehicles.words[16u+mid*32u+3u]<id) begin=mid+1u;else end=mid;
    }
    return begin<worldVehicleCount() && uVehicles.words[16u+begin*32u+3u]==id ? 16u+begin*32u : 0u;
}
uint worldVehicleRiderColour(uint address,uint seat)
{return (uVehicles.words[address+16u+seat/4u]>>((seat&3u)*8u))&255u;}
uint worldVehiclePalette(uint colours,uint remaps)
{
    uint packed=((colours&255u)+1u);
    if(remaps>=2u) packed|=(((colours>>8u)&255u)+1u)<<8u;
    if(remaps>=3u) packed|=(((colours>>16u)&255u)+1u)<<16u;
    return packed;
}
uint worldVehicleGroupImage(int car,int group,int yaw,int rank)
{
    int spritePrecision=int(worldVehicleCarWord(car,17+group*2));
    if(spritePrecision<1||spritePrecision>7) return 0xffffffffu;
    uint rotations=(1u<<uint(spritePrecision))>>1u;
    return worldVehicleCarWord(car,16+group*2)+
        (uint(yaw>>worldVehicleYawShift(spritePrecision))+rotations*uint(rank))*worldVehicleCarWord(car,3);
}
void worldVehicleEmit(ivec3 xyz,uint image,uint colours,uint remaps,bool ghost,uint layer,
    uint destination,bool writeRecords,inout uint count)
{
    uint sprite=worldVehicleImage(image);
    if(sprite==0xffffffffu) {
        atomicOr(uStatus.overflow,65536u);
        atomicCompSwap(uStatus.reserved,0u,image+1u);
        return;
    }
    uint palette=ghost?uVehicleCatalog.words[9]:worldVehiclePalette(colours,remaps);
    uint effects=ghost?1u:remaps;
    if((uScene.viewFlags&(1u<<20))!=0u) { palette=uCatalog.viewPalettes.x;effects=1u<<10; }
    worldEmitEntitySpriteAt(xyz,worldVehicleClipPosition,sprite,palette,effects,layer,destination,writeRecords,count);
}
void worldVehicleSplash(uint a,int car,ivec3 xyz,uint layer,uint destination,bool writeRecords,inout uint count)
{
    uint effect=worldVehicleCarWord(car,9),orientation=uVehicles.words[a+7u];
    if(effect<10u||effect>14u) return;
    int velocity=int(uVehicles.words[a+26u]);
    if(effect==10u) {
        if(uVehicles.words[a+24u]!=117u || uVehicles.words[a+25u]<48u || uVehicles.words[a+25u]>=112u) return;
        uint head=a;
        [[dont_unroll]] for(uint link=0u;link<255u;link++) {
            if((uVehicles.words[head+6u]&(1u<<29))!=0u) break;
            uint previous=worldVehicleFind(uVehicles.words[head+30u]);
            if(previous==0u||previous==head) return;
            head=previous;
        }
        if((uVehicles.words[head+6u]&(1u<<29))==0u) return;
        velocity=int(uVehicles.words[head+26u]);
    } else {
        if((orientation&7u)!=0u || uVehicles.words[a+8u]!=0u) return;
        if(effect>=13u) {
            uint previous=worldVehicleFind(uVehicles.words[a+30u]);if(previous==0u) return;
            velocity=int(uVehicles.words[previous+26u]);
            if(effect==14u && (uVehicles.words[a+6u]&(1u<<30))==0u) return;
        }
    }
    if(velocity<=327680) return; // Original5.0_mph fixed-point threshold.
    uint image=effect==11u?29046u:(effect>=13u?29078u:29014u);
    image+=(((orientation/8u)+uScene.rotation)&3u)*8u+((uScene.sourceTick/2u)&7u);
    worldVehicleEmit(xyz,image,0u,0u,false,layer,destination,writeRecords,count);
}
void worldVehicleRotoRider(ivec3 raster,int slot,uint image,uint colour,uint layer,
    uint destination,bool writeRecords,inout uint count)
{
    ivec2 offset=ivec2(worldVehicleRotoDepthX(slot),worldVehicleRotoDepthY(slot));
    if(uScene.rotation==1u) offset=ivec2(-offset.y,offset.x);
    else if(uScene.rotation==2u) offset=-offset;
    else if(uScene.rotation==3u) offset=ivec2(offset.y,-offset.x);
    ivec3 anchor=raster+ivec3(offset,0);
    int depth=worldComponentDepth(anchor.x,anchor.y,anchor.z,int(uScene.rotation));
    if(!worldComponentDepthValid(depth,int(layer))) {atomicOr(uStatus.overflow,16u);return;}
    uint first=count;
    worldVehicleEmit(raster,image,colour,1u,false,layer,destination,writeRecords,count);
    if(writeRecords && count>first && destination+first<uScene.outputCapacity)
        uOutputs.records[destination+first].reserved.x=depth;
}
// Each index is one immutable vehicle, scheduled once after terrain work. No
// tile scans, camera-dependent CPU preparation, or compatibility paint replay.
void visitWorldVehicle(uint index,uint destination,bool writeRecords,inout uint count)
{
    if(index>=worldVehicleCount() || uScene.zoom>2 || (uScene.viewFlags&(1u<<14))!=0u
        || ((uScene.viewFlags&(1u<<20))!=0u && (uScene.viewFlags&(1u<<25))!=0u)
        || uVehicleCatalog.words[0]!=0x56534331u || uVehicleCatalog.words[1]!=1u) return;
    uint a=16u+index*32u;
    ivec3 xyz=ivec3(uVehicles.words[a],uVehicles.words[a+1u],uVehicles.words[a+2u]);
    worldVehicleClipPosition=xyz;
    if(xyz.x<0||xyz.y<0||xyz.x>=32032||xyz.y>=32032 || (uScene.viewFlags&(1u<<18))!=0u) return;
    if((uScene.viewFlags&((1u<<15)|(1u<<17)|(1u<<25)))==((1u<<15)|(1u<<17)|(1u<<25))
        && xyz.z>int(uSelection.words[11])) return;
    uint flags=uVehicles.words[a+6u],colours=uVehicles.words[a+14u],peeps=min(uVehicles.words[a+15u],32u);
    bool ghost=(flags&(1u<<31))!=0u;
    if((flags&(1u<<15))!=0u) {
        worldVehicleEmit(xyz,uVehicleCatalog.words[10]+uVehicles.words[a+10u],0u,0u,false,1u,destination,writeRecords,count);return;
    }
    int car=int(uVehicles.words[a+5u]);
    bool inverted=(flags&(1u<<11))!=0u;
    if(inverted) { car++;xyz.z+=16; }
    if(car<0||uint(car)>=uVehicleCatalog.words[2]||worldVehicleCarWord(car,0)==0u) return;
    uint style=worldVehicleCarWord(car,1),base=worldVehicleCarWord(car,2);
    int yaw=int((uVehicles.words[a+7u]+uScene.rotation*8u)&31u),pitch=int(uVehicles.words[a+8u]);
    int restraints=int(uVehicles.words[a+13u]);
    uint animation=uVehicles.words[a+10u],spin=uVehicles.words[a+12u],layer=1u;
    if(style==1u) return; // Flat-ride body is already owned by the static ride emitter.
    if(style==7u) {
        uint previous=worldVehicleFind(uVehicles.words[a+30u]),next=worldVehicleFind(uVehicles.words[a+31u]);
        if(previous==0u||next==0u) return;
        xyz=(ivec3(uVehicles.words[previous],uVehicles.words[previous+1u],uVehicles.words[previous+2u])+
            ivec3(uVehicles.words[next],uVehicles.words[next+1u],uVehicles.words[next+2u]))/2;
        style=0u;
    }
    if(style==8u) return; // Proxy is represented by its separately published real neighbor, never recursively repainted.
    if(style==0u) {
        WorldVehicleSelection selection=worldVehicleSelect(car,pitch,int(uVehicles.words[a+9u]),yaw,int(flags),
            int(uVehicles.words[a+24u]),restraints);
        if(selection.car<0) return;
        car=selection.car;
        if(worldVehicleCarWord(car,10)>=16u) return;
        base=worldVehicleGroupImage(car,selection.group,selection.yaw,selection.rank);
        if(base==0xffffffffu) return;
        if(selection.swing!=0) base+=uVehicles.words[a+11u];
        uint carFlags=worldVehicleCarWord(car,6);
        if((carFlags&(1u<<14))!=0u) base+=worldVehicleCarWord(car,8)*spin/256u;
        if((carFlags&(1u<<23))!=0u) base+=animation;
        worldVehicleEmit(xyz,base,colours,3u,ghost,layer++,destination,writeRecords,count);
        if(uScene.zoom<2) {
            [[dont_unroll]] for(uint row=0u;row<8u && row<worldVehicleCarWord(car,5) && row*2u<peeps;row++) {
                uint image=base+worldVehicleCarWord(car,4)*(row+1u);
                if(row==0u&&(carFlags&(1u<<24))!=0u) image+=worldVehicleCarWord(car,4)*animation;
                uint rider=worldVehicleRiderColour(a,row*2u)|(worldVehicleRiderColour(a,row*2u+1u)<<8u);
                worldVehicleEmit(xyz,image,rider,2u,ghost,layer++,destination,writeRecords,count);
            }
        }
    } else if(style==2u || style==3u || style==9u) {
        uint back,front;
        if(style==2u) {base+=uint(restraints/64)*2u;back=base+2u;front=base+1u;}
        else if(style==3u) {
            uint direction=uint(yaw/8);
            base+=restraints<64?animation*2u+8u:((direction==0u||direction==3u)?8u:uint(restraints/64)*2u+(direction==1u?28u:22u));
            back=base;front=base+1u;
        } else {
            base+=4u+((animation/4u)&3u);
            if(restraints>=64) base+=7u+uint(restraints/64);
            back=base;front=base+4u;
        }
        worldVehicleEmit(xyz,back,colours,style==3u?3u:2u,ghost,layer++,destination,writeRecords,count);
        worldVehicleEmit(xyz,front,colours,style==3u?3u:2u,ghost,layer++,destination,writeRecords,count);
        if(style==2u && uScene.zoom<2 && !ghost) {
            [[dont_unroll]] for(uint row=0u;row<4u&&row*2u<peeps;row++) {
                uint image=worldVehicleCarWord(car,2)+9u+(restraints/64==3?2u:0u)+((uint(yaw/8)+row)&3u)*3u;
                uint rider=worldVehicleRiderColour(a,row*2u)|(worldVehicleRiderColour(a,row*2u+1u)<<8u);
                worldVehicleEmit(xyz,image,rider,2u,false,layer++,destination,writeRecords,count);
            }
        }
        if(style==9u && peeps!=0u && !ghost) {
            [[dont_unroll]] for(int ordinal=0;ordinal<=48;ordinal++) {
                int slot=worldVehicleRotoVisibleSlot(ordinal);
                int passenger=worldVehicleRotoPassenger(slot,int(animation),yaw,int(peeps));
                if(passenger<0) continue;
                uint image=worldVehicleCarWord(car,2)+20u+uint(slot);
                if(restraints>=64) image+=64u+uint(restraints/64);
                worldVehicleRotoRider(xyz,slot,image,worldVehicleRiderColour(a,uint(passenger)),
                    uint(worldVehicleRotoLayer(ordinal)),destination,writeRecords,count);
            }
        }
    } else if(style==4u || style==15u) {
        uint rotation=((spin/8u)+uScene.rotation*8u)&31u;
        int heading=yaw;
        if(pitch==5||pitch==6) heading^=16;
        uint slope=(pitch==1||pitch==5)?8u:((pitch==2||pitch==6)?40u:0u);
        base+=(rotation&7u)+(slope==0u?0u:slope+uint(heading&24));
        worldVehicleEmit(xyz,base,colours,2u,ghost,layer++,destination,writeRecords,count);
        if(uScene.zoom<2 && !ghost) {
            [[dont_unroll]] for(uint row=0u;row<4u;row++) {
                uint side=row==2u?3u:(row==3u?2u:row);
                uint seat=0u;
                if(style==4u) {
                    side=row==1u?2u:(row==2u?1u:row);
                    if(row*2u>=peeps) continue;
                    seat=row*2u;side=(rotation/8u+side)&3u;
                } else {
                    seat=(side+4u-rotation/8u)&3u;
                    if(seat>=peeps) continue;
                }
                uint rider=worldVehicleRiderColour(a,seat);
                if(style==4u) rider|=worldVehicleRiderColour(a,seat+1u)<<8u;
                worldVehicleEmit(xyz,base+(side+1u)*72u,rider,style==4u?2u:1u,false,layer++,destination,writeRecords,count);
            }
        }
    } else if(style==5u || style==6u) {
        uint golf=uVehicles.words[a+29u],mode=golf&255u;
        base=worldVehicleCarWord((car/4)*4,2);
        if(style==6u) {
            if(mode==1u&&uScene.zoom<1) worldVehicleEmit(xyz,base,0u,0u,false,layer++,destination,writeRecords,count);
        } else if(peeps!=0u && uScene.zoom<2 && (golf&(1u<<24))!=0u) {
            int frame=-1;uint f=animation;
            if(mode==0u&&f<6u) frame=int(f);
            else if(mode==1u&&f<4u) frame=12+int(f);
            else if((mode==2u||mode==7u)&&f<15u) {
                frame=f<10u?6:(f==10u?7:(f<14u?8:9));if(mode==7u) frame+=25;
            } else if(mode==3u&&f<7u) frame=12+int(f<4u?f:6u-f);
            else if(mode==4u&&f<15u) frame=16+int(f);
            else if(mode==5u&&f<4u) frame=15-int(f);
            else if((mode==6u||mode==8u)&&f<10u) {frame=(f==0u||f>=6u)?10:11;if(mode==8u) frame+=25;}
            if(frame>=0) worldVehicleEmit(xyz,base+1u+uint(frame)*4u+uint(yaw/8),(golf>>8u)&65535u,
                2u,false,layer++,destination,writeRecords,count);
        }
    } else if(style==16u) {
        if(restraints>=64) {
            if(worldVehicleCarWord(car,17+37*2)==0u || (yaw&3)!=0) return;
            int spritePrecision=int(worldVehicleCarWord(car,17+37*2));
            base=worldVehicleCarWord(car,16+37*2)+uint((yaw>>worldVehicleYawShift(spritePrecision))+((restraints-64)/64)*4)*worldVehicleCarWord(car,3);
        } else base=worldVehicleGroupImage(car,0,yaw,0)+uVehicles.words[a+11u];
        worldVehicleEmit(xyz,base,colours,3u,ghost,layer++,destination,writeRecords,count);
        worldVehicleEmit(xyz,base+1u,colours,3u,ghost,layer++,destination,writeRecords,count);
    } else if(style==17u) {
        // One body and at most fourteen rider pairs fit this single-anchor
        // authored group. Reject unsupported larger groups; never truncate.
        if(uScene.zoom<2 && !worldVehicleClassicLayersValid(int(peeps))) {atomicOr(uStatus.overflow,16u);return;}
        int image=0,shift=5,reverse=0;
        if(pitch>=1&&pitch<=8) {
            int p=pitch>4?pitch-4:pitch;reverse=pitch>4?16:0;
            image=p==1?4:(p==2?68:(p==3?20:52));shift=p==2?1:(p==3?2:3);
        } else if(pitch>=50&&pitch<=55) {
            int p=(pitch-50)%3;reverse=pitch>=53?16:0;image=135+p*16;shift=3;
        }
        base+=uint(image+((yaw^reverse)>>shift)*4)+((spin>>4u)&3u);
        if(restraints>=64) base=worldVehicleCarWord(car,2)+132u+uint((restraints-64)/64);
        worldVehicleEmit(xyz,base,colours,2u,ghost,layer++,destination,writeRecords,count);
        if(uScene.zoom<2) {
            [[dont_unroll]] for(uint row=0u;row<peeps/2u;row++) {
                uint turn=(spin>>4u)+uScene.rotation*4u+row*4u;
                uint swap=(turn&15u)>=8u?1u:0u;
                uint rider=worldVehicleRiderColour(a,row*2u+swap)|(worldVehicleRiderColour(a,row*2u+1u-swap)<<8u);
                worldVehicleEmit(xyz,base+((turn&7u)>=4u?366u:183u),rider,2u,ghost,layer++,destination,writeRecords,count);
            }
        }
    }
    if(style==0u||style==4u) worldVehicleSplash(a,car,xyz,layer,destination,writeRecords,count);
}
#endif
