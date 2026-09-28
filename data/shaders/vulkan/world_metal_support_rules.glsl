// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Scalar MetalSupports.cpp A/B rules. A cursor emits one parent at a time;
// neither invocation memory nor part count is truncated at a chosen height.
#ifndef OPENRCT2_WORLD_METAL_SUPPORT_RULES
#define OPENRCT2_WORLD_METAL_SUPPORT_RULES
#include "world_support_state.glsl"
struct WorldMetalPart
{
    int imageOffset; int x; int y; int z;
    int boundsX; int boundsY; int boundsZ; int sizeX; int sizeY; int sizeZ;
};
SUPPORT_FN WorldMetalPart worldMetalPart(int image,int x,int y,int z,int bx,int by,int bz,int sx,int sy,int sz)
{
    WorldMetalPart p;
    p.imageOffset=image;p.x=x;p.y=y;p.z=z;
    p.boundsX=bx;p.boundsY=by;p.boundsZ=bz;p.sizeX=sx;p.sizeY=sy;p.sizeZ=sz;
    return p;
}
SUPPORT_TABLE_BEGIN(worldMetalX,9)4,28,4,28,16,16,4,28,16 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldMetalY,9)4,4,28,28,16,4,16,16,28 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldMetalPlaceRotation,36)0,2,3,1,1,0,2,3,2,3,1,0,3,1,0,2,4,4,4,4,5,6,8,7,6,8,7,5,7,5,6,8,8,7,5,6 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldMetalGraphics,32)0,0,0,0,1,2,1,2,3,3,3,3,4,5,4,5,7,8,6,9,10,10,10,10,11,11,11,11,12,12,12,12 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldMetalReposition,36)42,57,51,64,43,10,3,25,16,49,40,66,59,48,0,17,11,26,14,29,7,20,65,33,34,32,35,21,4,30,15,58,69,62,52,47 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldMetalCentreReposition,16)43,48,65,58,48,65,58,43,65,58,43,48,58,43,48,65 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldMetalCrossX,8)-15,0,-2,-1,-26,0,-2,-1 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldMetalCrossY,8)-1,-2,-1,-15,-1,-2,-1,-26 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldMetalCrossSizeX,8)18,3,18,3,32,3,32,3 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldMetalCrossSizeY,8)3,18,3,18,3,32,3,32 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldMetalCrossImages,104)3370,3371,3370,3371,3372,3373,3372,3373,3374,3375,3374,3375,3376,3377,3376,3377,3374,3375,3374,3375,3376,3377,3376,3377,3370,3371,3370,3371,3372,3373,3372,3373,3374,3375,3374,3375,3376,3377,3376,3377,3374,3375,3374,3375,3376,3377,3376,3377,3378,3383,3378,3383,3380,3385,3380,3385,3378,3383,3378,3383,3380,3385,3380,3385,3382,3379,3382,3379,3384,3381,3384,3381,3382,3379,3382,3379,3384,3381,3384,3381,3378,3379,3378,3379,3380,3381,3380,3381,3386,3387,3386,3387,3388,3389,3388,3389,3370,3371,3370,3371,3372,3373,3372,3373 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldMetalDeductions,13)6,3,3,6,3,3,6,6,6,6,4,3,6 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldMetalBases,13)3243,3279,3298,3334,-1,-1,-1,-1,-1,-1,-1,3243,3334 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldMetalBeams,13)3209,3262,3262,3317,3658,3658,3141,3158,3175,3192,3124,3209,3353 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldMetalCaps,13)3226,3262,3262,3317,3658,3658,3141,3158,3175,3192,3124,3226,3353 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldMetalSlopeImages,32)0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,0,0,0,0,0,0,0,0,15,0,0,0,16,0,17,18,0 SUPPORT_TABLE_END
SUPPORT_TABLE_BEGIN(worldMetalAssets,282)3124,3125,3126,3127,3128,3129,3130,3131,3132,3133,3134,3135,3136,3137,3138,3139,3140,3141,3142,3143,3144,3145,3146,3147,3148,3149,3150,3151,3152,3153,3154,3155,3156,3157,3158,3159,3160,3161,3162,3163,3164,3165,3166,3167,3168,3169,3170,3171,3172,3173,3174,3175,3176,3177,3178,3179,3180,3181,3182,3183,3184,3185,3186,3187,3188,3189,3190,3191,3192,3193,3194,3195,3196,3197,3198,3199,3200,3201,3202,3203,3204,3205,3206,3207,3208,3209,3210,3211,3212,3213,3214,3215,3216,3217,3218,3219,3220,3221,3222,3223,3224,3225,3226,3227,3228,3229,3230,3231,3232,3233,3234,3235,3236,3237,3238,3239,3240,3241,3243,3244,3245,3246,3247,3248,3249,3250,3251,3252,3253,3254,3255,3256,3257,3258,3259,3260,3261,3262,3263,3264,3265,3266,3267,3268,3269,3270,3271,3272,3273,3274,3275,3276,3277,3278,3279,3280,3281,3282,3283,3284,3285,3286,3287,3288,3289,3290,3291,3292,3293,3294,3295,3296,3297,3298,3299,3300,3301,3302,3303,3304,3305,3306,3307,3308,3309,3310,3311,3312,3313,3314,3315,3316,3317,3318,3319,3320,3321,3322,3323,3324,3325,3326,3327,3328,3329,3330,3331,3332,3333,3334,3335,3336,3337,3338,3339,3340,3341,3342,3343,3344,3345,3346,3347,3348,3349,3350,3351,3352,3353,3354,3355,3356,3357,3358,3359,3360,3361,3362,3363,3364,3365,3366,3367,3368,3369,3370,3371,3372,3373,3374,3375,3376,3377,3378,3379,3380,3381,3382,3383,3384,3385,3386,3387,3388,3389,3658,3659,3660,3661,3662,3663,3664,3665,3666,3667,3668,3669,3670,3671,3672,3673,3674 SUPPORT_TABLE_END
SUPPORT_FN int worldMetalAssetCount(){return 282;}
SUPPORT_FN int worldMetalAssetImage(int i){return i>=0&&i<worldMetalAssetCount()?worldMetalAssets[i]:-1;}
SUPPORT_FN int worldMetalSigned16(int value){int v=value&65535;return v>=32768?v-65536:v;}
SUPPORT_FN int worldMetalMin(int a,int b){return a<b?a:b;}
struct WorldMetalCursor
{
    int phase; int graphic; int original; int segment; int cross; int top; int crossHeight;
    int z; int slope; int joint; int extraStart; int extraEnd; int extraBeam;
    bool accepted;
};
// helperRotation=4 selects the unrotated API. alreadyRotatedPlacement is the
// SideBySide contract: rotate graphics, but leave its explicit side slot alone.
// Commit state at Begin, before streaming: callers must drain an accepted cursor
// before executing another support operation. Rejected Begin leaves state intact.
SUPPORT_FN WorldMetalCursor worldMetalBegin(SUPPORT_INOUT(WorldSupportState,state),int type,int place,
    int helperRotation,int cameraRotation,int height,int extra,bool typeB,bool alreadyRotatedPlacement)
{
    WorldMetalCursor c;
    c.phase=-1;c.graphic=0;c.original=0;c.segment=0;c.cross=-1;c.top=0;c.crossHeight=0;c.z=0;c.slope=0;c.joint=1;
    c.extraStart=0;c.extraEnd=0;c.extraBeam=0;c.accepted=false;
    if(state.passedSurface==0||type<0||type>=8||place<0||place>=9||helperRotation<0||helperRotation>4)return c;
    int direction=helperRotation==4?0:helperRotation;
    c.graphic=worldMetalGraphics[type*4+direction];
    c.original=helperRotation==4||alreadyRotatedPlacement?place:worldMetalPlaceRotation[place*4+direction];
    c.segment=c.original;
    int top=height;
    int segmentHeight=65535;
    if(top<state.heights[c.segment]){
        segmentHeight=top&65535;top-=worldMetalDeductions[c.graphic];
        if(top<0)return c;
        int chosen=-1;
        for(int attempt=0;attempt<4;attempt++){
            int packed=c.original==4?worldMetalCentreReposition[attempt*4+(cameraRotation&3)]:worldMetalReposition[attempt*9+c.original];
            if(top>state.heights[packed/8]){chosen=packed;break;}
        }
        if(chosen<0||(typeB&&(chosen&7)>=4))return c;
        c.cross=chosen&7;c.segment=chosen/8;
    }
    c.crossHeight=top;c.top=worldMetalSigned16(top);c.z=state.heights[c.segment];c.slope=state.slopes[c.segment];
    c.extraStart=extra<0?height-1:height;
    c.extraEnd=c.extraStart+(extra<0?-extra:extra);
    c.extraBeam=extra<0?worldMetalCaps[c.graphic]:worldMetalBeams[c.graphic];
    c.phase=c.cross>=0?0:1;c.accepted=true;
    state.heights[c.segment]=segmentHeight;state.slopes[c.segment]=32;
    return c;
}
SUPPORT_FN WorldMetalPart worldMetalNext(SUPPORT_INOUT(WorldMetalCursor,c))
{
    if(c.phase<0)return worldMetalPart(-1,0,0,0,0,0,0,0,0,0);
    int x=worldMetalX[c.segment],y=worldMetalY[c.segment];
    if(c.phase==0){
        c.phase=1;
        int bx=worldMetalX[c.original]+worldMetalCrossX[c.cross],by=worldMetalY[c.original]+worldMetalCrossY[c.cross];
        return worldMetalPart(worldMetalCrossImages[c.graphic*8+c.cross],bx,by,c.crossHeight,bx,by,c.crossHeight,
            worldMetalCrossSizeX[c.cross],worldMetalCrossSizeY[c.cross],1);
    }
    if(c.phase==1){
        c.phase=2;
        if((c.slope&32)==0&&c.crossHeight-c.z>=6&&worldMetalBases[c.graphic]>=0){
            int z=c.z;c.z+=6;
            return worldMetalPart(worldMetalBases[c.graphic]+worldMetalSlopeImages[c.slope&31],x,y,z,x,y,z,0,0,5);
        }
    }
    if(c.phase==2){
        c.phase=3;
        int length=worldMetalSigned16(worldMetalMin(worldMetalSigned16((c.z+16)&~15),c.top)-c.z);
        int z=c.z;c.z+=length;
        if(length>0)return worldMetalPart(worldMetalBeams[c.graphic]+length-1,x,y,z,x,y,z,0,0,length-1);
    }
    if(c.phase==3){
        int length=worldMetalSigned16(worldMetalMin(worldMetalSigned16(c.z+16),c.top)-c.z);
        if(length>0){
            int z=c.z;c.z+=length;
            int image=worldMetalBeams[c.graphic]+length-1;
            if((c.joint&3)==0&&length==16)image++;
            c.joint=(c.joint+1)&255;
            return worldMetalPart(image,x,y,z,x,y,z,0,0,length-1);
        }
        c.phase=4;c.z=c.extraStart;
    }
    int length=worldMetalSigned16(worldMetalMin(c.z+16,c.extraEnd)-c.z);
    if(length>0){
        int z=c.z;c.z+=length;x=worldMetalX[c.original];y=worldMetalY[c.original];
        return worldMetalPart(c.extraBeam+length-1,x,y,z,x,y,c.extraStart,0,0,0);
    }
    c.phase=-1;
    return worldMetalPart(-1,0,0,0,0,0,0,0,0,0);
}
#endif
