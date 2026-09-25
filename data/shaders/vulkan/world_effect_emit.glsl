// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
struct WorldEffect { int x; int y; int z; uint type; uint orientation; uint frame; uint state; uint colours; };
layout(std430,set=0,binding=27) readonly buffer WorldEffects { uint count; uint spriteBase; uint reserved0; uint reserved1; WorldEffect records[]; } uEffects;
void visitWorldEffect(uint index,uint destination,bool writeRecords,inout uint count)
{
    if(index>=uEffects.count || uScene.zoom>2 || (uScene.viewFlags&(1u<<14))!=0u) return;
    WorldEffect e=uEffects.records[index];
    ivec3 clipPosition=ivec3(e.x,e.y,e.z);
    if(e.x==-32768) return;
    if((uScene.viewFlags&(1u<<18))!=0u && e.type!=3u) return;
    uint direction=((e.orientation+uScene.rotation*8u)&31u)/8u;
    uint image=0u,remaps=0u,layer=0u;
    // Bank is original G1 22577..23188, resident once. These rules consume only raw family state.
    if(e.type==3u) {
        if(uScene.zoom>0 || e.state>=12u) return;
        const uint litter[12]=uint[](23101u,23103u,23105u,23107u,23109u,23111u,23113u,23115u,23117u,23121u,23125u,23129u);
        image=litter[e.state]+(direction&(e.state<8u?1u:3u)); layer=1u;
    } else if(e.type==4u) { if(e.frame>=3584u) return; image=22637u+e.frame/256u; }
    else if(e.type==6u) { if(uScene.zoom>0 || e.state>=5u || e.frame>=3072u) return; image=22577u+e.state*12u+e.frame/256u; remaps=2u; }
    else if(e.type==7u) { if(e.frame>=4608u) return; image=22878u+e.frame/256u; }
    else if(e.type==8u) { if(e.frame>=7168u) return; image=22927u+e.frame/256u; }
    else if(e.type==9u) { if(e.frame>=7936u) return; image=22896u+e.frame/256u; }
    else if(e.type==10u) { if(uScene.zoom>0 || e.frame>=16u || e.state>1u) return; image=22973u+e.state*64u+direction*16u+e.frame; e.z+=6; }
    else if(e.type==12u) {
        if(uScene.zoom>1) return;
        uint frame=0u;
        if(e.state==0u || e.state==4u) { if(e.frame>=6u) return; frame=8u+e.frame; }
        else if(e.state==2u) {
            const uint sequence[24]=uint[](1u,1u,1u,1u,2u,2u,2u,2u,3u,3u,3u,3u,2u,2u,2u,2u,1u,1u,1u,1u,0u,0u,0u,0u);
            if(e.frame>=24u) return; frame=sequence[e.frame];
        } else if(e.state==3u) {
            const uint sequence[39]=uint[](4u,4u,4u,4u,5u,5u,5u,5u,6u,6u,6u,6u,7u,7u,7u,7u,7u,7u,7u,7u,7u,7u,7u,6u,6u,6u,6u,5u,5u,5u,5u,4u,4u,4u,4u,0u,0u,0u,0u);
            if(e.frame>=39u) return; frame=sequence[e.frame];
        } else if(e.state!=1u) return;
        image=23133u+frame*4u+direction;
    } else return;
    uint palettes=remaps==0u?0u:((e.colours&255u)+1u)|((((e.colours>>8u)&255u)+1u)<<8u)|(remaps<<24u);
    worldEmitEntitySpriteAt(ivec3(e.x,e.y,e.z),clipPosition,uEffects.spriteBase+image-22577u,
        palettes,remaps,layer,destination,writeRecords,count);
}
