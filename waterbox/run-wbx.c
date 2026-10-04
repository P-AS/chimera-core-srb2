/* run-wbx.c - drives core.wbx through the miniBox host over the same work
 * folder and steps as run-native (harness.h), printing the same lines, so the
 * two builds diff directly. Every regular file in the work folder is mounted
 * into the guest under its name - what the frontend does with a project's
 * firmware - read from the disk as the guest reads it.
 *
 * usage: run-wbx <core.wbx> <workdir> [harness.h's options]
 */
#include "minibox.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "harness.h"

/* matches waterbox.config's memoryLayoutMiB: sbrk, sealed, invisible, plain, mmap */
#define LAYOUT_MIB 256, 4, 32, 4, 1024

typedef struct { FILE *f; } freader;
static intptr_t file_read(uintptr_t ud, uint8_t *d, uintptr_t s) { return (intptr_t)fread(d, 1, s, ((freader *)ud)->f); }

typedef int (MB_GUEST_ABI *intfn)(void);
typedef void (MB_GUEST_ABI *framefn)(uint64_t);
typedef uintptr_t (MB_GUEST_ABI *ptrfn)(void);
typedef uint32_t (MB_GUEST_ABI *u32fn)(void);
typedef uint64_t (MB_GUEST_ABI *u64fn)(void);

static mb_host *g_host;
static intfn g_Init, g_GetVideoWidth, g_GetVideoHeight;
static ptrfn g_GetLoadError, g_GetVideoBgra;
static framefn g_FrameAdvance;
static u32fn g_GetGameTic;
static u64fn g_GetCycleCount;

static uintptr_t proc(const char *n)
{
	mb_return r;
	wbx_get_proc_addr(g_host, n, &r);
	if (r.error_message[0]) { fprintf(stderr, "proc %s: %s\n", n, r.error_message); exit(2); }
	if (!r.data) { fprintf(stderr, "missing export %s\n", n); exit(2); }
	return r.data;
}

static int core_init(void) { return g_Init(); }
static const char *core_load_error(void) { return (const char *)g_GetLoadError(); }
static void core_frame(void) { g_FrameAdvance(0); }
static const uint32_t *core_video(int *w, int *h)
{
	*w = g_GetVideoWidth();
	*h = g_GetVideoHeight();
	return (const uint32_t *)g_GetVideoBgra();
}
static uint32_t core_gametic(void) { return g_GetGameTic(); }
static uint64_t core_clock(void) { return g_GetCycleCount(); }

int main(int argc, char **argv)
{
	if (argc < 3)
	{
		fprintf(stderr, "usage: run-wbx <core.wbx> <workdir> [options]\n");
		return 2;
	}
	static struct harness_opts o;
	if (!harness_parse(argc, argv, 3, &o, NULL))
		return 2;

	FILE *wf = fopen(argv[1], "rb");
	if (!wf) { perror(argv[1]); return 1; }
	const uint32_t mib[] = { LAYOUT_MIB };
	mb_memory_layout_template layout = {
		(uintptr_t)mib[0] << 20, (uintptr_t)mib[1] << 20, (uintptr_t)mib[2] << 20,
		(uintptr_t)mib[3] << 20, (uintptr_t)mib[4] << 20 };
	freader fr = { wf };
	mb_return r;
	wbx_create_host(&layout, "core.wbx", file_read, (uintptr_t)&fr, &r);
	fclose(wf);
	if (r.error_message[0]) { fprintf(stderr, "create: %s\n", r.error_message); return 1; }
	g_host = (mb_host *)r.data;

	DIR *d = opendir(argv[2]);
	if (!d) { perror(argv[2]); return 1; }
	struct dirent *de;
	while ((de = readdir(d)) != NULL)
	{
		char path[4096];
		snprintf(path, sizeof path, "%s/%s", argv[2], de->d_name);
		struct stat st;
		if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
			continue;
		char real[4096];
		if (!realpath(path, real)) { perror(path); return 1; }
		wbx_mount_file_path(g_host, de->d_name, real, &r);
		if (r.error_message[0]) { fprintf(stderr, "mount %s: %s\n", de->d_name, r.error_message); return 1; }
	}
	closedir(d);
	wbx_activate_host(g_host, &r);

	g_Init = (intfn)proc("Init");
	g_GetLoadError = (ptrfn)proc("GetLoadError");
	g_FrameAdvance = (framefn)proc("FrameAdvance");
	g_GetVideoBgra = (ptrfn)proc("GetVideoBgra");
	g_GetVideoWidth = (intfn)proc("GetVideoWidth");
	g_GetVideoHeight = (intfn)proc("GetVideoHeight");
	g_GetGameTic = (u32fn)proc("GetGameTic");
	g_GetCycleCount = (u64fn)proc("GetCycleCount");

	const struct harness_core c = {
		.init = core_init,
		.load_error = core_load_error,
		.frame = core_frame,
		.video = core_video,
		.gametic = core_gametic,
		.clock = core_clock,
	};
	/* Init runs before Seal: the started machine is the sealed baseline */
	if (c.init() != 1)
	{
		fprintf(stderr, "run-wbx: Init failed: %s\n", c.load_error());
		return 1;
	}
	wbx_deactivate_host(g_host, &r);
	wbx_seal(g_host, &r);
	if (r.error_message[0]) { fprintf(stderr, "seal: %s\n", r.error_message); return 1; }
	wbx_activate_host(g_host, &r);

	const int ret = harness_run(&c, &o);
	wbx_deactivate_host(g_host, &r);
	wbx_destroy_host(g_host, &r);
	return ret;
}
