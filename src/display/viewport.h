/*
** viewport.h
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

#ifndef VIEWPORT_H
#define VIEWPORT_H

#include <vector>

#include "scene.h"
#include "flashable.h"
#include "disposable.h"
#include "util.h"

struct ViewportPrivate;

/* Camera params for the draw-time perspective transform, mirroring the
 * project-side Heightfield::Perspective law. */
struct ViewportPerspective
{
	bool active = false;
	float focalX = 0.0f;
	float focalY = 0.0f;
	float strength = 0.0f;
	float closeness = 1.0f;
	float zoom = 1.0f;
	float comp = 0.0f;
	float scrollX = 0.0f;
	float scrollY = 0.0f;

	void project(float flatX, float flatY, float lift, float closeLift,
	             float boost, float &outX, float &outY, float &outS) const
	{
		float fx = flatX - scrollX;
		float fy = flatY - scrollY;
		float distance = (focalY - fy) - closeLift * closeness;
		float denom = 1.0f + strength * distance;
		if (denom < 0.03f)
			denom = 0.03f;
		float s = (zoom / denom) * boost;
		outX = focalX + (fx - focalX) * s;
		outY = focalY + (fy - focalY) * s - lift * s + comp;
		outS = s;
	}
};

class Viewport : public Scene, public SceneElement, public Flashable, public Disposable
{
public:
	Viewport(int x, int y, int width, int height);
	Viewport(Rect *rect);
	Viewport();
	~Viewport();

	void update();

	DECL_ATTR( Rect,  Rect&  )
	DECL_ATTR( OX,    int    )
	DECL_ATTR( OY,    int    )
	DECL_ATTR( Color, Color& )
	DECL_ATTR( Tone,  Tone&  )
	DECL_ATTR( Shader, class CustomShader* )
	DECL_ATTR( Shaders, std::vector<class CustomShader*>& )

	const ViewportPerspective &perspective() const { return persp; }
	void setPerspective(const ViewportPerspective &value) { persp = value; }

	void initDynAttribs();

private:
	ViewportPerspective persp;

	void initViewport(int x, int y, int width, int height);
	void geometryChanged();

	void composite();
	void draw();
	void onGeometryChange(const Geometry &);
	bool isEffectiveViewport(Rect *&, Color *&, Tone *&) const;

	void releaseResources();
	const char *klassName() const { return "viewport"; }

	ABOUT_TO_ACCESS_DISP

	ViewportPrivate *p;
	friend struct ViewportPrivate;

	IntruListLink<Scene> sceneLink;
};

class ViewportElement : public SceneElement
{
public:
	ViewportElement(Viewport *viewport = 0, int z = 0, int spriteY = 0);
	~ViewportElement();

	DECL_ATTR( Viewport,  Viewport* )

protected:
	virtual void onViewportChange() {}

private:
	Viewport *m_viewport;
	sigslot::connection viewportDispCon;
	sigslot::connection viewportElementDispCon;
	void viewportElementDisposal();
};

#endif // VIEWPORT_H
