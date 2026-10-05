/* i_video.c - SRB2's video layer without a window (upstream's sdl/i_video.c,
 * less SDL): the engine draws its 8-bit screens with the software renderer
 * as upstream does, into a buffer of the machine's own, and the core turns
 * screens[0] into BGRA through the palette the engine last set
 * (chimera_video_bgra). Or upstream's OpenGL renderer, on the GL that
 * ogl_chimera.c gives it, its frame already BGRA.
 *
 * One renderer, the machine's: the renderer setting
 * (chimera_video_set_renderer, before the start). The renderer is part of the
 * game - a few things play differently in OpenGL (A_OverlayThink, the orbital
 * camera) - so it is the project's: a switch the game asks for later (the
 * video menu, a configuration's "renderer") is refused. Where OpenGL cannot
 * start (the native reference has none) the machine draws in software and
 * says so.
 *
 * One mode, the machine's: the resolution setting (chimera_video_set_mode,
 * before the start; 320x200 if none). Whatever mode the engine asks for - a
 * configuration's scr_width, the video menu - it gets that one, so the
 * picture's size is the project's. The resolution is the picture's alone: the
 * game is the same at every one (the gate's resolution leg). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "command.h"
#include "i_system.h"
#include "i_video.h"
#include "screen.h"
#include "v_video.h"
#include "console.h"
#include "s_sound.h"
#include "netcode/d_clisrv.h"
#include "netcode/d_netcmd.h"
#include "hardware/hw_main.h"
#include "hardware/hw_drv.h"
#include "hardware/hw_shaders.h"

#include "chimera-platform.h"

/* platform/ogl_chimera.c: upstream's sdl/ogl_sdl.h's and hwsym_sdl.h's */
boolean OglSdlSurface(INT32 w, INT32 h);
void OglSdlFinishUpdate(boolean waitvbl);
void *hwSym(const char *funcName, void *handle);
const UINT32 *chimera_gl_frame(void);

rendermode_t rendermode = render_soft;
rendermode_t chosenrendermode = render_soft;

boolean allow_fullscreen = false;

consvar_t cv_vidwait = CVAR_INIT ("vid_wait", "Off", CV_SAVE, CV_OnOff, NULL);

static RGBA_t g_palette[256];
static INT32 g_mode_w = BASEVIDWIDTH, g_mode_h = BASEVIDHEIGHT;
static int g_want_gl;

void chimera_video_set_renderer(int opengl)
{
	g_want_gl = opengl ? 1 : 0;
}

int chimera_video_opengl(void)
{
	return rendermode == render_opengl;
}

void chimera_video_set_mode(int w, int h)
{
	if (w >= BASEVIDWIDTH && h >= BASEVIDHEIGHT && w <= MAXVIDWIDTH && h <= MAXVIDHEIGHT)
	{
		g_mode_w = w;
		g_mode_h = h;
	}
}

void I_StartupGraphics(void)
{
	if (graphics_started)
		return;
	CV_RegisterVar(&cv_vidwait);
	keyboard_started = true;

	/* as upstream's -renderer: chosen at the command line, so the start
	 * keeps it whatever a configuration says (SCR_SetMode) */
	chosenrendermode = rendermode = g_want_gl ? render_opengl : render_soft;
	if (rendermode == render_opengl)
	{
		VID_StartupOpenGL();
		if (vid.glstate != VID_GL_LIBRARY_LOADED)
		{
			fprintf(stderr, "chimera: OpenGL did not start; drawing in software\n");
			chosenrendermode = rendermode = render_soft;
		}
	}

	vid.width = g_mode_w;
	vid.height = g_mode_h;
	vid.recalc = true;
	vid.direct = NULL;
	vid.bpp = 1;
	vid.WndParent = NULL;
	VID_SetMode(0);

	graphics_started = true;
}

void I_ShutdownGraphics(void) {}

/* upstream's: the renderer's entry points by name (hwSym, ogl_chimera.c,
 * as sdl/hwsym_sdl.c finds them linked in), then its Init, which loads the GL */
void VID_StartupOpenGL(void)
{
	static boolean glstartup = false;
	if (glstartup)
		return;
	glstartup = true;
	HWD.pfnInit = hwSym("Init", NULL);
	HWD.pfnFinishUpdate = NULL;
	HWD.pfnDraw2DLine = hwSym("Draw2DLine", NULL);
	HWD.pfnDrawPolygon = hwSym("DrawPolygon", NULL);
	HWD.pfnDrawIndexedTriangles = hwSym("DrawIndexedTriangles", NULL);
	HWD.pfnRenderSkyDome = hwSym("RenderSkyDome", NULL);
	HWD.pfnSetBlend = hwSym("SetBlend", NULL);
	HWD.pfnClearBuffer = hwSym("ClearBuffer", NULL);
	HWD.pfnSetTexture = hwSym("SetTexture", NULL);
	HWD.pfnUpdateTexture = hwSym("UpdateTexture", NULL);
	HWD.pfnDeleteTexture = hwSym("DeleteTexture", NULL);
	HWD.pfnReadScreenTexture = hwSym("ReadScreenTexture", NULL);
	HWD.pfnGClipRect = hwSym("GClipRect", NULL);
	HWD.pfnClearMipMapCache = hwSym("ClearMipMapCache", NULL);
	HWD.pfnSetSpecialState = hwSym("SetSpecialState", NULL);
	HWD.pfnSetTexturePalette = hwSym("SetTexturePalette", NULL);
	HWD.pfnGetTextureUsed = hwSym("GetTextureUsed", NULL);
	HWD.pfnDrawModel = hwSym("DrawModel", NULL);
	HWD.pfnCreateModelVBOs = hwSym("CreateModelVBOs", NULL);
	HWD.pfnSetTransform = hwSym("SetTransform", NULL);
	HWD.pfnPostImgRedraw = hwSym("PostImgRedraw", NULL);
	HWD.pfnFlushScreenTextures = hwSym("FlushScreenTextures", NULL);
	HWD.pfnDoScreenWipe = hwSym("DoScreenWipe", NULL);
	HWD.pfnDrawScreenTexture = hwSym("DrawScreenTexture", NULL);
	HWD.pfnMakeScreenTexture = hwSym("MakeScreenTexture", NULL);
	HWD.pfnDrawScreenFinalTexture = hwSym("DrawScreenFinalTexture", NULL);
	HWD.pfnInitShaders = hwSym("InitShaders", NULL);
	HWD.pfnLoadShader = hwSym("LoadShader", NULL);
	HWD.pfnCompileShader = hwSym("CompileShader", NULL);
	HWD.pfnSetShader = hwSym("SetShader", NULL);
	HWD.pfnUnSetShader = hwSym("UnSetShader", NULL);
	HWD.pfnSetShaderInfo = hwSym("SetShaderInfo", NULL);
	HWD.pfnSetPaletteLookup = hwSym("SetPaletteLookup", NULL);
	HWD.pfnCreateLightTable = hwSym("CreateLightTable", NULL);
	HWD.pfnUpdateLightTable = hwSym("UpdateLightTable", NULL);
	HWD.pfnClearLightTables = hwSym("ClearLightTables", NULL);
	HWD.pfnSetScreenPalette = hwSym("SetScreenPalette", NULL);

	vid.glstate = HWD.pfnInit() ? VID_GL_LIBRARY_LOADED : VID_GL_LIBRARY_ERROR;
}

void I_SetPalette(RGBA_t *palette)
{
	memcpy(g_palette, palette, sizeof g_palette);
}

/* the one mode */
INT32 VID_NumModes(void) { return 1; }
INT32 VID_GetModeForSize(INT32 w, INT32 h)
{
	(void)w; (void)h;
	return 0;
}
void VID_PrepareModeList(void) {}
const char *VID_GetModeName(INT32 modenum)
{
	static char name[16];
	(void)modenum;
	snprintf(name, sizeof name, "%dx%d", g_mode_w, g_mode_h);
	return name;
}

INT32 VID_SetMode(INT32 modenum)
{
	(void)modenum;
	vid.recalc = 1;
	vid.bpp = 1;
	vid.width = g_mode_w;
	vid.height = g_mode_h;
	vid.modenum = 0;
	VID_CheckRenderer();
	return 1;
}

/* the software renderer's buffer: all NUMSCREENS screens, as upstream's
 * Impl_VideoSetupBuffer lays them out */
boolean VID_CheckRenderer(void)
{
	/* the machine's renderer is the project's: a switch is refused */
	if (setrenderneeded)
	{
		setrenderneeded = 0;
		CV_StealthSetValue(&cv_renderer, rendermode);
	}
	vid.rowbytes = vid.width * vid.bpp;
	vid.direct = NULL;
	free(vid.buffer);
	vid.buffer = calloc(NUMSCREENS, vid.rowbytes * vid.height);
	if (!vid.buffer)
		I_Error("Not enough memory for video buffer\n");
	SCR_SetDrawFuncs();
	if (rendermode == render_opengl)
		OglSdlSurface(vid.width, vid.height);
	return false;
}

void VID_CheckGLLoaded(rendermode_t oldrender) { (void)oldrender; }

UINT32 I_GetRefreshRate(void) { return TICRATE; }

/* upstream's I_FinishUpdate draws its overlays onto the screen, then presents
 * it: the overlays are the picture's, and the frontend takes the frame when it
 * wants it, so there is nothing to present (nor a frame to skip) */
void I_UpdateNoBlit(void) {}
void I_FinishUpdate(void)
{
	SCR_CalculateFPS();
	if (marathonmode)
		SCR_DisplayMarathonInfo();
	if (cv_closedcaptioning.value)
		SCR_ClosedCaptions();
	if (cv_ticrate.value)
		SCR_DisplayTicRate();
	if (cv_showping.value && netgame && consoleplayer != serverplayer)
		SCR_DisplayLocalPing();
	if (rendermode == render_opengl)
	{
		// Final postprocess step of palette rendering, after everything else has been drawn.
		if (HWR_ShouldUsePaletteRendering())
		{
			HWD.pfnMakeScreenTexture(HWD_SCREENTEXTURE_GENERIC2);
			HWD.pfnSetShader(HWR_GetShaderFromTarget(SHADER_PALETTE_POSTPROCESS));
			HWD.pfnDrawScreenTexture(HWD_SCREENTEXTURE_GENERIC2, NULL, 0);
			HWD.pfnUnSetShader();
		}
		OglSdlFinishUpdate(cv_vidwait.value);
	}
}
void I_UpdateNoVsync(void) {}
void I_WaitVBL(INT32 count) { (void)count; }
void I_ReadScreen(UINT8 *scr)
{
	if (rendermode == render_soft && screens[0])
		memcpy(scr, screens[0], vid.width * vid.height);
}
void I_BeginRead(void) {}
void I_EndRead(void) {}

/* ---- the frame, for the frontend */

int chimera_video_width(void) { return vid.width; }
int chimera_video_height(void) { return vid.height; }

void chimera_video_bgra(uint32_t *out)
{
	uint32_t lut[256];
	if (rendermode == render_opengl)
	{
		const UINT32 *frame = chimera_gl_frame();
		if (frame)
			memcpy(out, frame, sizeof(uint32_t) * vid.width * vid.height);
		else
			memset(out, 0, sizeof(uint32_t) * vid.width * vid.height);
		return;
	}
	if (!screens[0])
	{
		memset(out, 0, sizeof(uint32_t) * vid.width * vid.height);
		return;
	}
	for (int i = 0; i < 256; i++)
		lut[i] = 0xFF000000u | ((uint32_t)g_palette[i].s.red << 16) | ((uint32_t)g_palette[i].s.green << 8)
			| g_palette[i].s.blue;
	for (int y = 0; y < vid.height; y++)
	{
		const UINT8 *src = screens[0] + (size_t)y * vid.rowbytes;
		uint32_t *dst = out + (size_t)y * vid.width;
		for (int x = 0; x < vid.width; x++)
			dst[x] = lut[src[x]];
	}
}
