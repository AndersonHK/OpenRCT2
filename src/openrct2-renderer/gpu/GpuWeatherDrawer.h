/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "GpuCommandStream.h"

#include <openrct2/drawing/IDrawingEngine.h>

namespace OpenRCT2::Ui::Gpu
{
    class WeatherDrawer final : public Drawing::IWeatherDrawer
    {
        CommandBatch<WeatherCommand>* _commands = nullptr;

    public:
        void SetCommands(CommandBatch<WeatherCommand>& commands)
        {
            _commands = &commands;
        }

        void Draw(
            Drawing::RenderTarget&, int32_t x, int32_t y, int32_t width, int32_t height, int32_t xStart, int32_t yStart,
            const uint8_t* weatherPattern) override
        {
            if (_commands == nullptr || width <= 0 || height <= 0)
                return;
            auto& command = _commands->allocate();
            command.bounds = { x, y, x + width, y + height };
            command.offset = { xStart, yStart };
            command.pattern = weatherPattern[3] == 32 ? 1 : 0;
        }
    };
} // namespace OpenRCT2::Ui::Gpu
