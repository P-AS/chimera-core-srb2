/* run-native.c - the native reference: the core's exports (wbx-entry.c), the
 * driver, the platform layer and SRB2's engine, built for the host - no
 * sandbox, no window - and driven as run-wbx drives core.wbx (harness.h).
 * The work folder holds what the sandbox would see mounted: srb2.pk3,
 * zones.pk3, characters.pk3 and music.pk3.
 *
 * usage: run-native <workdir> [harness.h's options] [--host-clock] [--print-cvar NAME]...
 *        run-native --list-input
 *   --host-clock    the host's clock instead of the machine's: a diagnostic
 *                   (the time leg's teeth), never how the core runs
 *   --print-cvar    after the run, print "cvar NAME VALUE": what the engine's
 *                   option is (the settings leg)
 *   --print-unlocks after the run, print "unlocks ...": what the game data has
 *                   unlocked (the settings leg)
 *   --print-skin    after the run, print "skin NAME": the player's character
 *                   in the level (the settings leg)
 *   --list-input    print the controller: "button NAME" and "axis NAME" lines,
 *                   in its order, and exit (the declaration leg)
 */
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>

#include "harness.h"
#include "platform/chimera-platform.h"

extern int Init(void);
extern const char *GetLoadError(void);
extern void FrameAdvance(uint64_t packed);
extern uint32_t *GetVideoBgra(void);
extern int GetVideoWidth(void);
extern int GetVideoHeight(void);
extern uint32_t GetGameTic(void);
extern int16_t *GetAudio(void);
extern int GetAudioSampleCount(void);
extern int InputWasRead(void);
extern uint64_t GetCycleCount(void);
extern uint64_t GetStateDigest(void);
extern void SetButton(int32_t index, int32_t state);
extern void SetAxis(int32_t index, int32_t value);
extern int GetButtonCount(void);
extern const char *GetButtonName(int32_t i);
extern int GetAxisCount(void);
extern const char *GetAxisName(int32_t i);
extern int32_t GetSaveDataFileCount(void);
extern const char *GetSaveDataFileName(int32_t index);
extern int64_t GetSaveDataFileSize(int32_t index);
extern const uint8_t *GetSaveDataFileBuffer(int32_t index);
extern uint8_t *GetMemoryDomainPtr(int i);
extern int64_t GetMemoryDomainSize(int i);
extern const char *GetGameProperties(void);

static void frame(void) { FrameAdvance(0); }
static const uint32_t *video(int *w, int *h)
{
	*w = GetVideoWidth();
	*h = GetVideoHeight();
	return GetVideoBgra();
}

static const int16_t *audio(int *n)
{
	*n = GetAudioSampleCount();
	return GetAudio();
}

static const uint8_t *game_state(int64_t *size)
{
	*size = GetMemoryDomainSize(0);
	return GetMemoryDomainPtr(0);
}

/* the engine's option by name (srb2-driver.c) */
extern const char *chimera_cvar_string(const char *name);
extern const char *chimera_unlocks_summary(void);
extern const char *chimera_player_skin(void);
static int g_print_unlocks, g_print_skin;

static const char *g_cvars[32];
static int g_ncvars;

static int known(const char *arg)
{
	static int want_cvar;
	if (want_cvar)
	{
		want_cvar = 0;
		if (g_ncvars < (int)(sizeof g_cvars / sizeof g_cvars[0]))
			g_cvars[g_ncvars++] = arg;
		return 1;
	}
	if (!strcmp(arg, "--host-clock"))
	{
		chimera_host_clock = 1;
		return 1;
	}
	if (!strcmp(arg, "--print-cvar"))
		return want_cvar = 1;
	if (!strcmp(arg, "--print-unlocks"))
		return g_print_unlocks = 1;
	if (!strcmp(arg, "--print-skin"))
		return g_print_skin = 1;
	return 0;
}

/* a crash of the native build says where (the sandbox's says less) */
static void crashed(int sig)
{
	void *frames[64];
	const int n = backtrace(frames, 64);
	fprintf(stderr, "run-native: signal %d\n", sig);
	backtrace_symbols_fd(frames, n, 2);
	_exit(128 + sig);
}

int main(int argc, char **argv)
{
	signal(SIGSEGV, crashed);
	signal(SIGFPE, crashed);
	signal(SIGABRT, crashed);
	if (argc == 2 && !strcmp(argv[1], "--list-input"))
	{
		for (int i = 0; i < GetButtonCount(); i++)
			printf("button %s\n", GetButtonName(i));
		for (int i = 0; i < GetAxisCount(); i++)
			printf("axis %s\n", GetAxisName(i));
		return 0;
	}
	if (argc < 2)
	{
		fprintf(stderr, "usage: run-native <workdir> [options] [--host-clock] [--print-cvar NAME]...\n");
		return 2;
	}
	static struct harness_opts o;
	if (!harness_parse(argc, argv, 2, &o, known))
		return 2;
	static char ppm_abs[4096];
	if (o.ppm && o.ppm[0] != '/' && getcwd(ppm_abs, sizeof ppm_abs - strlen(o.ppm) - 2))
	{
		strcat(ppm_abs, "/");
		strcat(ppm_abs, o.ppm);
		o.ppm = ppm_abs;
	}
	static char wav_abs[4096];
	if (o.wav && o.wav[0] != '/' && getcwd(wav_abs, sizeof wav_abs - strlen(o.wav) - 2))
	{
		strcat(wav_abs, "/");
		strcat(wav_abs, o.wav);
		o.wav = wav_abs;
	}
	static char in_abs[4096];
	if (o.input && o.input[0] != '/' && getcwd(in_abs, sizeof in_abs - strlen(o.input) - 2))
	{
		strcat(in_abs, "/");
		strcat(in_abs, o.input);
		o.input = in_abs;
	}
	static char sd_abs[4096];
	if (o.savedata_out && o.savedata_out[0] != '/' && getcwd(sd_abs, sizeof sd_abs - strlen(o.savedata_out) - 2))
	{
		strcat(sd_abs, "/");
		strcat(sd_abs, o.savedata_out);
		o.savedata_out = sd_abs;
	}
	static char gs_abs[4096];
	if (o.game_state && o.game_state[0] != '/' && getcwd(gs_abs, sizeof gs_abs - strlen(o.game_state) - 2))
	{
		strcat(gs_abs, "/");
		strcat(gs_abs, o.game_state);
		o.game_state = gs_abs;
	}
	if (chdir(argv[1]) != 0)
	{
		perror(argv[1]);
		return 1;
	}

	const struct harness_core c = {
		.init = Init,
		.load_error = GetLoadError,
		.frame = frame,
		.video = video,
		.audio = audio,
		.gametic = GetGameTic,
		.input_was_read = InputWasRead,
		.clock = GetCycleCount,
		.state_digest = GetStateDigest,
		.set_button = SetButton,
		.set_axis = SetAxis,
		.button_count = GetButtonCount,
		.button_name = GetButtonName,
		.axis_count = GetAxisCount,
		.axis_name = GetAxisName,
		.savedata_count = GetSaveDataFileCount,
		.savedata_name = GetSaveDataFileName,
		.savedata_size = GetSaveDataFileSize,
		.savedata_buffer = GetSaveDataFileBuffer,
		.game_state = game_state,
		.game_properties = GetGameProperties,
	};
	if (c.init() != 1)
	{
		fprintf(stderr, "run-native: Init failed: %s\n", c.load_error());
		return 1;
	}
	const int ret = harness_run(&c, &o);
	for (int i = 0; i < g_ncvars; i++)
		printf("cvar %s %s\n", g_cvars[i], chimera_cvar_string(g_cvars[i]));
	if (g_print_unlocks)
		printf("unlocks %s\n", chimera_unlocks_summary());
	if (g_print_skin)
		printf("skin %s\n", chimera_player_skin());
	return ret;
}
