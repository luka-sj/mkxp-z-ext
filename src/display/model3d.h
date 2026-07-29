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
class CustomShader;
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

	/* Camera look target. Unset, the camera aims at the bbox centre; setting
	 * any coordinate pins it so the picture plane can be held fixed. */
	float getCameraTargetX() const;
	void setCameraTargetX(float value);
	float getCameraTargetY() const;
	void setCameraTargetY(float value);
	float getCameraTargetZ() const;
	void setCameraTargetZ(float value);

	/* Projection window shift in output px: the direction that would land at
	 * (+shift_x, +shift_y) of the image centre renders centred instead. */
	float getCameraShiftX() const;
	void setCameraShiftX(float value);
	float getCameraShiftY() const;
	void setCameraShiftY(float value);

	/* Lighting */
	float getLightX() const;
	void setLightX(float value);
	float getLightY() const;
	void setLightY(float value);
	float getLightZ() const;
	void setLightZ(float value);
	float getAmbient() const;
	void setAmbient(float value);

	/* Mesh bounds in model units (read-only). The ortho view volume is sized
	 * from the radius, so these make px-per-model-unit computable. */
	float getBBoxRadius() const;
	float getBBoxWidth() const;
	float getBBoxHeight() const;
	float getBBoxDepth() const;

	/* Custom shader */
	CustomShader *getShader() const;
	void setShader(CustomShader *shader);

	/* Render the model to a Bitmap */
	Bitmap *render(int width, int height);

private:
	void releaseResources();
	const char *klassName() const { return "Model3D"; }

	Model3DPrivate *p;
};

#endif // MODEL3D_H
