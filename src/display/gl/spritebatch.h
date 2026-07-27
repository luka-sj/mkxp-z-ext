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

/* Skips GL state that a run of similar sprites has already established.
 * Nothing is deferred, so draw order is unchanged. Implementation in
 * sprite.cpp. */
namespace SpriteBatch
{
	/* Drops the assumption that the last run's state is still bound. Must be
	 * called before anything that draws by another route. */
	void flush();
}

#endif // SPRITEBATCH_H
