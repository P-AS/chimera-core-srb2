/* wbx-entry.c - the Chimera guest ABI over the driver (srb2-driver.c).
 *
 * Compiles identically for the guest (miniBox's emulibc) and for the native
 * reference (native-shim/emulibc.h): the same driver and exports, one in the
 * sandbox and one out of it, which is what makes the equivalence gate a proof.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <emulibc.h>
#include <waterbox_settings.h>

#include "srb2-driver.h"
#include "platform/chimera-platform.h"

/* the largest picture the core reports (the engine's 320x200 for now; its
 * other resolutions later) */
#define VIDEO_MAX_W 1920
#define VIDEO_MAX_H 1200
/* a step is a tic: 44100/35 stereo samples */
#define AUDIO_RATE 44100
#define SAMPLES_PER_STEP (AUDIO_RATE / 35)

static char g_load_error[1024];
/* output, not the machine: not in a savestate */
static uint32_t *g_video;
static int16_t *g_audio;
ECL_INVISIBLE static int g_render = 1;

ECL_EXPORT const char *GetLoadError(void) { return g_load_error; }

/* the engine started as upstream's main starts it. Its folder (-workdir,
 * patches/0003: configuration, game data, saves, replays) is the machine's
 * own root, so its files are the machine's names - "config.cfg",
 * "gamedata.dat" - never a folder of the host's; -home, the user's home it
 * otherwise derives that from (the host's $HOME, which the core never
 * answers), is required and unused. Nothing is written yet. Settings:
 *   warp   a map to start in, as the game's -warp takes it (a number, or
 *          MAPxx); empty: the game's own start, the intro and the title */
ECL_EXPORT int Init(void)
{
	static char warp[16];
	static char *argv[10] = { "srb2", "-home", ".", "-workdir", "." };
	int argc = 5;
	g_load_error[0] = '\0';
	if (wbx_setting_str("warp", warp, (int)sizeof warp) > 0 && warp[0])
	{
		argv[argc++] = "-warp";
		argv[argc++] = warp;
	}
	argv[argc] = NULL;
	g_video = alloc_invisible(sizeof(uint32_t) * VIDEO_MAX_W * VIDEO_MAX_H);
	g_audio = alloc_invisible(sizeof(int16_t) * 2 * SAMPLES_PER_STEP);
	if (!g_video || !g_audio)
	{
		snprintf(g_load_error, sizeof g_load_error, "out of memory for the picture");
		return 0;
	}
	if (srb2_start(argc, argv) != 0)
	{
		snprintf(g_load_error, sizeof g_load_error, "%s", srb2_error());
		return 0;
	}
	return 1;
}

/* no input yet: the engine reads none of the frontend's */
ECL_EXPORT void SetButton(int32_t index, int32_t state) { (void)index; (void)state; }
ECL_EXPORT void SetAxis(int32_t index, int32_t value) { (void)index; (void)value; }

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
	(void)packed;
	srb2_frame();
	if (g_render)
		chimera_video_bgra(g_video);
}

/* turbo: the engine still draws (what drawing touches is the machine's); only
 * the picture's conversion is skipped */
ECL_EXPORT void SetRenderingEnabled(int on) { g_render = on != 0; }

ECL_EXPORT uint32_t *GetVideoBgra(void) { return g_video; }
ECL_EXPORT int GetVideoWidth(void) { return chimera_video_width(); }
ECL_EXPORT int GetVideoHeight(void) { return chimera_video_height(); }

/* silence, a step's worth, until the core mixes the engine's sound */
ECL_EXPORT int16_t *GetAudio(void) { return g_audio; }
ECL_EXPORT int GetAudioSampleCount(void) { return SAMPLES_PER_STEP; }

ECL_EXPORT int GetVsyncNumerator(void) { return 35; }
ECL_EXPORT int GetVsyncDenominator(void) { return 1; }

/* every step is a tic the game reads, until input says otherwise */
ECL_EXPORT int InputWasRead(void) { return 1; }

ECL_EXPORT int GetMemoryDomainCount(void) { return 0; }
ECL_EXPORT const char *GetMemoryDomainName(int i) { (void)i; return ""; }
ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i) { (void)i; return NULL; }
ECL_EXPORT int64_t GetMemoryDomainSize(int i) { (void)i; return 0; }

/* the machine's clock, in I_GetPrecisePrecision() units */
ECL_EXPORT uint64_t GetCycleCount(void) { return chimera_clock_precise(); }

/* the engine's tic counter, for the harnesses */
ECL_EXPORT uint32_t GetGameTic(void) { return srb2_gametic(); }
