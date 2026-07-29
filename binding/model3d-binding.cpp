/*
** model3d-binding.cpp
**
** This file is part of mkxp.
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
#include "display/model3d.h"
#include "display/bitmap.h"
#include "display/customshader.h"

#if RAPI_FULL > 187
DEF_TYPE(Model3D);
#else
DEF_ALLOCFUNC(Model3D);
#endif

void bitmapInitProps(Bitmap *b, VALUE self);

RB_METHOD_GUARD(model3dInitialize) {
    const char *filename;
    rb_get_args(argc, argv, "z", &filename RB_ARG_END);

    Model3D *m = 0;

    GFX_LOCK;
    m = new Model3D(filename);
    GFX_UNLOCK;

    setPrivateData(self, m);

    return self;
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(model3dRender) {
    int width, height;
    rb_get_args(argc, argv, "ii", &width, &height RB_ARG_END);

    Model3D *m = getPrivateData<Model3D>(self);

    Bitmap *b = 0;

    GFX_LOCK;
    b = m->render(width, height);
    GFX_UNLOCK;

    VALUE obj = wrapObject(b, BitmapType);
    bitmapInitProps(b, obj);

    return obj;
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(model3dGetShader) {
    RB_UNUSED_PARAM;
    Model3D *m = getPrivateData<Model3D>(self);
    return rb_iv_get(self, "shader");
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(model3dSetShader) {
    VALUE shaderObj;
    rb_get_args(argc, argv, "o", &shaderObj RB_ARG_END);

    Model3D *m = getPrivateData<Model3D>(self);

    CustomShader *shader = 0;
    if (!NIL_P(shaderObj))
        shader = getPrivateDataCheck<CustomShader>(shaderObj, CustomShaderType);

    GFX_LOCK;
    m->setShader(shader);
    GFX_UNLOCK;

    rb_iv_set(self, "shader", shaderObj);
    return shaderObj;
}
RB_METHOD_GUARD_END

/* Read-only bounds getter macro */
#define DEF_M3D_READER_F(PropName) \
RB_METHOD_GUARD(model3dGet##PropName) { \
    RB_UNUSED_PARAM; \
    Model3D *m = getPrivateData<Model3D>(self); \
    return rb_float_new(m->get##PropName()); \
} \
RB_METHOD_GUARD_END

DEF_M3D_READER_F(BBoxRadius)
DEF_M3D_READER_F(BBoxWidth)
DEF_M3D_READER_F(BBoxHeight)
DEF_M3D_READER_F(BBoxDepth)

/* Float property getter/setter macro */
#define DEF_M3D_PROP_F(PropName, rubyGetter, rubySetter) \
RB_METHOD_GUARD(model3dGet##PropName) { \
    RB_UNUSED_PARAM; \
    Model3D *m = getPrivateData<Model3D>(self); \
    return rb_float_new(m->get##PropName()); \
} \
RB_METHOD_GUARD_END \
\
RB_METHOD_GUARD(model3dSet##PropName) { \
    double value; \
    rb_get_args(argc, argv, "f", &value RB_ARG_END); \
    Model3D *m = getPrivateData<Model3D>(self); \
    m->set##PropName((float)value); \
    return rb_float_new(value); \
} \
RB_METHOD_GUARD_END

DEF_M3D_PROP_F(RotationX, "rotation_x", "rotation_x=")
DEF_M3D_PROP_F(RotationY, "rotation_y", "rotation_y=")
DEF_M3D_PROP_F(RotationZ, "rotation_z", "rotation_z=")
DEF_M3D_PROP_F(Scale, "scale", "scale=")
DEF_M3D_PROP_F(PositionX, "position_x", "position_x=")
DEF_M3D_PROP_F(PositionY, "position_y", "position_y=")
DEF_M3D_PROP_F(PositionZ, "position_z", "position_z=")
DEF_M3D_PROP_F(CameraFOV, "camera_fov", "camera_fov=")
DEF_M3D_PROP_F(CameraX, "camera_x", "camera_x=")
DEF_M3D_PROP_F(CameraY, "camera_y", "camera_y=")
DEF_M3D_PROP_F(CameraZ, "camera_z", "camera_z=")
DEF_M3D_PROP_F(CameraTargetX, "camera_target_x", "camera_target_x=")
DEF_M3D_PROP_F(CameraTargetY, "camera_target_y", "camera_target_y=")
DEF_M3D_PROP_F(CameraTargetZ, "camera_target_z", "camera_target_z=")
DEF_M3D_PROP_F(CameraShiftX, "camera_shift_x", "camera_shift_x=")
DEF_M3D_PROP_F(CameraShiftY, "camera_shift_y", "camera_shift_y=")
DEF_M3D_PROP_F(LightX, "light_x", "light_x=")
DEF_M3D_PROP_F(LightY, "light_y", "light_y=")
DEF_M3D_PROP_F(LightZ, "light_z", "light_z=")
DEF_M3D_PROP_F(Ambient, "ambient", "ambient=")

#define INIT_M3D_PROP_F(PropName, rubyName) \
    _rb_define_method(klass, rubyName, model3dGet##PropName); \
    _rb_define_method(klass, rubyName "=", model3dSet##PropName);

void model3dBindingInit() {
    VALUE klass = rb_define_class("Model3D", rb_cObject);
#if RAPI_FULL > 187
    rb_define_alloc_func(klass, classAllocate<&Model3DType>);
#else
    rb_define_alloc_func(klass, Model3DAllocate);
#endif

    disposableBindingInit<Model3D>(klass);

    _rb_define_method(klass, "initialize", model3dInitialize);
    _rb_define_method(klass, "render", model3dRender);
    _rb_define_method(klass, "shader", model3dGetShader);
    _rb_define_method(klass, "shader=", model3dSetShader);

    _rb_define_method(klass, "bbox_radius", model3dGetBBoxRadius);
    _rb_define_method(klass, "bbox_width", model3dGetBBoxWidth);
    _rb_define_method(klass, "bbox_height", model3dGetBBoxHeight);
    _rb_define_method(klass, "bbox_depth", model3dGetBBoxDepth);

    INIT_M3D_PROP_F(RotationX, "rotation_x")
    INIT_M3D_PROP_F(RotationY, "rotation_y")
    INIT_M3D_PROP_F(RotationZ, "rotation_z")
    INIT_M3D_PROP_F(Scale, "scale")
    INIT_M3D_PROP_F(PositionX, "position_x")
    INIT_M3D_PROP_F(PositionY, "position_y")
    INIT_M3D_PROP_F(PositionZ, "position_z")
    INIT_M3D_PROP_F(CameraFOV, "camera_fov")
    INIT_M3D_PROP_F(CameraX, "camera_x")
    INIT_M3D_PROP_F(CameraY, "camera_y")
    INIT_M3D_PROP_F(CameraZ, "camera_z")
    INIT_M3D_PROP_F(CameraTargetX, "camera_target_x")
    INIT_M3D_PROP_F(CameraTargetY, "camera_target_y")
    INIT_M3D_PROP_F(CameraTargetZ, "camera_target_z")
    INIT_M3D_PROP_F(CameraShiftX, "camera_shift_x")
    INIT_M3D_PROP_F(CameraShiftY, "camera_shift_y")
    INIT_M3D_PROP_F(LightX, "light_x")
    INIT_M3D_PROP_F(LightY, "light_y")
    INIT_M3D_PROP_F(LightZ, "light_z")
    INIT_M3D_PROP_F(Ambient, "ambient")
}
