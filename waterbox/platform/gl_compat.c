/* gl_compat.c - SRB2's OpenGL 1.x, on a core-profile context through
 * Chimera's GPU bridge.
 *
 * The bridge (miniBox source/gl, docs/gpu-bridge.md in Chimera) carries the
 * calls on its master list, which is modern GL only, onto a context Chimera
 * makes - on Linux a 3.3 core profile. SRB2's renderer (r_opengl.c, unchanged)
 * is OpenGL 1.x: matrix stacks, client-side arrays, the texture environment,
 * the alpha test, a light for models, and GLSL written against the
 * compatibility built-ins. This file is the difference. r_opengl reaches GL
 * only through GetGLFunc, by name; glc_proc answers it with a function here
 * where the call is one core GL does not have, and with the bridge's wrapper
 * where it is:
 *
 *   matrices      the modelview, projection and texture stacks are kept here
 *                 and given to every program as uniforms
 *   arrays        client arrays are streamed into buffers under a vertex
 *                 array object of this file's, at fixed attribute locations;
 *                 arrays in the renderer's own buffers (models, the sky) are
 *                 pointed at in place; client indices go into a buffer too
 *   fixed stages  with no program of the renderer's in use, a program of this
 *                 file's: the texture environment (modulate, replace) on two
 *                 units, the alpha test, one light (models)
 *   GLSL          the renderer's shaders are rewritten for GLSL 3.30: the
 *                 compatibility built-ins become this file's inputs, outputs
 *                 and uniforms, and the alpha test runs after the shader's own
 *                 main, as the fixed stage after a shader does
 *   textures      luminance-alpha and alpha formats become RGBA with a
 *                 swizzle; GL_GENERATE_MIPMAP becomes glGenerateMipmap; GL_CLAMP
 *                 is GL_CLAMP_TO_EDGE
 *   framebuffer   a context made with no surface has no default framebuffer:
 *                 the renderer draws into one of this file's, read back once a
 *                 frame (glc_read_frame)
 *
 * Every object a GL name is kept for is the renderer's state or this file's,
 * in guest memory, and a savestate brings back names the driver may no longer
 * mean. So every name handed out is also listed where a state does not reach
 * (ECL_INVISIBLE), and when the context moves (glc_forget) all of them are
 * deleted at once, before anything is made again - see ogl_chimera.c.
 *
 * Guest only: the native reference has no bridge. */
#ifdef CHIMERA_GUEST

#include <glad/gl.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emulibc.h"
#include "gl-bridge.h"

#include "chimera-platform.h"

/* the calling convention r_opengl declares its GL pointers with */
#ifndef APIENTRY
#define APIENTRY GLAD_API_PTR
#endif

/* the GL 1.x names core GL has no header for */
#ifndef GL_MODELVIEW
#define GL_MODELVIEW 0x1700
#endif
#ifndef GL_PROJECTION
#define GL_PROJECTION 0x1701
#endif
#ifndef GL_TEXTURE_MATRIX_MODE
#define GL_TEXTURE_MATRIX_MODE 0x1702
#endif /* glMatrixMode(GL_TEXTURE) */
#ifndef GL_MODELVIEW_MATRIX
#define GL_MODELVIEW_MATRIX 0x0BA6
#endif
#ifndef GL_PROJECTION_MATRIX
#define GL_PROJECTION_MATRIX 0x0BA7
#endif
#ifndef GL_VERTEX_ARRAY
#define GL_VERTEX_ARRAY 0x8074
#endif
#ifndef GL_NORMAL_ARRAY
#define GL_NORMAL_ARRAY 0x8075
#endif
#ifndef GL_COLOR_ARRAY
#define GL_COLOR_ARRAY 0x8076
#endif
#ifndef GL_TEXTURE_COORD_ARRAY
#define GL_TEXTURE_COORD_ARRAY 0x8078
#endif
#ifndef GL_TEXTURE_ENV
#define GL_TEXTURE_ENV 0x2300
#endif
#ifndef GL_TEXTURE_ENV_MODE
#define GL_TEXTURE_ENV_MODE 0x2200
#endif
#ifndef GL_MODULATE
#define GL_MODULATE 0x2100
#endif
#ifndef GL_ALPHA_TEST
#define GL_ALPHA_TEST 0x0BC0
#endif
#ifndef GL_LIGHTING
#define GL_LIGHTING 0x0B50
#endif
#ifndef GL_LIGHT0
#define GL_LIGHT0 0x4000
#endif
#ifndef GL_NORMALIZE
#define GL_NORMALIZE 0x0BA1
#endif
#ifndef GL_RESCALE_NORMAL
#define GL_RESCALE_NORMAL 0x803A
#endif
#ifndef GL_FOG
#define GL_FOG 0x0B60
#endif
#ifndef GL_COLOR_MATERIAL
#define GL_COLOR_MATERIAL 0x0B57
#endif
#ifndef GL_LIGHT_MODEL_AMBIENT
#define GL_LIGHT_MODEL_AMBIENT 0x0B53
#endif
#ifndef GL_AMBIENT
#define GL_AMBIENT 0x1200
#endif
#ifndef GL_DIFFUSE
#define GL_DIFFUSE 0x1201
#endif
#ifndef GL_POSITION
#define GL_POSITION 0x1203
#endif
#ifndef GL_LUMINANCE
#define GL_LUMINANCE 0x1909
#endif
#ifndef GL_LUMINANCE_ALPHA
#define GL_LUMINANCE_ALPHA 0x190A
#endif
#ifndef GL_LUMINANCE8
#define GL_LUMINANCE8 0x8040
#endif
#ifndef GL_INTENSITY
#define GL_INTENSITY 0x8049
#endif
#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif
#ifndef GL_CLAMP
#define GL_CLAMP 0x2900
#endif
#ifndef GL_MAX_TEXTURE_UNITS
#define GL_MAX_TEXTURE_UNITS 0x84E2
#endif

/* ---- the objects, by name, where a savestate does not reach ----------- */

enum { K_TEXTURE, K_BUFFER, K_VERTEX_ARRAY, K_FRAMEBUFFER, K_RENDERBUFFER, K_PROGRAM, K_SHADER, K_KINDS };
#define REG_MAX 65536
static ECL_INVISIBLE uint32_t g_reg[K_KINDS][REG_MAX];
static ECL_INVISIBLE uint32_t g_reg_n[K_KINDS];

static void reg_add(int kind, GLuint name)
{
	if (name && g_reg_n[kind] < REG_MAX)
		g_reg[kind][g_reg_n[kind]++] = name;
}

static void reg_remove(int kind, GLuint name)
{
	for (uint32_t i = 0; i < g_reg_n[kind]; i++)
		if (g_reg[kind][i] == name)
		{
			g_reg[kind][i] = g_reg[kind][--g_reg_n[kind]];
			return;
		}
}

/* ---- state: the renderer's GL 1.x as it set it (guest memory) ---------- */

enum { A_VERTEX, A_COLOR, A_TEX0, A_NORMAL, A_TEX1, A_COUNT };
static const char *const g_attr_names[A_COUNT] = {
	"chimera_Vertex", "chimera_Color", "chimera_MultiTexCoord0", "chimera_Normal", "chimera_MultiTexCoord1"
};

struct arr
{
	int enabled;
	GLint size;
	GLenum type;
	GLsizei stride;
	const void *ptr;
	GLuint buffer; /* the array buffer bound when it was given: ptr is an offset */
};

#define MV_DEPTH 32
#define PROJ_DEPTH 4

static struct
{
	GLenum mode;
	float mv[MV_DEPTH][16];
	int mv_top;
	float proj[PROJ_DEPTH][16];
	int proj_top;
	float texm[PROJ_DEPTH][16];
	int tex_top;
	unsigned mv_ver, proj_ver;

	struct arr arr[A_COUNT];
	int client_unit;
	float cur[A_COUNT][4];
	GLuint array_buffer; /* what the renderer has bound */

	int unit;
	int tex2d[2];
	GLenum env[2];
	GLuint bound[2];

	int alpha_on;
	GLenum alpha_func;
	float alpha_ref;
	unsigned alpha_ver;

	int lighting, light0, normalize;
	float light_pos[4], model_ambient[4], mat_ambient[4], mat_diffuse[4];

	GLuint program; /* the renderer's, 0 for the fixed stages */
} S;

/* per texture name: made by generating its mipmaps, and which swizzle */
enum { TF_MIPMAP = 1, TF_ALPHA_ONLY = 2 };
static uint8_t *g_texflags;
static size_t g_texflags_n;

static uint8_t *texflag(GLuint name)
{
	if (name >= g_texflags_n)
	{
		size_t n = g_texflags_n ? g_texflags_n : 1024;
		while (n <= name)
			n *= 2;
		g_texflags = realloc(g_texflags, n);
		memset(g_texflags + g_texflags_n, 0, n - g_texflags_n);
		g_texflags_n = n;
	}
	return &g_texflags[name];
}

/* per shader name: its stage; per program: what this file sets in it */
struct proginfo
{
	int vertex, fragment; /* stages attached */
	GLint mv, proj, alpha_func, alpha_ref;
	unsigned mv_ver, proj_ver, alpha_ver;
};
static GLenum *g_shadertype;
static size_t g_shadertype_n;
static struct proginfo *g_prog;
static size_t g_prog_n;

static GLenum *shadertype(GLuint name)
{
	if (name >= g_shadertype_n)
	{
		size_t n = g_shadertype_n ? g_shadertype_n : 256;
		while (n <= name)
			n *= 2;
		g_shadertype = realloc(g_shadertype, n * sizeof *g_shadertype);
		memset(g_shadertype + g_shadertype_n, 0, (n - g_shadertype_n) * sizeof *g_shadertype);
		g_shadertype_n = n;
	}
	return &g_shadertype[name];
}

static struct proginfo *proginfo(GLuint name)
{
	if (name >= g_prog_n)
	{
		size_t n = g_prog_n ? g_prog_n : 256;
		while (n <= name)
			n *= 2;
		g_prog = realloc(g_prog, n * sizeof *g_prog);
		memset(g_prog + g_prog_n, 0, (n - g_prog_n) * sizeof *g_prog);
		g_prog_n = n;
	}
	return &g_prog[name];
}

/* ---- this file's objects, and what it knows the driver has ------------- */

static struct
{
	GLuint vao, vbo[A_COUNT], ebo;
	GLuint fbo, color, depth;
	int w, h;
	GLuint fixed, fixed_vs, fixed_fs, default_vs;
	struct
	{
		GLint tex_on[2], tex_mode[2], alpha_only[2], sampler[2];
		GLint lighting, light_pos, model_ambient, mat_ambient, mat_diffuse, normalize;
	} u;
	unsigned fixed_ver; /* the fixed program's last uniforms, hashed */
	uint32_t fixed_key[24];
	/* what is bound in the driver, so a call that would change nothing is
	 * not made */
	GLuint array, prog;
	int attr_on[A_COUNT];
	float attr_cur[A_COUNT][4];
	int attr_cur_known[A_COUNT];
	int ok;
} G;

static char g_extensions[256];

/* ---- matrices ----------------------------------------------------------- */

static void mat_identity(float *m)
{
	memset(m, 0, 16 * sizeof *m);
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static float *mat_cur(void)
{
	switch (S.mode)
	{
		case GL_PROJECTION: return S.proj[S.proj_top];
		case GL_TEXTURE_MATRIX_MODE: return S.texm[S.tex_top];
		default: return S.mv[S.mv_top];
	}
}

static void mat_changed(void)
{
	if (S.mode == GL_PROJECTION)
		S.proj_ver++;
	else if (S.mode == GL_MODELVIEW)
		S.mv_ver++;
}

/* m = m * b, column-major as GL's */
static void mat_mul(float *m, const float *b)
{
	float r[16];
	for (int c = 0; c < 4; c++)
		for (int row = 0; row < 4; row++)
		{
			float s = 0;
			for (int k = 0; k < 4; k++)
				s += m[k * 4 + row] * b[c * 4 + k];
			r[c * 4 + row] = s;
		}
	memcpy(m, r, sizeof r);
}

static void APIENTRY c_MatrixMode(GLenum mode) { S.mode = mode; }
static void APIENTRY c_LoadIdentity(void) { mat_identity(mat_cur()); mat_changed(); }
static void APIENTRY c_MultMatrixf(const GLfloat *m) { mat_mul(mat_cur(), m); mat_changed(); }

static void APIENTRY c_PushMatrix(void)
{
	int *top = S.mode == GL_PROJECTION ? &S.proj_top : S.mode == GL_TEXTURE_MATRIX_MODE ? &S.tex_top : &S.mv_top;
	const int depth = S.mode == GL_MODELVIEW ? MV_DEPTH : PROJ_DEPTH;
	float(*stack)[16] = S.mode == GL_PROJECTION ? S.proj : S.mode == GL_TEXTURE_MATRIX_MODE ? S.texm : S.mv;
	if (*top + 1 >= depth)
		return;
	memcpy(stack[*top + 1], stack[*top], sizeof stack[0]);
	(*top)++;
}

static void APIENTRY c_PopMatrix(void)
{
	int *top = S.mode == GL_PROJECTION ? &S.proj_top : S.mode == GL_TEXTURE_MATRIX_MODE ? &S.tex_top : &S.mv_top;
	if (*top > 0)
		(*top)--;
	mat_changed();
}

static void APIENTRY c_Translatef(GLfloat x, GLfloat y, GLfloat z)
{
	float t[16];
	mat_identity(t);
	t[12] = x;
	t[13] = y;
	t[14] = z;
	c_MultMatrixf(t);
}

static void APIENTRY c_Scalef(GLfloat x, GLfloat y, GLfloat z)
{
	float s[16];
	mat_identity(s);
	s[0] = x;
	s[5] = y;
	s[10] = z;
	c_MultMatrixf(s);
}

static void APIENTRY c_Rotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
	const float len = sqrtf(x * x + y * y + z * z);
	if (len == 0.0f)
		return;
	x /= len;
	y /= len;
	z /= len;
	const float a = angle * 3.14159265358979323846f / 180.0f;
	const float c = cosf(a), s = sinf(a), t = 1.0f - c;
	float r[16] = {
		t * x * x + c, t * x * y + s * z, t * x * z - s * y, 0,
		t * x * y - s * z, t * y * y + c, t * y * z + s * x, 0,
		t * x * z + s * y, t * y * z - s * x, t * z * z + c, 0,
		0, 0, 0, 1,
	};
	c_MultMatrixf(r);
}

static void APIENTRY c_GetFloatv(GLenum pname, GLfloat *out)
{
	if (pname == GL_MODELVIEW_MATRIX)
		memcpy(out, S.mv[S.mv_top], 16 * sizeof *out);
	else if (pname == GL_PROJECTION_MATRIX)
		memcpy(out, S.proj[S.proj_top], 16 * sizeof *out);
	else
		glGetFloatv(pname, out);
}

static void APIENTRY c_GetIntegerv(GLenum pname, GLint *out)
{
	if (pname == GL_MAX_TEXTURE_UNITS)
		*out = 2;
	else
		glGetIntegerv(pname, out);
}

/* ---- the fixed stages' state --------------------------------------------- */

static int fixed_cap(GLenum cap, int on)
{
	switch (cap)
	{
		case GL_TEXTURE_2D:
			if (S.unit < 2)
				S.tex2d[S.unit] = on;
			return 1;
		case GL_ALPHA_TEST:
			S.alpha_on = on;
			S.alpha_ver++;
			return 1;
		case GL_LIGHTING: S.lighting = on; return 1;
		case GL_LIGHT0: S.light0 = on; return 1;
		case GL_NORMALIZE: case GL_RESCALE_NORMAL: S.normalize = on; return 1;
		case GL_FOG: case GL_COLOR_MATERIAL: case GL_TEXTURE_1D: case GL_TEXTURE_3D:
			return 1;
		default:
			return 0;
	}
}

static void APIENTRY c_Enable(GLenum cap) { if (!fixed_cap(cap, 1)) glEnable(cap); }
static void APIENTRY c_Disable(GLenum cap) { if (!fixed_cap(cap, 0)) glDisable(cap); }

static void APIENTRY c_AlphaFunc(GLenum func, GLclampf ref)
{
	S.alpha_func = func;
	S.alpha_ref = ref;
	S.alpha_ver++;
}

static void APIENTRY c_TexEnvi(GLenum target, GLenum pname, GLint param)
{
	if (target == GL_TEXTURE_ENV && pname == GL_TEXTURE_ENV_MODE && S.unit < 2)
		S.env[S.unit] = (GLenum)param;
}

static void APIENTRY c_ShadeModel(GLenum mode) { (void)mode; } /* smooth, always */

static void transform4(const float *m, const float *v, float *out)
{
	for (int r = 0; r < 4; r++)
		out[r] = m[r] * v[0] + m[4 + r] * v[1] + m[8 + r] * v[2] + m[12 + r] * v[3];
}

static void APIENTRY c_Lightfv(GLenum light, GLenum pname, const GLfloat *v)
{
	/* a light's position is the modelview's of the moment it is given */
	if (light == GL_LIGHT0 && pname == GL_POSITION)
		transform4(S.mv[S.mv_top], v, S.light_pos);
}

static void APIENTRY c_LightModelfv(GLenum pname, const GLfloat *v)
{
	if (pname == GL_LIGHT_MODEL_AMBIENT)
		memcpy(S.model_ambient, v, 4 * sizeof *v);
}

static void APIENTRY c_Materialfv(GLenum face, GLenum pname, const GLfloat *v)
{
	(void)face;
	if (pname == GL_AMBIENT)
		memcpy(S.mat_ambient, v, 4 * sizeof *v);
	else if (pname == GL_DIFFUSE)
		memcpy(S.mat_diffuse, v, 4 * sizeof *v);
}

static void APIENTRY c_Materiali(GLenum face, GLenum pname, GLint v) { (void)face; (void)pname; (void)v; }

/* ---- arrays ---------------------------------------------------------------- */

static int arr_index(GLenum cap)
{
	switch (cap)
	{
		case GL_VERTEX_ARRAY: return A_VERTEX;
		case GL_COLOR_ARRAY: return A_COLOR;
		case GL_NORMAL_ARRAY: return A_NORMAL;
		case GL_TEXTURE_COORD_ARRAY: return S.client_unit ? A_TEX1 : A_TEX0;
		default: return -1;
	}
}

static void APIENTRY c_EnableClientState(GLenum cap)
{
	const int a = arr_index(cap);
	if (a >= 0)
		S.arr[a].enabled = 1;
}

static void APIENTRY c_DisableClientState(GLenum cap)
{
	const int a = arr_index(cap);
	if (a >= 0)
		S.arr[a].enabled = 0;
}

static void set_arr(int a, GLint size, GLenum type, GLsizei stride, const void *ptr)
{
	S.arr[a].size = size;
	S.arr[a].type = type;
	S.arr[a].stride = stride;
	S.arr[a].ptr = ptr;
	S.arr[a].buffer = S.array_buffer;
}

static void APIENTRY c_VertexPointer(GLint size, GLenum type, GLsizei stride, const void *p) { set_arr(A_VERTEX, size, type, stride, p); }
static void APIENTRY c_ColorPointer(GLint size, GLenum type, GLsizei stride, const void *p) { set_arr(A_COLOR, size, type, stride, p); }
static void APIENTRY c_NormalPointer(GLenum type, GLsizei stride, const void *p) { set_arr(A_NORMAL, 3, type, stride, p); }
static void APIENTRY c_TexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *p)
{
	set_arr(S.client_unit ? A_TEX1 : A_TEX0, size, type, stride, p);
}

static void APIENTRY c_ClientActiveTexture(GLenum unit) { S.client_unit = unit == GL_TEXTURE1; }

static void APIENTRY c_Color4ubv(const GLubyte *c)
{
	for (int i = 0; i < 4; i++)
		S.cur[A_COLOR][i] = c[i] / 255.0f;
}

static void APIENTRY c_MultiTexCoord2f(GLenum unit, GLfloat s, GLfloat t)
{
	const int a = unit == GL_TEXTURE1 ? A_TEX1 : A_TEX0;
	S.cur[a][0] = s;
	S.cur[a][1] = t;
	S.cur[a][2] = 0;
	S.cur[a][3] = 1;
}

static void APIENTRY c_MultiTexCoord2fv(GLenum unit, const GLfloat *v) { c_MultiTexCoord2f(unit, v[0], v[1]); }

static void APIENTRY c_BindBuffer(GLenum target, GLuint buffer)
{
	if (target == GL_ARRAY_BUFFER)
	{
		S.array_buffer = buffer;
		G.array = buffer;
	}
	glBindBuffer(target, buffer);
}

static void bind_array(GLuint b)
{
	if (G.array != b)
	{
		glBindBuffer(GL_ARRAY_BUFFER, b);
		G.array = b;
	}
}

static GLsizei type_size(GLenum type)
{
	switch (type)
	{
		case GL_BYTE: case GL_UNSIGNED_BYTE: return 1;
		case GL_SHORT: case GL_UNSIGNED_SHORT: return 2;
		default: return 4;
	}
}

/* the arrays for vertices [first, first + count), at location = index */
static void attribs(GLint first, GLsizei count)
{
	for (int a = 0; a < A_COUNT; a++)
	{
		const struct arr *r = &S.arr[a];
		if (r->enabled && count > 0)
		{
			const GLsizei es = type_size(r->type) * r->size;
			const GLsizei stride = r->stride ? r->stride : es;
			const GLboolean norm = (a == A_COLOR || a == A_NORMAL) && r->type != GL_FLOAT;
			if (r->buffer)
			{
				bind_array(r->buffer);
				glVertexAttribPointer(a, r->size, r->type, norm, stride,
					(const void *)((uintptr_t)r->ptr + (uintptr_t)first * stride));
			}
			else
			{
				bind_array(G.vbo[a]);
				glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count - 1) * stride + es,
					(const char *)r->ptr + (size_t)first * stride, GL_STREAM_DRAW);
				glVertexAttribPointer(a, r->size, r->type, norm, stride, NULL);
			}
			if (!G.attr_on[a])
			{
				glEnableVertexAttribArray(a);
				G.attr_on[a] = 1;
			}
		}
		else
		{
			if (G.attr_on[a])
			{
				glDisableVertexAttribArray(a);
				G.attr_on[a] = 0;
			}
			if (!G.attr_cur_known[a] || memcmp(G.attr_cur[a], S.cur[a], sizeof S.cur[a]))
			{
				glVertexAttrib4f(a, S.cur[a][0], S.cur[a][1], S.cur[a][2], S.cur[a][3]);
				memcpy(G.attr_cur[a], S.cur[a], sizeof S.cur[a]);
				G.attr_cur_known[a] = 1;
			}
		}
	}
	bind_array(S.array_buffer);
}

/* ---- programs ---------------------------------------------------------------- */

static void use_program(GLuint p)
{
	if (G.prog != p)
	{
		glUseProgram(p);
		G.prog = p;
	}
}

static void common_uniforms(GLuint p)
{
	struct proginfo *pi = proginfo(p);
	if (pi->mv_ver != S.mv_ver)
	{
		glUniformMatrix4fv(pi->mv, 1, GL_FALSE, S.mv[S.mv_top]);
		pi->mv_ver = S.mv_ver;
	}
	if (pi->proj_ver != S.proj_ver)
	{
		glUniformMatrix4fv(pi->proj, 1, GL_FALSE, S.proj[S.proj_top]);
		pi->proj_ver = S.proj_ver;
	}
	if (pi->alpha_ver != S.alpha_ver)
	{
		glUniform1i(pi->alpha_func, S.alpha_on ? (GLint)S.alpha_func : 0);
		glUniform1f(pi->alpha_ref, S.alpha_ref);
		pi->alpha_ver = S.alpha_ver;
	}
}

static void fixed_uniforms(void)
{
	uint32_t key[24];
	int k = 0;
	for (int u = 0; u < 2; u++)
	{
		key[k++] = S.tex2d[u];
		key[k++] = S.env[u];
		key[k++] = (*texflag(S.bound[u]) & TF_ALPHA_ONLY) != 0;
	}
	key[k++] = S.lighting && S.light0;
	key[k++] = S.normalize;
	memcpy(&key[k], S.light_pos, 16), k += 4;
	memcpy(&key[k], S.model_ambient, 16), k += 4;
	memcpy(&key[k], S.mat_ambient, 16), k += 4;
	memcpy(&key[k], S.mat_diffuse, 16), k += 4;
	if (G.fixed_ver && !memcmp(key, G.fixed_key, sizeof key))
		return;
	for (int u = 0; u < 2; u++)
	{
		glUniform1i(G.u.tex_on[u], S.tex2d[u]);
		glUniform1i(G.u.tex_mode[u], S.env[u] == GL_REPLACE);
		glUniform1i(G.u.alpha_only[u], (*texflag(S.bound[u]) & TF_ALPHA_ONLY) != 0);
	}
	glUniform1i(G.u.lighting, S.lighting && S.light0);
	glUniform1i(G.u.normalize, S.normalize);
	glUniform4fv(G.u.light_pos, 1, S.light_pos);
	glUniform4fv(G.u.model_ambient, 1, S.model_ambient);
	glUniform4fv(G.u.mat_ambient, 1, S.mat_ambient);
	glUniform4fv(G.u.mat_diffuse, 1, S.mat_diffuse);
	memcpy(G.fixed_key, key, sizeof key);
	G.fixed_ver = 1;
}

static void program_for_draw(void)
{
	const GLuint p = S.program ? S.program : G.fixed;
	use_program(p);
	common_uniforms(p);
	if (!S.program)
		fixed_uniforms();
}

/* bound at once: the renderer sets its uniforms right after (0, the fixed
 * stages, waits for the draw) */
static void APIENTRY c_UseProgram(GLuint p)
{
	S.program = p;
	if (p)
		use_program(p);
}

/* ---- draws ---------------------------------------------------------------------- */

static void APIENTRY c_DrawArrays(GLenum mode, GLint first, GLsizei count)
{
	if (count <= 0)
		return;
	attribs(first, count);
	program_for_draw();
	glDrawArrays(mode, 0, count);
}

static void APIENTRY c_DrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
	if (count <= 0)
		return;
	GLuint max = 0;
	for (GLsizei i = 0; i < count; i++)
	{
		const GLuint v = type == GL_UNSIGNED_INT ? ((const GLuint *)indices)[i]
			: type == GL_UNSIGNED_SHORT ? ((const GLushort *)indices)[i] : ((const GLubyte *)indices)[i];
		if (v > max)
			max = v;
	}
	attribs(0, (GLsizei)max + 1);
	program_for_draw();
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)count * type_size(type), indices, GL_STREAM_DRAW);
	glDrawElements(mode, count, type, NULL);
}

/* ---- textures ---------------------------------------------------------------------- */

static void APIENTRY c_ActiveTexture(GLenum unit)
{
	S.unit = (int)(unit - GL_TEXTURE0);
	glActiveTexture(unit);
}

static void APIENTRY c_BindTexture(GLenum target, GLuint tex)
{
	if (target == GL_TEXTURE_2D && S.unit < 2)
		S.bound[S.unit] = tex;
	glBindTexture(target, tex);
}

static void APIENTRY c_GenTextures(GLsizei n, GLuint *names)
{
	glGenTextures(n, names);
	for (GLsizei i = 0; i < n; i++)
	{
		reg_add(K_TEXTURE, names[i]);
		*texflag(names[i]) = 0;
	}
}

static void APIENTRY c_DeleteTextures(GLsizei n, const GLuint *names)
{
	glDeleteTextures(n, names);
	for (GLsizei i = 0; i < n; i++)
		reg_remove(K_TEXTURE, names[i]);
}

static GLuint bound2d(void) { return S.unit < 2 ? S.bound[S.unit] : 0; }

static void APIENTRY c_TexParameteri(GLenum target, GLenum pname, GLint param)
{
	if (pname == GL_GENERATE_MIPMAP)
	{
		uint8_t *f = texflag(bound2d());
		*f = param ? (*f | TF_MIPMAP) : (*f & ~TF_MIPMAP);
		return;
	}
	if (param == GL_CLAMP && (pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T || pname == GL_TEXTURE_WRAP_R))
		param = GL_CLAMP_TO_EDGE;
	glTexParameteri(target, pname, param);
}

static void swizzle(GLenum target, GLint r, GLint g, GLint b, GLint a)
{
	glTexParameteri(target, GL_TEXTURE_SWIZZLE_R, r);
	glTexParameteri(target, GL_TEXTURE_SWIZZLE_G, g);
	glTexParameteri(target, GL_TEXTURE_SWIZZLE_B, b);
	glTexParameteri(target, GL_TEXTURE_SWIZZLE_A, a);
}

static void APIENTRY c_TexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border,
	GLenum format, GLenum type, const void *pixels)
{
	uint8_t *f = target == GL_TEXTURE_2D ? texflag(bound2d()) : NULL;
	/* GL 1.x's single-channel formats, as RGBA and a swizzle: luminance is
	 * the source's red */
	if (internal == GL_LUMINANCE_ALPHA || internal == GL_ALPHA || internal == GL_LUMINANCE || internal == GL_INTENSITY)
	{
		if (internal == GL_LUMINANCE_ALPHA)
			swizzle(target, GL_RED, GL_RED, GL_RED, GL_ALPHA);
		else if (internal == GL_ALPHA)
			swizzle(target, GL_ZERO, GL_ZERO, GL_ZERO, GL_ALPHA);
		else if (internal == GL_LUMINANCE)
			swizzle(target, GL_RED, GL_RED, GL_RED, GL_ONE);
		else
			swizzle(target, GL_RED, GL_RED, GL_RED, GL_RED);
		if (f)
			*f = internal == GL_ALPHA ? (*f | TF_ALPHA_ONLY) : (*f & ~TF_ALPHA_ONLY);
		internal = GL_RGBA8;
	}
	else
	{
		if (target == GL_TEXTURE_2D)
			swizzle(target, GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA);
		if (f)
			*f &= ~TF_ALPHA_ONLY;
		if (internal == GL_LUMINANCE8)
			internal = GL_R8;
	}
	glTexImage2D(target, level, internal, w, h, border, format, type, pixels);
	if (f && (*f & TF_MIPMAP) && level == 0)
		glGenerateMipmap(target);
}

static void APIENTRY c_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
	GLenum format, GLenum type, const void *pixels)
{
	glTexSubImage2D(target, level, x, y, w, h, format, type, pixels);
	if (target == GL_TEXTURE_2D && (*texflag(bound2d()) & TF_MIPMAP) && level == 0)
		glGenerateMipmap(target);
}

static void APIENTRY c_TexImage3D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLsizei d,
	GLint border, GLenum format, GLenum type, const void *pixels)
{
	if (internal == GL_LUMINANCE8)
		internal = GL_R8;
	glTexImage3D(target, level, internal, w, h, d, border, format, type, pixels);
}

static void APIENTRY c_GenBuffers(GLsizei n, GLuint *names)
{
	glGenBuffers(n, names);
	for (GLsizei i = 0; i < n; i++)
		reg_add(K_BUFFER, names[i]);
}

static void APIENTRY c_DeleteBuffers(GLsizei n, const GLuint *names)
{
	for (GLsizei i = 0; i < n; i++)
	{
		reg_remove(K_BUFFER, names[i]);
		if (S.array_buffer == names[i])
			S.array_buffer = 0;
		if (G.array == names[i])
			G.array = 0;
	}
	glDeleteBuffers(n, names);
}

/* ---- GLSL: the compatibility built-ins, as GLSL 3.30 ---------------------------- */

static const char g_vs_prelude[] =
	"#version 330 core\n"
	"in vec4 chimera_Vertex;\n"
	"in vec4 chimera_Color;\n"
	"in vec4 chimera_MultiTexCoord0;\n"
	"in vec4 chimera_MultiTexCoord1;\n"
	"in vec3 chimera_Normal;\n"
	"uniform mat4 chimera_ModelViewMatrix;\n"
	"uniform mat4 chimera_ProjectionMatrix;\n"
	"uniform int chimera_AlphaFunc;\n"
	"uniform float chimera_AlphaRef;\n"
	"out vec4 chimera_FrontColor;\n"
	"out vec4 chimera_TexCoord[2];\n"
	"vec4 chimera_ClipVertex;\n"
	"vec4 chimera_ftransform() { return chimera_ProjectionMatrix * chimera_ModelViewMatrix * chimera_Vertex; }\n";

static const char g_fs_prelude[] =
	"#version 330 core\n"
	"in vec4 chimera_FrontColor;\n"
	"in vec4 chimera_TexCoord[2];\n"
	"out vec4 chimera_FragColor;\n"
	"uniform mat4 chimera_ModelViewMatrix;\n"
	"uniform mat4 chimera_ProjectionMatrix;\n"
	"uniform int chimera_AlphaFunc;\n"
	"uniform float chimera_AlphaRef;\n"
	"bool chimera_alpha_pass(float a)\n"
	"{\n"
	"	if (chimera_AlphaFunc == 0x0200) return false;\n"
	"	if (chimera_AlphaFunc == 0x0201) return a < chimera_AlphaRef;\n"
	"	if (chimera_AlphaFunc == 0x0202) return a == chimera_AlphaRef;\n"
	"	if (chimera_AlphaFunc == 0x0203) return a <= chimera_AlphaRef;\n"
	"	if (chimera_AlphaFunc == 0x0204) return a > chimera_AlphaRef;\n"
	"	if (chimera_AlphaFunc == 0x0205) return a != chimera_AlphaRef;\n"
	"	if (chimera_AlphaFunc == 0x0206) return a >= chimera_AlphaRef;\n"
	"	return true;\n"
	"}\n";

/* after the renderer's own main: the alpha test, as the fixed stage after a
 * shader does it */
static const char g_fs_epilogue[] =
	"\nvoid main()\n"
	"{\n"
	"	chimera_main();\n"
	"	if (!chimera_alpha_pass(chimera_FragColor.a)) discard;\n"
	"}\n";

struct word { const char *from, *to; };

static const struct word g_vs_words[] = {
	{ "gl_Vertex", "chimera_Vertex" },
	{ "gl_Color", "chimera_Color" },
	{ "gl_MultiTexCoord0", "chimera_MultiTexCoord0" },
	{ "gl_MultiTexCoord1", "chimera_MultiTexCoord1" },
	{ "gl_Normal", "chimera_Normal" },
	{ "gl_ModelViewMatrix", "chimera_ModelViewMatrix" },
	{ "gl_ProjectionMatrix", "chimera_ProjectionMatrix" },
	{ "gl_ModelViewProjectionMatrix", "(chimera_ProjectionMatrix * chimera_ModelViewMatrix)" },
	{ "gl_NormalMatrix", "mat3(transpose(inverse(chimera_ModelViewMatrix)))" },
	{ "gl_FrontColor", "chimera_FrontColor" },
	{ "gl_TexCoord", "chimera_TexCoord" },
	{ "gl_ClipVertex", "chimera_ClipVertex" },
	{ "ftransform", "chimera_ftransform" },
	{ "attribute", "in" },
	{ "varying", "out" },
	{ "texture1D", "texture" },
	{ "texture2D", "texture" },
	{ "texture3D", "texture" },
	{ NULL, NULL },
};

static const struct word g_fs_words[] = {
	{ "gl_Color", "chimera_FrontColor" },
	{ "gl_TexCoord", "chimera_TexCoord" },
	{ "gl_FragColor", "chimera_FragColor" },
	{ "gl_ModelViewMatrix", "chimera_ModelViewMatrix" },
	{ "gl_ProjectionMatrix", "chimera_ProjectionMatrix" },
	{ "varying", "in" },
	{ "texture1D", "texture" },
	{ "texture2D", "texture" },
	{ "texture3D", "texture" },
	{ "main", "chimera_main" },
	{ NULL, NULL },
};

static int ident_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/* the source with each whole word in the table replaced and every #version
 * line dropped; the result is malloc'd */
static char *rewrite(const char *src, const struct word *words, const char *prelude, const char *epilogue)
{
	size_t cap = strlen(prelude) + strlen(src) * 2 + strlen(epilogue) + 1024, n = 0;
	char *out = malloc(cap);
	memcpy(out, prelude, strlen(prelude));
	n = strlen(prelude);
	const char *p = src;
	int line_start = 1;
	while (*p)
	{
		if (n + 256 >= cap)
		{
			cap *= 2;
			out = realloc(out, cap);
		}
		if (line_start)
		{
			const char *q = p;
			while (*q == ' ' || *q == '\t')
				q++;
			if (!strncmp(q, "#version", 8))
			{
				while (*p && *p != '\n')
					p++;
				continue;
			}
		}
		if (ident_char(*p) && !(p > src && ident_char(p[-1])))
		{
			const char *e = p;
			while (ident_char(*e))
				e++;
			const size_t len = (size_t)(e - p);
			const struct word *w;
			for (w = words; w->from; w++)
				if (strlen(w->from) == len && !strncmp(p, w->from, len))
					break;
			const char *put = w->from ? w->to : p;
			const size_t put_len = w->from ? strlen(w->to) : len;
			while (n + put_len + 256 >= cap)
			{
				cap *= 2;
				out = realloc(out, cap);
			}
			memcpy(out + n, put, put_len);
			n += put_len;
			p = e;
			line_start = 0;
			continue;
		}
		line_start = *p == '\n';
		out[n++] = *p++;
	}
	if (n + strlen(epilogue) + 1 >= cap)
		out = realloc(out, n + strlen(epilogue) + 1);
	memcpy(out + n, epilogue, strlen(epilogue));
	n += strlen(epilogue);
	out[n] = 0;
	return out;
}

static GLuint APIENTRY c_CreateShader(GLenum type)
{
	const GLuint s = glCreateShader(type);
	*shadertype(s) = type;
	reg_add(K_SHADER, s);
	return s;
}

static void APIENTRY c_DeleteShader(GLuint s)
{
	reg_remove(K_SHADER, s);
	glDeleteShader(s);
}

static void APIENTRY c_ShaderSource(GLuint s, GLsizei count, const GLchar *const *strings, const GLint *lengths)
{
	size_t total = 0;
	for (GLsizei i = 0; i < count; i++)
		total += lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(strings[i]);
	char *src = malloc(total + 1);
	size_t n = 0;
	for (GLsizei i = 0; i < count; i++)
	{
		const size_t l = lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(strings[i]);
		memcpy(src + n, strings[i], l);
		n += l;
	}
	src[n] = 0;
	const int vertex = *shadertype(s) == GL_VERTEX_SHADER;
	char *out = vertex ? rewrite(src, g_vs_words, g_vs_prelude, "") : rewrite(src, g_fs_words, g_fs_prelude, g_fs_epilogue);
	const GLchar *one = out;
	glShaderSource(s, 1, &one, NULL);
	free(out);
	free(src);
}

static GLuint APIENTRY c_CreateProgram(void)
{
	const GLuint p = glCreateProgram();
	memset(proginfo(p), 0, sizeof(struct proginfo));
	reg_add(K_PROGRAM, p);
	return p;
}

static void APIENTRY c_DeleteProgram(GLuint p)
{
	reg_remove(K_PROGRAM, p);
	if (S.program == p)
		S.program = 0;
	if (G.prog == p)
		G.prog = 0;
	glDeleteProgram(p);
}

static void APIENTRY c_AttachShader(GLuint p, GLuint s)
{
	if (*shadertype(s) == GL_VERTEX_SHADER)
		proginfo(p)->vertex = 1;
	else
		proginfo(p)->fragment = 1;
	glAttachShader(p, s);
}

static void prelink(GLuint p)
{
	for (int a = 0; a < A_COUNT; a++)
		glBindAttribLocation(p, a, g_attr_names[a]);
	glBindFragDataLocation(p, 0, "chimera_FragColor");
}

static void postlink(GLuint p)
{
	struct proginfo *pi = proginfo(p);
	pi->mv = glGetUniformLocation(p, "chimera_ModelViewMatrix");
	pi->proj = glGetUniformLocation(p, "chimera_ProjectionMatrix");
	pi->alpha_func = glGetUniformLocation(p, "chimera_AlphaFunc");
	pi->alpha_ref = glGetUniformLocation(p, "chimera_AlphaRef");
	pi->mv_ver = pi->proj_ver = pi->alpha_ver = 0;
}

static void APIENTRY c_LinkProgram(GLuint p)
{
	/* a program of a fragment stage alone is GL 1.x's fixed vertex stage */
	if (!proginfo(p)->vertex && G.default_vs)
		glAttachShader(p, G.default_vs);
	prelink(p);
	glLinkProgram(p);
	postlink(p);
	if (G.prog == p)
		G.prog = 0; /* a relinked program in use is re-bound */
}

/* ---- strings ---------------------------------------------------------------------- */

/* each answer a buffer of its own: the bridge copies a returned string into
 * one buffer of the guest's, so the next call would overwrite the last - and
 * the renderer keeps GL_VERSION and GL_RENDERER both */
static const GLubyte *APIENTRY c_GetString(GLenum name)
{
	static char version[128], renderer[128], vendor[128], glsl[128];
	char *keep = name == GL_VERSION ? version : name == GL_RENDERER ? renderer
		: name == GL_VENDOR ? vendor : name == GL_SHADING_LANGUAGE_VERSION ? glsl : NULL;
	if (name == GL_EXTENSIONS)
		return (const GLubyte *)g_extensions;
	const GLubyte *s = glGetString(name);
	if (!keep || !s)
		return s;
	snprintf(keep, 128, "%s", (const char *)s);
	return (const GLubyte *)keep;
}

/* ---- the fixed stages' program ------------------------------------------------- */

static const char g_fixed_vs[] =
	"void main()\n"
	"{\n"
	"	vec4 eye = chimera_ModelViewMatrix * chimera_Vertex;\n"
	"	gl_Position = chimera_ProjectionMatrix * eye;\n"
	"	chimera_TexCoord[0] = chimera_MultiTexCoord0;\n"
	"	chimera_TexCoord[1] = chimera_MultiTexCoord1;\n"
	"	if (fixed_lighting != 0)\n"
	"	{\n"
	"		vec3 n = mat3(transpose(inverse(chimera_ModelViewMatrix))) * chimera_Normal;\n"
	"		if (fixed_normalize != 0) n = normalize(n);\n"
	"		vec3 l = fixed_light_pos.w == 0.0 ? normalize(fixed_light_pos.xyz) : normalize(fixed_light_pos.xyz - eye.xyz);\n"
	"		vec3 c = fixed_model_ambient.rgb * fixed_mat_ambient.rgb + max(dot(n, l), 0.0) * fixed_mat_diffuse.rgb;\n"
	"		chimera_FrontColor = vec4(clamp(c, 0.0, 1.0), fixed_mat_diffuse.a);\n"
	"	}\n"
	"	else\n"
	"		chimera_FrontColor = chimera_Color;\n"
	"}\n";

static const char g_fixed_vs_uniforms[] =
	"uniform int fixed_lighting;\n"
	"uniform int fixed_normalize;\n"
	"uniform vec4 fixed_light_pos;\n"
	"uniform vec4 fixed_model_ambient;\n"
	"uniform vec4 fixed_mat_ambient;\n"
	"uniform vec4 fixed_mat_diffuse;\n";

static const char g_fixed_fs[] =
	"uniform sampler2D fixed_tex0;\n"
	"uniform sampler2D fixed_tex1;\n"
	"uniform int fixed_tex_on[2];\n"
	"uniform int fixed_tex_replace[2];\n"
	"uniform int fixed_alpha_only[2];\n"
	"vec4 chimera_env(vec4 c, vec4 t, int replace, int alpha_only)\n"
	"{\n"
	"	if (alpha_only != 0) return replace != 0 ? vec4(c.rgb, t.a) : vec4(c.rgb, c.a * t.a);\n"
	"	return replace != 0 ? t : c * t;\n"
	"}\n"
	"void main()\n"
	"{\n"
	"	vec4 c = chimera_FrontColor;\n"
	"	if (fixed_tex_on[0] != 0) c = chimera_env(c, texture(fixed_tex0, chimera_TexCoord[0].st), fixed_tex_replace[0], fixed_alpha_only[0]);\n"
	"	if (fixed_tex_on[1] != 0) c = chimera_env(c, texture(fixed_tex1, chimera_TexCoord[1].st), fixed_tex_replace[1], fixed_alpha_only[1]);\n"
	"	chimera_FragColor = c;\n"
	"}\n";

/* a vertex stage for programs given none: GL 1.x's, without the light */
static const char g_default_vs[] =
	"void main()\n"
	"{\n"
	"	gl_Position = chimera_ProjectionMatrix * chimera_ModelViewMatrix * chimera_Vertex;\n"
	"	chimera_FrontColor = chimera_Color;\n"
	"	chimera_TexCoord[0] = chimera_MultiTexCoord0;\n"
	"	chimera_TexCoord[1] = chimera_MultiTexCoord1;\n"
	"}\n";

static GLuint compile(GLenum type, const char *a, const char *b, const char *c)
{
	const GLuint s = glCreateShader(type);
	const GLchar *parts[3] = { a, b, c };
	glShaderSource(s, 3, parts, NULL);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		char log[1024] = "";
		glGetShaderInfoLog(s, sizeof log, NULL, log);
		fprintf(stderr, "chimera gl: a shader of the fixed stages did not compile: %s\n", log);
	}
	reg_add(K_SHADER, s);
	return s;
}

static int make_fixed(void)
{
	G.default_vs = compile(GL_VERTEX_SHADER, g_vs_prelude, "", g_default_vs);
	G.fixed_vs = compile(GL_VERTEX_SHADER, g_vs_prelude, g_fixed_vs_uniforms, g_fixed_vs);
	/* the fixed fragment stage carries the alpha test as the rewritten ones do */
	char *fs = rewrite(g_fixed_fs, g_fs_words, g_fs_prelude, g_fs_epilogue);
	G.fixed_fs = compile(GL_FRAGMENT_SHADER, fs, "", "");
	free(fs);
	G.fixed = glCreateProgram();
	reg_add(K_PROGRAM, G.fixed);
	glAttachShader(G.fixed, G.fixed_vs);
	glAttachShader(G.fixed, G.fixed_fs);
	prelink(G.fixed);
	glLinkProgram(G.fixed);
	GLint ok = 0;
	glGetProgramiv(G.fixed, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		char log[1024] = "";
		glGetProgramInfoLog(G.fixed, sizeof log, NULL, log);
		fprintf(stderr, "chimera gl: the fixed stages' program did not link: %s\n", log);
		return 0;
	}
	memset(proginfo(G.fixed), 0, sizeof(struct proginfo));
	postlink(G.fixed);
	G.u.tex_on[0] = glGetUniformLocation(G.fixed, "fixed_tex_on[0]");
	G.u.tex_on[1] = glGetUniformLocation(G.fixed, "fixed_tex_on[1]");
	G.u.tex_mode[0] = glGetUniformLocation(G.fixed, "fixed_tex_replace[0]");
	G.u.tex_mode[1] = glGetUniformLocation(G.fixed, "fixed_tex_replace[1]");
	G.u.alpha_only[0] = glGetUniformLocation(G.fixed, "fixed_alpha_only[0]");
	G.u.alpha_only[1] = glGetUniformLocation(G.fixed, "fixed_alpha_only[1]");
	G.u.sampler[0] = glGetUniformLocation(G.fixed, "fixed_tex0");
	G.u.sampler[1] = glGetUniformLocation(G.fixed, "fixed_tex1");
	G.u.lighting = glGetUniformLocation(G.fixed, "fixed_lighting");
	G.u.normalize = glGetUniformLocation(G.fixed, "fixed_normalize");
	G.u.light_pos = glGetUniformLocation(G.fixed, "fixed_light_pos");
	G.u.model_ambient = glGetUniformLocation(G.fixed, "fixed_model_ambient");
	G.u.mat_ambient = glGetUniformLocation(G.fixed, "fixed_mat_ambient");
	G.u.mat_diffuse = glGetUniformLocation(G.fixed, "fixed_mat_diffuse");
	glUseProgram(G.fixed);
	glUniform1i(G.u.sampler[0], 0);
	glUniform1i(G.u.sampler[1], 1);
	G.prog = G.fixed;
	G.fixed_ver = 0;
	return 1;
}

/* ---- the framebuffer ------------------------------------------------------------- */

static void make_framebuffer(int w, int h)
{
	glGenFramebuffers(1, &G.fbo);
	glGenRenderbuffers(1, &G.color);
	glGenRenderbuffers(1, &G.depth);
	reg_add(K_FRAMEBUFFER, G.fbo);
	reg_add(K_RENDERBUFFER, G.color);
	reg_add(K_RENDERBUFFER, G.depth);
	glBindRenderbuffer(GL_RENDERBUFFER, G.color);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
	glBindRenderbuffer(GL_RENDERBUFFER, G.depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
	glBindFramebuffer(GL_FRAMEBUFFER, G.fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, G.color);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, G.depth);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		fprintf(stderr, "chimera gl: the %dx%d framebuffer is not complete\n", w, h);
	G.w = w;
	G.h = h;
}

/* ---- the platform's calls ------------------------------------------------------- */

static void default_state(void)
{
	memset(&S, 0, sizeof S);
	S.mode = GL_MODELVIEW;
	mat_identity(S.mv[0]);
	mat_identity(S.proj[0]);
	mat_identity(S.texm[0]);
	S.mv_ver = S.proj_ver = S.alpha_ver = 1;
	S.env[0] = S.env[1] = GL_MODULATE;
	S.alpha_func = GL_ALWAYS;
	for (int a = 0; a < A_COUNT; a++)
		S.cur[a][3] = 1.0f;
	S.cur[A_COLOR][0] = S.cur[A_COLOR][1] = S.cur[A_COLOR][2] = 1.0f;
	S.cur[A_NORMAL][2] = 1.0f;
	S.light_pos[2] = 1.0f;
	S.model_ambient[0] = S.model_ambient[1] = S.model_ambient[2] = 0.2f;
	S.model_ambient[3] = 1.0f;
	S.mat_ambient[0] = S.mat_ambient[1] = S.mat_ambient[2] = 0.2f;
	S.mat_ambient[3] = 1.0f;
	S.mat_diffuse[0] = S.mat_diffuse[1] = S.mat_diffuse[2] = 0.8f;
	S.mat_diffuse[3] = 1.0f;
}

/* this file's objects in the current context, and the driver's state made to
 * agree with S (what the renderer set) */
static int make_objects(int w, int h)
{
	memset(G.attr_on, 0, sizeof G.attr_on);
	memset(G.attr_cur_known, 0, sizeof G.attr_cur_known);
	glGenVertexArrays(1, &G.vao);
	reg_add(K_VERTEX_ARRAY, G.vao);
	glBindVertexArray(G.vao);
	glGenBuffers(A_COUNT, G.vbo);
	glGenBuffers(1, &G.ebo);
	for (int a = 0; a < A_COUNT; a++)
		reg_add(K_BUFFER, G.vbo[a]);
	reg_add(K_BUFFER, G.ebo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, G.ebo);
	G.array = 0;
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	make_framebuffer(w, h);
	if (!make_fixed())
		return 0;
	/* the texture units' bindings and the active unit, as the renderer left
	 * them (its own state comes back with SetStates) */
	glActiveTexture(GL_TEXTURE0 + (S.unit < 0 ? 0 : S.unit));
	return 1;
}

int glc_start(void *bridge, int w, int h)
{
	if (!chimera_gl_install((chimera_gl_bridge_fn)bridge))
		return 0;
	const char *version = (const char *)glGetString(GL_VERSION);
	if (!version || (version[0] < '3' && version[1] == '.'))
	{
		fprintf(stderr, "chimera gl: the bridge's context is OpenGL %s; 3.3 is needed\n", version ? version : "?");
		return 0;
	}
	GLint n = 0;
	glGetIntegerv(GL_NUM_EXTENSIONS, &n);
	g_extensions[0] = 0;
	for (GLint i = 0; i < n; i++)
	{
		const char *e = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
		if (e && !strcmp(e, "GL_EXT_texture_filter_anisotropic"))
			strcat(g_extensions, "GL_EXT_texture_filter_anisotropic ");
	}
	default_state();
	G.ok = make_objects(w, h);
	return G.ok;
}

/* a new size: the framebuffer again */
void glc_surface(int w, int h)
{
	if (!G.ok || (w == G.w && h == G.h))
		return;
	GLuint dead[2] = { G.color, G.depth };
	glDeleteFramebuffers(1, &G.fbo);
	glDeleteRenderbuffers(2, dead);
	reg_remove(K_FRAMEBUFFER, G.fbo);
	reg_remove(K_RENDERBUFFER, G.color);
	reg_remove(K_RENDERBUFFER, G.depth);
	make_framebuffer(w, h);
}

uint64_t glc_context_id(void) { return chimera_gl_context_id(); }

/* the context moved: every object this core made in the driver deleted -
 * the renderer's and this file's, by the list a savestate does not reach -
 * before anything is made again. In a context that is new, they are gone
 * already and the deletes name nothing. */
void glc_forget(void)
{
	glBindVertexArray(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glUseProgram(0);
	if (g_reg_n[K_TEXTURE]) glDeleteTextures((GLsizei)g_reg_n[K_TEXTURE], g_reg[K_TEXTURE]);
	if (g_reg_n[K_BUFFER]) glDeleteBuffers((GLsizei)g_reg_n[K_BUFFER], g_reg[K_BUFFER]);
	if (g_reg_n[K_VERTEX_ARRAY]) glDeleteVertexArrays((GLsizei)g_reg_n[K_VERTEX_ARRAY], g_reg[K_VERTEX_ARRAY]);
	if (g_reg_n[K_FRAMEBUFFER]) glDeleteFramebuffers((GLsizei)g_reg_n[K_FRAMEBUFFER], g_reg[K_FRAMEBUFFER]);
	if (g_reg_n[K_RENDERBUFFER]) glDeleteRenderbuffers((GLsizei)g_reg_n[K_RENDERBUFFER], g_reg[K_RENDERBUFFER]);
	for (uint32_t i = 0; i < g_reg_n[K_PROGRAM]; i++) glDeleteProgram(g_reg[K_PROGRAM][i]);
	for (uint32_t i = 0; i < g_reg_n[K_SHADER]; i++) glDeleteShader(g_reg[K_SHADER][i]);
	memset(g_reg_n, 0, sizeof g_reg_n);
	/* every name the renderer keeps is about to be forgotten: so are the
	 * buffers and programs it had bound */
	S.array_buffer = 0;
	S.program = 0;
	S.bound[0] = S.bound[1] = 0;
	for (int a = 0; a < A_COUNT; a++)
		if (S.arr[a].buffer)
			S.arr[a].enabled = 0, S.arr[a].buffer = 0;
	if (g_texflags)
		memset(g_texflags, 0, g_texflags_n);
	if (g_prog)
		memset(g_prog, 0, g_prog_n * sizeof *g_prog);
	G.ok = 0;
}

/* this file's objects again, in the context that is current */
int glc_restore(int w, int h)
{
	G.ok = make_objects(w, h);
	return G.ok;
}

/* the frame, BGRA, top row first, from the framebuffer */
void glc_read_frame(uint32_t *out, uint32_t *scratch, int w, int h)
{
	glBindFramebuffer(GL_READ_FRAMEBUFFER, G.fbo);
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, scratch);
	for (int y = 0; y < h; y++)
	{
		const uint32_t *src = scratch + (size_t)(h - 1 - y) * w;
		uint32_t *dst = out + (size_t)y * w;
		for (int x = 0; x < w; x++)
			dst[x] = src[x] | 0xFF000000u;
	}
}

void glc_finish(void) { glFinish(); }

/* ---- the names r_opengl asks for ------------------------------------------------- */

struct proc { const char *name; void *fn; };
static const struct proc g_procs[] = {
	{ "glMatrixMode", (void *)c_MatrixMode },
	{ "glLoadIdentity", (void *)c_LoadIdentity },
	{ "glMultMatrixf", (void *)c_MultMatrixf },
	{ "glPushMatrix", (void *)c_PushMatrix },
	{ "glPopMatrix", (void *)c_PopMatrix },
	{ "glTranslatef", (void *)c_Translatef },
	{ "glScalef", (void *)c_Scalef },
	{ "glRotatef", (void *)c_Rotatef },
	{ "glGetFloatv", (void *)c_GetFloatv },
	{ "glGetIntegerv", (void *)c_GetIntegerv },
	{ "glEnable", (void *)c_Enable },
	{ "glDisable", (void *)c_Disable },
	{ "glAlphaFunc", (void *)c_AlphaFunc },
	{ "glTexEnvi", (void *)c_TexEnvi },
	{ "glShadeModel", (void *)c_ShadeModel },
	{ "glLightfv", (void *)c_Lightfv },
	{ "glLightModelfv", (void *)c_LightModelfv },
	{ "glMaterialfv", (void *)c_Materialfv },
	{ "glMateriali", (void *)c_Materiali },
	{ "glEnableClientState", (void *)c_EnableClientState },
	{ "glDisableClientState", (void *)c_DisableClientState },
	{ "glVertexPointer", (void *)c_VertexPointer },
	{ "glColorPointer", (void *)c_ColorPointer },
	{ "glNormalPointer", (void *)c_NormalPointer },
	{ "glTexCoordPointer", (void *)c_TexCoordPointer },
	{ "glClientActiveTexture", (void *)c_ClientActiveTexture },
	{ "glColor4ubv", (void *)c_Color4ubv },
	{ "glMultiTexCoord2f", (void *)c_MultiTexCoord2f },
	{ "glMultiTexCoord2fv", (void *)c_MultiTexCoord2fv },
	{ "glBindBuffer", (void *)c_BindBuffer },
	{ "glGenBuffers", (void *)c_GenBuffers },
	{ "glDeleteBuffers", (void *)c_DeleteBuffers },
	{ "glUseProgram", (void *)c_UseProgram },
	{ "glDrawArrays", (void *)c_DrawArrays },
	{ "glDrawElements", (void *)c_DrawElements },
	{ "glActiveTexture", (void *)c_ActiveTexture },
	{ "glBindTexture", (void *)c_BindTexture },
	{ "glGenTextures", (void *)c_GenTextures },
	{ "glDeleteTextures", (void *)c_DeleteTextures },
	{ "glTexParameteri", (void *)c_TexParameteri },
	{ "glTexImage2D", (void *)c_TexImage2D },
	{ "glTexSubImage2D", (void *)c_TexSubImage2D },
	{ "glTexImage3D", (void *)c_TexImage3D },
	{ "glCreateShader", (void *)c_CreateShader },
	{ "glDeleteShader", (void *)c_DeleteShader },
	{ "glShaderSource", (void *)c_ShaderSource },
	{ "glCreateProgram", (void *)c_CreateProgram },
	{ "glDeleteProgram", (void *)c_DeleteProgram },
	{ "glAttachShader", (void *)c_AttachShader },
	{ "glLinkProgram", (void *)c_LinkProgram },
	{ "glGetString", (void *)c_GetString },
	{ NULL, NULL },
};

void *glc_proc(const char *name)
{
	for (const struct proc *p = g_procs; p->name; p++)
		if (!strcmp(p->name, name))
			return p->fn;
	/* the rest are core GL's, and the bridge's wrappers */
	return chimera_gl_lookup(name);
}

#endif /* CHIMERA_GUEST */
