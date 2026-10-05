/* ogl_chimera.c - SRB2's OpenGL renderer without a window (upstream's
 * sdl/ogl_sdl.c, less SDL). The renderer (hardware/, r_opengl/) is upstream's,
 * unchanged: it reaches OpenGL only through GetGLFunc, by name, and this file
 * answers it.
 *
 * In the guest the answer is Mesa's softpipe behind OSMesa, compiled into the
 * core (waterbox/setup-mesa.sh): plain C, no JIT, no dispatch on the host's
 * CPU, so the picture is decided by code the core carries and is the same on
 * every machine, and every byte of the GL's state - textures, shaders, the
 * framebuffer - is guest memory, in every savestate like the rest of the
 * machine. The native reference has no OpenGL: LoadGL fails, and the engine
 * stays on the software renderer (i_video.c says so).
 *
 * The frame: OSMesa draws into a buffer of the machine's own, BGRA and top
 * row first (OSMESA_Y_UP 0), which is the frontend's layout. A finished frame
 * is copied out (chimera_gl_frame) before the renderer draws the screen
 * texture back for the next frame's effects, as upstream does after its swap. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the renderer's Init, as SRB2's objects are built (sources.mk's
 * SRB2_RENAMES): the core's own Init is its export */
#define Init r_opengl_Init

#include "doomdef.h"
#include "doomstat.h"
#include "command.h"
#include "console.h"
#include "i_video.h"
#include "screen.h"
#include "v_video.h"
#include "hardware/r_opengl/r_opengl.h"
#include "hardware/hw_main.h"

#include "chimera-platform.h"

#ifdef CHIMERA_GUEST
/* OSMesa's entry points, declared rather than included: <GL/osmesa.h> brings
 * its own gl.h, which r_opengl.h already has */
typedef struct osmesa_context *OSMesaContext;
typedef void (*OSMESAproc)(void);
OSMesaContext OSMesaCreateContextExt(GLenum format, GLint depthBits, GLint stencilBits, GLint accumBits,
	OSMesaContext sharelist);
GLboolean OSMesaMakeCurrent(OSMesaContext ctx, void *buffer, GLenum type, GLsizei width, GLsizei height);
void OSMesaPixelStore(GLint pname, GLint value);
OSMESAproc OSMesaGetProcAddress(const char *funcName);
#define OSMESA_BGRA 0x1
#define OSMESA_Y_UP 0x11

static OSMesaContext g_ctx;
#endif

/* what upstream's ogl_sdl.c defines for r_opengl */
PFNglClear pglClear;
PFNglGetIntegerv pglGetIntegerv;
PFNglGetString pglGetString;
INT32 oglflags = 0;

typedef void (APIENTRY *PFNglFinish)(void);
static PFNglFinish pglFinish;

static UINT32 *g_surface; /* what OSMesa draws into */
static UINT32 *g_frame;   /* the last finished frame */
static INT32 g_w, g_h;

void *GetGLFunc(const char *proc)
{
#ifdef CHIMERA_GUEST
	return g_ctx ? (void *)OSMesaGetProcAddress(proc) : NULL;
#else
	(void)proc;
	return NULL;
#endif
}

/* the context, made current on a first surface (OSMesa has no current
 * context without a buffer), then the renderer's functions */
boolean LoadGL(void)
{
#ifdef CHIMERA_GUEST
	if (!g_ctx)
	{
		/* 24-bit depth, 8-bit stencil, as a desktop's default framebuffer */
		g_ctx = OSMesaCreateContextExt(OSMESA_BGRA, 24, 8, 0, NULL);
		if (!g_ctx)
			return false;
		g_w = BASEVIDWIDTH;
		g_h = BASEVIDHEIGHT;
		g_surface = calloc((size_t)g_w * g_h, 4);
		if (!g_surface || !OSMesaMakeCurrent(g_ctx, g_surface, GL_UNSIGNED_BYTE, g_w, g_h))
			return false;
		OSMesaPixelStore(OSMESA_Y_UP, 0);
	}
	pglFinish = (PFNglFinish)GetGLFunc("glFinish");
	if (!pglFinish)
		return false;
	return SetupGLfunc();
#else
	return false;
#endif
}

/* upstream's OglSdlSurface: the surface at the mode's size, then the
 * renderer's states and its start (HWR_Startup, once) */
boolean OglSdlSurface(INT32 w, INT32 h)
{
	static boolean first_init = false;
	int majorGL = 0, minorGL = 0;

#ifdef CHIMERA_GUEST
	if (!g_ctx)
		return false;
	if (w != g_w || h != g_h)
	{
		UINT32 *surface = calloc((size_t)w * h, 4);
		if (!surface || !OSMesaMakeCurrent(g_ctx, surface, GL_UNSIGNED_BYTE, w, h))
			I_Error("OpenGL: no %dx%d surface", w, h);
		OSMesaPixelStore(OSMESA_Y_UP, 0);
		free(g_surface);
		g_surface = surface;
		g_w = w;
		g_h = h;
	}
	free(g_frame);
	g_frame = calloc((size_t)w * h, 4);
#endif

	oglflags = 0;
	if (!first_init)
	{
		gl_version = pglGetString(GL_VERSION);
		gl_renderer = pglGetString(GL_RENDERER);
		gl_extensions = pglGetString(GL_EXTENSIONS);
		CONS_Printf("OpenGL %s, %s\n", gl_version, gl_renderer);
	}
	first_init = true;

	if (isExtAvailable("GL_EXT_texture_filter_anisotropic", gl_extensions))
		pglGetIntegerv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maximumAnisotropy);
	else
		maximumAnisotropy = 1;

	if (sscanf((const char *)gl_version, "%d.%d", &majorGL, &minorGL) && (!(majorGL == 1 && minorGL <= 3)))
		supportMipMap = true;
	else
		supportMipMap = false;

	SetupGLFunc4();

	glanisotropicmode_cons_t[1].value = maximumAnisotropy;

	SetModelView(w, h);
	SetStates();
	pglClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	HWR_Startup();
	textureformatGL = GL_RGBA;
	return true;
}

/* upstream's OglSdlFinishUpdate: the final screen texture drawn into the
 * surface, which is then the frame; the swap is the copy */
void OglSdlFinishUpdate(boolean waitvbl)
{
	(void)waitvbl;
	HWR_MakeScreenFinalTexture();
	HWR_DrawScreenFinalTexture(vid.width, vid.height);
	pglFinish();
	if (g_frame && g_surface)
	{
		const size_t n = (size_t)g_w * g_h;
		for (size_t i = 0; i < n; i++)
			g_frame[i] = g_surface[i] | 0xFF000000u;
	}

	GClipRect(0, 0, vid.width, vid.height, NZCLIP_PLANE);

	// Sryder:	We need to draw the final screen texture again into the other buffer in the original position so that
	//			effects that want to take the old screen can do so after this
	// Generic2 has the screen image without palette rendering brightness adjustments.
	// Using that here will prevent brightness adjustments being applied twice.
	DrawScreenTexture(HWD_SCREENTEXTURE_GENERIC2, NULL, 0);
}

EXPORT void HWRAPI(OglSdlSetPalette) (RGBA_t *palette)
{
	size_t palsize = (sizeof(RGBA_t) * 256);
	// on a palette change, you have to reload all of the textures
	if (memcmp(&myPaletteData, palette, palsize))
	{
		memcpy(&myPaletteData, palette, palsize);
		Flush();
	}
}

/* upstream's sdl/hwsym_sdl.c: the renderer's entry points by name, for
 * i_video.c's VID_StartupOpenGL (only a file that includes r_opengl.h, with
 * _CREATE_DLL_, sees them as functions) */
#define GETFUNC(func) \
	else if (0 == strcmp(#func, funcName)) \
		funcPointer = &func \

void *hwSym(const char *funcName, void *handle)
{
	void *funcPointer = NULL;
	(void)handle;
	if (0 == strcmp("SetTexturePalette", funcName))
		funcPointer = &OglSdlSetPalette;

	GETFUNC(Init);
	GETFUNC(Draw2DLine);
	GETFUNC(DrawPolygon);
	GETFUNC(DrawIndexedTriangles);
	GETFUNC(RenderSkyDome);
	GETFUNC(SetBlend);
	GETFUNC(ClearBuffer);
	GETFUNC(SetTexture);
	GETFUNC(UpdateTexture);
	GETFUNC(DeleteTexture);
	GETFUNC(ReadScreenTexture);
	GETFUNC(GClipRect);
	GETFUNC(ClearMipMapCache);
	GETFUNC(SetSpecialState);
	GETFUNC(GetTextureUsed);
	GETFUNC(DrawModel);
	GETFUNC(CreateModelVBOs);
	GETFUNC(SetTransform);
	GETFUNC(PostImgRedraw);
	GETFUNC(FlushScreenTextures);
	GETFUNC(DoScreenWipe);
	GETFUNC(DrawScreenTexture);
	GETFUNC(MakeScreenTexture);
	GETFUNC(DrawScreenFinalTexture);

	GETFUNC(InitShaders);
	GETFUNC(LoadShader);
	GETFUNC(CompileShader);
	GETFUNC(SetShader);
	GETFUNC(UnSetShader);

	GETFUNC(SetShaderInfo);

	GETFUNC(SetPaletteLookup);
	GETFUNC(CreateLightTable);
	GETFUNC(UpdateLightTable);
	GETFUNC(ClearLightTables);
	GETFUNC(SetScreenPalette);

	if (!funcPointer)
		I_Error("hwSym: no renderer function %s", funcName);
	return funcPointer;
}

/* the last finished frame, BGRA, top row first; NULL before the first */
const UINT32 *chimera_gl_frame(void)
{
	return g_frame;
}
