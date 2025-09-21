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
#include "gl-util.h"
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

    IntRect screenRect;
    int isOnScreen;

    EtcTemps tmp;

    GLuint fbo = 0;
    GLuint fboTexture = 0;
    int fboWidth = 0;
    int fboHeight = 0;
    bool fboInitialized = false;

    ViewportPrivate(int x, int y, int width, int height, Viewport *self)
        : self(self),
          rect(&tmp.rect),
          color(&tmp.color),
          tone(&tmp.tone),
          shader(0),
          isOnScreen(false)
    {
        rect->set(x, y, width, height);
        updateRectCon();
    }

	~ViewportPrivate()
	{
		rectCon.disconnect();
		shader = 0;
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

    // Initialize or reinitialize FBO with given width/height
    void initFBO(int width, int height)
    {
        if (width <= 0 || height <= 0) return;
        // If already initialized and size matches, skip
        if (fboInitialized && fboWidth == width && fboHeight == height)
            return;
        // Clean up old if any
        if (fboInitialized)
        {
            gl.DeleteTextures(1, &fboTexture);
            gl.DeleteFramebuffers(1, &fbo);
        }
        fboWidth = width;
        fboHeight = height;

        gl.GenTextures(1, &fboTexture);
        gl.BindTexture(GL_TEXTURE_2D, fboTexture);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // Optional wrap
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                      GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        gl.GenFramebuffers(1, &fbo);
        gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D, fboTexture, 0);
        // Unbind
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

        fboInitialized = true;
    }

    void releaseFBO()
    {
        if (!fboInitialized) return;
        if (fboTexture)
            gl.DeleteTextures(1, &fboTexture);
        if (fbo)
            gl.DeleteFramebuffers(1, &fbo);
        fboTexture = 0;
        fbo = 0;
        fboWidth = fboHeight = 0;
        fboInitialized = false;
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

	/* Initialize frame buffer */
	p->initFBO(width, height);
}

Viewport::~Viewport()
{
    p->releaseFBO();
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

static void drawTexturedQuad(int width, int height)
{
    // Construct a quad
    Quad quad;

    // Position: from (0, 0) to (width, height)
    FloatRect posRect(0.0f, 0.0f, (float)width, (float)height);

    // Texture coordinates: (0, 0) to (1, 1)
    FloatRect texRect(0.0f, 0.0f, 1.0f, 1.0f);

    quad.setTexPosRect(texRect, posRect);

    // Use full white (no tint)
    quad.setColor(Vec4(1.0f, 1.0f, 1.0f, 1.0f));

    // Draw
    quad.draw();
}


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

    // If shader is present, render to the FBO first
    if (p->shader && !p->shader->isDisposed())
    {
        IntRect rect = p->rect->toIntRect();
        int w = rect.w;
        int h = rect.h;
        p->initFBO(w, h);  // ensure FBO matches size

        // 1) bind FBO and render scene
        gl.BindFramebuffer(GL_FRAMEBUFFER, p->fbo);
        gl.Viewport(0, 0, w, h);
        gl.ClearColor(0, 0, 0, 0);
        gl.Clear(GL_COLOR_BUFFER_BIT);

        // Render everything into FBO
        Scene::composite();

        // 2) unbind FBO, restore viewport for rendering to screen
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        // Set viewport to match the viewport's position and size on screen
        gl.Viewport(rect.x, rect.y, w, h);

        // 3) apply shader and draw the textured quad from FBO
        p->shader->bind();
        p->shader->applyViewportProj();
        p->shader->setResolution(Vec2((float)w, (float)h));
        p->shader->setTime(SDL_GetTicks() / 1000.0f);
        p->shader->setTexSize(Vec2i(w, h));
        p->shader->setTranslation(Vec2i(0, 0));

        // Bind the FBO texture
        gl.ActiveTexture(GL_TEXTURE0);
        gl.BindTexture(GL_TEXTURE_2D, p->fboTexture);

        // Draw quad covering the viewport rect
        // You need a helper for drawing a textured quad at (0,0)-(w,h)
        drawTexturedQuad(w, h);

        p->shader->unbind();
    }
    else
    {
        // No shader: just render normally
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
