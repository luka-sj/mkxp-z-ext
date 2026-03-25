/*
** model3d.cpp
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

#include "model3d.h"
#include "bitmap.h"
#include "sharedstate.h"
#include "debugwriter.h"
#include "filesystem/filesystem.h"
#include "util/exception.h"
#include "gl/gl-fun.h"
#include "gl/gl-meta.h"
#include "gl/gl-util.h"
#include "gl/glstate.h"
#include "gl/shader.h"

#include "tinyobjloader/tiny_obj_loader.h"

#include <string>
#include <vector>
#include <cstring>
#include <cmath>
#include <sstream>
#include <map>

#include <SDL_image.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/*  Minimal 4x4 / 3x3 matrix math                                    */
/* ------------------------------------------------------------------ */

static void mat4_identity(float m[16])
{
	memset(m, 0, sizeof(float) * 16);
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4_multiply(float out[16], const float a[16], const float b[16])
{
	float tmp[16];
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
		{
			tmp[c * 4 + r] = 0;
			for (int k = 0; k < 4; ++k)
				tmp[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
		}
	memcpy(out, tmp, sizeof(float) * 16);
}

static void mat4_perspective(float out[16], float fovDeg, float aspect,
                             float zNear, float zFar)
{
	memset(out, 0, sizeof(float) * 16);
	float f = 1.0f / tanf(fovDeg * (float)M_PI / 360.0f);
	out[0]  = f / aspect;
	out[5]  = f;
	out[10] = (zFar + zNear) / (zNear - zFar);
	out[11] = -1.0f;
	out[14] = (2.0f * zFar * zNear) / (zNear - zFar);
}

static void mat4_lookAt(float out[16],
                        float eyeX, float eyeY, float eyeZ,
                        float atX,  float atY,  float atZ,
                        float upX,  float upY,  float upZ)
{
	float fx = atX - eyeX, fy = atY - eyeY, fz = atZ - eyeZ;
	float len = sqrtf(fx*fx + fy*fy + fz*fz);
	fx /= len; fy /= len; fz /= len;

	/* side = f x up */
	float sx = fy*upZ - fz*upY;
	float sy = fz*upX - fx*upZ;
	float sz = fx*upY - fy*upX;
	len = sqrtf(sx*sx + sy*sy + sz*sz);
	sx /= len; sy /= len; sz /= len;

	/* u = s x f */
	float ux = sy*fz - sz*fy;
	float uy = sz*fx - sx*fz;
	float uz = sx*fy - sy*fx;

	mat4_identity(out);
	out[0] = sx;  out[4] = sy;  out[8]  = sz;
	out[1] = ux;  out[5] = uy;  out[9]  = uz;
	out[2] = -fx; out[6] = -fy; out[10] = -fz;
	out[12] = -(sx*eyeX + sy*eyeY + sz*eyeZ);
	out[13] = -(ux*eyeX + uy*eyeY + uz*eyeZ);
	out[14] =  (fx*eyeX + fy*eyeY + fz*eyeZ);
}

static void mat4_translate(float out[16], float x, float y, float z)
{
	mat4_identity(out);
	out[12] = x; out[13] = y; out[14] = z;
}

static void mat4_scale(float out[16], float s)
{
	mat4_identity(out);
	out[0] = s; out[5] = s; out[10] = s;
}

static void mat4_rotateX(float out[16], float rad)
{
	mat4_identity(out);
	float c = cosf(rad), s = sinf(rad);
	out[5] = c;  out[6] = s;
	out[9] = -s; out[10] = c;
}

static void mat4_rotateY(float out[16], float rad)
{
	mat4_identity(out);
	float c = cosf(rad), s = sinf(rad);
	out[0] = c;  out[2] = -s;
	out[8] = s;  out[10] = c;
}

static void mat4_rotateZ(float out[16], float rad)
{
	mat4_identity(out);
	float c = cosf(rad), s = sinf(rad);
	out[0] = c;  out[1] = s;
	out[4] = -s; out[5] = c;
}

/* Extract upper-left 3x3 from a 4x4, then compute its inverse-transpose
 * (used for transforming normals). Output is a 3x3 stored as 9 floats. */
static void mat4_normalMat3(float out[9], const float mv[16])
{
	/* Upper-left 3x3 of model-view */
	float a = mv[0], b = mv[4], c = mv[8];
	float d = mv[1], e = mv[5], f = mv[9];
	float g = mv[2], h = mv[6], i = mv[10];

	float det = a*(e*i - f*h) - b*(d*i - f*g) + c*(d*h - e*g);
	if (fabsf(det) < 1e-8f)
	{
		/* Degenerate — fall back to identity */
		memset(out, 0, sizeof(float) * 9);
		out[0] = out[4] = out[8] = 1.0f;
		return;
	}

	float inv = 1.0f / det;
	/* Inverse-transpose: cofactor matrix / det (already transposed layout) */
	out[0] = (e*i - f*h) * inv;
	out[1] = (c*h - b*i) * inv;
	out[2] = (b*f - c*e) * inv;
	out[3] = (f*g - d*i) * inv;
	out[4] = (a*i - c*g) * inv;
	out[5] = (c*d - a*f) * inv;
	out[6] = (d*h - e*g) * inv;
	out[7] = (b*g - a*h) * inv;
	out[8] = (a*e - b*d) * inv;
}

/* ------------------------------------------------------------------ */
/*  Shader source (embedded via xxd at build time)                    */
/* ------------------------------------------------------------------ */

#ifdef MKXPZ_BUILD_XCODE
#include "filesystem/filesystem.h"
#else
#include "model3d.vert.xxd"
#include "model3d.frag.xxd"
#endif

/* ------------------------------------------------------------------ */
/*  Custom material reader for tinyobjloader (reads via PhysFS)       */
/* ------------------------------------------------------------------ */

class PhysFSMaterialReader : public tinyobj::MaterialReader
{
public:
	explicit PhysFSMaterialReader(const std::string &baseDir)
	    : baseDir(baseDir)
	{}

	bool operator()(const std::string &matId,
	                std::vector<tinyobj::material_t> *materials,
	                std::map<std::string, int> *matMap,
	                std::string *warn,
	                std::string *err) override
	{
		std::string path = baseDir + matId;

		SDL_RWops ops;
		try
		{
			shState->fileSystem().openReadRaw(ops, path.c_str(), false);
		}
		catch (const Exception &)
		{
			if (err)
				*err = "Could not open material file: " + path;
			return false;
		}

		Sint64 size = SDL_RWsize(&ops);
		if (size <= 0)
		{
			SDL_RWclose(&ops);
			if (err)
				*err = "Empty material file: " + path;
			return false;
		}

		std::string buf(size, '\0');
		SDL_RWread(&ops, &buf[0], 1, size);
		SDL_RWclose(&ops);

		std::istringstream stream(buf);
		tinyobj::LoadMtl(matMap, materials, &stream, warn, err);
		return true;
	}

private:
	std::string baseDir;
};

/* ------------------------------------------------------------------ */
/*  Private data                                                      */
/* ------------------------------------------------------------------ */

struct MaterialGroup
{
	int startVertex;
	int vertexCount;
	GLuint texGL;     /* GL texture ID (owned, deleted in destructor) */
	bool hasTex;
	float diffuseR, diffuseG, diffuseB, diffuseA;
};

struct Model3DPrivate
{
	/* Transform */
	float rotX, rotY, rotZ;
	float scale;
	float posX, posY, posZ;

	/* Camera */
	float fov;
	float camX, camY, camZ;

	/* Lighting */
	float lightX, lightY, lightZ;
	float ambient;

	/* GPU resources */
	GLuint vbo;
	int vertexCount;

	/* Shader */
	GLuint program, vertShader, fragShader;
	GLint u_modelMat, u_viewMat, u_projMat, u_normalMat;
	GLint u_lightDir, u_ambient;
	GLint u_diffuseTex, u_diffuseColor;

	/* 1x1 white fallback texture for untextured materials */
	GLuint whiteTex;

	/* Material groups */
	std::vector<MaterialGroup> materials;

	/* Bounding box (computed at load time) */
	float bboxMin[3], bboxMax[3];
	float bboxCenter[3];
	float bboxRadius;

	/* Depth renderbuffer (attached to Bitmap FBO at render time) */
	GLuint depthRbo;
	int depthRboW, depthRboH;

	Model3DPrivate()
	    : rotX(0), rotY(0), rotZ(0),
	      scale(1.0f),
	      posX(0), posY(0), posZ(0),
	      fov(45.0f),
	      camX(0), camY(0), camZ(5.0f),
	      lightX(0.5f), lightY(1.0f), lightZ(0.8f),
	      ambient(0.2f),
	      vbo(0), vertexCount(0),
	      program(0), vertShader(0), fragShader(0), whiteTex(0),
	      bboxRadius(1.0f),
	      depthRbo(0), depthRboW(0), depthRboH(0)
	{
		bboxMin[0] = bboxMin[1] = bboxMin[2] = 0;
		bboxMax[0] = bboxMax[1] = bboxMax[2] = 0;
		bboxCenter[0] = bboxCenter[1] = bboxCenter[2] = 0;
	}

	~Model3DPrivate()
	{
		for (size_t i = 0; i < materials.size(); ++i)
			if (materials[i].hasTex)
				gl.DeleteTextures(1, &materials[i].texGL);

		if (vbo)
			gl.DeleteBuffers(1, &vbo);
		if (program)
			gl.DeleteProgram(program);
		if (vertShader)
			gl.DeleteShader(vertShader);
		if (fragShader)
			gl.DeleteShader(fragShader);
		if (whiteTex)
			gl.DeleteTextures(1, &whiteTex);

		if (depthRbo)
			gl.DeleteRenderbuffers(1, &depthRbo);
	}

	void ensureDepthRbo(int w, int h)
	{
		if (depthRboW == w && depthRboH == h && depthRbo)
			return;

		if (depthRbo)
			gl.DeleteRenderbuffers(1, &depthRbo);

		gl.GenRenderbuffers(1, &depthRbo);
		gl.BindRenderbuffer(GL_RENDERBUFFER, depthRbo);
		gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);
		depthRboW = w;
		depthRboH = h;
	}
};

/* ------------------------------------------------------------------ */
/*  Shader compilation helper                                         */
/* ------------------------------------------------------------------ */

static GLuint compileShader(GLenum type, const char *src, int len,
                            const char *name)
{
	GLuint shader = gl.CreateShader(type);

	/* Prepend common header (precision defines) just like setupShaderSource */
	static const char glesDefine[] = "#define GLSLES\n";
	static const char fragDefine[] = "#define FRAGMENT_SHADER\n";

	const GLchar *sources[4];
	GLint lengths[4];
	int count = 0;

	if (gl.glsles)
	{
		sources[count] = glesDefine;
		lengths[count] = sizeof(glesDefine) - 1;
		count++;
	}

	if (type == GL_FRAGMENT_SHADER)
	{
		sources[count] = fragDefine;
		lengths[count] = sizeof(fragDefine) - 1;
		count++;
	}

#ifndef MKXPZ_BUILD_XCODE
	extern const unsigned char ___shader_common_h[];
	extern const unsigned int ___shader_common_h_len;
	sources[count] = (const GLchar *)___shader_common_h;
	lengths[count] = ___shader_common_h_len;
#else
	sources[count] = (const GLchar *)Shader::commonHeader().c_str();
	lengths[count] = Shader::commonHeader().length();
#endif
	count++;

	sources[count] = src;
	lengths[count] = len;
	count++;

	gl.ShaderSource(shader, count, sources, lengths);
	gl.CompileShader(shader);

	GLint success = 0;
	gl.GetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		GLint logLen = 0;
		gl.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
		std::string log(logLen, '\0');
		gl.GetShaderInfoLog(shader, logLen, 0, &log[0]);
		gl.DeleteShader(shader);
		throw Exception(Exception::MKXPError,
		                "Model3D: %s shader compilation failed:\n%s",
		                name, log.c_str());
	}

	return shader;
}

/* ------------------------------------------------------------------ */
/*  Constructor                                                       */
/* ------------------------------------------------------------------ */

Model3D::Model3D(const char *filename)
{
	p = new Model3DPrivate;

	/* ---- Read .obj via PhysFS ---- */
	SDL_RWops ops;
	shState->fileSystem().openReadRaw(ops, filename, false);

	Sint64 size = SDL_RWsize(&ops);
	if (size <= 0)
	{
		SDL_RWclose(&ops);
		delete p;
		throw Exception(Exception::RGSSError,
		                "Failed to read model file '%s'", filename);
	}

	std::string objData(size, '\0');
	SDL_RWread(&ops, &objData[0], 1, size);
	SDL_RWclose(&ops);

	/* Determine base directory for .mtl / texture paths */
	std::string fnStr(filename);
	std::string baseDir;
	size_t slash = fnStr.find_last_of("/\\");
	if (slash != std::string::npos)
		baseDir = fnStr.substr(0, slash + 1);

	/* ---- Parse with tinyobjloader ---- */
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> mats;
	std::string warn, err;

	PhysFSMaterialReader matReader(baseDir);
	std::istringstream objStream(objData);

	bool ok = tinyobj::LoadObj(&attrib, &shapes, &mats, &warn, &err,
	                           &objStream, &matReader);

	if (!warn.empty())
		Debug() << "Model3D [" << filename << "] warning: " << warn.c_str();

	if (!ok)
	{
		delete p;
		throw Exception(Exception::MKXPError,
		                "Model3D: Failed to load '%s': %s",
		                filename, err.c_str());
	}

	/* ---- Build interleaved vertex data, grouped by material ---- */
	/* Map: material_id -> list of floats (pos3 + norm3 + uv2) */
	std::map<int, std::vector<float>> matVerts;

	for (size_t s = 0; s < shapes.size(); ++s)
	{
		const tinyobj::mesh_t &mesh = shapes[s].mesh;
		size_t indexOffset = 0;

		for (size_t f = 0; f < mesh.num_face_vertices.size(); ++f)
		{
			int fv = mesh.num_face_vertices[f];
			int matId = mesh.material_ids[f];

			/* Compute a flat face normal from the first triangle.
			 * Used as fallback when the OBJ normals are missing or NaN. */
			float faceNorm[3] = {0, 1, 0};
			if (fv >= 3)
			{
				tinyobj::index_t i0 = mesh.indices[indexOffset + 0];
				tinyobj::index_t i1 = mesh.indices[indexOffset + 1];
				tinyobj::index_t i2 = mesh.indices[indexOffset + 2];
				float ax = attrib.vertices[3*i0.vertex_index+0];
				float ay = attrib.vertices[3*i0.vertex_index+1];
				float az = attrib.vertices[3*i0.vertex_index+2];
				float bx = attrib.vertices[3*i1.vertex_index+0];
				float by = attrib.vertices[3*i1.vertex_index+1];
				float bz = attrib.vertices[3*i1.vertex_index+2];
				float cx = attrib.vertices[3*i2.vertex_index+0];
				float cy = attrib.vertices[3*i2.vertex_index+1];
				float cz = attrib.vertices[3*i2.vertex_index+2];
				float e1x = bx-ax, e1y = by-ay, e1z = bz-az;
				float e2x = cx-ax, e2y = cy-ay, e2z = cz-az;
				faceNorm[0] = e1y*e2z - e1z*e2y;
				faceNorm[1] = e1z*e2x - e1x*e2z;
				faceNorm[2] = e1x*e2y - e1y*e2x;
				float len = sqrtf(faceNorm[0]*faceNorm[0] +
				                  faceNorm[1]*faceNorm[1] +
				                  faceNorm[2]*faceNorm[2]);
				if (len > 1e-8f)
				{
					faceNorm[0] /= len;
					faceNorm[1] /= len;
					faceNorm[2] /= len;
				}
			}

			for (int v = 0; v < fv; ++v)
			{
				tinyobj::index_t idx = mesh.indices[indexOffset + v];
				std::vector<float> &verts = matVerts[matId];

				/* Position */
				verts.push_back(attrib.vertices[3 * idx.vertex_index + 0]);
				verts.push_back(attrib.vertices[3 * idx.vertex_index + 1]);
				verts.push_back(attrib.vertices[3 * idx.vertex_index + 2]);

				/* Normal — use OBJ normal if valid, else computed face normal */
				bool useObjNormal = false;
				if (idx.normal_index >= 0)
				{
					float nx = attrib.normals[3 * idx.normal_index + 0];
					float ny = attrib.normals[3 * idx.normal_index + 1];
					float nz = attrib.normals[3 * idx.normal_index + 2];
					if (!(nx != nx) && !(ny != ny) && !(nz != nz)) /* !isnan */
					{
						verts.push_back(nx);
						verts.push_back(ny);
						verts.push_back(nz);
						useObjNormal = true;
					}
				}
				if (!useObjNormal)
				{
					verts.push_back(faceNorm[0]);
					verts.push_back(faceNorm[1]);
					verts.push_back(faceNorm[2]);
				}

				/* Texcoord */
				if (idx.texcoord_index >= 0)
				{
					verts.push_back(attrib.texcoords[2 * idx.texcoord_index + 0]);
					verts.push_back(1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]);
				}
				else
				{
					verts.push_back(0); verts.push_back(0);
				}
			}
			indexOffset += fv;
		}
	}

	/* Flatten into a single buffer, recording material groups */
	std::vector<float> allVerts;
	for (auto &kv : matVerts)
	{
		MaterialGroup mg;
		mg.startVertex = (int)(allVerts.size() / 8);
		mg.vertexCount = (int)(kv.second.size() / 8);
		mg.hasTex = false;
		mg.texGL = 0;
		mg.diffuseR = 0.8f;
		mg.diffuseG = 0.8f;
		mg.diffuseB = 0.8f;
		mg.diffuseA = 1.0f;

		int matId = kv.first;
		if (matId >= 0 && matId < (int)mats.size())
		{
			const tinyobj::material_t &mat = mats[matId];
			mg.diffuseR = mat.diffuse[0];
			mg.diffuseG = mat.diffuse[1];
			mg.diffuseB = mat.diffuse[2];
			mg.diffuseA = (mat.dissolve > 0) ? mat.dissolve : 1.0f;

			if (!mat.diffuse_texname.empty())
			{
				std::string texPath = baseDir + mat.diffuse_texname;

				/* Load texture via PhysFS + SDL_image directly,
				 * bypassing Bitmap's path resolution which can
				 * fail with extensions or special characters. */
				SDL_RWops texOps;
				try
				{
					shState->fileSystem().openReadRaw(texOps, texPath.c_str(), false);
				}
				catch (const Exception &)
				{
					Debug() << "Model3D: texture not found: " << texPath.c_str();
					goto skipTex;
				}

				{
					SDL_Surface *surf = IMG_Load_RW(&texOps, 1);
					if (!surf)
					{
						Debug() << "Model3D: IMG_Load_RW failed for: " << texPath.c_str();
						goto skipTex;
					}

					/* Convert to RGBA */
					SDL_Surface *rgba = SDL_ConvertSurfaceFormat(
					    surf, SDL_PIXELFORMAT_ABGR8888, 0);
					SDL_FreeSurface(surf);

					if (!rgba)
					{
						Debug() << "Model3D: surface convert failed for: " << texPath.c_str();
						goto skipTex;
					}

					GLuint tex;
					gl.GenTextures(1, &tex);
					gl.BindTexture(GL_TEXTURE_2D, tex);
					gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
					              rgba->w, rgba->h, 0,
					              GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
					gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
					gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
					gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
					gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

					mg.texGL = tex;
					mg.hasTex = true;

					SDL_FreeSurface(rgba);
				}
				skipTex:;
			}
		}

		p->materials.push_back(mg);
		allVerts.insert(allVerts.end(), kv.second.begin(), kv.second.end());
	}

	p->vertexCount = (int)(allVerts.size() / 8);

	/* ---- Compute bounding box ---- */
	if (p->vertexCount > 0)
	{
		p->bboxMin[0] = p->bboxMax[0] = allVerts[0];
		p->bboxMin[1] = p->bboxMax[1] = allVerts[1];
		p->bboxMin[2] = p->bboxMax[2] = allVerts[2];

		for (int i = 1; i < p->vertexCount; ++i)
		{
			float x = allVerts[i * 8 + 0];
			float y = allVerts[i * 8 + 1];
			float z = allVerts[i * 8 + 2];
			if (x < p->bboxMin[0]) p->bboxMin[0] = x;
			if (y < p->bboxMin[1]) p->bboxMin[1] = y;
			if (z < p->bboxMin[2]) p->bboxMin[2] = z;
			if (x > p->bboxMax[0]) p->bboxMax[0] = x;
			if (y > p->bboxMax[1]) p->bboxMax[1] = y;
			if (z > p->bboxMax[2]) p->bboxMax[2] = z;
		}

		p->bboxCenter[0] = (p->bboxMin[0] + p->bboxMax[0]) * 0.5f;
		p->bboxCenter[1] = (p->bboxMin[1] + p->bboxMax[1]) * 0.5f;
		p->bboxCenter[2] = (p->bboxMin[2] + p->bboxMax[2]) * 0.5f;

		float dx = p->bboxMax[0] - p->bboxMin[0];
		float dy = p->bboxMax[1] - p->bboxMin[1];
		float dz = p->bboxMax[2] - p->bboxMin[2];
		p->bboxRadius = sqrtf(dx*dx + dy*dy + dz*dz) * 0.5f;

		/* Set default camera to frame the entire model */
		float dist = p->bboxRadius / tanf(p->fov * (float)M_PI / 360.0f);
		p->camX = p->bboxCenter[0];
		p->camY = p->bboxCenter[1];
		p->camZ = p->bboxCenter[2] + dist * 1.2f;
	}

	/* ---- Upload VBO ---- */
	gl.GenBuffers(1, &p->vbo);
	gl.BindBuffer(GL_ARRAY_BUFFER, p->vbo);
	gl.BufferData(GL_ARRAY_BUFFER,
	              allVerts.size() * sizeof(float),
	              allVerts.data(), GL_STATIC_DRAW);
	gl.BindBuffer(GL_ARRAY_BUFFER, 0);

	/* ---- Compile 3D shader ---- */
#ifdef MKXPZ_BUILD_XCODE
	std::string vertSrc = mkxp_fs::contentsOfAssetAsString("Shaders/model3d", "vert");
	std::string fragSrc = mkxp_fs::contentsOfAssetAsString("Shaders/model3d", "frag");
	p->vertShader = compileShader(GL_VERTEX_SHADER,
	                              vertSrc.c_str(), vertSrc.size(), "model3d.vert");
	p->fragShader = compileShader(GL_FRAGMENT_SHADER,
	                              fragSrc.c_str(), fragSrc.size(), "model3d.frag");
#else
	p->vertShader = compileShader(GL_VERTEX_SHADER,
	                              (const char *)___shader_model3d_vert,
	                              ___shader_model3d_vert_len, "model3d.vert");
	p->fragShader = compileShader(GL_FRAGMENT_SHADER,
	                              (const char *)___shader_model3d_frag,
	                              ___shader_model3d_frag_len, "model3d.frag");
#endif

	p->program = gl.CreateProgram();
	gl.AttachShader(p->program, p->vertShader);
	gl.AttachShader(p->program, p->fragShader);

	gl.BindAttribLocation(p->program, 0, "a_position");
	gl.BindAttribLocation(p->program, 1, "a_normal");
	gl.BindAttribLocation(p->program, 2, "a_texCoord");

	gl.LinkProgram(p->program);

	GLint success = 0;
	gl.GetProgramiv(p->program, GL_LINK_STATUS, &success);
	if (!success)
	{
		GLint logLen = 0;
		gl.GetProgramiv(p->program, GL_INFO_LOG_LENGTH, &logLen);
		std::string log(logLen, '\0');
		gl.GetProgramInfoLog(p->program, logLen, 0, &log[0]);
		delete p;
		throw Exception(Exception::MKXPError,
		                "Model3D: shader link failed:\n%s", log.c_str());
	}

	p->u_modelMat     = gl.GetUniformLocation(p->program, "u_modelMat");
	p->u_viewMat      = gl.GetUniformLocation(p->program, "u_viewMat");
	p->u_projMat      = gl.GetUniformLocation(p->program, "u_projMat");
	p->u_normalMat    = gl.GetUniformLocation(p->program, "u_normalMat");
	p->u_lightDir     = gl.GetUniformLocation(p->program, "u_lightDir");
	p->u_ambient      = gl.GetUniformLocation(p->program, "u_ambient");
	p->u_diffuseTex   = gl.GetUniformLocation(p->program, "u_diffuseTex");
	p->u_diffuseColor = gl.GetUniformLocation(p->program, "u_diffuseColor");

	/* Create a 1x1 white fallback texture for untextured materials */
	static const uint8_t white[] = { 255, 255, 255, 255 };
	gl.GenTextures(1, &p->whiteTex);
	gl.BindTexture(GL_TEXTURE_2D, p->whiteTex);
	gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0,
	              GL_RGBA, GL_UNSIGNED_BYTE, white);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

Model3D::~Model3D()
{
	dispose();
}

/* ------------------------------------------------------------------ */
/*  Property accessors                                                */
/* ------------------------------------------------------------------ */

#define DEF_ATTR_SIMPLE_M3D(name, field) \
	float Model3D::get##name() const { guardDisposed(); return p->field; } \
	void  Model3D::set##name(float v) { guardDisposed(); p->field = v; }

DEF_ATTR_SIMPLE_M3D(RotationX, rotX)
DEF_ATTR_SIMPLE_M3D(RotationY, rotY)
DEF_ATTR_SIMPLE_M3D(RotationZ, rotZ)
DEF_ATTR_SIMPLE_M3D(Scale, scale)
DEF_ATTR_SIMPLE_M3D(PositionX, posX)
DEF_ATTR_SIMPLE_M3D(PositionY, posY)
DEF_ATTR_SIMPLE_M3D(PositionZ, posZ)
DEF_ATTR_SIMPLE_M3D(CameraFOV, fov)
DEF_ATTR_SIMPLE_M3D(CameraX, camX)
DEF_ATTR_SIMPLE_M3D(CameraY, camY)
DEF_ATTR_SIMPLE_M3D(CameraZ, camZ)
DEF_ATTR_SIMPLE_M3D(LightX, lightX)
DEF_ATTR_SIMPLE_M3D(LightY, lightY)
DEF_ATTR_SIMPLE_M3D(LightZ, lightZ)
DEF_ATTR_SIMPLE_M3D(Ambient, ambient)

#undef DEF_ATTR_SIMPLE_M3D

/* ------------------------------------------------------------------ */
/*  render()                                                          */
/* ------------------------------------------------------------------ */

Bitmap *Model3D::render(int width, int height)
{
	guardDisposed();

	if (width <= 0 || height <= 0)
		throw Exception(Exception::RGSSError,
		                "Model3D::render: invalid dimensions %d x %d",
		                width, height);

	/* ---- Create result Bitmap and render directly into its FBO ---- */
	Bitmap *result = new Bitmap(width, height);
	TEXFBO &resultTex = result->getGLTypes();

	/* ---- Save GL state ---- */
	glState.viewport.pushSet(IntRect(0, 0, width, height));
	glState.scissorTest.pushSet(false);
	FBO::ID savedFBO = FBO::boundFramebufferID;

	/* Attach a depth renderbuffer to the Bitmap's FBO */
	p->ensureDepthRbo(width, height);
	FBO::bind(resultTex.fbo);
	gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
	                           GL_RENDERBUFFER, p->depthRbo);

	/* ---- 3D render state ---- */
	gl.Viewport(0, 0, width, height);
	gl.Enable(GL_DEPTH_TEST);
	gl.DepthFunc(GL_LESS);
	gl.DepthMask(GL_TRUE);
	gl.Disable(GL_CULL_FACE);
	gl.Enable(GL_BLEND);
	gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	gl.ClearColor(0, 0, 0, 0);
	gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* ---- Matrices ---- */
	float proj[16], view[16], model[16];
	float tmp1[16], tmp2[16], tmp3[16], tmp4[16];

	float farPlane = p->bboxRadius * 10.0f;
	if (farPlane < 100.0f) farPlane = 100.0f;
	mat4_perspective(proj, p->fov, (float)width / height, 0.1f, farPlane);
	mat4_lookAt(view, p->camX, p->camY, p->camZ,
	            p->bboxCenter[0], p->bboxCenter[1], p->bboxCenter[2],
	            0, 1, 0);

	mat4_translate(tmp1, p->posX, p->posY, p->posZ);
	mat4_rotateZ(tmp2, p->rotZ * (float)M_PI / 180.0f);
	mat4_multiply(tmp3, tmp1, tmp2);
	mat4_rotateY(tmp2, p->rotY * (float)M_PI / 180.0f);
	mat4_multiply(tmp4, tmp3, tmp2);
	mat4_rotateX(tmp2, p->rotX * (float)M_PI / 180.0f);
	mat4_multiply(tmp3, tmp4, tmp2);
	mat4_scale(tmp1, p->scale);
	mat4_multiply(model, tmp3, tmp1);

	/* Normal matrix: inverse-transpose of model-view 3x3 */
	float mv[16];
	mat4_multiply(mv, view, model);
	float normalMat[9];
	mat4_normalMat3(normalMat, mv);

	/* ---- Bind shader & set uniforms ---- */
	gl.UseProgram(p->program);
	gl.UniformMatrix4fv(p->u_projMat, 1, GL_FALSE, proj);
	gl.UniformMatrix4fv(p->u_viewMat, 1, GL_FALSE, view);
	gl.UniformMatrix4fv(p->u_modelMat, 1, GL_FALSE, model);

	/* Pass model matrix as normal matrix — the vertex shader extracts the
	 * upper-left 3x3 and normalizes. Correct for uniform scale. */
	gl.UniformMatrix4fv(p->u_normalMat, 1, GL_FALSE, model);

	gl.Uniform3f(p->u_lightDir, p->lightX, p->lightY, p->lightZ);
	gl.Uniform1f(p->u_ambient, p->ambient);

	/* ---- Draw mesh ---- */
	gl.BindBuffer(GL_ARRAY_BUFFER, p->vbo);

	const int stride = 8 * sizeof(float);
	gl.EnableVertexAttribArray(0);
	gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
	gl.EnableVertexAttribArray(1);
	gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
	                       (void *)(3 * sizeof(float)));
	gl.EnableVertexAttribArray(2);
	gl.VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
	                       (void *)(6 * sizeof(float)));

	for (size_t i = 0; i < p->materials.size(); ++i)
	{
		const MaterialGroup &mg = p->materials[i];

		gl.ActiveTexture(GL_TEXTURE0);
		gl.BindTexture(GL_TEXTURE_2D, mg.hasTex ? mg.texGL : p->whiteTex);
		gl.Uniform1i(p->u_diffuseTex, 0);

		gl.Uniform4f(p->u_diffuseColor,
		             mg.diffuseR, mg.diffuseG, mg.diffuseB, mg.diffuseA);

		gl.DrawArrays(GL_TRIANGLES, mg.startVertex, mg.vertexCount);
	}

	gl.DisableVertexAttribArray(0);
	gl.DisableVertexAttribArray(1);
	gl.DisableVertexAttribArray(2);
	gl.BindBuffer(GL_ARRAY_BUFFER, 0);

	/* ---- Detach depth renderbuffer from Bitmap's FBO ---- */
	gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
	                           GL_RENDERBUFFER, 0);

	/* ---- Restore GL state ---- */
	gl.Disable(GL_DEPTH_TEST);
	gl.DepthMask(GL_FALSE);

	FBO::bind(savedFBO);
	glState.program.refresh();

	glState.scissorTest.pop();
	glState.viewport.pop();
	glState.viewport.refresh();
	glState.clearColor.refresh();

	return result;
}

void Model3D::releaseResources()
{
	delete p;
}
