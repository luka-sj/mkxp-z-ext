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
    std::string fragContents;
    try {
        readFile(fragmentPath, fragContents);
        fragmentSource = fragContents;
    } catch (const Exception &e) {
        throw Exception(Exception::MKXPError, "Failed to load fragment shader: %s", fragmentPath);
    }

    // Don't use the parent's init method - compile directly
    compileShadersDirect(fragContents);
}

void CustomShader::compileShadersDirect(const std::string& fragmentSource)
{
    GFX_LOCK;

    const char* vertexSource =
        "#version 120\n"
        "attribute vec2 position;\n"
        "attribute vec2 texCoord;\n"
        "varying vec2 v_texCoord;\n"
        "uniform mat4 projMat;\n"
        "void main() {\n"
        "    gl_Position = projMat * vec4(position, 0.0, 1.0);\n"
        "    v_texCoord = texCoord;\n"
        "}\n";

    GLint success;

    // Compile vertex shader (bypass setupShaderSource)
    const GLchar* vertSrc = vertexSource;
    gl.ShaderSource(vertShader, 1, &vertSrc, NULL);
    gl.CompileShader(vertShader);

    gl.GetShaderiv(vertShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        throw Exception(Exception::MKXPError, "Vertex shader compilation failed");
    }

    // Compile fragment shader (bypass setupShaderSource)
    const GLchar* fragSrc = fragmentSource.c_str();
    gl.ShaderSource(fragShader, 1, &fragSrc, NULL);
    gl.CompileShader(fragShader);

    gl.GetShaderiv(fragShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        throw Exception(Exception::MKXPError, "Fragment shader compilation failed");
    }

    // Link program
    gl.AttachShader(program, vertShader);
    gl.AttachShader(program, fragShader);

    // Set up standard attributes
    gl.BindAttribLocation(program, 0, "position"); // Assuming Position = 0
    gl.BindAttribLocation(program, 1, "texCoord"); // Assuming TexCoord = 1

    gl.LinkProgram(program);

    gl.GetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        throw Exception(Exception::MKXPError, "Shader program linking failed");
    }

    // Cache projection matrix uniform
    u_projMat = gl.GetUniformLocation(program, "projMat");
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
        gl.DeleteProgram(program);
    }

    if (vertShader) {
        gl.DeleteShader(vertShader);
    }

    if (fragShader) {
        gl.DeleteShader(fragShader);
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
