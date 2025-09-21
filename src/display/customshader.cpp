#include "customshader.h"
#include "exception.h"
#include "glstate.h"
#include "util.h"
#include <cstring>

CustomShader::CustomShader(const char *fragmentPath)
    : shader(0), disposed(false)
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

    // Create and initialize shader
    shader = new Shader();
    shader->init((const unsigned char*)minimalVert, strlen(minimalVert),
                 (const unsigned char*)fragContents.c_str(), fragContents.size(),
                 "minimal_custom", fragmentPath, "CustomShader");
}

void CustomShader::bind()
{
    if (disposed || !shader) return;
    shader->bind();
}

void CustomShader::unbind()
{
    if (disposed || !shader) return;
    shader->unbind();
}

void CustomShader::setUniformF(const char *name, float value)
{
    if (disposed || !shader) return;
    GLint loc = gl.GetUniformLocation(shader->program, name);
    if (loc != -1) {
        gl.Uniform1f(loc, value);
    }
}

void CustomShader::setUniformI(const char *name, int value)
{
    if (disposed || !shader) return;
    GLint loc = gl.GetUniformLocation(shader->program, name);
    if (loc != -1) {
        gl.Uniform1i(loc, value);
    }
}

void CustomShader::setUniformVec2(const char *name, const Vec2 &value)
{
    if (disposed || !shader) return;
    GLint loc = gl.GetUniformLocation(shader->program, name);
    if (loc != -1) {
        gl.Uniform2f(loc, value.x, value.y);
    }
}

void CustomShader::setUniformVec4(const char *name, const Vec4 &value)
{
    if (disposed || !shader) return;
    GLint loc = gl.GetUniformLocation(shader->program, name);
    if (loc != -1) {
        gl.Uniform4f(loc, value.x, value.y, value.z, value.w);
    }
}

void CustomShader::setUniformMatrix(const char *name, const float *matrix)
{
    if (disposed || !shader) return;
    GLint loc = gl.GetUniformLocation(shader->program, name);
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
    return shader ? shader->program : 0;
}

void CustomShader::dispose()
{
    if (disposed) return;

    if (shader) {
        delete shader;
        shader = 0;
    }

    disposed = true;
}

bool CustomShader::isDisposed() const
{
    return disposed;
}
