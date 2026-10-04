/* harness.h - the loop both harnesses run (run-native: the exports called
 * directly; run-wbx: the same exports in core.wbx, through miniBox), so their
 * output diffs line for line.
 *
 * options (after the harness's own arguments):
 *   -n F            steps to run (default 300)
 *   -p N            print the picture's hash, the tic and the machine's clock
 *                   every N steps (default 35; 0: only the last line)
 *   --ppm FILE      write the last step's picture
 *   --stall-at F    stall the host before step F ...
 *   --stall-ms MS   ... for this long (default 300): the machine must not notice
 *
 * It ends with "run <hash> tic <n> clock <c>": the hash of every step's picture
 * in order, the engine's tic counter and the machine's clock - the whole run.
 */
#ifndef HARNESS_H
#define HARNESS_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

struct harness_core
{
	int (*init)(void);
	const char *(*load_error)(void);
	void (*frame)(void);
	const uint32_t *(*video)(int *w, int *h);
	uint32_t (*gametic)(void);
	uint64_t (*clock)(void);
	/* before each step (run-wbx's savestate legs); may be NULL */
	void (*pre_frame)(long step);
};

struct harness_opts
{
	long frames, every, stall_at, stall_ms;
	const char *ppm;
};

/* parses argv[first..]; an option it does not know is left to the caller
 * when known(arg) says so, else an error */
static int harness_parse(int argc, char **argv, int first, struct harness_opts *o, int (*known)(const char *))
{
	o->frames = 300;
	o->every = 35;
	o->stall_at = 0;
	o->stall_ms = 300;
	o->ppm = NULL;
	for (int i = first; i < argc; i++)
	{
		if (!strcmp(argv[i], "-n") && i + 1 < argc)
			o->frames = atol(argv[++i]);
		else if (!strcmp(argv[i], "-p") && i + 1 < argc)
			o->every = atol(argv[++i]);
		else if (!strcmp(argv[i], "--ppm") && i + 1 < argc)
			o->ppm = argv[++i];
		else if (!strcmp(argv[i], "--stall-at") && i + 1 < argc)
			o->stall_at = atol(argv[++i]);
		else if (!strcmp(argv[i], "--stall-ms") && i + 1 < argc)
			o->stall_ms = atol(argv[++i]);
		else if (known && known(argv[i]))
			continue;
		else
		{
			fprintf(stderr, "unknown option %s\n", argv[i]);
			return 0;
		}
	}
	return 1;
}

static uint64_t harness_fnv1a(const void *data, size_t len)
{
	const uint8_t *p = data;
	uint64_t h = 0xcbf29ce484222325ull;
	for (size_t i = 0; i < len; i++)
		h = (h ^ p[i]) * 0x100000001b3ull;
	return h;
}

/* written with open/write: the native reference's link wraps fopen (the
 * core's files, platform/files.c), which writes nothing */
static int harness_write_ppm(const char *path, const uint32_t *px, int w, int h)
{
	const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return -1;
	char head[64];
	const int n = snprintf(head, sizeof head, "P6\n%d %d\n255\n", w, h);
	uint8_t *rgb = malloc((size_t)w * h * 3);
	for (int i = 0; i < w * h; i++)
	{
		rgb[3 * i] = (uint8_t)(px[i] >> 16);
		rgb[3 * i + 1] = (uint8_t)(px[i] >> 8);
		rgb[3 * i + 2] = (uint8_t)px[i];
	}
	const int ok = write(fd, head, n) == n && write(fd, rgb, (size_t)w * h * 3) == (ssize_t)w * h * 3;
	free(rgb);
	return close(fd) == 0 && ok ? 0 : -1;
}

/* Init has run (run-wbx seals the machine after it); the steps */
static int harness_run(const struct harness_core *c, const struct harness_opts *o)
{
	uint64_t run = 0xcbf29ce484222325ull;
	int w = 0, h = 0;
	const uint32_t *px = NULL;
	for (long f = 1; f <= o->frames; f++)
	{
		if (f == o->stall_at)
		{
			struct timespec ts = { o->stall_ms / 1000, (o->stall_ms % 1000) * 1000000L };
			nanosleep(&ts, NULL);
		}
		if (c->pre_frame)
			c->pre_frame(f);
		c->frame();
		px = c->video(&w, &h);
		const uint64_t pic = harness_fnv1a(px, sizeof(uint32_t) * (size_t)w * (size_t)h);
		run = (run ^ pic) * 0x100000001b3ull;
		if (o->every > 0 && (f % o->every == 0 || f == o->frames))
			printf("step %ld tic %u clock %llu picture %016llx\n", f, c->gametic(),
				(unsigned long long)c->clock(), (unsigned long long)pic);
	}
	printf("run %016llx tic %u clock %llu\n", (unsigned long long)run, c->gametic(), (unsigned long long)c->clock());
	fflush(stdout);
	if (o->ppm && px && harness_write_ppm(o->ppm, px, w, h) != 0)
	{
		perror(o->ppm);
		return 1;
	}
	return 0;
}
#endif
