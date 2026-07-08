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
#include "debugwriter.h"
#include "sharedstate.h"
#include "filesystem/filesystem.h"
#include "util/exception.h"
#include "util/util.h"
#include "display/gl/gl-fun.h"
#include "display/gl/glstate.h"
#include "display/bitmap.h"
#include <string>
#include <cstring>
#include <unordered_map>
#include <utility>

/*
 * Program cache for custom shaders.
 */
namespace {
	using ProgramKey = std::pair<std::string, std::string>; // (vertSrc, fragSrc)

	struct ProgramKeyHash {
		size_t operator()(const ProgramKey &k) const {
			return std::hash<std::string>()(k.first) ^
			      (std::hash<std::string>()(k.second) << 1);
		}
	};

	typedef std::unordered_map<ProgramKey, GLuint, ProgramKeyHash> ProgramCache;

	static ProgramCache &programCache()
	{
		static ProgramCache c;
		return c;
	}

	/* Returns 0 (never a valid GL program name) on cache miss. */
	static GLuint findCachedProgram(const std::string &vertSrc,
	                                const std::string &fragSrc)
	{
		ProgramCache &c = programCache();
		ProgramCache::iterator it = c.find(ProgramKey(vertSrc, fragSrc));
		return (it == c.end()) ? 0u : it->second;
	}

	static void insertCachedProgram(const std::string &vertSrc,
	                                const std::string &fragSrc,
	                                GLuint program)
	{
		programCache()[ProgramKey(vertSrc, fragSrc)] = program;
	}
}

#define ADOPT_CACHED_PROGRAM(cached_) do { \
	gl.DeleteProgram(program); \
	gl.DeleteShader(vertShader); \
	gl.DeleteShader(fragShader); \
	vertShader = 0; \
	fragShader = 0; \
	program = (cached_); \
	ownsProgram = false; \
} while (0)

// Helper function to get shader compilation log
static std::string getShaderLog(GLuint shader)
{
	GLint logLength = 0;
	gl.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);

	if (logLength <= 0)
		return "No error details available";

	std::string log(logLength, '\0');
	gl.GetShaderInfoLog(shader, log.size(), 0, &log[0]);

	// Trim trailing whitespace/nulls
	size_t end = log.find_last_not_of(" \t\n\r\0");
	if (end != std::string::npos)
		log = log.substr(0, end + 1);

	return log;
}

// Helper function to get program link log
static std::string getProgramLog(GLuint program)
{
	GLint logLength = 0;
	gl.GetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);

	if (logLength <= 0)
		return "No error details available";

	std::string log(logLength, '\0');
	gl.GetProgramInfoLog(program, log.size(), 0, &log[0]);

	// Trim trailing whitespace/nulls
	size_t end = log.find_last_not_of(" \t\n\r\0");
	if (end != std::string::npos)
		log = log.substr(0, end + 1);

	return log;
}

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

// Insert `inject` into GLSL `src` after any leading #version / #extension
// directives, which must remain the first statements in the source.
static void insertAfterDirectives(std::string &src, const std::string &inject)
{
	size_t insertPos = 0;
	size_t searchPos = 0;
	while (searchPos < src.size())
	{
		// Skip whitespace
		while (searchPos < src.size() && (src[searchPos] == ' ' || src[searchPos] == '\t' ||
		       src[searchPos] == '\n' || src[searchPos] == '\r'))
			searchPos++;

		if (searchPos >= src.size())
			break;

		// Check for preprocessor directives that must come first
		if (src[searchPos] == '#')
		{
			size_t dirStart = searchPos + 1;
			while (dirStart < src.size() && (src[dirStart] == ' ' || src[dirStart] == '\t'))
				dirStart++;

			bool isSpecialDir = false;
			if (src.compare(dirStart, 7, "version") == 0)
				isSpecialDir = true;
			else if (src.compare(dirStart, 9, "extension") == 0)
				isSpecialDir = true;

			if (isSpecialDir)
			{
				// Skip to end of line (include the newline)
				size_t eol = src.find('\n', searchPos);
				if (eol == std::string::npos)
					eol = src.size();
				else
					eol++;
				insertPos = eol;
				searchPos = eol;
				continue;
			}
		}

		// Not a #version or #extension line, stop here
		break;
	}

	src.insert(insertPos, inject);
}

CustomShaderImpl::CustomShaderImpl(const char *fragContents, int fragSize,
                                   const char *fragName,
                                   const char *vertContents, int vertSize,
                                   const char *vertName)
{
	// Build the final vertex source. A user vertex source (from the sibling
	// .vert file) replaces the built-in simpleVert when present, and gets
	// the same common.h precision-header injection as fragment sources.
	std::string vertSrc;
	if (vertContents)
	{
		vertSrc.assign(vertContents, vertSize);
		insertAfterDirectives(vertSrc, Shader::commonHeaderSource(false));
	}
	else
	{
		vertSrc = simpleVert;
	}

	// Build the final fragment source. Inject the GLES/desktop precision
	// header (common.h) so custom shaders compile on OpenGL ES (no default
	// float precision otherwise).
	std::string fragSrc(fragContents, fragSize);
	insertAfterDirectives(fragSrc, Shader::commonHeaderSource(true));

	// Cache lookup — a link is a pure function of (vertSrc, fragSrc) plus
	// the fixed attribute bindings we always apply.
	GLuint cached = findCachedProgram(vertSrc, fragSrc);
	if (cached != 0)
	{
		ADOPT_CACHED_PROGRAM(cached);
		ShaderBase::init();
		u_time = gl.GetUniformLocation(program, "time");
		return;
	}

	// Cache miss — compile + link on our own handles.
	const GLchar *vertSources[1] = { vertSrc.c_str() };
	GLint vertLengths[1] = { (GLint)vertSrc.size() };

	gl.ShaderSource(vertShader, 1, vertSources, vertLengths);
	gl.CompileShader(vertShader);

	GLint success = 0;
	gl.GetShaderiv(vertShader, GL_COMPILE_STATUS, &success);

	if (!success)
	{
		std::string log = getShaderLog(vertShader);
		if (vertContents)
			throw Exception(Exception::MKXPError,
			                "Vertex shader compilation failed for '%s':\n%s",
			                vertName, log.c_str());
		throw Exception(Exception::MKXPError,
		                "Internal vertex shader compilation failed for '%s':\n%s",
		                fragName, log.c_str());
	}

	const GLchar *fragSources[1] = { fragSrc.c_str() };
	GLint fragLengths[1] = { (GLint)fragSrc.size() };

	gl.ShaderSource(fragShader, 1, fragSources, fragLengths);
	gl.CompileShader(fragShader);

	gl.GetShaderiv(fragShader, GL_COMPILE_STATUS, &success);

	if (!success)
	{
		std::string log = getShaderLog(fragShader);
		throw Exception(Exception::MKXPError,
		                "Shader compilation failed for '%s':\n%s",
		                fragName, log.c_str());
	}

	// Link program with error handling
	gl.AttachShader(program, vertShader);
	gl.AttachShader(program, fragShader);

	gl.BindAttribLocation(program, Shader::Position, "position");
	gl.BindAttribLocation(program, Shader::TexCoord, "texCoord");
	gl.BindAttribLocation(program, Shader::Color, "color");

	gl.LinkProgram(program);

	gl.GetProgramiv(program, GL_LINK_STATUS, &success);

	if (!success)
	{
		std::string log = getProgramLog(program);
		throw Exception(Exception::MKXPError,
		                "Shader program linking failed for '%s':\n%s",
		                fragName, log.c_str());
	}

	insertCachedProgram(vertSrc, fragSrc, program);
	ownsProgram = false;

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
		case UNIFORM_VEC3:
			gl.Uniform3f(loc, val.data.vec3[0], val.data.vec3[1], val.data.vec3[2]);
			break;
		case UNIFORM_VEC4:
			gl.Uniform4f(loc, val.data.vec4[0], val.data.vec4[1], val.data.vec4[2], val.data.vec4[3]);
			break;
		case UNIFORM_MAT4:
			gl.UniformMatrix4fv(loc, 1, GL_FALSE, val.data.mat4);
			break;
		}
	}
}

void CustomShaderImpl::applyBitmaps(const BitmapMap &bitmaps, int startUnit)
{
	int unit = startUnit;
	for (BitmapMap::const_iterator it = bitmaps.begin(); it != bitmaps.end(); ++it)
	{
		Bitmap *bitmap = it->second;
		if (!bitmap || bitmap->isDisposed())
			continue;

		GLint loc = gl.GetUniformLocation(program, it->first.c_str());
		if (loc < 0)
			continue;

		// Also set the size inverse uniform if it exists (name + "SizeInv")
		std::string sizeInvName = it->first + "SizeInv";
		GLint sizeInvLoc = gl.GetUniformLocation(program, sizeInvName.c_str());

		TEX::ID tex = bitmap->getGLTypes().tex;
		setTexUniform(loc, unit, tex);

		if (sizeInvLoc >= 0)
		{
			gl.Uniform2f(sizeInvLoc, 1.0f / bitmap->width(), 1.0f / bitmap->height());
		}

		unit++;
	}
}

// Suffix appended after the user's fragment shader (with main renamed).
// Calls the user's original main, then applies built-in sprite effects.
static const char *spriteFragSuffix =
	"\nvoid main() {\n"
	"    _mkxp_user_main();\n"
	"    vec4 _mkxp_frag = gl_FragColor;\n"
	"    float _mkxp_luma = dot(_mkxp_frag.rgb, vec3(.299, .587, .114));\n"
	"    _mkxp_frag.rgb = mix(_mkxp_frag.rgb, vec3(_mkxp_luma), _mkxp_tone.w);\n"
	"    _mkxp_frag.rgb += _mkxp_tone.rgb;\n"
	"    _mkxp_frag.a *= _mkxp_opacity;\n"
	"    _mkxp_frag.rgb = mix(_mkxp_frag.rgb, _mkxp_color.rgb, _mkxp_color.a);\n"
	"    if (_mkxp_invert) {\n"
	"        _mkxp_frag.rgb = vec3(1.0) - _mkxp_frag.rgb;\n"
	"    }\n"
	"    lowp float _mkxp_underBush = float(v_texCoord.y < _mkxp_bushDepth);\n"
	"    _mkxp_frag.a *= clamp(_mkxp_bushOpacity + _mkxp_underBush, 0.0, 1.0);\n"
	"    gl_FragColor = _mkxp_frag;\n"
	"}\n";

// Build a wrapped fragment shader source that renames the user's main()
// to _mkxp_user_main() and appends tone/color post-processing.
// Returns empty string if wrapping fails (main not found).
static std::string buildWrappedFragSource(const char *fragContents, int fragSize)
{
	std::string src(fragContents, fragSize);

	// Find "void main" — search for "void" then whitespace then "main"
	// then optional whitespace then "("
	size_t pos = 0;
	size_t mainPos = std::string::npos;

	while (pos < src.size())
	{
		size_t voidPos = src.find("void", pos);
		if (voidPos == std::string::npos)
			break;

		// Check that "void" is at a word boundary (not part of another identifier)
		if (voidPos > 0 && (isalnum(src[voidPos - 1]) || src[voidPos - 1] == '_'))
		{
			pos = voidPos + 4;
			continue;
		}

		// Skip whitespace after "void"
		size_t p = voidPos + 4;
		while (p < src.size() && (src[p] == ' ' || src[p] == '\t' || src[p] == '\n' || src[p] == '\r'))
			p++;

		// Check for "main"
		if (p + 4 > src.size() || src.substr(p, 4) != "main")
		{
			pos = p;
			continue;
		}

		// Check word boundary after "main"
		size_t afterMain = p + 4;
		if (afterMain < src.size() && (isalnum(src[afterMain]) || src[afterMain] == '_'))
		{
			pos = afterMain;
			continue;
		}

		// Skip whitespace after "main"
		size_t q = afterMain;
		while (q < src.size() && (src[q] == ' ' || src[q] == '\t' || src[q] == '\n' || src[q] == '\r'))
			q++;

		// Check for "("
		if (q < src.size() && src[q] == '(')
		{
			mainPos = p; // position of "main" token
			break;
		}

		pos = q;
	}

	if (mainPos == std::string::npos)
		return std::string();

	// Replace "main" with "_mkxp_user_main" at the found position
	src.replace(mainPos, 4, "_mkxp_user_main");

	// Prepend the GLES/desktop precision header (common.h) the built-in
	// shaders receive — on GLES it declares the default float precision, on
	// desktop it #defines the precision qualifiers (lowp/mediump/highp) away
	// — followed by the built-in effect uniform declarations.
	std::string inject = Shader::commonHeaderSource(true);

	inject +=
		"uniform lowp vec4 _mkxp_tone;\n"
		"uniform lowp float _mkxp_opacity;\n"
		"uniform lowp vec4 _mkxp_color;\n"
		"uniform bool _mkxp_invert;\n"
		"uniform lowp float _mkxp_bushDepth;\n"
		"uniform lowp float _mkxp_bushOpacity;\n";

	// Insert after any #version / #extension directives, which must come first.
	insertAfterDirectives(src, inject);

	return src + spriteFragSuffix;
}

CustomSpriteShaderImpl::CustomSpriteShaderImpl(const char *fragContents, int fragSize,
                                               const char *fragName,
                                               const char *vertContents, int vertSize,
                                               const char *vertName)
{
	// Build the final vertex source. A user vertex source (from the sibling
	// .vert file) replaces the built-in spriteVert when present, and gets
	// the same common.h precision-header injection as fragment sources.
	std::string vertSrc;
	if (vertContents)
	{
		vertSrc.assign(vertContents, vertSize);
		insertAfterDirectives(vertSrc, Shader::commonHeaderSource(false));
	}
	else
	{
		vertSrc = spriteVert;
	}

	// Try the wrapped fragment source first
	std::string wrappedSrc = buildWrappedFragSource(fragContents, fragSize);

	// Cache lookup on the wrapped variant
	if (!wrappedSrc.empty())
	{
		GLuint cachedWrapped = findCachedProgram(vertSrc, wrappedSrc);
		if (cachedWrapped != 0)
		{
			ADOPT_CACHED_PROGRAM(cachedWrapped);
			finishUniformLookups();
			return;
		}
	}

	// Prepare fallback source
	std::string fallbackSrc(fragContents, fragSize);
	insertAfterDirectives(fallbackSrc, Shader::commonHeaderSource(true));

	if (wrappedSrc.empty())
	{
		Debug() << "CustomShader [" << fragName << "]: buildWrappedFragSource failed (main not found)";
		GLuint cachedFallback = findCachedProgram(vertSrc, fallbackSrc);
		if (cachedFallback != 0)
		{
			ADOPT_CACHED_PROGRAM(cachedFallback);
			finishUniformLookups();
			return;
		}
	}

	// Cache miss — compile + link on our own handles.
	const GLchar *vertSources[1] = { vertSrc.c_str() };
	GLint vertLengths[1] = { (GLint)vertSrc.size() };

	gl.ShaderSource(vertShader, 1, vertSources, vertLengths);
	gl.CompileShader(vertShader);

	GLint success = 0;
	gl.GetShaderiv(vertShader, GL_COMPILE_STATUS, &success);

	if (!success)
	{
		std::string log = getShaderLog(vertShader);
		if (vertContents)
			throw Exception(Exception::MKXPError,
			                "Vertex shader compilation failed for '%s':\n%s",
			                vertName, log.c_str());
		throw Exception(Exception::MKXPError,
		                "Internal vertex shader compilation failed for '%s':\n%s",
		                fragName, log.c_str());
	}

	// Try wrapping first, fall back on compile failure. The FINAL source
	// that succeeds is what we cache with — never the source that failed
	// to compile.
	bool wrapped = false;
	std::string compiledFragSrc;

	if (!wrappedSrc.empty())
	{
		// Debug() << "CustomShader [" << fragName << "]: Wrapped source built successfully";

		const GLchar *wrapSources[1] = { wrappedSrc.c_str() };
		GLint wrapLengths[1] = { (GLint)wrappedSrc.size() };

		gl.ShaderSource(fragShader, 1, wrapSources, wrapLengths);
		gl.CompileShader(fragShader);

		gl.GetShaderiv(fragShader, GL_COMPILE_STATUS, &success);
		wrapped = (success != 0);

		if (!wrapped)
		{
			std::string log = getShaderLog(fragShader);
			// Debug() << "CustomShader [" << fragName << "]: Wrapped shader FAILED to compile:\n" << log.c_str();
			// Debug() << "CustomShader [" << fragName << "]: Wrapped source:\n" << wrappedSrc.c_str();
		}
		else
		{
			// Debug() << "CustomShader [" << fragName << "]: Wrapped shader compiled OK";
			compiledFragSrc = wrappedSrc;
		}
	}
	else
	{
		// Debug() << "CustomShader [" << fragName << "]: buildWrappedFragSource failed (main not found)";
	}

	if (!wrapped)
	{
		// Debug() << "CustomShader [" << fragName << "]: Falling back to unwrapped shader (no built-in effects)";

		// Fallback path — but check the fallback cache one more time
		GLuint cachedFallback = findCachedProgram(vertSrc, fallbackSrc);
		if (cachedFallback != 0)
		{
			ADOPT_CACHED_PROGRAM(cachedFallback);
			finishUniformLookups();
			return;
		}

		const GLchar *fragSources[1] = { fallbackSrc.c_str() };
		GLint fragLengths[1] = { (GLint)fallbackSrc.size() };

		gl.ShaderSource(fragShader, 1, fragSources, fragLengths);
		gl.CompileShader(fragShader);

		gl.GetShaderiv(fragShader, GL_COMPILE_STATUS, &success);

		if (!success)
		{
			std::string log = getShaderLog(fragShader);
			throw Exception(Exception::MKXPError,
			                "Shader compilation failed for '%s':\n%s",
			                fragName, log.c_str());
		}

		compiledFragSrc = fallbackSrc;
	}

	// Link program with error handling
	gl.AttachShader(program, vertShader);
	gl.AttachShader(program, fragShader);

	gl.BindAttribLocation(program, Shader::Position, "position");
	gl.BindAttribLocation(program, Shader::TexCoord, "texCoord");
	gl.BindAttribLocation(program, Shader::Color, "color");

	gl.LinkProgram(program);

	gl.GetProgramiv(program, GL_LINK_STATUS, &success);

	if (!success)
	{
		std::string log = getProgramLog(program);
		throw Exception(Exception::MKXPError,
		                "Shader program linking failed for '%s':\n%s",
		                fragName, log.c_str());
	}

	insertCachedProgram(vertSrc, compiledFragSrc, program);
	ownsProgram = false;

	finishUniformLookups();
}

void CustomSpriteShaderImpl::finishUniformLookups()
{
	ShaderBase::init();

	u_spriteMat = gl.GetUniformLocation(program, "spriteMat");
	u_time = gl.GetUniformLocation(program, "time");
	u_opacity = gl.GetUniformLocation(program, "_mkxp_opacity");
	u_tone = gl.GetUniformLocation(program, "_mkxp_tone");
	u_color = gl.GetUniformLocation(program, "_mkxp_color");
	u_invert = gl.GetUniformLocation(program, "_mkxp_invert");
	u_bushDepth = gl.GetUniformLocation(program, "_mkxp_bushDepth");
	u_bushOpacity = gl.GetUniformLocation(program, "_mkxp_bushOpacity");

	/* Look up standard uniform names that the user's shader may declare.
	 * If present, the draw code sets them and neutralizes the suffix's
	 * application to avoid double-applying the effect. */
	u_stdOpacity = gl.GetUniformLocation(program, "opacity");
	u_stdTone = gl.GetUniformLocation(program, "tone");
	u_stdColor = gl.GetUniformLocation(program, "color");
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

void CustomSpriteShaderImpl::setTone(const Vec4 &value)
{
	if (u_tone >= 0)
		gl.Uniform4f(u_tone, value.x, value.y, value.z, value.w);
}

void CustomSpriteShaderImpl::setColor(const Vec4 &value)
{
	if (u_color >= 0)
		gl.Uniform4f(u_color, value.x, value.y, value.z, value.w);
}

void CustomSpriteShaderImpl::setInvert(bool value)
{
	if (u_invert >= 0)
		gl.Uniform1i(u_invert, value ? 1 : 0);
}

void CustomSpriteShaderImpl::setBushDepth(float value)
{
	if (u_bushDepth >= 0)
		gl.Uniform1f(u_bushDepth, value);
}

void CustomSpriteShaderImpl::setBushOpacity(float value)
{
	if (u_bushOpacity >= 0)
		gl.Uniform1f(u_bushOpacity, value);
}

void CustomSpriteShaderImpl::setStdOpacity(float value)
{
	if (u_stdOpacity >= 0)
		gl.Uniform1f(u_stdOpacity, value);
}

void CustomSpriteShaderImpl::setStdTone(const Vec4 &value)
{
	if (u_stdTone >= 0)
		gl.Uniform4f(u_stdTone, value.x, value.y, value.z, value.w);
}

void CustomSpriteShaderImpl::setStdColor(const Vec4 &value)
{
	if (u_stdColor >= 0)
		gl.Uniform4f(u_stdColor, value.x, value.y, value.z, value.w);
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
		case UNIFORM_VEC3:
			gl.Uniform3f(loc, val.data.vec3[0], val.data.vec3[1], val.data.vec3[2]);
			break;
		case UNIFORM_VEC4:
			gl.Uniform4f(loc, val.data.vec4[0], val.data.vec4[1], val.data.vec4[2], val.data.vec4[3]);
			break;
		case UNIFORM_MAT4:
			gl.UniformMatrix4fv(loc, 1, GL_FALSE, val.data.mat4);
			break;
		}
	}
}

void CustomSpriteShaderImpl::applyBitmaps(const BitmapMap &bitmaps, int startUnit)
{
	int unit = startUnit;
	for (BitmapMap::const_iterator it = bitmaps.begin(); it != bitmaps.end(); ++it)
	{
		Bitmap *bitmap = it->second;
		if (!bitmap || bitmap->isDisposed())
			continue;

		GLint loc = gl.GetUniformLocation(program, it->first.c_str());
		if (loc < 0)
			continue;

		// Also set the size inverse uniform if it exists (name + "SizeInv")
		std::string sizeInvName = it->first + "SizeInv";
		GLint sizeInvLoc = gl.GetUniformLocation(program, sizeInvName.c_str());

		TEX::ID tex = bitmap->getGLTypes().tex;
		setTexUniform(loc, unit, tex);

		if (sizeInvLoc >= 0)
		{
			gl.Uniform2f(sizeInvLoc, 1.0f / bitmap->width(), 1.0f / bitmap->height());
		}

		unit++;
	}
}

struct CustomShaderPrivate
{
	std::string filename;
	CustomShaderImpl *shader;
	CustomSpriteShaderImpl *spriteShader;
	UniformMap uniforms;
	BitmapMap bitmaps;

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

	// Look for a sibling vertex source: the same path with the extension
	// replaced by ".vert" (e.g. "Foo.glsl" -> "Foo.vert"). Present -> it is
	// compiled as the vertex stage for both impls; absent -> the built-in
	// vertex shaders are used, unchanged. Probe/read it the same way the
	// fragment source is (filesystem exists check + readFile).
	std::string vertPath(filename);
	size_t dot = vertPath.find_last_of('.');
	size_t slash = vertPath.find_last_of("/\\");
	if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
		vertPath.erase(dot);
	vertPath += ".vert";

	std::string vertContents;
	bool haveVert = false;
	if (shState->fileSystem().exists(vertPath.c_str()))
	{
		if (!readFile(vertPath.c_str(), vertContents))
		{
			delete p;
			throw Exception(Exception::RGSSError,
			                "Failed to read shader file '%s'", vertPath.c_str());
		}
		haveVert = true;
	}

	try
	{
		p->shader = new CustomShaderImpl(
			fragContents.c_str(), fragContents.size(), filename,
			haveVert ? vertContents.c_str() : 0,
			haveVert ? (int)vertContents.size() : 0,
			haveVert ? vertPath.c_str() : 0);
		p->spriteShader = new CustomSpriteShaderImpl(
			fragContents.c_str(), fragContents.size(), filename,
			haveVert ? vertContents.c_str() : 0,
			haveVert ? (int)vertContents.size() : 0,
			haveVert ? vertPath.c_str() : 0);
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

void CustomShader::setVec3(const char *name, float x, float y, float z)
{
	guardDisposed();
	UniformValue val;
	val.type = UNIFORM_VEC3;
	val.data.vec3[0] = x;
	val.data.vec3[1] = y;
	val.data.vec3[2] = z;
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

void CustomShader::setMat4(const char *name, const float value[16])
{
	guardDisposed();
	UniformValue val;
	val.type = UNIFORM_MAT4;
	memcpy(val.data.mat4, value, sizeof(val.data.mat4));
	p->uniforms[name] = val;
}

void CustomShader::setBitmap(const char *name, Bitmap *bitmap)
{
	guardDisposed();
	if (bitmap && !bitmap->isDisposed())
	{
		bitmap->ensureNonMega();
		p->bitmaps[name] = bitmap;
	}
	else
	{
		// Remove the bitmap if null or disposed
		p->bitmaps.erase(name);
	}
}

const UniformMap &CustomShader::getUniforms() const
{
	guardDisposed();
	return p->uniforms;
}

const BitmapMap &CustomShader::getBitmaps() const
{
	guardDisposed();
	return p->bitmaps;
}

void CustomShader::releaseResources()
{
	delete p;
}
