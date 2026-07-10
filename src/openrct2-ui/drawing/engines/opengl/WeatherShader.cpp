/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifndef DISABLE_OPENGL

    #include "WeatherShader.h"

using namespace OpenRCT2::Ui;

namespace
{
    constexpr size_t kInitialInstanceCapacity = 64;

    struct VertexData
    {
        GLfloat position[2];
    };

    constexpr VertexData kVertexData[4] = {
        { 0.0f, 0.0f },
        { 1.0f, 0.0f },
        { 0.0f, 1.0f },
        { 1.0f, 1.0f },
    };
} // namespace

WeatherShader::WeatherShader()
    : OpenGLShaderProgram("weather")
    , _instanceCapacity(kInitialInstanceCapacity)
{
    GetLocations();

    glCall(glGenBuffers, 1, &_vbo);
    glCall(glGenBuffers, 1, &_vboInstances);
    glCall(glGenVertexArrays, 1, &_vao);

    glCall(glBindVertexArray, _vao);

    glCall(glBindBuffer, GL_ARRAY_BUFFER, _vbo);
    glCall(glBufferData, GL_ARRAY_BUFFER, sizeof(kVertexData), kVertexData, GL_STATIC_DRAW);
    glCall(
        glVertexAttribPointer, vPosition, 2, GL_FLOAT, GL_FALSE, glSizeOf<VertexData>(),
        reinterpret_cast<void*>(offsetof(VertexData, position)));

    glCall(glBindBuffer, GL_ARRAY_BUFFER, _vboInstances);
    glCall(glBufferData, GL_ARRAY_BUFFER, sizeof(DrawWeatherCommand) * _instanceCapacity, nullptr, GL_STREAM_DRAW);
    glCall(
        glVertexAttribIPointer, vBounds, 4, GL_INT, glSizeOf<DrawWeatherCommand>(),
        reinterpret_cast<void*>(offsetof(DrawWeatherCommand, bounds)));
    glCall(
        glVertexAttribIPointer, vOffset, 2, GL_INT, glSizeOf<DrawWeatherCommand>(),
        reinterpret_cast<void*>(offsetof(DrawWeatherCommand, offset)));
    glCall(
        glVertexAttribIPointer, vPattern, 1, GL_INT, glSizeOf<DrawWeatherCommand>(),
        reinterpret_cast<void*>(offsetof(DrawWeatherCommand, pattern)));

    glCall(glEnableVertexAttribArray, vPosition);
    glCall(glEnableVertexAttribArray, vBounds);
    glCall(glEnableVertexAttribArray, vOffset);
    glCall(glEnableVertexAttribArray, vPattern);

    glCall(glVertexAttribDivisor, vBounds, 1);
    glCall(glVertexAttribDivisor, vOffset, 1);
    glCall(glVertexAttribDivisor, vPattern, 1);
}

WeatherShader::~WeatherShader()
{
    glCall(glDeleteBuffers, 1, &_vbo);
    glCall(glDeleteBuffers, 1, &_vboInstances);
    glCall(glDeleteVertexArrays, 1, &_vao);
}

void WeatherShader::GetLocations()
{
    uScreenSize = GetUniformLocation("uScreenSize");

    vPosition = GetAttributeLocation("vPosition");
    vBounds = GetAttributeLocation("vBounds");
    vOffset = GetAttributeLocation("vOffset");
    vPattern = GetAttributeLocation("vPattern");
}

void WeatherShader::SetScreenSize(int32_t width, int32_t height)
{
    Use();
    glCall(glUniform2i, uScreenSize, width, height);
}

void WeatherShader::DrawInstances(const WeatherCommandBatch& instances)
{
    if (instances.empty())
    {
        return;
    }

    Use();
    glCall(glBindVertexArray, _vao);
    glCall(glBindBuffer, GL_ARRAY_BUFFER, _vboInstances);

    while (instances.size() > _instanceCapacity)
    {
        _instanceCapacity *= 2;
    }

    // Orphan the previous frame's storage so the CPU never waits for the GPU
    // to finish reading these short-lived commands.
    glCall(glBufferData, GL_ARRAY_BUFFER, sizeof(DrawWeatherCommand) * _instanceCapacity, nullptr, GL_STREAM_DRAW);
    glCall(glBufferSubData, GL_ARRAY_BUFFER, 0, sizeof(DrawWeatherCommand) * instances.size(), instances.data());
    glCall(glDrawArraysInstanced, GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(instances.size()));
}

#endif /* DISABLE_OPENGL */
