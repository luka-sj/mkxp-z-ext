/*
** model3d.h
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

#ifndef MODEL3D_H
#define MODEL3D_H

#include "util/disposable.h"

class Bitmap;
struct Model3DPrivate;

class Model3D : public Disposable
{
public:
	Model3D(const char *filename);
	~Model3D();

	/* Transform */
	float getRotationX() const;
	void setRotationX(float value);
	float getRotationY() const;
	void setRotationY(float value);
	float getRotationZ() const;
	void setRotationZ(float value);
	float getScale() const;
	void setScale(float value);
	float getPositionX() const;
	void setPositionX(float value);
	float getPositionY() const;
	void setPositionY(float value);
	float getPositionZ() const;
	void setPositionZ(float value);

	/* Camera */
	float getCameraFOV() const;
	void setCameraFOV(float value);
	float getCameraX() const;
	void setCameraX(float value);
	float getCameraY() const;
	void setCameraY(float value);
	float getCameraZ() const;
	void setCameraZ(float value);

	/* Lighting */
	float getLightX() const;
	void setLightX(float value);
	float getLightY() const;
	void setLightY(float value);
	float getLightZ() const;
	void setLightZ(float value);
	float getAmbient() const;
	void setAmbient(float value);

	/* Render the model to a Bitmap */
	Bitmap *render(int width, int height);

private:
	void releaseResources();
	const char *klassName() const { return "Model3D"; }

	Model3DPrivate *p;
};

#endif // MODEL3D_H
