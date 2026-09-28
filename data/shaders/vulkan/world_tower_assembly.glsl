// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_TOWER_ASSEMBLY
#define OPENRCT2_WORLD_TOWER_ASSEMBLY
#include "world_component_depth.glsl"
// All roles share one assembly contact. Ordinary object roles retain0..15;
// the shaft owns body/cap pairs16..143, ahead of rear0 and behind front144.
// This is a fine hardware-depth field, strictly below the next world contact.
const int WORLD_TOWER_REAR=0;
const int WORLD_TOWER_SHAFT=16;
const int WORLD_TOWER_TIER_HEIGHT=32;
const int WORLD_TOWER_MAX_TIERS=64;
const int WORLD_TOWER_FRONT=WORLD_TOWER_SHAFT+WORLD_TOWER_MAX_TIERS*2;
const int WORLD_TOWER_FIRST_RIDER=1;
const int WORLD_TOWER_SHAFT_ANCHOR=4;
#ifdef __cplusplus
#define TOWER_FN constexpr
#else
#define TOWER_FN
#endif
TOWER_FN bool worldTowerFamily(int family) { return family>=20 && family<=22; }
TOWER_FN int worldTowerContactDepth(int x,int y,int baseZ,int rotation)
{ return worldComponentDepth(x,y,baseZ,rotation); }
struct WorldTowerShaftOrder { int layer; };
TOWER_FN WorldTowerShaftOrder worldTowerShaftOrder(int relativeWorldZ)
{
    int tier=relativeWorldZ/WORLD_TOWER_TIER_HEIGHT;
    // Invalid/imported geometry cannot escape the band's material ownership.
    if(tier<0) tier=0;
    if(tier>=WORLD_TOWER_MAX_TIERS) tier=WORLD_TOWER_MAX_TIERS-1;
    WorldTowerShaftOrder order;
    order.layer=WORLD_TOWER_SHAFT+tier*2;
    return order;
}
#undef TOWER_FN
#ifndef __cplusplus
// Cold ride-owned contact. No moving pose, current art selection or map scan.
layout(std430,set=0,binding=15) readonly buffer FlatRideCatalog { uint words[]; } uFlatRides;
bool worldTowerContact(uint id,inout int depth,inout int baseZ)
{
    if(uFlatRides.words[0]!=0x57464c54u || uFlatRides.words[1]!=1u || id>=uFlatRides.words[3]) return false;
    uint ride=uFlatRides.words[2]+id*20u;
    if(!worldTowerFamily(int(uFlatRides.words[ride]))) return false;
    uint xy=uFlatRides.words[ride+18u],height=uFlatRides.words[ride+19u];
    if((height&0x80000000u)==0u) return false;
    baseZ=int(height&65535u);
    depth=worldTowerContactDepth(int(xy&65535u),int(xy>>16u),baseZ,int(uScene.rotation));
    return true;
}
bool worldTowerContact(uint id,inout int depth)
{ int baseZ;return worldTowerContact(id,depth,baseZ); }
#endif
#endif
