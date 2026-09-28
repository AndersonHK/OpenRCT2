/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

// Force-include only in HardwareDisplayDrawingEngine.cpp, with PCH disabled.
// Include SDL before renaming the call so its real declaration is unchanged.
#include <SDL_render.h>

extern "C" void SDLCALL OraclePresent(SDL_Renderer* renderer);
#define SDL_RenderPresent OraclePresent
