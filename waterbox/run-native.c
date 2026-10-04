/* run-native.c - the native reference: SRB2's engine and the core's platform
 * layer, built for the host, no sandbox and no window. The work folder holds
 * what the sandbox would see mounted: srb2.pk3, zones.pk3, characters.pk3 and
 * music.pk3.
 *
 * usage: run-native <workdir> [-n frames] [-p every] [--ppm out.ppm]
 *   -n      passes of the engine's loop to run (default 300)
 *   -p      print the picture's hash every this many frames (default 35)
 *   --ppm   write the last frame's picture
 */
#include <execinfo.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "srb2-driver.h"
#include "platform/chimera-platform.h"

/* a crash of the native build says where */
static void crashed(int sig)
{
	void *frames[64];
	const int n = backtrace(frames, 64);
	fprintf(stderr, "run-native: signal %d\n", sig);
	backtrace_symbols_fd(frames, n, 2);
	_exit(128 + sig);
}

static uint64_t fnv1a(const void *data, size_t len)
{
	const uint8_t *p = data;
	uint64_t h = 0xcbf29ce484222325ull;
	for (size_t i = 0; i < len; i++)
		h = (h ^ p[i]) * 0x100000001b3ull;
	return h;
}

static int write_ppm(const char *path, const uint32_t *px, int w, int h)
{
	FILE *f = fopen(path, "wb");
	if (!f)
		return -1;
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	for (int i = 0; i < w * h; i++)
	{
		const uint8_t rgb[3] = { (uint8_t)(px[i] >> 16), (uint8_t)(px[i] >> 8), (uint8_t)px[i] };
		fwrite(rgb, 1, 3, f);
	}
	return fclose(f);
}

int main(int argc, char **argv)
{
	signal(SIGSEGV, crashed);
	signal(SIGFPE, crashed);
	signal(SIGABRT, crashed);
	if (argc < 2)
	{
		fprintf(stderr, "usage: run-native <workdir> [-n frames] [-p every] [--ppm out.ppm]\n");
		return 2;
	}
	long frames = 300, every = 35;
	const char *ppm = NULL;
	for (int i = 2; i < argc; i++)
	{
		if (!strcmp(argv[i], "-n") && i + 1 < argc)
			frames = atol(argv[++i]);
		else if (!strcmp(argv[i], "-p") && i + 1 < argc)
			every = atol(argv[++i]);
		else if (!strcmp(argv[i], "--ppm") && i + 1 < argc)
			ppm = argv[++i];
		else
		{
			fprintf(stderr, "run-native: unknown option %s\n", argv[i]);
			return 2;
		}
	}
	char ppm_abs[4096];
	if (ppm && ppm[0] != '/')
	{
		if (!getcwd(ppm_abs, sizeof ppm_abs - strlen(ppm) - 2))
			return 1;
		strcat(ppm_abs, "/");
		strcat(ppm_abs, ppm);
		ppm = ppm_abs;
	}
	if (chdir(argv[1]) != 0)
	{
		perror(argv[1]);
		return 1;
	}

	/* the engine's command line: its home (configuration, game data) is the
	 * work folder's .srb2 */
	static char *eargv[] = { "srb2", "-home", ".", NULL };
	if (srb2_start(3, eargv) != 0)
	{
		fprintf(stderr, "run-native: the engine halted at start: %s\n", srb2_error());
		return 1;
	}

	const int w = chimera_video_width(), h = chimera_video_height();
	uint32_t *px = malloc(sizeof(uint32_t) * w * h);
	for (long f = 1; f <= frames; f++)
	{
		srb2_frame();
		if (srb2_halted())
		{
			fprintf(stderr, "run-native: the engine halted at frame %ld: %s\n", f, srb2_error());
			return 1;
		}
		if (every > 0 && (f % every == 0 || f == frames))
		{
			chimera_video_bgra(px);
			printf("frame %ld picture %016llx\n", f, (unsigned long long)fnv1a(px, sizeof(uint32_t) * w * h));
		}
	}
	if (ppm)
	{
		chimera_video_bgra(px);
		if (write_ppm(ppm, px, w, h) != 0)
		{
			perror(ppm);
			return 1;
		}
	}
	return 0;
}
