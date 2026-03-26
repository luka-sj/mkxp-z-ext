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
#include <map>
#include <vector>

class Bitmap;

// Uniform value types
enum UniformType {
	UNIFORM_FLOAT,
	UNIFORM_INT,
	UNIFORM_VEC2,
	UNIFORM_VEC3,
	UNIFORM_VEC4
};

struct UniformValue {
	UniformType type;
	union {
		float f;
		int i;
		float vec2[2];
		float vec3[3];
		float vec4[4];
	} data;
};

typedef std::map<std::string, UniformValue> UniformMap;
typedef std::map<std::string, Bitmap*> BitmapMap;

// Internal shader class that can access protected Shader::init (for viewports)
class CustomShaderImpl : public ShaderBase
{
public:
	CustomShaderImpl(const char *fragContents, int fragSize,
	                 const char *fragName);
	virtual ~CustomShaderImpl() {}

	void setTime(float value);
	void applyUniforms(const UniformMap &uniforms);
	void applyBitmaps(const BitmapMap &bitmaps, int startUnit = 1);

private:
	GLint u_time;
};

// Sprite-specific shader class with transformation matrix support
class CustomSpriteShaderImpl : public ShaderBase
{
public:
	CustomSpriteShaderImpl(const char *fragContents, int fragSize,
	                       const char *fragName);
	virtual ~CustomSpriteShaderImpl() {}

	void setSpriteMat(const float value[16]);
	void setTime(float value);
	void setOpacity(float value);
	void setTone(const Vec4 &value);
	void setColor(const Vec4 &value);
	void setInvert(bool value);
	void setBushDepth(float value);
	void setBushOpacity(float value);
	void applyUniforms(const UniformMap &uniforms);
	void applyBitmaps(const BitmapMap &bitmaps, int startUnit = 1);

	/* True if the user's shader declares the standard uniform name,
	 * meaning the user handles the effect and the suffix should be
	 * neutralized to prevent double-application. */
	bool hasUserOpacity() const { return u_stdOpacity >= 0; }
	bool hasUserTone()    const { return u_stdTone >= 0; }
	bool hasUserColor()   const { return u_stdColor >= 0; }

	void setStdOpacity(float value);
	void setStdTone(const Vec4 &value);
	void setStdColor(const Vec4 &value);

private:
	GLint u_spriteMat;
	GLint u_time;
	GLint u_opacity;
	GLint u_tone;
	GLint u_color;
	GLint u_invert;
	GLint u_bushDepth;
	GLint u_bushOpacity;

	/* Locations of standard uniform names in the user's shader */
	GLint u_stdOpacity;
	GLint u_stdTone;
	GLint u_stdColor;
};

struct CustomShaderPrivate;

class CustomShader : public Disposable
{
public:
	CustomShader(const char *filename);
	~CustomShader();

	const std::string &getFilename() const;
	const std::string &getSource() const;
	CustomShaderImpl *getShader() const;
	CustomSpriteShaderImpl *getSpriteShader() const;

	// Parameter setters
	void setFloat(const char *name, float value);
	void setInt(const char *name, int value);
	void setVec2(const char *name, float x, float y);
	void setVec3(const char *name, float x, float y, float z);
	void setVec4(const char *name, float x, float y, float z, float w);
	void setBitmap(const char *name, Bitmap *bitmap);

	// Get the uniform map for applying to shaders
	const UniformMap &getUniforms() const;
	const BitmapMap &getBitmaps() const;

private:
	void releaseResources();
	const char *klassName() const { return "shader"; }

	CustomShaderPrivate *p;
};

#endif // CUSTOMSHADER_H
