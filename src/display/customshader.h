/*
** customshader.h
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

#ifndef CUSTOMSHADER_H
#define CUSTOMSHADER_H

#include "gl/shader.h"
#include "disposable.h"
#include "etc-internal.h"
#include <string>

class CustomShader : public Shader, public Disposable
{
public:
    CustomShader(const char *fragmentPath);
    ~CustomShader();

    // Override Shader methods to make them public
    void bind();
    void unbind();

    // Uniform setters
    void setUniformF(const char *name, float value);
    void setUniformI(const char *name, int value);
    void setUniformVec2(const char *name, const Vec2 &value);
    void setUniformVec4(const char *name, const Vec4 &value);
    void setUniformMatrix(const char *name, const float *matrix);

    // Built-in uniforms
    void setResolution(const Vec2 &value);
    void setTime(float value);

    GLuint getProgram() const;

    // Disposable interface
    void dispose();
    bool isDisposed() const;

private:
    bool disposed;
    std::string fragmentSource;
    GLint u_projMat;

    void releaseResources();
    const char *klassName() const { return "shader"; }

    void loadAndCompileShader(const char *fragmentPath);
    void compileShadersDirect(const std::string& fragmentSource); // Add this line
};

#endif // CUSTOMSHADER_H
