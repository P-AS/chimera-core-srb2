/* run-native.c - the native reference: the core's exports (wbx-entry.c), the
 * driver, the platform layer and SRB2's engine, built for the host - no
 * sandbox, no window - and driven as run-wbx drives core.wbx (harness.h).
 * The work folder holds what the sandbox would see mounted: srb2.pk3,
 * zones.pk3, characters.pk3 and music.pk3.
 *
 * usage: run-native <workdir> [harness.h's options] [--host-clock]
 *   --host-clock    the host's clock instead of the machine's: a diagnostic
 *                   (the time leg's teeth), never how the core runs
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
extern int InputWasRead(void);
extern uint64_t GetCycleCount(void);

static void frame(void) { FrameAdvance(0); }
static const uint32_t *video(int *w, int *h)
{
	*w = GetVideoWidth();
	*h = GetVideoHeight();
	return GetVideoBgra();
}

static int known(const char *arg)
{
	if (!strcmp(arg, "--host-clock"))
	{
		chimera_host_clock = 1;
		return 1;
	}
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
	if (argc < 2)
	{
		fprintf(stderr, "usage: run-native <workdir> [options] [--host-clock]\n");
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
		.gametic = GetGameTic,
		.input_was_read = InputWasRead,
		.clock = GetCycleCount,
	};
	if (c.init() != 1)
	{
		fprintf(stderr, "run-native: Init failed: %s\n", c.load_error());
		return 1;
	}
	return harness_run(&c, &o);
}
