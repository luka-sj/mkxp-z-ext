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
#include "sharedstate.h"
#include "filesystem/filesystem.h"
#include "util/exception.h"
#include "util/util.h"
#include "display/gl/gl-fun.h"
#include <string>
#include <cstring>

// Simple passthrough vertex shader (for viewports)
static const char *simpleVert =
	"attribute vec2 position;\n"
	"attribute vec2 texCoord;\n"
	"varying vec2 v_texCoord;\n"
	"uniform mat4 projMat;\n"
	"uniform vec2 texSizeInv;\n"
	"uniform vec2 translation;\n"
	"void main() {\n"
	"    gl_Position = projMat * vec4(position + translation, 0.0, 1.0);\n"
	"    v_texCoord = texCoord * texSizeInv;\n"
	"}\n";

// Sprite vertex shader with transformation matrix
static const char *spriteVert =
	"attribute vec2 position;\n"
	"attribute vec2 texCoord;\n"
	"varying vec2 v_texCoord;\n"
	"uniform mat4 projMat;\n"
	"uniform mat4 spriteMat;\n"
	"uniform vec2 texSizeInv;\n"
	"void main() {\n"
	"    gl_Position = projMat * spriteMat * vec4(position, 0.0, 1.0);\n"
	"    v_texCoord = texCoord * texSizeInv;\n"
	"}\n";

CustomShaderImpl::CustomShaderImpl(const char *fragContents, int fragSize,
                                   const char *fragName)
{
	Shader::init(
		(const unsigned char*)simpleVert, strlen(simpleVert),
		(const unsigned char*)fragContents, fragSize,
		"CustomShaderVert", fragName, "CustomShader");

	ShaderBase::init();

	u_time = gl.GetUniformLocation(program, "time");
}

void CustomShaderImpl::setTime(float value)
{
	if (u_time >= 0)
		gl.Uniform1f(u_time, value);
}

void CustomShaderImpl::applyUniforms(const UniformMap &uniforms)
{
	for (UniformMap::const_iterator it = uniforms.begin(); it != uniforms.end(); ++it)
	{
		GLint loc = gl.GetUniformLocation(program, it->first.c_str());
		if (loc < 0)
			continue;

		const UniformValue &val = it->second;
		switch (val.type)
		{
		case UNIFORM_FLOAT:
			gl.Uniform1f(loc, val.data.f);
			break;
		case UNIFORM_INT:
			gl.Uniform1i(loc, val.data.i);
			break;
		case UNIFORM_VEC2:
			gl.Uniform2f(loc, val.data.vec2[0], val.data.vec2[1]);
			break;
		case UNIFORM_VEC4:
			gl.Uniform4f(loc, val.data.vec4[0], val.data.vec4[1], val.data.vec4[2], val.data.vec4[3]);
			break;
		}
	}
}

CustomSpriteShaderImpl::CustomSpriteShaderImpl(const char *fragContents, int fragSize,
                                               const char *fragName)
{
	Shader::init(
		(const unsigned char*)spriteVert, strlen(spriteVert),
		(const unsigned char*)fragContents, fragSize,
		"CustomSpriteShaderVert", fragName, "CustomSpriteShader");

	ShaderBase::init();

	u_spriteMat = gl.GetUniformLocation(program, "spriteMat");
	u_time = gl.GetUniformLocation(program, "time");
	u_opacity = gl.GetUniformLocation(program, "opacity");
}

void CustomSpriteShaderImpl::setSpriteMat(const float value[16])
{
	gl.UniformMatrix4fv(u_spriteMat, 1, GL_FALSE, value);
}

void CustomSpriteShaderImpl::setTime(float value)
{
	if (u_time >= 0)
		gl.Uniform1f(u_time, value);
}

void CustomSpriteShaderImpl::setOpacity(float value)
{
	if (u_opacity >= 0)
		gl.Uniform1f(u_opacity, value);
}

void CustomSpriteShaderImpl::applyUniforms(const UniformMap &uniforms)
{
	for (UniformMap::const_iterator it = uniforms.begin(); it != uniforms.end(); ++it)
	{
		GLint loc = gl.GetUniformLocation(program, it->first.c_str());
		if (loc < 0)
			continue;

		const UniformValue &val = it->second;
		switch (val.type)
		{
		case UNIFORM_FLOAT:
			gl.Uniform1f(loc, val.data.f);
			break;
		case UNIFORM_INT:
			gl.Uniform1i(loc, val.data.i);
			break;
		case UNIFORM_VEC2:
			gl.Uniform2f(loc, val.data.vec2[0], val.data.vec2[1]);
			break;
		case UNIFORM_VEC4:
			gl.Uniform4f(loc, val.data.vec4[0], val.data.vec4[1], val.data.vec4[2], val.data.vec4[3]);
			break;
		}
	}
}

struct CustomShaderPrivate
{
	std::string filename;
	CustomShaderImpl *shader;
	CustomSpriteShaderImpl *spriteShader;
	UniformMap uniforms;

	CustomShaderPrivate(const char *filename)
	    : filename(filename),
	      shader(0),
	      spriteShader(0)
	{
	}

	~CustomShaderPrivate()
	{
		delete shader;
		delete spriteShader;
	}
};

CustomShader::CustomShader(const char *filename)
{
	p = new CustomShaderPrivate(filename);

	if (!shState->fileSystem().exists(filename))
	{
		delete p;
		throw Exception(Exception::RGSSError,
		                "Shader file '%s' not found", filename);
	}

	// Read the fragment shader file
	std::string fragContents;
	if (!readFile(filename, fragContents))
	{
		delete p;
		throw Exception(Exception::RGSSError,
		                "Failed to read shader file '%s'", filename);
	}

	try
	{
		p->shader = new CustomShaderImpl(
			fragContents.c_str(), fragContents.size(), filename);
		p->spriteShader = new CustomSpriteShaderImpl(
			fragContents.c_str(), fragContents.size(), filename);
	}
	catch (const Exception &e)
	{
		delete p;
		throw;
	}
}

CustomShader::~CustomShader()
{
	dispose();
}

const std::string &CustomShader::getFilename() const
{
	guardDisposed();
	return p->filename;
}

CustomShaderImpl *CustomShader::getShader() const
{
	guardDisposed();
	return p->shader;
}

CustomSpriteShaderImpl *CustomShader::getSpriteShader() const
{
	guardDisposed();
	return p->spriteShader;
}

void CustomShader::setFloat(const char *name, float value)
{
	guardDisposed();
	UniformValue val;
	val.type = UNIFORM_FLOAT;
	val.data.f = value;
	p->uniforms[name] = val;
}

void CustomShader::setInt(const char *name, int value)
{
	guardDisposed();
	UniformValue val;
	val.type = UNIFORM_INT;
	val.data.i = value;
	p->uniforms[name] = val;
}

void CustomShader::setVec2(const char *name, float x, float y)
{
	guardDisposed();
	UniformValue val;
	val.type = UNIFORM_VEC2;
	val.data.vec2[0] = x;
	val.data.vec2[1] = y;
	p->uniforms[name] = val;
}

void CustomShader::setVec4(const char *name, float x, float y, float z, float w)
{
	guardDisposed();
	UniformValue val;
	val.type = UNIFORM_VEC4;
	val.data.vec4[0] = x;
	val.data.vec4[1] = y;
	val.data.vec4[2] = z;
	val.data.vec4[3] = w;
	p->uniforms[name] = val;
}

const UniformMap &CustomShader::getUniforms() const
{
	guardDisposed();
	return p->uniforms;
}

void CustomShader::releaseResources()
{
	delete p;
}
