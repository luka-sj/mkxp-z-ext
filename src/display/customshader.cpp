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

    // Initialize common uniforms that viewport system expects
    initBaseUniforms();
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

    // Compile vertex shader
    const GLchar* vertSrc = vertexSource;
    gl.ShaderSource(vertShader, 1, &vertSrc, NULL);
    gl.CompileShader(vertShader);

    gl.GetShaderiv(vertShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint logLength;
        gl.GetShaderiv(vertShader, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(logLength, '\0');
        gl.GetShaderInfoLog(vertShader, log.size(), 0, &log[0]);
        throw Exception(Exception::MKXPError, "Vertex shader compilation failed: %s", log.c_str());
    }

    // Compile fragment shader
    const GLchar* fragSrc = fragmentSource.c_str();
    gl.ShaderSource(fragShader, 1, &fragSrc, NULL);
    gl.CompileShader(fragShader);

    gl.GetShaderiv(fragShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint logLength;
        gl.GetShaderiv(fragShader, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(logLength, '\0');
        gl.GetShaderInfoLog(fragShader, log.size(), 0, &log[0]);
        throw Exception(Exception::MKXPError, "Fragment shader compilation failed: %s", log.c_str());
    }

    // Link program
    gl.AttachShader(program, vertShader);
    gl.AttachShader(program, fragShader);

    // Bind standard attribute locations
    gl.BindAttribLocation(program, Position, "position");
    gl.BindAttribLocation(program, TexCoord, "texCoord");
    gl.BindAttribLocation(program, Color, "color");

    gl.LinkProgram(program);

    gl.GetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        GLint logLength;
        gl.GetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(logLength, '\0');
        gl.GetProgramInfoLog(program, log.size(), 0, &log[0]);
        throw Exception(Exception::MKXPError, "Shader program linking failed: %s", log.c_str());
    }
}

void CustomShader::initBaseUniforms()
{
    // Cache uniform locations that the viewport system might need
    u_projMat = gl.GetUniformLocation(program, "projMat");
    u_texSizeInv = gl.GetUniformLocation(program, "texSizeInv");
    u_translation = gl.GetUniformLocation(program, "translation");

    // Custom shader uniforms
    u_resolution = gl.GetUniformLocation(program, "resolution");
    u_time = gl.GetUniformLocation(program, "time");
    u_texture = gl.GetUniformLocation(program, "texture");
}

void CustomShader::bind()
{
    if (disposed) return;
    Shader::bind();

    // Set the main texture to unit 0
    if (u_texture != -1) {
        gl.Uniform1i(u_texture, 0);
    }
}

void CustomShader::unbind()
{
    if (disposed) return;
    Shader::unbind();
}

void CustomShader::setTexSize(const Vec2i &value)
{
    if (disposed || u_texSizeInv == -1) return;
    gl.Uniform2f(u_texSizeInv, 1.f / value.x, 1.f / value.y);
}

void CustomShader::setTranslation(const Vec2i &value)
{
    if (disposed || u_translation == -1) return;
    gl.Uniform2f(u_translation, value.x, value.y);
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

void CustomShader::applyViewportProj()
{
    if (disposed || u_projMat == -1) return;

    const IntRect &vp = glState.viewport.get();

    // Create orthographic projection matrix (similar to ShaderBase::GLProjMat::apply)
    const float a = 2.f / vp.w;
    const float b = 2.f / vp.h;
    const float c = -2.f;

    GLfloat mat[16] = {
         a,  0,  0,  0,
         0,  b,  0,  0,
         0,  0,  c,  0,
        -1, -1, -1,  1
    };

    gl.UniformMatrix4fv(u_projMat, 1, GL_FALSE, mat);
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
    if (disposed || u_resolution == -1) return;
    gl.Uniform2f(u_resolution, value.x, value.y);
}

void CustomShader::setTime(float value)
{
    if (disposed || u_time == -1) return;
    gl.Uniform1f(u_time, value);
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
