// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Original WoodenSupports.cpp rules, shared CPU/GLSL. Streaming scalar state:
// no height-dependent parts array and no support-height truncation.
#ifndef OPENRCT2_WORLD_WOODEN_SUPPORT_RULES
#define OPENRCT2_WORLD_WOODEN_SUPPORT_RULES
#include "world_support_state.glsl"
struct WorldWoodenPart
{
    int imageOffset; int x; int y; int z;
    int boundsX; int boundsY; int boundsZ; int sizeX; int sizeY; int sizeZ;
    int orphan;
};
SUPPORT_FN WorldWoodenPart worldWoodenPart(int image,int z,int bx,int by,int bz,int sx,int sy,int sz,int orphan)
{
    WorldWoodenPart p;p.imageOffset=image;p.x=0;p.y=0;p.z=z;
    p.boundsX=bx;p.boundsY=by;p.boundsZ=bz;p.sizeX=sx;p.sizeY=sy;p.sizeZ=sz;p.orphan=orphan;
    return p;
}
SUPPORT_TABLE_BEGIN(worldWoodenImages,48)
    3392,3393,3394,3536,
    3390,3391,3394,3514,
    3558,3559,3560,3570,
    3561,3562,3563,3592,
    3564,3565,3566,3614,
    3567,3568,3569,3636,
    3677,3678,3680,3739,
    3675,3676,3679,3717,
    3761,3762,3763,3773,
    3764,3765,3766,3795,
    3767,3768,3769,3817,
    3770,3771,3772,3839 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldWoodenTransitions,168)
    3465,3466,3467,3468,
    3469,3470,3471,3472,
    3473,3474,3475,3476,
    3477,3478,3479,3480,
    3481,3482,3483,3484,
    3485,3486,3487,3488,
    36255,36256,36257,36258,
    3493,3494,3495,3496,
    3497,3498,3499,3500,
    3501,3502,3503,3504,
    3505,3506,3507,3508,
    3509,3510,3511,3512,
    3513,3513,3513,3513,
    36259,36260,36261,36262,
    36263,36264,36265,36266,
    36267,36268,36269,36270,
    36271,36272,36273,36274,
    36275,36276,36277,36278,
    36279,36280,36281,36282,
    36283,36284,36285,36286,
    36287,36288,36289,36290,
    3681,3682,3683,3684,
    3685,3686,3687,3688,
    3689,3690,3691,3692,
    3693,3694,3695,3696,
    3697,3698,3699,3700,
    3701,3702,3703,3704,
    36291,36292,36293,36294,
    3709,3710,3711,3712,
    3713,3714,3715,3716,
    3717,3718,3719,3720,
    3721,3722,3723,3724,
    3725,3726,3727,3728,
    3729,3729,3729,3729,
    36295,36296,36297,36298,
    36299,36300,36301,36302,
    36303,36304,36305,36306,
    36307,36308,36309,36310,
    36311,36312,36313,36314,
    36315,36316,36317,36318,
    36319,36320,36321,36322,
    36323,36324,36325,36326 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldWoodenTransitionBounds,588)
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    10,0,0,10,32,44,1,
    0,10,0,32,10,44,1,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    10,0,0,10,32,44,1,
    0,10,0,32,10,44,1,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    10,0,2,10,32,76,1,
    0,10,2,32,10,76,1,
    0,0,0,1,1,8,0,
    0,0,0,1,1,4,0,
    0,0,0,1,1,4,0,
    0,0,0,1,1,4,0,
    0,0,0,1,1,4,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    2,2,1,28,28,2,0,
    2,2,1,28,28,2,0,
    2,2,1,28,28,2,0,
    2,2,1,28,28,2,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    10,0,2,10,32,52,1,
    0,10,2,32,10,52,1,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    10,0,2,10,32,76,1,
    0,10,2,32,10,76,1,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0,
    0,0,0,1,1,8,0 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldWoodenSubtypeRotation,24)
    0,1,0,1,
    1,0,1,0,
    2,3,4,5,
    3,4,5,2,
    4,5,2,3,
    5,2,3,4 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldWoodenSlopeOffsets,32)
    0,0,1,2,3,4,5,6,
    7,8,9,10,11,12,13,0,
    0,0,0,0,0,0,0,14,
    0,0,0,17,0,16,15,0 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldWoodenAssets,448)
    3390,3391,3392,3393,3394,3465,3466,3467,
    3468,3469,3470,3471,3472,3473,3474,3475,
    3476,3477,3478,3479,3480,3481,3482,3483,
    3484,3485,3486,3487,3488,3493,3494,3495,
    3496,3497,3498,3499,3500,3501,3502,3503,
    3504,3505,3506,3507,3508,3509,3510,3511,
    3512,3513,3514,3515,3516,3517,3518,3519,
    3520,3521,3522,3523,3524,3525,3526,3527,
    3528,3529,3530,3531,3532,3533,3534,3535,
    3536,3537,3538,3539,3540,3541,3542,3543,
    3544,3545,3546,3547,3548,3549,3550,3551,
    3552,3553,3554,3555,3556,3557,3558,3559,
    3560,3561,3562,3563,3564,3565,3566,3567,
    3568,3569,3570,3571,3572,3573,3574,3575,
    3576,3577,3578,3579,3580,3581,3582,3583,
    3584,3585,3586,3587,3588,3589,3590,3591,
    3592,3593,3594,3595,3596,3597,3598,3599,
    3600,3601,3602,3603,3604,3605,3606,3607,
    3608,3609,3610,3611,3612,3613,3614,3615,
    3616,3617,3618,3619,3620,3621,3622,3623,
    3624,3625,3626,3627,3628,3629,3630,3631,
    3632,3633,3634,3635,3636,3637,3638,3639,
    3640,3641,3642,3643,3644,3645,3646,3647,
    3648,3649,3650,3651,3652,3653,3654,3655,
    3656,3657,3675,3676,3677,3678,3679,3680,
    3681,3682,3683,3684,3685,3686,3687,3688,
    3689,3690,3691,3692,3693,3694,3695,3696,
    3697,3698,3699,3700,3701,3702,3703,3704,
    3709,3710,3711,3712,3713,3714,3715,3716,
    3717,3718,3719,3720,3721,3722,3723,3724,
    3725,3726,3727,3728,3729,3730,3731,3732,
    3733,3734,3735,3736,3737,3738,3739,3740,
    3741,3742,3743,3744,3745,3746,3747,3748,
    3749,3750,3751,3752,3753,3754,3755,3756,
    3757,3758,3759,3760,3761,3762,3763,3764,
    3765,3766,3767,3768,3769,3770,3771,3772,
    3773,3774,3775,3776,3777,3778,3779,3780,
    3781,3782,3783,3784,3785,3786,3787,3788,
    3789,3790,3791,3792,3793,3794,3795,3796,
    3797,3798,3799,3800,3801,3802,3803,3804,
    3805,3806,3807,3808,3809,3810,3811,3812,
    3813,3814,3815,3816,3817,3818,3819,3820,
    3821,3822,3823,3824,3825,3826,3827,3828,
    3829,3830,3831,3832,3833,3834,3835,3836,
    3837,3838,3839,3840,3841,3842,3843,3844,
    3845,3846,3847,3848,3849,3850,3851,3852,
    3853,3854,3855,3856,3857,3858,3859,3860,
    36255,36256,36257,36258,36259,36260,36261,36262,
    36263,36264,36265,36266,36267,36268,36269,36270,
    36271,36272,36273,36274,36275,36276,36277,36278,
    36279,36280,36281,36282,36283,36284,36285,36286,
    36287,36288,36289,36290,36291,36292,36293,36294,
    36295,36296,36297,36298,36299,36300,36301,36302,
    36303,36304,36305,36306,36307,36308,36309,36310,
    36311,36312,36313,36314,36315,36316,36317,36318,
    36319,36320,36321,36322,36323,36324,36325,36326 SUPPORT_TABLE_END
SUPPORT_FN int worldWoodenAssetCount(){return 448;}
SUPPORT_FN int worldWoodenAssetImage(int index){return index>=0&&index<worldWoodenAssetCount()?worldWoodenAssets[index]:-1;}
SUPPORT_FN int worldWoodenSigned16(int value){int v=value&65535;return v>=32768?v-65536:v;}
struct WorldWoodenCursor
{
    int phase; int imageBase; int type; int subtype; int direction; int transition;
    int z; int steps; int slope; int water; int slopeBoundZ; int prepend;
    bool accepted; bool hasSupports;
};
// accepted mirrors the common setup admission, not its public return value.
// After the terminal Next, hasSupports is the exact original A/B return value:
// a requested but unavailable corner transition can make it false AFTER base art.
SUPPORT_FN WorldWoodenCursor worldWoodenBegin(WorldSupportState state,int type,int subtype,int direction,
    int height,int transition,bool typeB,bool rotateSubtype,bool prependAvailable)
{
    WorldWoodenCursor c;
    c.phase=-1;c.imageBase=0;c.type=type;c.subtype=subtype;c.direction=direction;c.transition=transition;
    c.z=0;c.steps=0;c.slope=0;c.water=state.waterHeight;c.slopeBoundZ=typeB?3:11;c.prepend=prependAvailable?1:0;
    c.accepted=false;c.hasSupports=false;
    if(state.passedSurface==0||type<0||type>=2||subtype<0||subtype>=6||direction<0||direction>=4
        ||(transition!=255&&(transition<0||transition>=21)))return c;
    if(rotateSubtype)c.subtype=worldWoodenSubtypeRotation[subtype*4+direction];
    c.imageBase=(type*6+c.subtype)*4;
    c.z=((state.generalHeight+15)&~15)&65535;
    int length=worldWoodenSigned16(height-c.z);
    if(length<0)return c;
    c.steps=length/16;c.slope=state.generalSlope;
    if((c.slope&32)!=0){
        c.phase=typeB&&c.steps==0?4:0;
    }else if((c.slope&16)!=0){
        c.steps-=2;if(c.steps<0)return c;c.phase=1;
    }else if((c.slope&15)!=0){
        c.steps--;if(c.steps<0)return c;c.phase=3;
    }else c.phase=4;
    c.accepted=true;
    return c;
}
SUPPORT_FN WorldWoodenPart worldWoodenNext(SUPPORT_INOUT(WorldWoodenCursor,c))
{
    if(c.phase<0)return worldWoodenPart(-1,0,0,0,0,0,0,0,0);
    int z=c.z;
    if(c.phase==0){
        c.phase=4;c.hasSupports=true;
        return worldWoodenPart(worldWoodenImages[c.imageBase+2],z-2,0,0,z-2,32,32,0,0);
    }
    if(c.phase==1||c.phase==2||c.phase==3){
        int phase=c.phase;
        int image=worldWoodenImages[c.imageBase+3]+worldWoodenSlopeOffsets[c.slope&31]+(phase==2?4:0);
        c.z=phase==1?c.z+16:(c.z+16)&65535;c.phase=phase==1?2:4;c.hasSupports=true;
        return worldWoodenPart(image,z,0,0,z+2,32,32,phase==1?11:c.slopeBoundZ,0);
    }
    if(c.phase==4&&c.steps>0){
        bool halfSection=(z&16)!=0||c.steps==1||z+16==c.water;
        int image=worldWoodenImages[c.imageBase+(halfSection?1:0)];
        int boundZ=halfSection?(c.steps==1?7:12):(c.steps==2?23:28);
        c.z=(c.z+(halfSection?16:32))&65535;c.steps-=halfSection?1:2;c.hasSupports=true;
        return worldWoodenPart(image,z,0,0,z,32,32,boundZ,0);
    }
    c.phase=-1;
    if(c.transition!=255){
        c.hasSupports=c.subtype<2;
        if(c.hasSupports){
            int bounds=(c.transition*4+c.direction)*7;
            int image=worldWoodenTransitions[(c.type*21+c.transition)*4+c.direction];
            return worldWoodenPart(image,c.z,worldWoodenTransitionBounds[bounds],worldWoodenTransitionBounds[bounds+1],
                c.z+worldWoodenTransitionBounds[bounds+2],worldWoodenTransitionBounds[bounds+3],
                worldWoodenTransitionBounds[bounds+4],worldWoodenTransitionBounds[bounds+5],
                worldWoodenTransitionBounds[bounds+6]*c.prepend);
        }
    }
    return worldWoodenPart(-1,0,0,0,0,0,0,0,0);
}
#endif
