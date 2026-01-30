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

#include "binding-types.h"
#include "binding-util.h"
#include "disposable-binding.h"
#include "sharedstate.h"
#include "display/customshader.h"

#if RAPI_FULL > 187
DEF_TYPE(CustomShader);
#else
DEF_ALLOCFUNC(CustomShader);
#endif

RB_METHOD_GUARD(shaderInitialize) {
    const char *filename;
    rb_get_args(argc, argv, "z", &filename RB_ARG_END);

    CustomShader *s = 0;

    GFX_LOCK;

    s = new CustomShader(filename);

    GFX_UNLOCK;

    setPrivateData(self, s);

    return self;
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(shaderGetFilename) {
    RB_UNUSED_PARAM;

    CustomShader *s = getPrivateData<CustomShader>(self);

    return rb_utf8_str_new_cstr(s->getFilename().c_str());
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(shaderSetFloat) {
    const char *name;
    double value;
    rb_get_args(argc, argv, "zf", &name, &value RB_ARG_END);

    CustomShader *s = getPrivateData<CustomShader>(self);

    GFX_LOCK;
    s->setFloat(name, (float)value);
    GFX_UNLOCK;

    return Qnil;
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(shaderSetInt) {
    const char *name;
    int value;
    rb_get_args(argc, argv, "zi", &name, &value RB_ARG_END);

    CustomShader *s = getPrivateData<CustomShader>(self);

    GFX_LOCK;
    s->setInt(name, value);
    GFX_UNLOCK;

    return Qnil;
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(shaderSetVec2) {
    const char *name;
    double x, y;
    rb_get_args(argc, argv, "zff", &name, &x, &y RB_ARG_END);

    CustomShader *s = getPrivateData<CustomShader>(self);

    GFX_LOCK;
    s->setVec2(name, (float)x, (float)y);
    GFX_UNLOCK;

    return Qnil;
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(shaderSetVec4) {
    const char *name;
    double x, y, z, w;
    rb_get_args(argc, argv, "zffff", &name, &x, &y, &z, &w RB_ARG_END);

    CustomShader *s = getPrivateData<CustomShader>(self);

    GFX_LOCK;
    s->setVec4(name, (float)x, (float)y, (float)z, (float)w);
    GFX_UNLOCK;

    return Qnil;
}
RB_METHOD_GUARD_END

void shaderBindingInit() {
    VALUE klass = rb_define_class("Shader", rb_cObject);
#if RAPI_FULL > 187
    rb_define_alloc_func(klass, classAllocate<&CustomShaderType>);
#else
    rb_define_alloc_func(klass, CustomShaderAllocate);
#endif
    
    disposableBindingInit<CustomShader>(klass);

    _rb_define_method(klass, "initialize", shaderInitialize);
    _rb_define_method(klass, "filename", shaderGetFilename);

    _rb_define_method(klass, "set_float", shaderSetFloat);
    _rb_define_method(klass, "set_int", shaderSetInt);
    _rb_define_method(klass, "set_vec2", shaderSetVec2);
    _rb_define_method(klass, "set_vec4", shaderSetVec4);
}
