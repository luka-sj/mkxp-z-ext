/*
** spritebatch.h
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

#ifndef SPRITEBATCH_H
#define SPRITEBATCH_H

/* Coalesces runs of compatible sprites into a single draw call.
 *
 * Every Sprite is otherwise its own draw: a program bind, a spriteMat and
 * viewport-projection upload, a texture bind, two glTexParameteri pairs and a
 * VAO bind/glDrawElements. A map screen submits several hundred of those, and
 * the great majority share a texture and differ only in position.
 *
 * Sprites whose draw state matches the open batch append their four
 * CPU-transformed vertices instead of drawing; the run is emitted by one
 * glDrawElements when something incompatible comes along. Because the vertices
 * arrive pre-transformed the batch draws under an identity spriteMat.
 *
 * ORDERING CONTRACT — the batch defers drawing, so anything that draws by any
 * other route MUST flush first or it will appear underneath the deferred run:
 *
 *   - Scene::composite() flushes before every element that is not batchable,
 *     and again after the element loop (which is still inside the viewport's
 *     scissor push, so a batch never leaks across a viewport boundary).
 *   - Sprite::draw() flushes before taking any non-batched path.
 *
 * SceneElement::batchable() must therefore stay in agreement with the branch
 * Sprite::draw() actually takes; Sprite::draw() re-checks and flushes rather
 * than trusting it.
 *
 * The implementation lives in sprite.cpp, next to the draw path whose state it
 * has to mirror.
 */
namespace SpriteBatch
{
	/* Emits the open run, if any. Safe to call when nothing is open. */
	void flush();
}

#endif // SPRITEBATCH_H
