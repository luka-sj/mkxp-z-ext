/*
** customshader.cpp
**
** This file is part of mkxp.
**
** Copyright (C) 2013 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "customshader.h"
#include "exception.h"
#include "glstate.h"
#include "util.h"
#include <cstring>
#include <SDL_timer.h>

CustomShader::CustomShader(const char *fragmentPath)
    : Shader(), disposed(false)
{
    loadAndCompileShader(fragmentPath);
}

CustomShader::~CustomShader()
{
    dispose();
}

void CustomShader::loadAndCompileShader(const char *fragmentPath)
{
    // Read fragment shader file
    std::string fragContents;
    try {
        readFile(fragmentPath, fragContents);
        fragmentSource = fragContents;
    } catch (const Exception &e) {
        throw Exception(Exception::MKXPError, "Failed to load fragment shader: %s", fragmentPath);
    }

    // Minimal vertex shader for custom shaders
    const char* minimalVert =
        "#version 120\n"
        "attribute vec2 position;\n"
        "attribute vec2 texCoord;\n"
        "varying vec2 v_texCoord;\n"
        "uniform mat4 projMat;\n"
        "void main() {\n"
        "    gl_Position = projMat * vec4(position, 0.0, 1.0);\n"
        "    v_texCoord = texCoord;\n"
        "}\n";

    // Initialize the inherited Shader
    init((const unsigned char*)minimalVert, strlen(minimalVert),
         (const unsigned char*)fragContents.c_str(), fragContents.size(),
         "minimal_custom", fragmentPath, "CustomShader");
}

void CustomShader::bind()
{
    if (disposed) return;
    Shader::bind();
}

void CustomShader::unbind()
{
    if (disposed) return;
    Shader::unbind();
}

void CustomShader::releaseResources() {
    GFX_LOCK;

    if (program) {
        glDeleteProgram(program);
        program = 0;
    }

    if (vertShader) {
        glDeleteShader(vertShader);
        vertShader = 0;
    }

    if (fragShader) {
        glDeleteShader(fragShader);
        fragShader = 0;
    }
}

void CustomShader::setUniformF(const char *name, float value)
{
    if (disposed) return;
    GLint loc = gl.GetUniformLocation(program, name);
    if (loc != -1) {
        gl.Uniform1f(loc, value);
    }
}

void CustomShader::setUniformI(const char *name, int value)
{
    if (disposed) return;
    GLint loc = gl.GetUniformLocation(program, name);
    if (loc != -1) {
        gl.Uniform1i(loc, value);
    }
}

void CustomShader::setUniformVec2(const char *name, const Vec2 &value)
{
    if (disposed) return;
    GLint loc = gl.GetUniformLocation(program, name);
    if (loc != -1) {
        gl.Uniform2f(loc, value.x, value.y);
    }
}

void CustomShader::setUniformVec4(const char *name, const Vec4 &value)
{
    if (disposed) return;
    GLint loc = gl.GetUniformLocation(program, name);
    if (loc != -1) {
        gl.Uniform4f(loc, value.x, value.y, value.z, value.w);
    }
}

void CustomShader::setUniformMatrix(const char *name, const float *matrix)
{
    if (disposed) return;
    GLint loc = gl.GetUniformLocation(program, name);
    if (loc != -1) {
        gl.UniformMatrix4fv(loc, 1, GL_FALSE, matrix);
    }
}

void CustomShader::setResolution(const Vec2 &value)
{
    setUniformVec2("resolution", value);
}

void CustomShader::setTime(float value)
{
    setUniformF("time", value);
}

GLuint CustomShader::getProgram() const
{
    return program;
}

void CustomShader::dispose()
{
    if (disposed) return;
    disposed = true;
}

bool CustomShader::isDisposed() const
{
    return disposed;
}
