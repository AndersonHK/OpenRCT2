// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_VEHICLE_RULES
#define OPENRCT2_WORLD_VEHICLE_RULES
// Caller supplies immutable selector/car words. The same code is CPU-testable.
#ifdef __cplusplus
#define VEHICLE_FN inline
// Match SPIR-V OpSMod with positive32; C++ signed remainder differs below zero.
#define VEHICLE_HEADING_WRAP(value) ((value)&31)
#else
#define VEHICLE_FN
#define VEHICLE_HEADING_WRAP(value) ((value)%32)
#endif
// Keep the qualified170c GPU selector shape while the171 codegen variant is
// investigated. This local integer flag is not a GPU buffer ABI requirement.
struct WorldVehicleSelection { int car; int group; int rank; int yaw; int swing; };
VEHICLE_FN WorldVehicleSelection worldVehicleSelect(int car,int pitch,int roll,int yaw,int flags,int trackType,int restraints)
{
    WorldVehicleSelection outValue;
    outValue.car=-1;outValue.group=0;outValue.rank=0;outValue.yaw=0;outValue.swing=1;
    if(pitch<0||pitch>=61||roll<0||roll>=20) return outValue;
    if((flags&(1<<16))!=0) {
        pitch=int(VEHICLE_RULE_WORD(int(VEHICLE_RULE_WORD(6))+pitch));
        roll=int(VEHICLE_RULE_WORD(int(VEHICLE_RULE_WORD(6))+61+roll));yaw=(yaw+16)&31;
    }
    int node=int(VEHICLE_RULE_WORD(8+pitch*20+roll));
    for(int step=0;step<32;step++) {
        if(node<0||node>=int(VEHICLE_RULE_WORD(3))) return outValue;
        int p=int(VEHICLE_RULE_WORD(5))+node*6;
        int encodedGroup=int(VEHICLE_RULE_WORD(p)),rank=int(VEHICLE_RULE_WORD(p+1));
        int group=encodedGroup<0?encodedGroup:(encodedGroup&255);
        int guardGroup=encodedGroup<0?0:(encodedGroup>>8);
        int condition=int(VEHICLE_RULE_WORD(p+3));
        bool inverted=(flags&(1<<11))!=0;
        bool reduce=condition==2 || (condition==1&&inverted);
        // Source exceptions preserve the inverted car on vertical transitions.
        if(condition==3) reduce=inverted&&trackType!=VEHICLE_TRACK_DOWN90_TO_DOWN60&&trackType!=VEHICLE_TRACK_DOWN60_TO_DOWN90;
        if(condition==4) reduce=inverted&&trackType!=VEHICLE_TRACK_DOWN90&&trackType!=VEHICLE_TRACK_DOWN90_TO_DOWN60&&trackType!=VEHICLE_TRACK_DOWN60_TO_DOWN90;
        if(reduce) car--;
        if(car<0 || !VEHICLE_CAR_PRESENT(car)) return outValue;
        if(group==-2) {
            group=0;
            if(restraints>=64 && (yaw&7)==0 && VEHICLE_GROUP_PRECISION(car,37)!=0) {
                group=37;rank=(restraints-64)/64;outValue.swing=0;
            }
        }
        if(group>=0 && (group==0 || group==37 || VEHICLE_GROUP_PRECISION(car,guardGroup)!=0)) {
            outValue.car=car;outValue.group=group;outValue.rank=rank;
            outValue.yaw=VEHICLE_HEADING_WRAP(yaw+int(VEHICLE_RULE_WORD(p+2)));return outValue;
        }
        yaw=VEHICLE_HEADING_WRAP(yaw+int(VEHICLE_RULE_WORD(p+5)));node=int(VEHICLE_RULE_WORD(p+4));
    }
    return outValue;
}
VEHICLE_FN int worldVehicleYawShift(int spritePrecision)
{ return spritePrecision<=1?5:(spritePrecision>=6?0:6-spritePrecision); }
// Original RotoDrop angular image slots and visible back-to-front sequence.
VEHICLE_FN int worldVehicleRotoSlot(int passenger,int animation,int yaw)
{ return (((passenger&3)*16)+(passenger&60)+animation/4+(yaw/8)*16)&63; }
VEHICLE_FN int worldVehicleRotoVisibleSlot(int ordinal)
{ return (ordinal&1)!=0?48-ordinal/2:ordinal/2; }
VEHICLE_FN int worldVehicleRotoPassenger(int slot,int animation,int yaw,int passengers)
{
    int unrotated=(slot-animation/4-(yaw/8)*16)&63;
    if((unrotated&3)!=0) return -1;
    // The original map is not injective above16 riders: later passengers replace
    // earlier colours in the same angular slot. Check the four possible columns
    // and preserve that last-writer ownership without a64-entry private array.
    int passenger=-1;
    for(int column=0;column<4;column++) {
        int row=((unrotated>>2)-column*4)&15;
        int candidate=row*4+column;
        if(row<8 && candidate<passengers && candidate>passenger) passenger=candidate;
    }
    return passenger;
}
VEHICLE_FN bool worldVehicleClassicLayersValid(int passengers)
{ return passengers>=0 && passengers<=32 && passengers/2<=14; }
#undef VEHICLE_HEADING_WRAP
#undef VEHICLE_FN
#endif
