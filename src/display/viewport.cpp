/*
** viewport.cpp
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

#include "viewport.h"

#include "sharedstate.h"
#include "etc.h"
#include "util.h"
#include "quad.h"
#include "gl-fun.h"
#include "glstate.h"
#include "graphics.h"
#include "customshader.h"

#include <SDL_timer.h>  // For SDL_GetTicks
#include <SDL_rect.h>

#include "sigslot/signal.hpp"

struct ViewportPrivate
{
    /* Needed for geometry changes */
    Viewport *self;

    Rect *rect;
    sigslot::connection rectCon;

    Color *color;
    Tone *tone;

    CustomShader *shader;

    // Framebuffer for shader rendering
    GLuint shaderFBO;
    GLuint shaderTexture;
    int shaderWidth, shaderHeight;

    IntRect screenRect;
    int isOnScreen;

    EtcTemps tmp;

    ViewportPrivate(int x, int y, int width, int height, Viewport *self)
        : self(self),
          rect(&tmp.rect),
          color(&tmp.color),
          tone(&tmp.tone),
          shader(0),
          shaderFBO(0),
          shaderTexture(0),
          shaderWidth(0),
          shaderHeight(0),
          isOnScreen(false)
    {
        rect->set(x, y, width, height);
        updateRectCon();
    }

	~ViewportPrivate()
	{
		rectCon.disconnect();
		// Clean up shader reference (don't delete - it's managed elsewhere)
		shader = 0;

		// Clean up framebuffer resources
		if (shaderFBO) {
		    gl.DeleteFramebuffers(1, &shaderFBO);
		    shaderFBO = 0;
		}
		if (shaderTexture) {
		    gl.DeleteTextures(1, &shaderTexture);
		    shaderTexture = 0;
		}
	}

	void onRectChange()
	{
		self->geometry.rect = rect->toIntRect();
		self->notifyGeometryChange();
		recomputeOnScreen();
	}

	void updateRectCon()
	{
		rectCon.disconnect();
		rectCon = rect->valueChanged.connect
		        (&ViewportPrivate::onRectChange, this);
	}

	void recomputeOnScreen()
	{
		SDL_Rect r1 = { screenRect.x, screenRect.y,
		                screenRect.w, screenRect.h };

		SDL_Rect r2 = { rect->x,     rect->y,
		                rect->width, rect->height };

		SDL_Rect result;
		isOnScreen = SDL_IntersectRect(&r1, &r2, &result);
	}

	bool needsEffectRender(bool flashing)
	{
		bool rectEffective = !rect->isEmpty();
		bool colorToneEffective = color->hasEffect() || tone->hasEffect() || flashing;

		return (rectEffective && colorToneEffective && isOnScreen);
	}
};

Viewport::Viewport(int x, int y, int width, int height)
    : SceneElement(*shState->screen()),
      sceneLink(this)
{
	initViewport(x, y, width, height);
}

Viewport::Viewport(Rect *rect)
    : SceneElement(*shState->screen()),
      sceneLink(this)
{
	initViewport(rect->x, rect->y, rect->width, rect->height);
}

Viewport::Viewport()
    : SceneElement(*shState->screen()),
      sceneLink(this)
{
	const Graphics &graphics = shState->graphics();
	initViewport(0, 0, graphics.width(), graphics.height());
}

void Viewport::initViewport(int x, int y, int width, int height)
{
	p = new ViewportPrivate(x, y, width, height, this);

	/* Set our own geometry */
	geometry.rect = IntRect(x, y, width, height);

	/* Handle parent geometry */
	onGeometryChange(scene->getGeometry());
}

Viewport::~Viewport()
{
	dispose();
}

void Viewport::update()
{
	guardDisposed();

	Flashable::update();
}

DEF_ATTR_RD_SIMPLE(Viewport, OX,   int,   geometry.orig.x)
DEF_ATTR_RD_SIMPLE(Viewport, OY,   int,   geometry.orig.y)

DEF_ATTR_SIMPLE(Viewport, Rect,  Rect&,  *p->rect)
DEF_ATTR_SIMPLE(Viewport, Color, Color&, *p->color)
DEF_ATTR_SIMPLE(Viewport, Tone,  Tone&,  *p->tone)

void Viewport::setOX(int value)
{
	guardDisposed();

	if (geometry.orig.x == value)
		return;

	geometry.orig.x = value;
	notifyGeometryChange();
}

void Viewport::setOY(int value)
{
	guardDisposed();

	if (geometry.orig.y == value)
		return;

	geometry.orig.y = value;
	notifyGeometryChange();
}

void Viewport::initDynAttribs()
{
	p->rect = new Rect(*p->rect);
	p->color = new Color;
	p->tone = new Tone;

	p->updateRectCon();
}

void Viewport::setShader(CustomShader *shader)
{
    guardDisposed();

    // Add validation
    if (shader && shader->isDisposed()) {
        return; // Don't assign disposed shaders
    }

    p->shader = shader;
}

CustomShader *Viewport::getShader() const
{
    guardDisposed();
    return p->shader;
}

/* Scene */
void Viewport::composite()
{
    if (emptyFlashFlag)
        return;

    bool renderEffect = p->needsEffectRender(flashing);

    if (elements.getSize() == 0 && !renderEffect)
        return;

    /* Setup scissor */
    glState.scissorTest.pushSet(true);
    glState.scissorBox.pushSet(p->rect->toIntRect());

    // If we have a custom shader, render to texture first
    if (p->shader && !p->shader->isDisposed())
    {
        IntRect rect = p->rect->toIntRect();
        printf("Applying custom shader to viewport %dx%d\n", rect.w, rect.h);

        // Create framebuffer and texture if needed
        if (!p->shaderFBO) {
            gl.GenFramebuffers(1, &p->shaderFBO);
            gl.GenTextures(1, &p->shaderTexture);
            p->shaderWidth = p->shaderHeight = 0; // Force recreation
            printf("Created FBO and texture\n");
        }

        // Recreate texture if size changed
        if (p->shaderWidth != rect.w || p->shaderHeight != rect.h) {
            p->shaderWidth = rect.w;
            p->shaderHeight = rect.h;

            gl.BindTexture(GL_TEXTURE_2D, p->shaderTexture);
            gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rect.w, rect.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            gl.BindFramebuffer(GL_FRAMEBUFFER, p->shaderFBO);
            gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p->shaderTexture, 0);

            printf("Recreated texture %dx%d\n", rect.w, rect.h);
        }

        // Step 1: Render scene contents to the framebuffer texture
        gl.BindFramebuffer(GL_FRAMEBUFFER, p->shaderFBO);

        // Clear the framebuffer
        gl.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        gl.Clear(GL_COLOR_BUFFER_BIT);

        // Set viewport for FBO rendering
        glState.viewport.pushSet(IntRect(0, 0, rect.w, rect.h));

        // Render all child elements to the FBO (this fills our texture)
        printf("Rendering to FBO...\n");
        Scene::composite();
        printf("FBO render complete\n");

        // Restore viewport
        glState.viewport.pop();

        // Restore main framebuffer
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

        // Step 2: Render the texture using our custom shader
        printf("Applying shader to render texture...\n");
        p->shader->bind();

        // Set shader uniforms
        p->shader->applyViewportProj();
        p->shader->setResolution(Vec2((float)rect.w, (float)rect.h));
        p->shader->setTime(SDL_GetTicks() / 1000.0f);
        p->shader->setTexSize(Vec2i(rect.w, rect.h));
        p->shader->setTranslation(Vec2i(0, 0));

        // Bind the rendered texture to texture unit 0
        gl.ActiveTexture(GL_TEXTURE0);
        gl.BindTexture(GL_TEXTURE_2D, p->shaderTexture);
        p->shader->setUniformI("tex", 0);

        // For now, let's try a simple fallback without Quad class
        // Draw two triangles to form a rectangle covering the viewport

        // Disable depth testing and enable blending
        gl.Disable(GL_DEPTH_TEST);
        gl.Enable(GL_BLEND);
        gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Simple immediate mode rendering (for debugging)
        gl.Begin(GL_TRIANGLES);

        // First triangle
        gl.TexCoord2f(0.0f, 0.0f); gl.Vertex2f(0.0f, 0.0f);
        gl.TexCoord2f(1.0f, 0.0f); gl.Vertex2f((float)rect.w, 0.0f);
        gl.TexCoord2f(0.0f, 1.0f); gl.Vertex2f(0.0f, (float)rect.h);

        // Second triangle
        gl.TexCoord2f(1.0f, 0.0f); gl.Vertex2f((float)rect.w, 0.0f);
        gl.TexCoord2f(1.0f, 1.0f); gl.Vertex2f((float)rect.w, (float)rect.h);
        gl.TexCoord2f(0.0f, 1.0f); gl.Vertex2f(0.0f, (float)rect.h);

        gl.End();

        p->shader->unbind();

        // Restore texture binding
        gl.BindTexture(GL_TEXTURE_2D, 0);

        printf("Shader rendering complete\n");
    }
    else
    {
        // Standard rendering without shader
        Scene::composite();
    }

    /* If any effects are visible, request parent Scene to
     * render them. */
    if (renderEffect)
        scene->requestViewportRender
                (p->color->norm, flashColor, p->tone->norm);

    glState.scissorBox.pop();
    glState.scissorTest.pop();
}

/* SceneElement */
void Viewport::draw()
{
	composite();
}

void Viewport::onGeometryChange(const Geometry &geo)
{
	p->screenRect = geo.rect;
	p->recomputeOnScreen();
}

void Viewport::releaseResources()
{
	unlink();

	delete p;
}


ViewportElement::ViewportElement(Viewport *viewport, int z, int spriteY)
    : SceneElement(viewport ? *viewport : *shState->screen(), z, spriteY),
      m_viewport(viewport)
{
	if (rgssVer == 1 && viewport)
		viewportDispCon = viewport->wasDisposed.connect(&ViewportElement::viewportElementDisposal, this);
}

Viewport *ViewportElement::getViewport() const
{
	return m_viewport;
}

void ViewportElement::setViewport(Viewport *viewport)
{
	m_viewport = viewport;

	viewportDispCon.disconnect();
	if (rgssVer == 1 && viewport)
		viewportDispCon = viewport->wasDisposed.connect(&ViewportElement::viewportElementDisposal, this);

	setScene(viewport ? *viewport : *shState->screen());
	onViewportChange();
	onGeometryChange(scene->getGeometry());
}

void ViewportElement::viewportElementDisposal()
{
	viewportDispCon.disconnect();
	Disposable *self = dynamic_cast<Disposable*>(this);
	if(self != nullptr)
		self->dispose();
}

ViewportElement::~ViewportElement()
{
	viewportDispCon.disconnect();
}
