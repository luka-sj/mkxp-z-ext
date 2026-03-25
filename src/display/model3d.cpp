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
	TEX::ID diffuseTex;
	bool hasTex;
	float diffuseR, diffuseG, diffuseB, diffuseA;
	Bitmap *texBitmap; /* kept alive for GL texture lifetime */
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
	GLint u_diffuseTex, u_hasDiffuseTex, u_diffuseColor;

	/* Material groups */
	std::vector<MaterialGroup> materials;

	/* Offscreen FBO */
	GLuint fbo;
	GLuint colorTex;
	GLuint depthRbo;
	int fboWidth, fboHeight;

	Model3DPrivate()
	    : rotX(0), rotY(0), rotZ(0),
	      scale(1.0f),
	      posX(0), posY(0), posZ(0),
	      fov(45.0f),
	      camX(0), camY(0), camZ(5.0f),
	      lightX(0.5f), lightY(1.0f), lightZ(0.8f),
	      ambient(0.2f),
	      vbo(0), vertexCount(0),
	      program(0), vertShader(0), fragShader(0),
	      fbo(0), colorTex(0), depthRbo(0),
	      fboWidth(0), fboHeight(0)
	{}

	~Model3DPrivate()
	{
		for (size_t i = 0; i < materials.size(); ++i)
			delete materials[i].texBitmap;

		if (vbo)
			gl.DeleteBuffers(1, &vbo);
		if (program)
			gl.DeleteProgram(program);
		if (vertShader)
			gl.DeleteShader(vertShader);
		if (fragShader)
			gl.DeleteShader(fragShader);

		destroyFBO();
	}

	void destroyFBO()
	{
		if (depthRbo)
		{
			gl.DeleteRenderbuffers(1, &depthRbo);
			depthRbo = 0;
		}
		if (colorTex)
		{
			GLuint t = colorTex;
			gl.DeleteTextures(1, &t);
			colorTex = 0;
		}
		if (fbo)
		{
			gl.DeleteFramebuffers(1, &fbo);
			fbo = 0;
		}
		fboWidth = fboHeight = 0;
	}

	void ensureFBO(int w, int h)
	{
		if (fboWidth == w && fboHeight == h && fbo)
			return;

		destroyFBO();

		/* Color texture */
		GLuint tex;
		gl.GenTextures(1, &tex);
		gl.BindTexture(GL_TEXTURE_2D, tex);
		gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
		              GL_RGBA, GL_UNSIGNED_BYTE, 0);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		colorTex = tex;

		/* Depth renderbuffer */
		gl.GenRenderbuffers(1, &depthRbo);
		gl.BindRenderbuffer(GL_RENDERBUFFER, depthRbo);
		gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);

		/* Framebuffer */
		gl.GenFramebuffers(1, &fbo);
		gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
		gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		                        GL_TEXTURE_2D, colorTex, 0);
		gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
		                           GL_RENDERBUFFER, depthRbo);

		fboWidth = w;
		fboHeight = h;
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

			for (int v = 0; v < fv; ++v)
			{
				tinyobj::index_t idx = mesh.indices[indexOffset + v];
				std::vector<float> &verts = matVerts[matId];

				/* Position */
				verts.push_back(attrib.vertices[3 * idx.vertex_index + 0]);
				verts.push_back(attrib.vertices[3 * idx.vertex_index + 1]);
				verts.push_back(attrib.vertices[3 * idx.vertex_index + 2]);

				/* Normal */
				if (idx.normal_index >= 0)
				{
					verts.push_back(attrib.normals[3 * idx.normal_index + 0]);
					verts.push_back(attrib.normals[3 * idx.normal_index + 1]);
					verts.push_back(attrib.normals[3 * idx.normal_index + 2]);
				}
				else
				{
					verts.push_back(0); verts.push_back(1); verts.push_back(0);
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
		mg.texBitmap = 0;
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
				try
				{
					Bitmap *bmp = new Bitmap(texPath.c_str());
					bmp->ensureNonMega();
					mg.texBitmap = bmp;
					mg.diffuseTex = bmp->getGLTypes().tex;
					mg.hasTex = true;
				}
				catch (const Exception &e)
				{
					Debug() << "Model3D: Could not load texture '"
					        << texPath.c_str() << "': " << e.msg;
				}
			}
		}

		p->materials.push_back(mg);
		allVerts.insert(allVerts.end(), kv.second.begin(), kv.second.end());
	}

	p->vertexCount = (int)(allVerts.size() / 8);

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
	p->u_hasDiffuseTex= gl.GetUniformLocation(p->program, "u_hasDiffuseTex");
	p->u_diffuseColor = gl.GetUniformLocation(p->program, "u_diffuseColor");
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

	/* ---- Save GL state ---- */
	glState.viewport.pushSet(IntRect(0, 0, width, height));
	glState.scissorTest.pushSet(false);

	FBO::ID savedFBO = FBO::boundFramebufferID;

	/* ---- Prepare offscreen FBO ---- */
	p->ensureFBO(width, height);
	gl.BindFramebuffer(GL_FRAMEBUFFER, p->fbo);

	/* ---- 3D render state ---- */
	gl.Viewport(0, 0, width, height);
	gl.Enable(GL_DEPTH_TEST);
	gl.DepthFunc(GL_LESS);
	gl.DepthMask(GL_TRUE);
	gl.Enable(GL_CULL_FACE);
	gl.CullFace(GL_BACK);
	gl.Enable(GL_BLEND);
	gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	gl.ClearColor(0, 0, 0, 0);
	gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* ---- Matrices ---- */
	float proj[16], view[16], model[16];
	float tmp1[16], tmp2[16], tmp3[16], tmp4[16];

	mat4_perspective(proj, p->fov, (float)width / height, 0.1f, 100.0f);
	mat4_lookAt(view, p->camX, p->camY, p->camZ, 0, 0, 0, 0, 1, 0);

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

	/* UniformMatrix3fv might not be in the func table — use 9 Uniform3fv calls
	 * or pass as 3 vec3s. Actually, let's just use the 3x3 uniform. We need
	 * to add it or use an alternative. For GLES2 compat, pass as mat3. */
	/* Since we declared u_normalMat as mat3 in the shader, we need
	 * glUniformMatrix3fv. But it's not in our func table. Let's use
	 * 3 separate vec3 uniforms instead — but that changes the shader.
	 * Simpler: just pass the full 4x4 model matrix and compute normal mat
	 * in the vertex shader. Let's adjust. */
	/* Actually, we can approximate: if uniform scale only, normal matrix ≈
	 * upper-left 3x3 of model. For correct results we'll need
	 * glUniformMatrix3fv. Let's add a typedef and load it. */

	/* For now, use the model matrix directly and normalize in the shader.
	 * This is correct for uniform scale (which is our case). */
	/* Overwrite: change shader to use mat4 for u_normalMat, pass modelMat */
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

		if (mg.hasTex)
		{
			gl.ActiveTexture(GL_TEXTURE0);
			gl.BindTexture(GL_TEXTURE_2D, mg.diffuseTex.gl);
			gl.Uniform1i(p->u_diffuseTex, 0);
			gl.Uniform1i(p->u_hasDiffuseTex, 1);
		}
		else
		{
			gl.Uniform1i(p->u_hasDiffuseTex, 0);
		}

		gl.Uniform4f(p->u_diffuseColor,
		             mg.diffuseR, mg.diffuseG, mg.diffuseB, mg.diffuseA);

		gl.DrawArrays(GL_TRIANGLES, mg.startVertex, mg.vertexCount);
	}

	gl.DisableVertexAttribArray(0);
	gl.DisableVertexAttribArray(1);
	gl.DisableVertexAttribArray(2);
	gl.BindBuffer(GL_ARRAY_BUFFER, 0);

	/* ---- Restore 2D-safe GL state before blit ---- */
	gl.Disable(GL_DEPTH_TEST);
	gl.DepthMask(GL_FALSE);
	gl.Disable(GL_CULL_FACE);

	/* ---- Copy FBO result to a Bitmap ---- */
	Bitmap *result = new Bitmap(width, height);
	TEXFBO &resultTex = result->getGLTypes();

	/* Build a temporary TEXFBO referencing our offscreen FBO */
	TEXFBO srcFBO;
	srcFBO.tex.gl = p->colorTex;
	srcFBO.fbo.gl = p->fbo;
	srcFBO.width = width;
	srcFBO.height = height;
	srcFBO.selfHires = 0;

	GLMeta::blitBegin(resultTex);
	GLMeta::blitSource(srcFBO);
	GLMeta::blitRectangle(IntRect(0, 0, width, height), Vec2i(0, 0));
	GLMeta::blitEnd();

	/* ---- Restore previous GL state ---- */
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
