// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
// Original Paint.Entrance.cpp support request shared by ride and park entrances.
#ifndef OPENRCT2_WORLD_ENTRANCE_SUPPORT_EMIT
#define OPENRCT2_WORLD_ENTRANCE_SUPPORT_EMIT
void worldEmitEntranceSupports(WorldObjectRecord object,uvec2 tile,uint destination,bool writeRecords,inout uint count)
{
    bool hide=(uScene.viewFlags&(1u<<3))!=0u;
    if(hide && (uScene.viewFlags&(1u<<29))!=0u) return;
    bool ghost=(object.flags&1u)!=0u;
    uint palettes=ghost?uCatalog.reserved:24u; // Saturated brown23, encoded row24.
    uint effects=1u;
    if(hide) {palettes=uCatalog.viewPalettes.x;effects=1u<<10;}
    WorldWoodenCursor cursor=worldEntranceWoodenBegin(worldSupportState,
        int((object.direction+uScene.rotation)&3u),object.baseZ);
    [[dont_unroll]]
    while(cursor.phase>=0) {
        WorldWoodenPart part=worldWoodenNext(cursor);
        if(part.imageOffset<0) continue;
        uint sprite=worldTrackImage(uint(part.imageOffset));
        if(sprite==0xffffffffu) {worldReportComponentFailure(512u,tile,writeRecords);continue;}
        // No transition is requested: every column is its own original parent.
        // In particular it must never attach to the preceding scrolling text.
        worldSetPaintBounds(tile,ivec3(part.boundsX,part.boundsY,part.boundsZ),
            ivec3(part.sizeX,part.sizeY,part.sizeZ),0u);
        worldSetCoplanarSurfaceLayer();
        emitObjectSprite(tile,part.z,ivec2(part.x,part.y),sprite,palettes,effects,destination,writeRecords,count);
    }
}
#endif
