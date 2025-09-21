#ifndef CUSTOMSHADER_H
#define CUSTOMSHADER_H

#include "shader.h"
#include "disposable.h"
#include "etc-internal.h"
#include <string>

class CustomShader : public Disposable
{
public:
    CustomShader(const char *fragmentPath);
    ~CustomShader();

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
    Shader *shader;
    bool disposed;
    std::string fragmentSource;

    void loadAndCompileShader(const char *fragmentPath);
};

#endif // CUSTOMSHADER_H
