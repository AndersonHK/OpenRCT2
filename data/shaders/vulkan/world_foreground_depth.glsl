// Copyright (c) 2014-2026 OpenRCT2 developers. GPL-3.0-or-later.
#ifndef OPENRCT2_WORLD_FOREGROUND_DEPTH
#define OPENRCT2_WORLD_FOREGROUND_DEPTH
#ifdef __cplusplus
#define FOREGROUND_FN constexpr
#else
#define FOREGROUND_FN
#endif
// Explicit foreground components span an authored edge, but their image can
// start at its far end (or at the tile origin). Anchor the complete sprite to
// the camera-nearest contact endpoint, never to a texel or the top of its art.
// Callers opt in by semantic role; generic bounds never imply foreground.
FOREGROUND_FN int worldForegroundContact(int origin,int extent)
{
    return origin+(extent>0?extent-1:0);
}
// Members of one flat tile edge share its near boundary at (32,32,baseZ).
// Their art height is not a displacement towards the camera. Small component
// roles order the rail, its fixtures and the enclosing shell at that contact.
const int WORLD_FOREGROUND_RAIL_LAYER=1;
const int WORLD_FOREGROUND_FIXTURE_LAYER=4;
const int WORLD_FOREGROUND_SHELL_LAYER=8;
const int WORLD_FOREGROUND_TILE_CORNER=32;
FOREGROUND_FN int worldForegroundTileContact(int baseZ) { return 2*WORLD_FOREGROUND_TILE_CORNER+baseZ; }
#undef FOREGROUND_FN
#endif
