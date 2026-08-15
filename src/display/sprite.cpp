/*
 ** sprite.cpp
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

#include "sprite.h"

#include "sharedstate.h"
#include "bitmap.h"
#include "debugwriter.h"
#include "config.h"
#include "etc.h"
#include "etc-internal.h"
#include "util.h"
#include "customshader.h"

#include "gl-util.h"
#include "quad.h"
#include "transform.h"
#include "shader.h"
#include "glstate.h"
#include "quadarray.h"
#include "spritebatch.h"

// TODO: Replace M_PI with std::numbers::pi once we upgrade to C++20.
#include <math.h>
#ifndef M_PI
# define M_PI 3.14159265358979323846
#endif

#include <SDL.h>
#include <SDL_rect.h>

#include "sigslot/signal.hpp"

static float fwrap(float value, float range)
{
    float res = fmod(value, range);
    return res < 0 ? res + range : res;
}

/* Inset a tex rect half a texel per side so edge interpolation at
 * fractional sprite positions cannot sample the neighboring sheet frame. */
static FloatRect texelInset(const FloatRect &rect, float tx, float ty)
{
    float ix = std::min(tx, rect.w / 2.0f);
    float iy = std::min(ty, rect.h / 2.0f);
    return FloatRect(rect.x + ix, rect.y + iy,
                     rect.w - (2.0f * ix), rect.h - (2.0f * iy));
}

struct SpritePrivate
{
    Bitmap *bitmap;
    Bitmap *realBitmap;
    
    sigslot::connection bitmapDispCon;
    
    float realOX;
    float realOY;
    float realZoomX;
    float realZoomY;
    
    Rect *realSrcRect;
    
    Quad quad;
    Transform trans;

    /* Model transform used while corners are active: carries the scene
     * global offset only (no position/origin/zoom/rotation), so corner
     * coordinates are viewport-space px. Kept in sync in onGeometryChange. */
    Transform offsetTrans;

    FloatRect srcRect;
    FloatRect adjustedSrcRect;
    sigslot::connection srcRectCon;
    
    bool mirrored;
    int bushDepth;
    float bushSlope;
    float bushIntercept;
    bool bushY;
    bool bushUnder;
    bool bushDirty;
    NormValue bushOpacity;
    NormValue opacity;
    BlendType blendType;
    
    Bitmap *pattern;
    BlendType patternBlendType;
    bool patternTile;
    NormValue patternOpacity;
    Vec2 patternScroll;
    Vec2 patternZoom;
    
    bool invert;
    
    Scene::Geometry sceneGeo;
    
    /* Would this sprite be visible on
     * the screen if drawn? */
    bool isVisible;
    bool *spriteVisible;
    Viewport *viewport;
    
    Color *color;
    Tone *tone;
    CustomShader *shader;
    std::vector<CustomShader*> shaders;

    struct
    {
        int amp;
        int length;
        int speed;
        float phase;
        
        /* Wave effect is active (amp != 0) */
        bool active;
        /* qArray needs updating */
        bool dirty;
        SimpleQuadArray qArray;
    } wave;

    struct
    {
        /* Corners set: quad positions are authoritative */
        bool active;
        /* Viewport-space px, TL TR BR BL */
        Vec2 pts[4];
    } corners;

    struct
    {
        bool enabled;
        float lift;
        float closenessLift;
        bool hasClosenessLift;
        float boost;
        /* TL TR BR BL */
        float cornerLifts[4];
        bool hasCornerLifts;
        bool quadActive;
        Vec2 quadPts[4];
        Transform trans;
        bool projected;
    } persp;

    EtcTemps tmp;
    
    sigslot::connection prepareCon;
    
    SpritePrivate()
    : bitmap(0),
    realBitmap(0),
    realOX(0),
    realOY(0),
    realZoomX(1.0f),
    realZoomY(1.0f),
    realSrcRect(&tmp.rect),
    mirrored(false),
    bushDepth(0),
    bushSlope(0),
    bushIntercept(1.0f),
    bushY(true),
    bushUnder(true),
    bushDirty(true),
    bushOpacity(128),
    opacity(255),
    blendType(BlendNormal),
    pattern(0),
    patternTile(true),
    patternOpacity(255),
    invert(false),
    isVisible(false),
    color(&tmp.color),
    tone(&tmp.tone),
    shader(0)

    {
        updateSrcRectCon();
        
        prepareCon = shState->prepareDraw.connect
        (&SpritePrivate::prepare, this);
        
        patternScroll = Vec2(0,0);
        patternZoom = Vec2(1, 1);
        
        wave.amp = 0;
        wave.length = 180;
        wave.speed = 360;
        wave.phase = 0.0f;
        wave.dirty = false;

        corners.active = false;

        persp.enabled = false;
        persp.lift = 0.0f;
        persp.closenessLift = 0.0f;
        persp.hasClosenessLift = false;
        persp.boost = 1.0f;
        persp.hasCornerLifts = false;
        persp.quadActive = false;
        persp.projected = false;
    }
    
    ~SpritePrivate()
    {
        srcRectCon.disconnect();
        prepareCon.disconnect();

        bitmapDisposal();
    }
    
    void bitmapDisposal()
    {
        if (bitmap != realBitmap)
        {
            delete bitmap;
        }
        realBitmap = bitmap = 0;
        bitmapDispCon.disconnect();
    }

	void updateChild()
	{
		if (nullOrDisposed(bitmap))
			return;
		
		if (bitmap == realBitmap || !opacity)
		{
			return;
		}
		
		ChildPublic &shared = *bitmap->getChildInfo();
		
		shared.sceneRect = &sceneGeo.rect;
		shared.sceneOrig = &sceneGeo.orig;
		
		shared.x = trans.getPosition().x;
		shared.y = trans.getPosition().y;
		shared.realOffset = Vec2i(lroundf(realOX), lroundf(realOY));
		shared.realZoom = Vec2(std::max(realZoomX, 0.0f), std::max(realZoomY, 0.0f));
		shared.angle = fwrap(trans.getRotation(), 360);
		
		shared.mirrored = mirrored;
		
		shared.realSrcRect = realSrcRect->toIntRect();
		
		shared.waveAmp = wave.amp;
		
		//shared.width = sceneGeo.rect.w;
		//shared.height = sceneGeo.rect.h;
		
		bitmap->childUpdate();
		
		isVisible = shared.isVisible;
		
		if (!isVisible)
		{
			return;
		}
		
		if (trans.getOrigin().x != shared.offset.x || trans.getOrigin().y != shared.offset.y)
			trans.setOrigin(Vec2(shared.offset.x, shared.offset.y));
		if (trans.getScale().x != shared.zoom.x || trans.getScale().y != shared.zoom.y)
			trans.setScale(Vec2(shared.zoom.x, shared.zoom.y));
		if (srcRect.x != shared.srcRect.x || srcRect.y != shared.srcRect.y ||
		    srcRect.w != shared.srcRect.w || srcRect.h != shared.srcRect.h)
		{
			srcRect = shared.srcRect;
			onSrcRectChange();
		}
	}

    void recomputeBushDepth()
    {
        if (nullOrDisposed(bitmap))
            return;
        
        bushDirty = false;
        
        if (bushDepth <= 0)
        {
            bushSlope = 0;
            bushIntercept = 1.0f;
            bushY = true;
            bushUnder = true;
            return;
        }
        
        // Invert the angle if mirrored
        int mirror = mirrored ? -1 : 1;
        float angle = fwrap(mirror * trans.getRotation(), 360);
        
        // If it's not rotated, then we can skip all of those other calculations.
        if (angle == 0.0f)
        {
            bushSlope = 0;
            bushIntercept = (srcRect.y + srcRect.h - (bushDepth / trans.getScale().y)) / bitmap->height();
            bushY = true;
            bushUnder = true;
            return;
        }
        
        // Calculate the slope in segments of 45deg, so I don't have to deal with near-infinite slopes
        bushSlope = tan(abs(fwrap(angle - 45, 90) - 45) * M_PI / 180.0f);
        // Manually set negative slopes
        bushSlope *= fwrap(angle, 180) > 90 ? -1 : 1;
        
        
        // If the angle is within 45deg of 90deg or 270deg we use the x-axis instead
        // Additionally, since the shader's coordinates are percentage based, we need to make
        // the slope relative to the scaled bitmap's ratio
        float scaledW = bitmap->width() * trans.getScale().x;
        float scaledH = bitmap->height() * trans.getScale().y;
        if (fwrap(angle + 45, 180) < 90)
        {
            bushY = true;
            bushSlope = bushSlope * scaledW / scaledH;
        }
        else
        {
            bushY = false;
            bushSlope = bushSlope * scaledH / scaledW;
        }
        // Invert the check when we switch from the y-axis to the x-axis
        bushUnder = angle < 45 || angle >= 225;
        
        // Zoom and rotate the srcRect

        FloatRect src = srcRect;

        // Mirrored sprites whose src_rects extend beyond the bounds of the bitmap
        // need to swap the overflows
        if (mirrored)
        {
            float overflowX = std::max(src.x + src.w - bitmap->width(), 0.0f);
            
            if (src.x < 0)
            {
                src.x = 0;
            }
            src.x -= overflowX;
        }

        // A quick hack to get mirrored mega surfaces to work
        if (realBitmap != bitmap && mirrored && src.w > bitmap->width())
        {
            src.x *= -1;
            src.x -= srcRect.w - bitmap->width();
            if (realSrcRect->x < 0)
            {
                src.x += realSrcRect->x * (realZoomX / trans.getScale().x);
            }
        }

        src.x *= trans.getScale().x;
        src.y *= trans.getScale().y;
        src.w *= trans.getScale().x;
        src.h *= trans.getScale().y;
        
        // We use a "left-handed" coordinate system, with positive y values being below the x-axis,
        // so we need to use the negative of the angle to get the proper y values.
        float rotation  = -angle * M_PI / 180.0f;
        
        // p1 doesn't change, so we can skip rotating it
        Vec2 p1 = src.topLeft();
        Vec2 p2 = rotate_point(p1, rotation, src.topRight());
        Vec2 p3 = rotate_point(p1, rotation, src.bottomLeft());
        Vec2 p4 = rotate_point(p1, rotation, src.bottomRight());
        
        // Find the upper boundary of the bush effect and rotate it back.
        // The rotated slope is a horizontal line, so any x value will work.
        Vec2 point(0, std::max(std::max(p1.y, p2.y), std::max(p3.y, p4.y)) - bushDepth);
        point = rotate_point(p1, -rotation, point);
        
        // Unzoom the point and convert it into a percentage
        point.y = point.y / trans.getScale().y / bitmap->height();
        point.x = point.x / trans.getScale().x / bitmap->width();
        
        if (bushY)
            bushIntercept = (point.y - (bushSlope * point.x));
        else
            bushIntercept = (point.x - (bushSlope * point.y));
    }
    
    void onSrcRectChange()
    {
        if (bitmap == realBitmap)
            srcRect = realSrcRect->toFloatRect();
        
        adjustedSrcRect = srcRect;
        FloatRect &rect = adjustedSrcRect;
        Vec2i bmSize;
        Vec2i bmSizeHires;
        
        if (!nullOrDisposed(bitmap))
        {
            bmSize = Vec2i(bitmap->width(), bitmap->height());
            if (bitmap->hasHires())
            {
                bmSizeHires = Vec2i(bitmap->getHires()->width(), bitmap->getHires()->height());
            }
        }
        
        /* Clamp the rectangle so it doesn't reach outside
         * the bitmap bounds */
        if (rect.x < 0)
        {
            rect.w += rect.x;
            trans.setSrcRectOrigin(Vec2(rect.x, trans.getSrcRectOrigin().y));
        }
        else if(trans.getSrcRectOrigin().x != 0)
            trans.setSrcRectOrigin(Vec2(0, trans.getSrcRectOrigin().y));
        if (rect.y < 0)
        {
            rect.h += rect.y;
            trans.setSrcRectOrigin(Vec2(trans.getSrcRectOrigin().x, rect.y));
        }
        else if(trans.getSrcRectOrigin().y != 0)
            trans.setSrcRectOrigin(Vec2(trans.getSrcRectOrigin().x, 0));
        rect.x = clamp<float>(rect.x, 0, bmSize.x);
        rect.y = clamp<float>(rect.y, 0, bmSize.y);
        rect.w = clamp<float>(rect.w, 0, bmSize.x-rect.x);
        rect.h = clamp<float>(rect.h, 0, bmSize.y-rect.y);
        
        if (bmSizeHires.x && bmSizeHires.y && bmSize.x && bmSize.y)
        {
            FloatRect rectHires(rect.x * bmSizeHires.x / bmSize.x,
                                rect.y * bmSizeHires.y / bmSize.y,
                                rect.w * bmSizeHires.x / bmSize.x,
                                rect.h * bmSizeHires.y / bmSize.y);
            rectHires = texelInset(rectHires,
                                   0.5f * bmSizeHires.x / bmSize.x,
                                   0.5f * bmSizeHires.y / bmSize.y);
            quad.setTexRect(mirrored ? rectHires.hFlipped() : rectHires);
        }
        else
        {
            FloatRect texRect = texelInset(rect, 0.5f, 0.5f);
            quad.setTexRect(mirrored ? texRect.hFlipped() : texRect);
        }
        
        /* While corners are active they own the quad positions; only the
         * tex-rect update above applies (src_rect animation keeps working). */
        if (!corners.active && !persp.quadActive)
            quad.setPosRect(FloatRect(0, 0, rect.w, rect.h));
        bushDirty = true;

        if (wave.active)
            wave.dirty = true;
    }
    
    void updateSrcRectCon()
    {
        /* Cut old connection */
        srcRectCon.disconnect();
        /* Create new one */
        if (realBitmap == bitmap)
        {
            srcRectCon = realSrcRect->valueChanged.connect
            (&SpritePrivate::onSrcRectChange, this);
        }
    }
    
    const ViewportPerspective *activePerspective()
    {
        if (!viewport)
            return 0;

        const ViewportPerspective &vp = viewport->perspective();
        return vp.active ? &vp : 0;
    }

    void updatePerspective()
    {
        bool wasQuad = persp.quadActive;
        persp.projected = false;
        persp.quadActive = false;

        const ViewportPerspective *vp = persp.enabled ? activePerspective() : 0;
        if (!vp || corners.active)
        {
            if (wasQuad && !corners.active)
                quad.setPosRect(FloatRect(0, 0, adjustedSrcRect.w, adjustedSrcRect.h));
            return;
        }

        const Vec2 &pos = trans.getPosition();
        const Vec2 &scale = trans.getScale();

        if (persp.hasCornerLifts)
        {
            float ax = trans.getOrigin().x + trans.getSrcRectOrigin().x;
            float ay = trans.getOrigin().y + trans.getSrcRectOrigin().y;
            float x0 = pos.x - ax * scale.x;
            float y0 = pos.y - ay * scale.y;
            float x1 = x0 + adjustedSrcRect.w * scale.x;
            float y1 = y0 + adjustedSrcRect.h * scale.y;
            Vec2 pts[4];
            float s;
            vp->project(x0, y0, persp.cornerLifts[0], persp.cornerLifts[0], 1.0f, pts[0].x, pts[0].y, s);
            vp->project(x1, y0, persp.cornerLifts[1], persp.cornerLifts[1], 1.0f, pts[1].x, pts[1].y, s);
            vp->project(x1, y1, persp.cornerLifts[2], persp.cornerLifts[2], 1.0f, pts[2].x, pts[2].y, s);
            vp->project(x0, y1, persp.cornerLifts[3], persp.cornerLifts[3], 1.0f, pts[3].x, pts[3].y, s);
            persp.quadActive = true;
            bool changed = !wasQuad;
            for (int i = 0; i < 4 && !changed; ++i)
                changed = pts[i].x != persp.quadPts[i].x || pts[i].y != persp.quadPts[i].y;
            if (changed)
            {
                for (int i = 0; i < 4; ++i)
                    persp.quadPts[i] = pts[i];
                quad.setPosQuad(persp.quadPts);
            }
            return;
        }

        if (wasQuad)
            quad.setPosRect(FloatRect(0, 0, adjustedSrcRect.w, adjustedSrcRect.h));

        float closeLift = persp.hasClosenessLift ? persp.closenessLift : persp.lift;
        float sx, sy, s;
        vp->project(pos.x, pos.y, persp.lift, closeLift, persp.boost, sx, sy, s);
        persp.trans.setPosition(Vec2(sx, sy));
        persp.trans.setScale(Vec2(scale.x * s, scale.y * s));
        persp.trans.setOrigin(trans.getOrigin());
        persp.trans.setSrcRectOrigin(trans.getSrcRectOrigin());
        persp.trans.setRotation(trans.getRotation());
        persp.trans.setGlobalOffset(trans.getGlobalOffset());
        persp.projected = true;
    }

    void updateVisibility()
    {
        /* Child bitmaps handle their own visibility checks */
        if (bitmap != realBitmap)
            return;
        
        isVisible = false;
        
        if (nullOrDisposed(bitmap))
            return;
        
        if (!opacity)
            return;

        if (corners.active)
        {
            /* Corners define an arbitrary quad; skip the axis-aligned
             * bounding test (same opt-out as the zoom/rotation case). */
            isVisible = true;
            return;
        }

        if (persp.projected || persp.quadActive)
        {
            isVisible = true;
            return;
        }

        /* Compare sprite bounding box against the scene */
        
        /* If sprite is zoomed/rotated, just opt out for now
         * for simplicity's sake */
        const Vec2 &scale = trans.getScale();
        if (!scale.x || !scale.y)
            return;
        
        if (scale.x != 1 || scale.y != 1 || trans.getRotation() != 0)
        {
            isVisible = true;
            return;
        }
        
        if (wave.active)
        {
            /* Don't do expensive wave bounding box
             * calculations */
            isVisible = wave.qArray.quadCount != 0;
            return;
        }
        
        IntRect self = adjustedSrcRect;
        self.setPos(trans.getPositionI() + trans.getGlobalOffset() - trans.getAdjustedOriginI());
        
        isVisible = SDL_HasIntersection(&self, &sceneGeo.rect);
    }
    
    void emitWaveChunk(SVertex *&vert, float phase, float width,
                       const Vec2 &zoom, int chunkY, int chunkLength, int offsetLength)
    {
        float wavePos = phase + ((offsetLength + chunkY) / (float) wave.length) * (float) (M_PI * 2);
        float chunkX = sin(wavePos) * wave.amp / zoom.x;
        
        FloatRect pos(chunkX, chunkY / zoom.y, adjustedSrcRect.w, chunkLength / zoom.y);
        
        /* For some bizarre reason, combining a positive wave.amp with
         * a non-zero angle (including multiples of 360) reduces the width.
         * That being said, RGSS applies the wave effect after rotation
         * and we're applying it before, so since we're deviating anyway
         * and this behavior is weird I'm choosing to not do it. */
        //if (p->trans.getRotation())
        //    pos.w *= pos.w / (pos.w + (2.0f * wave.amp));
        
        FloatRect tex = mirrored ? adjustedSrcRect.hFlipped() : adjustedSrcRect;
        
        tex.y += pos.y;
        tex.h = pos.h;
        if (bitmap->hasHires())
        {
            Vec2 bmSize = Vec2(bitmap->width(), bitmap->height());
            Vec2 bmSizeHires = Vec2(bitmap->getHires()->width(), bitmap->getHires()->height());
            if (bmSizeHires.x && bmSizeHires.y && bmSize.x && bmSize.y)
            {
                tex.x *= bmSizeHires.x / bmSize.x;
                tex.y *= bmSizeHires.y / bmSize.y;
                tex.w *= bmSizeHires.x / bmSize.x;
                tex.h *= bmSizeHires.y / bmSize.y;
            }
        }
        
        Quad::setTexPosRect(vert, tex, pos);
        vert += 4;
    }
    
    void updateWave()
    {
        wave.dirty = false;
        
        if (nullOrDisposed(bitmap))
            return;

        /* Corners win over the wave effect while active */
        if (corners.active)
        {
            wave.active = false;
            return;
        }

        if (wave.amp == 0)
        {
            wave.active = false;
            return;
        }
        
        wave.active = true;
        
        float width = adjustedSrcRect.w;
        float height = adjustedSrcRect.h;
        const Vec2 &zoom = trans.getScale();
        
        /* The length of the sprite as it appears on screen */
        int visibleLength = height * zoom.y;
        
        if (!visibleLength || !width)
        {
            wave.qArray.resize(0);
            wave.qArray.commit();
            
            return;
        }
        
        /* RMVX does this, and I have no fucking clue why */
        if (wave.amp < 0)
        { 
            float amp = wave.amp;
            
            if (realBitmap != bitmap && realZoomX != trans.getScale().x)
            {
                amp *= realZoomX / trans.getScale().x;
            }
            
            float scaledAmp = amp / zoom.x;
            
            FloatRect tex = mirrored ? adjustedSrcRect.hFlipped() : adjustedSrcRect;
            FloatRect pos(0, 0, 0, adjustedSrcRect.h);
            float mult = (scaledAmp * 2) / srcRect.w;
            pos.x = -scaledAmp - (trans.getSrcRectOrigin().x * mult);
            pos.w = tex.w * (1 + mult);
            
            if ((pos.w * zoom.x) < 0.5f)
            {
                wave.qArray.resize(0);
                wave.qArray.commit();
                
                return;
            }
            
            if (bitmap->hasHires())
            {
                Vec2 bmSize = Vec2(bitmap->width(), bitmap->height());
                Vec2 bmSizeHires = Vec2(bitmap->getHires()->width(), bitmap->getHires()->height());
                if (bmSizeHires.x && bmSizeHires.y && bmSize.x && bmSize.y)
                {
                    tex.x *= bmSizeHires.x / bmSize.x;
                    tex.y *= bmSizeHires.y / bmSize.y;
                    tex.w *= bmSizeHires.x / bmSize.x;
                    tex.h *= bmSizeHires.y / bmSize.y;
                }
            }
            wave.qArray.resize(1);
            Quad::setTexPosRect(&wave.qArray.vertices[0], tex, pos);
            wave.qArray.commit();
            
            return;
        }
        
        /* A negative position in the srcRect affects the wave position. */
        int offsetLength = abs(trans.getSrcRectOrigin().y * zoom.y);
        
        /* First chunk length (aligned to 8 pixel boundary) */
        int posY = (int) trans.getPosition().y - (int) (trans.getOrigin().y * zoom.y) + trans.getGlobalOffset().y;
        int firstLength = 8 - ((posY + offsetLength) % 8);
        firstLength = std::min(firstLength % 8, visibleLength);
        
        /* If the position is negative, then the first chunk's alignment
         * needs a little more fiddling */
        int firstOffset;
        if (offsetLength && firstLength)
        {
            firstOffset = 8 - (posY % 8);
            firstOffset = std::min(firstOffset % 8, offsetLength);
            firstOffset += (offsetLength - firstOffset) & (~7);
        } else {
            firstOffset = 0;
        }
        
        /* Amount of full 8 pixel chunks in the middle */
        int chunks = (visibleLength - firstLength) / 8;
        
        /* Final chunk length */
        int lastLength = (visibleLength - firstLength) % 8;
        
        wave.qArray.resize(!!firstLength + chunks + !!lastLength);
        SVertex *vert = &wave.qArray.vertices[0];
        
        float phase = (wave.phase * (float) M_PI) / 180.0f;
        
        if (firstLength > 0)
            emitWaveChunk(vert, phase, width, zoom, 0, firstLength, firstOffset);
        
        for (int i = 0; i < chunks; ++i)
            emitWaveChunk(vert, phase, width, zoom, firstLength + i * 8, 8, offsetLength);
        
        if (lastLength > 0)
            emitWaveChunk(vert, phase, width, zoom, firstLength + chunks * 8, lastLength, offsetLength);
        
        wave.qArray.commit();
    }
    
    void prepare()
    {
        // Skip preparations and drawing if the bitmap is disposed or the sprite or viewport is invisible
        if (nullOrDisposed(realBitmap) || !(*spriteVisible) || (viewport && !viewport->getVisible()))
        {
            isVisible = false;
            return;
        }
        
        // Wave state influences updateVisibility, so we have to updateWave first.
        if (wave.dirty)
            updateWave();

        updateChild();

        updatePerspective();

        updateVisibility();
        
        if (!isVisible)
            return;
        
        if (bushDirty)
            recomputeBushDepth();
    }
};

Sprite::Sprite(Viewport *viewport)
: ViewportElement(viewport)
{
    p = new SpritePrivate;
    p->spriteVisible = &visible;
    p->viewport = viewport;
    onGeometryChange(scene->getGeometry());
}

Sprite::~Sprite()
{
    dispose();
}

DEF_ATTR_RD_SIMPLE(Sprite, Bitmap,     Bitmap*, p->realBitmap)
DEF_ATTR_RD_SIMPLE(Sprite, X,          float,   p->trans.getPosition().x)
DEF_ATTR_RD_SIMPLE(Sprite, Y,          float,   p->trans.getPosition().y)
DEF_ATTR_RD_SIMPLE(Sprite, OX,         float,   p->realOX)
DEF_ATTR_RD_SIMPLE(Sprite, OY,         float,   p->realOY)
DEF_ATTR_RD_SIMPLE(Sprite, ZoomX,      float,   p->realZoomX)
DEF_ATTR_RD_SIMPLE(Sprite, ZoomY,      float,   p->realZoomY)
DEF_ATTR_RD_SIMPLE(Sprite, Angle,      float,   p->trans.getRotation())
DEF_ATTR_RD_SIMPLE(Sprite, Mirror,     bool,    p->mirrored)
DEF_ATTR_RD_SIMPLE(Sprite, BushDepth,  int,     p->bushDepth)
DEF_ATTR_RD_SIMPLE(Sprite, BlendType,  int,     p->blendType)
DEF_ATTR_RD_SIMPLE(Sprite, Pattern,    Bitmap*, p->pattern)
DEF_ATTR_RD_SIMPLE(Sprite, PatternBlendType, int, p->patternBlendType)
DEF_ATTR_RD_SIMPLE(Sprite, Width,      int,     p->realSrcRect->width)
DEF_ATTR_RD_SIMPLE(Sprite, Height,     int,     p->realSrcRect->height)
DEF_ATTR_RD_SIMPLE(Sprite, WaveAmp,    int,     p->wave.amp)
DEF_ATTR_RD_SIMPLE(Sprite, WaveLength, int,     p->wave.length)
DEF_ATTR_RD_SIMPLE(Sprite, WaveSpeed,  int,     p->wave.speed)
DEF_ATTR_RD_SIMPLE(Sprite, WavePhase,  float,   p->wave.phase)

DEF_ATTR_SIMPLE(Sprite, BushOpacity, int,     p->bushOpacity)
DEF_ATTR_SIMPLE(Sprite, Opacity,     int,     p->opacity)
DEF_ATTR_SIMPLE(Sprite, SrcRect,     Rect&,  *p->realSrcRect)
DEF_ATTR_SIMPLE(Sprite, Color,       Color&, *p->color)
DEF_ATTR_SIMPLE(Sprite, Tone,        Tone&,  *p->tone)
DEF_ATTR_SIMPLE(Sprite, PatternTile, bool, p->patternTile)
DEF_ATTR_SIMPLE(Sprite, PatternOpacity, int, p->patternOpacity)
DEF_ATTR_SIMPLE(Sprite, PatternScrollX, int, p->patternScroll.x)
DEF_ATTR_SIMPLE(Sprite, PatternScrollY, int, p->patternScroll.y)
DEF_ATTR_SIMPLE(Sprite, PatternZoomX, float, p->patternZoom.x)
DEF_ATTR_SIMPLE(Sprite, PatternZoomY, float, p->patternZoom.y)
DEF_ATTR_SIMPLE(Sprite, Invert,      bool,    p->invert)
DEF_ATTR_SIMPLE(Sprite, Shader,      CustomShader*, p->shader)
DEF_ATTR_SIMPLE(Sprite, Shaders,     std::vector<CustomShader*>&, p->shaders)

void Sprite::setBitmap(Bitmap *bitmap)
{
    guardDisposed();
    
    if (p->realBitmap == bitmap)
        return;
    
    if (p->bitmap != p->realBitmap)
        delete p->bitmap;
    
    p->bitmap = bitmap;
    p->realBitmap = bitmap;
    
    p->bitmapDispCon.disconnect();
    
    if (nullOrDisposed(bitmap))
    {
        p->realBitmap = p->bitmap = 0;
        return;
    }
    
    p->bitmapDispCon = bitmap->wasDisposed.connect(&SpritePrivate::bitmapDisposal, p);
    
    if (bitmap->isMega())
    {
        p->bitmap = bitmap->spawnChild();
        p->srcRect = p->bitmap->rect();
    }
    
    *p->realSrcRect = p->realBitmap->rect();
    p->onSrcRectChange();
    p->updateSrcRectCon();
}

void Sprite::setX(float value)
{
    guardDisposed();

    if (p->trans.getPosition().x == value)
        return;

    p->trans.setPosition(Vec2(value, getY()));
}

void Sprite::setY(float value)
{
    guardDisposed();

    if (p->trans.getPosition().y == value)
        return;

    p->trans.setPosition(Vec2(getX(), value));

    if (p->wave.active)
        p->wave.dirty = true;

    if (rgssVer >= 2)
        setSpriteY(lroundf(value));
}

void Sprite::setOX(float value)
{
    guardDisposed();

    if (p->realOX == value)
        return;

    p->realOX = value;
    p->trans.setOrigin(Vec2(value, getOY()));
}

void Sprite::setOY(float value)
{
    guardDisposed();

    if (p->realOY == value)
        return;

    p->realOY = value;
    p->trans.setOrigin(Vec2(getOX(), value));

    if (p->wave.active)
        p->wave.dirty = true;
}

void Sprite::setZoomX(float value)
{
    guardDisposed();
    
    if (p->realZoomX == value)
        return;
    
    // RGSS lets you set the zoom below 0, but it doesn't render it
    p->realZoomX = value;
    p->trans.setScale(Vec2(std::max(value, 0.0f), std::max(getZoomY(), 0.0f)));
    
    if (p->wave.active)
        p->wave.dirty = true;
}

void Sprite::setZoomY(float value)
{
    guardDisposed();
    
    if (p->realZoomY == value)
        return;
    
    // RGSS lets you set the zoom below 0, but it doesn't render it
    p->trans.setScale(Vec2(std::max(getZoomX(), 0.0f), std::max(value, 0.0f)));
    p->bushDirty = true;
        
    p->realZoomY = value;
    if (p->wave.active)
        p->wave.dirty = true;
}

void Sprite::setAngle(float value)
{
    guardDisposed();
    
    if (p->trans.getRotation() == value)
        return;
    
    p->trans.setRotation(value);
    
    p->bushDirty = true;
}

void Sprite::setMirror(bool mirrored)
{
    guardDisposed();
    
    if (p->mirrored == mirrored)
        return;
    
    p->mirrored = mirrored;
    p->onSrcRectChange();
    
    if (p->wave.active)
        p->wave.dirty = true;
}

void Sprite::setBushDepth(int value)
{
    guardDisposed();
    
    if (p->bushDepth == value)
        return;
    
    p->bushDepth = value;
    p->bushDirty = true;
}

void Sprite::setBlendType(int type)
{
    guardDisposed();
    
    switch (type)
    {
        default :
        case BlendNormal :
            p->blendType = BlendNormal;
            return;
        case BlendAddition :
            p->blendType = BlendAddition;
            return;
        case BlendSubstraction :
            p->blendType = BlendSubstraction;
            return;
    }
}

void Sprite::setPattern(Bitmap *value)
{
    guardDisposed();
    
    if (p->pattern == value)
        return;
    
    p->pattern = value;
    
    if (!nullOrDisposed(value))
    {
        value->ensureNonMega();
    }
}

void Sprite::setPatternBlendType(int type)
{
    guardDisposed();
    
    switch (type)
    {
        default :
        case BlendNormal :
            p->patternBlendType = BlendNormal;
            return;
        case BlendAddition :
            p->patternBlendType = BlendAddition;
            return;
        case BlendSubstraction :
            p->patternBlendType = BlendSubstraction;
            return;
    }
}

#define DEF_WAVE_SETTER(Name, name, type) \
void Sprite::setWave##Name(type value) \
{ \
guardDisposed(); \
if (p->wave.name == value) \
return; \
p->wave.name = value; \
p->wave.dirty = true; \
}

DEF_WAVE_SETTER(Amp,    amp,    int)
DEF_WAVE_SETTER(Length, length, int)
DEF_WAVE_SETTER(Speed,  speed,  int)

#undef DEF_WAVE_SETTER

void Sprite::setWavePhase(float value)
{
	if (p->wave.phase == value)
		return;
	p->wave.phase = fwrap(value, 360.0f);
	p->wave.dirty = true;
}

void Sprite::setCorners(const Vec2 (&pts)[4])
{
    guardDisposed();

    for (int i = 0; i < 4; ++i)
        p->corners.pts[i] = pts[i];

    p->corners.active = true;
    p->quad.setPosQuad(p->corners.pts);

    /* Corners win over the wave effect while active */
    p->wave.dirty = true;
}

void Sprite::clearCorners()
{
    guardDisposed();

    if (!p->corners.active)
        return;

    p->corners.active = false;
    /* Restore the normal local rect */
    p->quad.setPosRect(FloatRect(0, 0, p->srcRect.w, p->srcRect.h));

    /* Wave resumes naturally on its usual triggers */
    p->wave.dirty = true;
}

DEF_ATTR_SIMPLE(Sprite, Perspective, bool,  p->persp.enabled)
DEF_ATTR_SIMPLE(Sprite, Lift,        float, p->persp.lift)
DEF_ATTR_SIMPLE(Sprite, ScaleBoost,  float, p->persp.boost)

void Sprite::setClosenessLift(float value)
{
    guardDisposed();

    p->persp.closenessLift = value;
    p->persp.hasClosenessLift = true;
}

void Sprite::clearClosenessLift()
{
    guardDisposed();

    p->persp.hasClosenessLift = false;
}

bool Sprite::hasClosenessLift() const
{
    guardDisposed();

    return p->persp.hasClosenessLift;
}

float Sprite::getClosenessLift() const
{
    guardDisposed();

    return p->persp.closenessLift;
}

void Sprite::setCornerLifts(const float (&lifts)[4])
{
    guardDisposed();

    for (int i = 0; i < 4; ++i)
        p->persp.cornerLifts[i] = lifts[i];

    p->persp.hasCornerLifts = true;
}

void Sprite::clearCornerLifts()
{
    guardDisposed();

    p->persp.hasCornerLifts = false;
}

bool Sprite::hasCornerLifts() const
{
    guardDisposed();

    return p->persp.hasCornerLifts;
}

void Sprite::getCornerLifts(float (&out)[4]) const
{
    guardDisposed();

    for (int i = 0; i < 4; ++i)
        out[i] = p->persp.cornerLifts[i];
}

void Sprite::initDynAttribs()
{
    p->realSrcRect = new Rect;
    p->color = new Color;
    p->tone = new Tone;
    
    p->updateSrcRectCon();
}

/* Flashable */
void Sprite::update()
{
    guardDisposed();
    
    Flashable::update();
    
    if (p->wave.speed != 0)
    {
        p->wave.phase += p->wave.speed / 180;
        p->wave.phase = fwrap(p->wave.phase, 360.0f);
        p->wave.dirty = true;
    }
}

/* ---- Sprite batching (see gl/spritebatch.h) ------------------------------ */

namespace {

enum BatchShader { BatchSimple, BatchAlpha, BatchEffect };

inline bool sameVec4(const Vec4 &a, const Vec4 &b)
{
	return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

/* Anything differing here needs a re-bind. */
struct BatchKey
{
	BatchShader shader;
	Bitmap *bitmap;
	BlendType blend;
	Vec4 tone;
	Vec4 color;
	float opacity;
	bool invert;
	bool bushY, bushUnder;
	float bushSlope, bushIntercept;
	float bushOpacity;

	bool matches(const BatchKey &o) const
	{
		return shader == o.shader && bitmap == o.bitmap && blend == o.blend &&
		       opacity == o.opacity && invert == o.invert &&
		       bushY == o.bushY && bushUnder == o.bushUnder &&
		       bushSlope == o.bushSlope && bushIntercept == o.bushIntercept &&
		       bushOpacity == o.bushOpacity &&
		       sameVec4(tone, o.tone) && sameVec4(color, o.color);
	}
};

/* State the current run has already established. */
struct RunState
{
	bool valid;
	BatchKey key;

	RunState() : valid(false) {}
};

RunState run;

/* Sets whatever the run has not set yet; spriteMat always differs per sprite. */
void bindRunState(const BatchKey &key, const float *spriteMat)
{
	const bool fresh = !run.valid || !run.key.matches(key);

	switch (key.shader)
	{
	case BatchEffect:
	{
		SpriteShader &shader = shState->shaders().sprite;
		shader.bind();
		if (fresh)
		{
			shader.applyViewportProj();
			shader.setTone(key.tone);
			shader.setOpacity(key.opacity);
			shader.setBushDepth(key.bushY, key.bushUnder,
			                    key.bushSlope, key.bushIntercept);
			shader.setBushOpacity(key.bushOpacity);
			shader.setShouldRenderPattern(false);
			shader.setInvert(key.invert);
			shader.setColor(key.color);
		}
		shader.setSpriteMat(spriteMat);
		if (fresh)
		{
			key.bitmap->bindTex(shader, false);
			TEX::setSmooth(false);
		}
		run.valid = true;
		run.key   = key;
		return;
	}
	case BatchAlpha:
	{
		AlphaSpriteShader &shader = shState->shaders().alphaSprite;
		shader.bind();
		if (fresh)
		{
			shader.applyViewportProj();
			shader.setAlpha(key.opacity);
		}
		shader.setSpriteMat(spriteMat);
		if (fresh)
		{
			key.bitmap->bindTex(shader, false);
			TEX::setSmooth(false);
		}
		run.valid = true;
		run.key   = key;
		return;
	}
	default:
	{
		SimpleSpriteShader &shader = shState->shaders().simpleSprite;
		shader.bind();
		if (fresh)
			shader.applyViewportProj();
		shader.setSpriteMat(spriteMat);
		if (fresh)
		{
			key.bitmap->bindTex(shader, false);
			TEX::setSmooth(false);
		}
		run.valid = true;
		run.key   = key;
		return;
	}
	}
}

} // anonymous namespace

void SpriteBatch::flush()
{
	run.valid = false;
}

/* Conservative hint for Scene::composite; draw() is authoritative. */
bool Sprite::batchable() const
{
	if (!p->isVisible || emptyFlashFlag)
		return false;

	if (p->shader && !p->shader->isDisposed())
		return false;

	for (size_t i = 0; i < p->shaders.size(); ++i)
		if (p->shaders[i] && !p->shaders[i]->isDisposed())
			return false;

	return !p->wave.active && p->bushDepth == 0 && !flashing &&
	       !(p->pattern && !p->pattern->isDisposed());
}

/* SceneElement */
void Sprite::draw()
{
    if (!p->isVisible)
        return;

    if (emptyFlashFlag)
        return;

    ShaderBase *base;

    /* While corners are active the model matrix carries the scene global
     * offset only, so the quad's viewport-space corner coordinates map
     * straight through (no position/origin/zoom/rotation). */
    const float *spriteMat;
    if (p->corners.active || p->persp.quadActive)
        spriteMat = p->offsetTrans.getMatrix();
    else if (p->persp.projected)
        spriteMat = p->persp.trans.getMatrix();
    else
        spriteMat = p->trans.getMatrix();

    // Check for custom shaders (both single and multiple)
    bool hasCustomShader = p->shader && !p->shader->isDisposed();

    // Count valid shaders in the vector
    int validShaderCount = 0;
    for (size_t i = 0; i < p->shaders.size(); ++i)
    {
        if (p->shaders[i] && !p->shaders[i]->isDisposed())
            validShaderCount++;
    }

    bool hasAnyCustomShader = (validShaderCount > 0) || hasCustomShader;

    /* Custom shaders handle tone/color internally via injected uniforms,
     * so render them directly in a single pass */
    if (hasAnyCustomShader)
    {
        SpriteBatch::flush();

        /* When both flashing and effective color are set,
         * the one with higher alpha will be blended */
        const Vec4 *blend = (flashing && flashColor.w > p->color->norm.w) ?
        &flashColor : &p->color->norm;

        auto drawCustomShader = [&](CustomShader *customShader)
        {
            CustomSpriteShaderImpl *shader = customShader->getSpriteShader();
            shader->bind();
            shader->applyViewportProj();
            shader->setSpriteMat(spriteMat);
            shader->setTexSize(Vec2i(p->bitmap->width(), p->bitmap->height()));
            shader->setTime(SDL_GetTicks() / 1000.0f);
            shader->setOpacity(p->opacity.norm);
            shader->setTone(p->tone->norm);
            shader->setColor(*blend);
            shader->setInvert(p->invert);
            shader->setBushDepth(p->bushY, p->bushUnder, p->bushSlope, p->bushIntercept);
            shader->setBushOpacity(p->bushOpacity.norm);

            /* If the user's shader declares standard uniform names
             * (opacity, tone, color), set them with the real values
             * and neutralize the suffix's _mkxp_* uniforms so the
             * effect isn't applied twice. */
            if (shader->hasUserOpacity())
            {
                shader->setStdOpacity(p->opacity.norm);
                shader->setOpacity(1.0f);
            }
            if (shader->hasUserTone())
            {
                shader->setStdTone(p->tone->norm);
                shader->setTone(Vec4());
            }
            if (shader->hasUserColor())
            {
                shader->setStdColor(*blend);
                shader->setColor(Vec4());
            }

            shader->applyUniforms(customShader->getUniforms());
            shader->applyBitmaps(customShader->getBitmaps(), 1);

            base = shader;

            glState.blendMode.pushSet(p->blendType);
            p->bitmap->bindTex(*base, false);

            /* Corners win over the wave effect while active */
            if (p->wave.active && !p->corners.active && !p->persp.quadActive)
                p->wave.qArray.draw();
            else
                p->quad.draw();

            glState.blendMode.pop();
        };

        if (validShaderCount > 0)
        {
            for (size_t i = 0; i < p->shaders.size(); ++i)
            {
                if (p->shaders[i] && !p->shaders[i]->isDisposed())
                    drawCustomShader(p->shaders[i]);
            }
        }
        else if (hasCustomShader)
        {
            drawCustomShader(p->shader);
        }
        return;
    }

    bool renderEffect = p->color->hasEffect() ||
    p->tone->hasEffect()  ||
    flashing              ||
    p->bushDepth != 0     ||
    p->invert             ||
    (p->pattern && !p->pattern->isDisposed());

    int scalingMethod = NearestNeighbor;

    int sourceWidthHires = p->bitmap->hasHires() ? p->bitmap->getHires()->width() : p->bitmap->width();
    int sourceHeightHires = p->bitmap->hasHires() ? p->bitmap->getHires()->height() : p->bitmap->height();

    double framebufferScalingFactor = shState->config().enableHires ? shState->config().framebufferScalingFactor : 1.0;

    int targetWidthHires = (int)lround(framebufferScalingFactor * p->bitmap->width() * p->trans.getScale().x);
    int targetHeightHires = (int)lround(framebufferScalingFactor * p->bitmap->height() * p->trans.getScale().y);

    int scaleIsSpecial = UpScale;

    if (targetWidthHires == sourceWidthHires && targetHeightHires == sourceHeightHires)
    {
        scaleIsSpecial = SameScale;
    }

    if (targetWidthHires < sourceWidthHires && targetHeightHires < sourceHeightHires)
    {
        scaleIsSpecial = DownScale;
    }

    switch (scaleIsSpecial)
    {
    case SameScale:
        scalingMethod = NearestNeighbor;
        break;
    case DownScale:
        scalingMethod = shState->config().bitmapSmoothScalingDown;
        break;
    default:
        scalingMethod = shState->config().bitmapSmoothScaling;
    }

    if (p->trans.getRotation() != 0.0)
    {
        scalingMethod = shState->config().bitmapSmoothScaling;
    }

    /* Run path: reuse the state a matching predecessor already set. Wave, bush,
     * flash, pattern and smooth scaling all need per-sprite state, so they opt out. */
    if (!p->wave.active && p->bushDepth == 0 && !flashing &&
        !(p->pattern && !p->pattern->isDisposed()) &&
        scalingMethod == NearestNeighbor)
    {
        BatchKey key;
        key.shader        = renderEffect ? BatchEffect
                          : (p->opacity != 255 ? BatchAlpha : BatchSimple);
        key.bitmap        = p->bitmap;
        key.blend         = p->blendType;
        key.tone          = p->tone->norm;
        key.color         = p->color->norm;
        key.opacity       = p->opacity.norm;
        key.invert        = p->invert;
        key.bushY         = p->bushY;
        key.bushUnder     = p->bushUnder;
        key.bushSlope     = p->bushSlope;
        key.bushIntercept = p->bushIntercept;
        key.bushOpacity   = p->bushOpacity.norm;

        bindRunState(key, spriteMat);

        glState.blendMode.pushSet(p->blendType);
        p->quad.draw();
        glState.blendMode.pop();
        return;
    }

    SpriteBatch::flush();

    if (renderEffect)
    {
        if (scalingMethod != NearestNeighbor)
        {
            Debug() << "BUG: Smooth SpriteShader not implemented:" << scalingMethod;
            scalingMethod = NearestNeighbor;
        }

        SpriteShader &shader = shState->shaders().sprite;

        shader.bind();
        shader.applyViewportProj();
        shader.setSpriteMat(spriteMat);

        shader.setTone(p->tone->norm);
        shader.setOpacity(p->opacity.norm);
        shader.setBushDepth(p->bushY, p->bushUnder, p->bushSlope, p->bushIntercept);
        shader.setBushOpacity(p->bushOpacity.norm);

        if (p->pattern && p->patternOpacity > 0) {
            if (p->pattern->hasHires()) {
                Debug() << "BUG: High-res Sprite pattern not implemented";
            }

            shader.setPattern(p->pattern->getGLTypes().tex, Vec2(p->pattern->width(), p->pattern->height()));
            shader.setPatternBlendType(p->patternBlendType);
            shader.setPatternTile(p->patternTile);
            shader.setPatternZoom(p->patternZoom);
            shader.setPatternOpacity(p->patternOpacity.norm);
            shader.setPatternScroll(p->patternScroll);
            shader.setShouldRenderPattern(true);
        }
        else {
            shader.setShouldRenderPattern(false);
        }

        shader.setInvert(p->invert);

        /* When both flashing and effective color are set,
         * the one with higher alpha will be blended */
        const Vec4 *blend = (flashing && flashColor.w > p->color->norm.w) ?
        &flashColor : &p->color->norm;

        shader.setColor(*blend);

        base = &shader;
    }
    else if (p->opacity != 255)
    {
        if (scalingMethod != NearestNeighbor)
        {
            Debug() << "BUG: Smooth AlphaSpriteShader not implemented:" << scalingMethod;
            scalingMethod = NearestNeighbor;
        }

        AlphaSpriteShader &shader = shState->shaders().alphaSprite;
        shader.bind();

        shader.setSpriteMat(spriteMat);
        shader.setAlpha(p->opacity.norm);
        shader.applyViewportProj();
        base = &shader;
    }
    else
    {
        switch (scalingMethod)
        {
        case Bicubic:
        {
            BicubicSpriteShader &shader = shState->shaders().bicubicSprite;
            shader.bind();

            shader.setTexSize(Vec2i(sourceWidthHires, sourceHeightHires));
            shader.setSharpness(shState->config().bicubicSharpness);
            shader.setSpriteMat(spriteMat);
            shader.applyViewportProj();
            base = &shader;
        }
            break;
        case Lanczos3:
        {
            Lanczos3SpriteShader &shader = shState->shaders().lanczos3Sprite;
            shader.bind();

            shader.setTexSize(Vec2i(sourceWidthHires, sourceHeightHires));
            shader.setSpriteMat(spriteMat);
            shader.applyViewportProj();
            base = &shader;
        }
            break;
#ifdef MKXPZ_SSL
        case xBRZ:
        {
            XbrzSpriteShader &shader = shState->shaders().xbrzSprite;
            shader.bind();

            shader.setTexSize(Vec2i(sourceWidthHires, sourceHeightHires));
            shader.setTargetScale(Vec2((float)(shState->config().xbrzScalingFactor), (float)(shState->config().xbrzScalingFactor)));
            shader.setSpriteMat(spriteMat);
            shader.applyViewportProj();
            base = &shader;
        }
            break;
#endif
        default:
        {
            SimpleSpriteShader &shader = shState->shaders().simpleSprite;
            shader.bind();

            shader.setSpriteMat(spriteMat);
            shader.applyViewportProj();
            base = &shader;
        }
        }
    }

    glState.blendMode.pushSet(p->blendType);

    p->bitmap->bindTex(*base, false);

#ifdef MKXPZ_SSL
    if (scalingMethod == xBRZ)
    {
        XbrzShader &shader = shState->shaders().xbrz;
        shader.setTargetScale(Vec2((float)(shState->config().xbrzScalingFactor), (float)(shState->config().xbrzScalingFactor)));
    }
#endif

    TEX::setSmooth(scalingMethod == Bilinear);

    /* Corners win over the wave effect while active */
    if (p->wave.active && !p->corners.active && !p->persp.quadActive)
        p->wave.qArray.draw();
    else
        p->quad.draw();

    TEX::setSmooth(false);

    glState.blendMode.pop();
}

void Sprite::onGeometryChange(const Scene::Geometry &geo)
{
    /* Offset at which the sprite will be drawn
     * relative to screen origin */
    const Vec2i &offset = geo.offset();
    if (p->wave.active && p->trans.getGlobalOffset().y != offset.y)
        p->wave.dirty = true;
    p->trans.setGlobalOffset(offset);
    /* offsetTrans mirrors only the global offset (identity model otherwise),
     * so cornered sprites render in viewport-space px. */
    p->offsetTrans.setGlobalOffset(offset);

    p->sceneGeo = geo;
    p->viewport = getViewport();
}

void Sprite::releaseResources()
{
    unlink();
    
    delete p;
}
