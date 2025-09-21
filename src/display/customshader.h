/*
** customshader.h
**
** This file is part of mkxp.
*/

#ifndef CUSTOMSHADER_H
#define CUSTOMSHADER_H

#include "shader.h"
#include "disposable.h"
#include "etc-internal.h"
#include <string>

class CustomShader : public Shader, public Disposable
{
public:
    CustomShader(const char *fragmentPath);
    ~CustomShader();

    void bind();
    void unbind();

    // ShaderBase-like interface for viewport compatibility
    void setTexSize(const Vec2i &value);
    void setTranslation(const Vec2i &value);
    void applyViewportProj();

    // Custom uniform setters
    void setUniformF(const char *name, float value);
    void setUniformI(const char *name, int value);
    void setUniformVec2(const char *name, const Vec2 &value);
    void setUniformVec4(const char *name, const Vec4 &value);
    void setUniformMatrix(const char *name, const float *matrix);

    // Convenience methods
    void setResolution(const Vec2 &value);
    void setTime(float value);

    GLuint getProgram() const;

    void dispose();
    bool isDisposed() const;

private:
    void loadAndCompileShader(const char *fragmentPath);
    void compileShadersDirect(const std::string& fragmentSource);
    void initBaseUniforms();

    std::string fragmentSource;
    bool disposed;

    // Cached uniform locations
    GLint u_projMat;
    GLint u_texSizeInv;
    GLint u_translation;
    GLint u_resolution;
    GLint u_time;
    GLint u_texture;
};

#endif // CUSTOMSHADER_H
