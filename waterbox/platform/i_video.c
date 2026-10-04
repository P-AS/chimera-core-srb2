/* i_video.c - SRB2's video layer without a window (upstream's sdl/i_video.c,
 * less SDL): the engine draws its 8-bit screens with the software renderer
 * as upstream does, into a buffer of the machine's own, and the core turns
 * screens[0] into BGRA through the palette the engine last set
 * (chimera_video_bgra). No OpenGL: the renderer is always software.
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

#include "chimera-platform.h"

rendermode_t rendermode = render_soft;
rendermode_t chosenrendermode = render_soft;

boolean allow_fullscreen = false;

consvar_t cv_vidwait = CVAR_INIT ("vid_wait", "Off", CV_SAVE, CV_OnOff, NULL);

static RGBA_t g_palette[256];
static INT32 g_mode_w = BASEVIDWIDTH, g_mode_h = BASEVIDHEIGHT;

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

void VID_StartupOpenGL(void) {}

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
	vid.rowbytes = vid.width * vid.bpp;
	vid.direct = NULL;
	free(vid.buffer);
	vid.buffer = calloc(NUMSCREENS, vid.rowbytes * vid.height);
	if (!vid.buffer)
		I_Error("Not enough memory for video buffer\n");
	SCR_SetDrawFuncs();
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
