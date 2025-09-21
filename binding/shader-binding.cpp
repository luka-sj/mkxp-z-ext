/*
** shader-binding.cpp
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
#include "disposable-binding.h"
#include "binding-util.h"
#include "binding-types.h"
#include "exception.h"

DEF_TYPE_CUSTOMNAME(CustomShader, Shader);

RB_METHOD(shaderInitialize)
{
    const char *fragmentPath;
    rb_get_args(argc, argv, "z", &fragmentPath RB_ARG_END);

    CustomShader *s = 0;

    GUARD_EXC( s = new CustomShader(fragmentPath); )

    setPrivateData(self, s);

    return self;
}

RB_METHOD(shaderSetFloat)
{
    const char *name;
    double value;
    rb_get_args(argc, argv, "zf", &name, &value RB_ARG_END);

    CustomShader *s = getPrivateData<CustomShader>(self);

    GUARD_EXC( s->setUniformF(name, (float)value); )

    return Qnil;
}

RB_METHOD(shaderSetInt)
{
    const char *name;
    int value;
    rb_get_args(argc, argv, "zi", &name, &value RB_ARG_END);

    CustomShader *s = getPrivateData<CustomShader>(self);

    GUARD_EXC( s->setUniformI(name, value); )

    return Qnil;
}

RB_METHOD(shaderSetVec2)
{
    const char *name;
    double x, y;
    rb_get_args(argc, argv, "zff", &name, &x, &y RB_ARG_END);

    CustomShader *s = getPrivateData<CustomShader>(self);

    GUARD_EXC( s->setUniformVec2(name, Vec2((float)x, (float)y)); )

    return Qnil;
}

RB_METHOD(shaderSetVec4)
{
    const char *name;
    double x, y, z, w;
    rb_get_args(argc, argv, "zffff", &name, &x, &y, &z, &w RB_ARG_END);

    CustomShader *s = getPrivateData<CustomShader>(self);

    GUARD_EXC( s->setUniformVec4(name, Vec4((float)x, (float)y, (float)z, (float)w)); )

    return Qnil;
}

RB_METHOD(shaderSetMatrix)
{
    const char *name;
    VALUE ary;
    rb_get_args(argc, argv, "zA", &name, &ary RB_ARG_END);

    if (RARRAY_LEN(ary) != 16)
        rb_raise(rb_eArgError, "Matrix array must have 16 elements");

    float matrix[16];
    for (int i = 0; i < 16; i++)
        matrix[i] = (float)NUM2DBL(rb_ary_entry(ary, i));

    CustomShader *s = getPrivateData<CustomShader>(self);

    GUARD_EXC( s->setUniformMatrix(name, matrix); )

    return Qnil;
}

void shaderBindingInit()
{
    VALUE klass = rb_define_class("Shader", rb_cObject);
    rb_define_alloc_func(klass, classAllocate<&ShaderType>);

    disposableBindingInit<CustomShader>(klass);

    _rb_define_method(klass, "initialize", shaderInitialize);
    _rb_define_method(klass, "set_float", shaderSetFloat);
    _rb_define_method(klass, "set_int", shaderSetInt);
    _rb_define_method(klass, "set_vec2", shaderSetVec2);
    _rb_define_method(klass, "set_vec3", shaderSetVec3);
    _rb_define_method(klass, "set_vec4", shaderSetVec4);
    _rb_define_method(klass, "set_matrix", shaderSetMatrix);
}
