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
 *   --input FILE    the controller, a range of steps a line:
 *                     FROM-TO: Button; Button; Axis=value
 *                   (names as the core's GetButtonName/GetAxisName give them;
 *                   '#' starts a comment); a step's buttons and axes are what
 *                   the lines covering it say, the rest released and 0
 *   --wav FILE      write the run's sound (every step's samples) as a WAVE
 *   --savedata-out DIR  after the run, write the save data export (the files
 *                   the game wrote) under DIR, as chimera-run --export-savedata
 *   --game-state FILE   after every step, append the Game State domain's bytes
 *                   to FILE; after the run, write the property table
 *                   (GetGameProperties) to FILE.json (the properties leg)
 *
 * It ends with "run <hash> tic <n> clock <c> lag <l> audio <hash> peak <p> state
 * <hash>": the hash of every step's picture in order, the engine's tic
 * counter, the machine's clock, the steps that read no input, the hash of
 * every step's sound and its loudest sample, and the hash of every step's game
 * state (GetStateDigest: what the game is, apart from its picture) - the whole
 * run.
 */
#ifndef HARNESS_H
#define HARNESS_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

struct harness_core
{
	int (*init)(void);
	const char *(*load_error)(void);
	void (*frame)(void);
	const uint32_t *(*video)(int *w, int *h);
	/* the step's sound: n stereo frames of signed 16-bit */
	const int16_t *(*audio)(int *n);
	uint32_t (*gametic)(void);
	int (*input_was_read)(void);
	uint64_t (*state_digest)(void);
	uint64_t (*clock)(void);
	/* the controller */
	void (*set_button)(int32_t i, int32_t held);
	void (*set_axis)(int32_t i, int32_t value);
	int (*button_count)(void);
	const char *(*button_name)(int32_t i);
	int (*axis_count)(void);
	const char *(*axis_name)(int32_t i);
	/* the save data export group */
	int32_t (*savedata_count)(void);
	const char *(*savedata_name)(int32_t i);
	int64_t (*savedata_size)(int32_t i);
	const uint8_t *(*savedata_buffer)(int32_t i);
	/* the Game State domain (domain 0) and the property table naming it */
	const uint8_t *(*game_state)(int64_t *size);
	const char *(*game_properties)(void);
	/* before each step (run-wbx's savestate legs); may be NULL */
	void (*pre_frame)(long step);
};

struct harness_opts
{
	long frames, every, stall_at, stall_ms;
	const char *ppm;
	const char *savedata_out;
	const char *input;
	const char *wav;
	const char *game_state;
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
	o->savedata_out = NULL;
	o->input = NULL;
	o->wav = NULL;
	o->game_state = NULL;
	for (int i = first; i < argc; i++)
	{
		if (!strcmp(argv[i], "-n") && i + 1 < argc)
			o->frames = atol(argv[++i]);
		else if (!strcmp(argv[i], "-p") && i + 1 < argc)
			o->every = atol(argv[++i]);
		else if (!strcmp(argv[i], "--ppm") && i + 1 < argc)
			o->ppm = argv[++i];
		else if (!strcmp(argv[i], "--wav") && i + 1 < argc)
			o->wav = argv[++i];
		else if (!strcmp(argv[i], "--input") && i + 1 < argc)
			o->input = argv[++i];
		else if (!strcmp(argv[i], "--game-state") && i + 1 < argc)
			o->game_state = argv[++i];
		else if (!strcmp(argv[i], "--savedata-out") && i + 1 < argc)
			o->savedata_out = argv[++i];
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

/* the export: each file under dir, its folders made; a name that is not
 * relative, or climbs, is refused as the engine refuses it */
static int harness_write_savedata(const struct harness_core *c, const char *dir)
{
	const int32_t n = c->savedata_count();
	for (int32_t i = 0; i < n; i++)
	{
		const char *name = c->savedata_name(i);
		if (!name[0] || name[0] == '/' || strstr(name, "..") || strchr(name, '\\'))
		{
			fprintf(stderr, "savedata: refused name '%s'\n", name);
			return -1;
		}
		char path[4096];
		snprintf(path, sizeof path, "%s/%s", dir, name);
		/* every folder on the way, the output's own too */
		for (char *p = path + 1; *p; p++)
			if (*p == '/')
			{
				*p = 0;
				mkdir(path, 0755);
				*p = '/';
			}
		const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		const int64_t size = c->savedata_size(i);
		if (fd < 0 || write(fd, c->savedata_buffer(i), (size_t)size) != (ssize_t)size)
		{
			perror(path);
			return -1;
		}
		close(fd);
	}
	printf("savedata %d files\n", n);
	return 0;
}

/* ---- the input file */
struct harness_press
{
	long from, to;
	int axis, index;
	int32_t value;
};
static struct harness_press g_presses[4096];
static int g_npresses;

static char *harness_trim(char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r'))
		*--e = 0;
	return s;
}

static int harness_load_input(const struct harness_core *c, const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
	{
		perror(path);
		return 0;
	}
	char line[1024];
	int lineno = 0;
	while (fgets(line, sizeof line, f))
	{
		lineno++;
		char *hash = strchr(line, '#');
		if (hash)
			*hash = 0;
		char *body = strchr(line, ':');
		if (!body)
		{
			if (*harness_trim(line))
				goto bad;
			continue;
		}
		*body++ = 0;
		long from, to;
		const int got = sscanf(line, "%ld-%ld", &from, &to);
		if (got < 1)
			goto bad;
		if (got == 1)
			to = from;
		for (char *item = strtok(body, ";"); item; item = strtok(NULL, ";"))
		{
			char *name = harness_trim(item);
			if (!*name)
				continue;
			char *eq = strchr(name, '=');
			struct harness_press p = { from, to, eq != NULL, -1, 1 };
			if (eq)
			{
				*eq = 0;
				p.value = (int32_t)strtol(eq + 1, NULL, 0);
				name = harness_trim(name);
			}
			const int n = p.axis ? c->axis_count() : c->button_count();
			for (int i = 0; i < n; i++)
				if (!strcmp(p.axis ? c->axis_name(i) : c->button_name(i), name))
					p.index = i;
			if (p.index < 0 || g_npresses == (int)(sizeof g_presses / sizeof g_presses[0]))
			{
				fprintf(stderr, "%s:%d: no %s \"%s\"\n", path, lineno, p.axis ? "axis" : "button", name);
				fclose(f);
				return 0;
			}
			g_presses[g_npresses++] = p;
		}
	}
	fclose(f);
	return 1;
bad:
	fprintf(stderr, "%s:%d: not FROM-TO: names\n", path, lineno);
	fclose(f);
	return 0;
}

static void harness_apply_input(const struct harness_core *c, long step)
{
	for (int i = 0; i < c->button_count(); i++)
		c->set_button(i, 0);
	for (int i = 0; i < c->axis_count(); i++)
		c->set_axis(i, 0);
	for (int i = 0; i < g_npresses; i++)
		if (step >= g_presses[i].from && step <= g_presses[i].to)
		{
			if (g_presses[i].axis)
				c->set_axis(g_presses[i].index, g_presses[i].value);
			else
				c->set_button(g_presses[i].index, 1);
		}
}

/* Init has run (run-wbx seals the machine after it); the steps */
static int harness_run(const struct harness_core *c, const struct harness_opts *o)
{
	uint64_t run = 0xcbf29ce484222325ull, sound = 0xcbf29ce484222325ull, state = 0xcbf29ce484222325ull;
	long lag = 0;
	int peak = 0;
	int wav = -1;
	uint32_t wav_bytes = 0;
	if (o->wav)
	{
		wav = open(o->wav, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (wav < 0)
		{
			perror(o->wav);
			return 1;
		}
		static const uint8_t head[44] = { 0 };
		if (write(wav, head, 44) != 44)
			return 1;
	}
	if (o->input && !harness_load_input(c, o->input))
		return 2;
	/* open and write, as the picture and the sound: the native build's stdio
	 * is the machine's filesystem (platform/files.c) */
	int gs = -1;
	if (o->game_state && (gs = open(o->game_state, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
	{
		perror(o->game_state);
		return 1;
	}
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
		harness_apply_input(c, f);
		c->frame();
		if (!c->input_was_read())
			lag++;
		px = c->video(&w, &h);
		state = (state ^ c->state_digest()) * 0x100000001b3ull;
		int an;
		const int16_t *as = c->audio(&an);
		sound = (sound ^ harness_fnv1a(as, (size_t)an * 4)) * 0x100000001b3ull;
		for (int i = 0; i < an * 2; i++)
			if (abs(as[i]) > peak)
				peak = abs(as[i]);
		if (wav >= 0 && write(wav, as, (size_t)an * 4) == (ssize_t)an * 4)
			wav_bytes += (uint32_t)an * 4;
		if (gs >= 0)
		{
			int64_t size;
			const uint8_t *b = c->game_state(&size);
			if (write(gs, b, (size_t)size) != (ssize_t)size)
				perror(o->game_state);
		}
		const uint64_t pic = harness_fnv1a(px, sizeof(uint32_t) * (size_t)w * (size_t)h);
		run = (run ^ pic) * 0x100000001b3ull;
		if (o->every > 0 && (f % o->every == 0 || f == o->frames))
			printf("step %ld tic %u clock %llu lag %ld picture %016llx\n", f, c->gametic(),
				(unsigned long long)c->clock(), lag, (unsigned long long)pic);
	}
	printf("run %016llx tic %u clock %llu lag %ld audio %016llx peak %d state %016llx\n", (unsigned long long)run,
		c->gametic(), (unsigned long long)c->clock(), lag, (unsigned long long)sound, peak, (unsigned long long)state);
	if (wav >= 0)
	{
		/* the header, now the length is known: 44.1 kHz stereo 16-bit PCM */
		uint8_t h[44];
		const uint32_t v[] = { 36 + wav_bytes, 16, 0x00020001, 44100, 44100 * 4, 0x00100004, wav_bytes };
		memcpy(h, "RIFF", 4);
		memcpy(h + 4, &v[0], 4);
		memcpy(h + 8, "WAVEfmt ", 8);
		memcpy(h + 16, &v[1], 4);
		memcpy(h + 20, &v[2], 4);
		memcpy(h + 24, &v[3], 4);
		memcpy(h + 28, &v[4], 4);
		memcpy(h + 32, &v[5], 4);
		memcpy(h + 36, "data", 4);
		memcpy(h + 40, &v[6], 4);
		if (lseek(wav, 0, SEEK_SET) != 0 || write(wav, h, 44) != 44)
			perror(o->wav);
		close(wav);
	}
	if (gs >= 0)
	{
		close(gs);
		char path[4096];
		snprintf(path, sizeof path, "%s.json", o->game_state);
		const char *table = c->game_properties();
		const int j = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (j < 0 || write(j, table, strlen(table)) != (ssize_t)strlen(table))
		{
			perror(path);
			return 1;
		}
		close(j);
	}
	fflush(stdout);
	if (o->ppm && px && harness_write_ppm(o->ppm, px, w, h) != 0)
	{
		perror(o->ppm);
		return 1;
	}
	if (o->savedata_out && harness_write_savedata(c, o->savedata_out) != 0)
		return 1;
	return 0;
}
#endif
