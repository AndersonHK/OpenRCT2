/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "DrawCommands.h"
#include "OpenGLShaderProgram.h"

namespace OpenRCT2::Ui
{
    class WeatherShader final : public OpenGLShaderProgram
    {
    private:
        GLint uScreenSize{ -1 };

        GLint vPosition{ -1 };
        GLint vBounds{ -1 };
        GLint vOffset{ -1 };
        GLint vPattern{ -1 };

        GLuint _vbo{ 0 };
        GLuint _vboInstances{ 0 };
        GLuint _vao{ 0 };
        size_t _instanceCapacity{ 0 };

    public:
        WeatherShader();
        ~WeatherShader() override;

        void SetScreenSize(int32_t width, int32_t height);
        void DrawInstances(const WeatherCommandBatch& instances);

    private:
        void GetLocations();
    };
} // namespace OpenRCT2::Ui
