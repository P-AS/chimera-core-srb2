/* wbx-entry.c - the Chimera guest ABI over the driver (srb2-driver.c).
 *
 * Compiles identically for the guest (miniBox's emulibc) and for the native
 * reference (native-shim/emulibc.h): the same driver and exports, one in the
 * sandbox and one out of it, which is what makes the equivalence gate a proof.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include "srb2-driver.h"
#include "srb2-input.h"
#include "platform/chimera-platform.h"

/* the largest picture the core reports: the largest resolution setting
 * (sources.mk raises the engine's MAXVIDWIDTH/HEIGHT to it, patches/0005) */
#define VIDEO_MAX_W 3840
#define VIDEO_MAX_H 2160
/* a step is a tic: 44100/35 stereo samples */
#define AUDIO_RATE 44100
#define SAMPLES_PER_STEP (AUDIO_RATE / 35)

static char g_load_error[1024];
/* output, not the machine: not in a savestate */
static uint32_t *g_video;
static int16_t *g_audio;
ECL_INVISIBLE static int g_render = 1;

ECL_EXPORT const char *GetLoadError(void) { return g_load_error; }

/* ---- the settings (waterbox.config's "settings"; miniBox's settings file)
 *
 * Each is an SRB2 option, given to the engine as its "+" launch parameters:
 * upstream runs those after the configuration and autoexec.cfg
 * (M_PushSpecialParameters), so they are what the machine plays with,
 * whatever a configuration says. A setting the file does not have takes the
 * core's default, the declaration's.
 *
 *   warp            a map to start in, as the game's -warp takes it (a number,
 *                   or MAPxx); empty: the game's own start, the intro and title
 *   skin            the character Start Map plays as, by its skin name
 *                   (sonic, tails, knuckles, amy, fang, metalsonic, or an
 *                   addon's), as the game's +skin; empty: its own default,
 *                   Sonic. Only with a Start Map: otherwise the game's
 *                   character select chooses
 *   playStyle       the 1P play style menu's choice, as its two options:
 *                     Strafe     directionchar Camera,   configanalog Off
 *                     Manual     directionchar Movement, configanalog Off
 *                     Automatic  directionchar Movement, configanalog On
 *                     Old Analog directionchar Camera,   configanalog On
 *                   (M_HandlePlaystyleMenu); the core's default Manual
 *   cameraSpeed     cam_speed, 0 to 1; the core's default 1.0
 *   scoreTimeRings  timerres: Classic, Centiseconds, Mania, Tics; default Mania
 *   flipCamera      flipcam: the camera flips with gravity; default Yes
 *   autoBrake       autobrake: the player brakes when no direction is
 *                   held; default On (SRB2's own too)
 *   tutorialPrompt  tutorialprompt: the title's "play the tutorial?"
 *                   question on a first start; default Off
 *   unlockModes     Record Attack and NiGHTS Mode unlocked from the start
 *                   (SRB2 offers Marathon Run whenever Record Attack is); Off
 *   unlockCharacters every character unlocked; Off
 *   unlockAll       every unlockable (the modes, the characters, level select,
 *                   sound test, Pandora's Box, the emblem hints and radar...);
 *                   Off. The unlocks are the game data's (srb2-driver.c), so
 *                   a movie needs no run that earns them
 *   resolution      the picture's size, WxH: SRB2's own video modes
 *                   (sdl/i_video.c's windowedModes) and 2560x1440, 3840x2160;
 *                   default 1280x800, SRB2's own. The picture's alone: the
 *                   game plays the same at every one
 */
static const char *const g_playstyles[][3] = {
	{ "Strafe", "Camera", "Off" },
	{ "Manual", "Movement", "Off" },
	{ "Automatic", "Movement", "On" },
	{ "Old Analog", "Camera", "On" },
};
static const char *const g_timerres[] = { "Classic", "Centiseconds", "Mania", "Tics" };

/* the argument list: the engine keeps pointers into it for the run */
static char g_args[80][256];
static char *g_argv[200];
static int g_argc;

static void arg(const char *a)
{
	if (g_argc < (int)(sizeof g_argv / sizeof g_argv[0]) - 1)
		g_argv[g_argc++] = (char *)a;
}

static const char *arg_copy(const char *a)
{
	static int n;
	if (n == (int)(sizeof g_args / sizeof g_args[0]))
		return "";
	snprintf(g_args[n], sizeof g_args[0], "%s", a);
	return g_args[n++];
}

static void settings_args(void)
{
	char warp[16], skin[32], style[32], timer[32], speed[32];

	if (wbx_setting_str("warp", warp, (int)sizeof warp) > 0 && warp[0])
	{
		arg("-warp");
		arg(arg_copy(warp));
		/* run before the map starts (D_SRB2Main's COM_BufExecute for +skin) */
		if (wbx_setting_str("skin", skin, (int)sizeof skin) > 0 && skin[0])
		{
			arg("+skin");
			arg(arg_copy(skin));
		}
	}

	int st = 1; /* Manual */
	if (wbx_setting_str("playStyle", style, (int)sizeof style) > 0)
		for (int i = 0; i < 4; i++)
			if (!strcmp(style, g_playstyles[i][0]))
				st = i;
	arg("+directionchar");
	arg(g_playstyles[st][1]);
	arg("+configanalog");
	arg(g_playstyles[st][2]);

	/* the float as the engine's own fixed point would have it, printed back
	 * at five places (cam_speed is a CV_FLOAT: atof, times FRACUNIT) */
	double cs = 1.0;
	if (wbx_setting_str("cameraSpeed", speed, (int)sizeof speed) > 0)
		cs = strtod(speed, NULL);
	if (!(cs >= 0.0))
		cs = 0.0;
	if (cs > 1.0)
		cs = 1.0;
	snprintf(speed, sizeof speed, "%.5f", cs);
	arg("+cam_speed");
	arg(arg_copy(speed));

	int tr = 2; /* Mania */
	if (wbx_setting_str("scoreTimeRings", timer, (int)sizeof timer) > 0)
		for (int i = 0; i < 4; i++)
			if (!strcmp(timer, g_timerres[i]))
				tr = i;
	arg("+timerres");
	arg(g_timerres[tr]);

	arg("+flipcam");
	arg(wbx_setting_bool("flipCamera", 1) ? "Yes" : "No");

	arg("+autobrake");
	arg(wbx_setting_bool("autoBrake", 1) ? "On" : "Off");

	arg("+tutorialprompt");
	arg(wbx_setting_bool("tutorialPrompt", 0) ? "On" : "Off");

	srb2_set_unlocks(wbx_setting_bool("unlockModes", 0), wbx_setting_bool("unlockCharacters", 0),
		wbx_setting_bool("unlockAll", 0));

	char res[32];
	int w = 1280, h = 800;
	if (wbx_setting_str("resolution", res, (int)sizeof res) > 0)
		sscanf(res, "%dx%d", &w, &h);
	chimera_video_set_mode(w, h);
}

/* the engine started as upstream's main starts it. Its folder (-workdir,
 * patches/0003: configuration, game data, saves, replays) is the machine's
 * own root, so its files are the machine's names - "config.cfg",
 * "gamedata.dat" - never a folder of the host's; -home, the user's home it
 * otherwise derives that from (the host's $HOME, which the core never
 * answers), is required and unused. */
/* the project's files (file_slots.json): its add-ons, loaded as -file loads
 * them, in the slot's order. Its save data needs nothing: each file is
 * mounted under its own name (gamedata.dat, srb2sav*.ssg), where the game
 * reads it at start. */
static void slots_args(void)
{
	const int n = wbx_slot_count("addons");
	if (n <= 0)
		return;
	arg("-file");
	for (int i = 0; i < n; i++)
	{
		char name[256];
		if (wbx_slot_name("addons", i, name, (int)sizeof name))
			arg(arg_copy(name));
	}
}

ECL_EXPORT int Init(void)
{
	g_argc = 0;
	arg("srb2");
	arg("-home");
	arg(".");
	arg("-workdir");
	arg(".");
	slots_args();
	settings_args();
	g_argv[g_argc] = NULL;
	g_load_error[0] = '\0';
	g_video = alloc_invisible(sizeof(uint32_t) * VIDEO_MAX_W * VIDEO_MAX_H);
	g_audio = alloc_invisible(sizeof(int16_t) * 2 * SAMPLES_PER_STEP);
	if (!g_video || !g_audio)
	{
		snprintf(g_load_error, sizeof g_load_error, "out of memory for the picture");
		return 0;
	}
	if (srb2_start(g_argc, g_argv) != 0)
	{
		snprintf(g_load_error, sizeof g_load_error, "%s", srb2_error());
		return 0;
	}
	return 1;
}

/* the controller (srb2-input.c): its buttons come through SetButton, and the
 * first 64 also as FrameAdvance's mask; a step sees the union */
static uint8_t g_set_buttons[64];

ECL_EXPORT void SetButton(int32_t index, int32_t state)
{
	if (index >= 0 && index < (int32_t)sizeof g_set_buttons)
		g_set_buttons[index] = state != 0;
}
ECL_EXPORT void SetAxis(int32_t index, int32_t value) { srb2_input_set_axis(index, value); }

/* the controller's names, in its order (the declaration's; the harnesses') */
ECL_EXPORT int GetButtonCount(void) { return srb2_input_button_count(); }
ECL_EXPORT const char *GetButtonName(int32_t i)
{
	const struct srb2_button *b = srb2_input_button(i);
	return b ? b->name : "";
}
ECL_EXPORT int GetAxisCount(void) { return srb2_input_axis_count(); }
ECL_EXPORT const char *GetAxisName(int32_t i)
{
	const struct srb2_axis *a = srb2_input_axis(i);
	return a ? a->name : "";
}

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
	for (int i = 0; i < srb2_input_button_count(); i++)
		srb2_input_set_button(i, g_set_buttons[i] | (int)((packed >> i) & 1));
	srb2_frame();
	chimera_audio_mix(g_audio, SAMPLES_PER_STEP);
	if (g_render)
		chimera_video_bgra(g_video);
}

/* turbo: the engine still draws (what drawing touches is the machine's); only
 * the picture's conversion is skipped */
ECL_EXPORT void SetRenderingEnabled(int on) { g_render = on != 0; }

ECL_EXPORT uint32_t *GetVideoBgra(void) { return g_video; }
ECL_EXPORT int GetVideoWidth(void) { return chimera_video_width(); }
ECL_EXPORT int GetVideoHeight(void) { return chimera_video_height(); }
/* square pixels: SRB2 is shown at the size it draws, whatever the mode */
ECL_EXPORT int GetDisplayAspectX(void) { return chimera_video_width(); }
ECL_EXPORT int GetDisplayAspectY(void) { return chimera_video_height(); }

/* a step's sound, mixed after it (platform/i_sound.c) */
ECL_EXPORT int16_t *GetAudio(void) { return g_audio; }
ECL_EXPORT int GetAudioSampleCount(void) { return SAMPLES_PER_STEP; }

ECL_EXPORT int GetVsyncNumerator(void) { return 35; }
ECL_EXPORT int GetVsyncDenominator(void) { return 1; }

/* a step that built no tic command is lag: a wipe's frame, the title card */
ECL_EXPORT int InputWasRead(void) { return srb2_input_was_read(); }

ECL_EXPORT int GetMemoryDomainCount(void) { return 0; }
ECL_EXPORT const char *GetMemoryDomainName(int i) { (void)i; return ""; }
ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i) { (void)i; return NULL; }
ECL_EXPORT int64_t GetMemoryDomainSize(int i) { (void)i; return 0; }
ECL_EXPORT int GetMemoryDomainWritable(int i) { (void)i; return 0; }

/* the machine's clock, in I_GetPrecisePrecision() units */
ECL_EXPORT uint64_t GetCycleCount(void) { return chimera_clock_precise(); }

/* ---- save data (Chimera's docs/save-data.md): every file the game wrote -
 * gamedata.dat (unlocks, emblems, records), the save slots (srb2sav*.ssg),
 * record attack's replays (replay/...), Lua's files - but its configuration,
 * which is the settings'. In the machine's memory (platform/files.c). */
ECL_EXPORT int32_t GetSaveDataFileCount(void) { return chimera_savedata_count(); }
ECL_EXPORT const char *GetSaveDataFileName(int32_t index) { return chimera_savedata_name(index); }
ECL_EXPORT int64_t GetSaveDataFileSize(int32_t index) { return chimera_savedata_size(index); }
ECL_EXPORT const uint8_t *GetSaveDataFileBuffer(int32_t index) { return chimera_savedata_buffer(index); }

/* a digest of the game's state (srb2-driver.c), for the harnesses */
extern uint64_t chimera_state_digest(void);
ECL_EXPORT uint64_t GetStateDigest(void) { return chimera_state_digest(); }

/* the engine's tic counter, for the harnesses */
ECL_EXPORT uint32_t GetGameTic(void) { return srb2_gametic(); }
