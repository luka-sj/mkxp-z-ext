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

#include "util/disposable.h"
#include "display/gl/shader.h"
#include <string>

// Internal shader class that can access protected Shader::init (for viewports)
class CustomShaderImpl : public ShaderBase
{
public:
	CustomShaderImpl(const char *fragContents, int fragSize,
	                 const char *fragName);

	void setTime(float value);

private:
	GLint u_time;
};

// Sprite-specific shader class with transformation matrix support
class CustomSpriteShaderImpl : public ShaderBase
{
public:
	CustomSpriteShaderImpl(const char *fragContents, int fragSize,
	                       const char *fragName);

	void setSpriteMat(const float value[16]);
	void setTime(float value);
	void setOpacity(float value);

private:
	GLint u_spriteMat;
	GLint u_time;
	GLint u_opacity;
};

struct CustomShaderPrivate;

class CustomShader : public Disposable
{
public:
	CustomShader(const char *filename);
	~CustomShader();

	const std::string &getFilename() const;
	CustomShaderImpl *getShader() const;
	CustomSpriteShaderImpl *getSpriteShader() const;

private:
	void releaseResources();
	const char *klassName() const { return "shader"; }

	CustomShaderPrivate *p;
};

#endif // CUSTOMSHADER_H
