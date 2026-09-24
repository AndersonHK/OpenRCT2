// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// TrackPaintUtilOnridePhoto{Small,}Paint. Pure rules also compiled by CPU tests.
#ifndef OPENRCT2_WORLD_TRACK_PHOTO
#define OPENRCT2_WORLD_TRACK_PHOTO
uint worldPhotoImage(uint direction,bool small,uint timeout,uint part)
{
    uint base=small?23485u:25615u;
    return part<2u?base+8u+direction:base+((direction+2u)&3u)+(timeout!=0u?4u:0u);
}
int worldPhotoX(uint direction,uint part)
{
    if(direction==0u) return part<2u?26:6;
    if(direction==1u) return part==1u?28:0;
    if(direction==2u) return part<2u?6:26;
    return part==0u?0:28;
}
int worldPhotoY(uint direction,uint part)
{
    if(direction==0u) return part==1u?28:0;
    if(direction==1u) return part<2u?6:26;
    if(direction==2u) return part==0u?0:28;
    return part<2u?26:6;
}
int worldPhotoZ(uint direction,uint part)
{
    return part==1u || (part==2u && direction>=2u)?-3:0;
}
#endif
