/* run-wbx.c - drives core.wbx through the miniBox host over the same work
 * folder and steps as run-native (harness.h), printing the same lines, so the
 * two builds diff directly. Every regular file in the work folder is mounted
 * into the guest under its name - what the frontend does with a project's
 * firmware - read from the disk as the guest reads it.
 *
 * usage: run-wbx <core.wbx> <workdir> [harness.h's options] [--rerecord] [--session-at N]
 *        [--stale-state N]
 *   --rerecord      round-trip the whole machine through the host's save and
 *                   load before every step: the run must not change
 *   --session-at N  before step N, save the machine, destroy the host, build a
 *                   new one from the same core and files, load the state into
 *                   it and finish the run there - a reopened project; with the
 *                   engine suspended on its own cothread mid-wipe, the case
 *                   that proves that stack travels in a state
 *   --stale-state N save before step N and load that state again before step
 *                   N+1: step N runs twice, so the run must change (the
 *                   savestate legs' teeth)
 */
#include "minibox.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "harness.h"

/* matches waterbox.config's memoryLayoutMiB: sbrk, sealed, invisible, plain, mmap */
#define LAYOUT_MIB 256, 4, 64, 4, 1024

typedef struct { FILE *f; } freader;
static intptr_t file_read(uintptr_t ud, uint8_t *d, uintptr_t s) { return (intptr_t)fread(d, 1, s, ((freader *)ud)->f); }

typedef int (MB_GUEST_ABI *intfn)(void);
typedef void (MB_GUEST_ABI *framefn)(uint64_t);
typedef uintptr_t (MB_GUEST_ABI *ptrfn)(void);
typedef uint32_t (MB_GUEST_ABI *u32fn)(void);
typedef uint64_t (MB_GUEST_ABI *u64fn)(void);

static mb_host *g_host;
static intfn g_Init, g_GetVideoWidth, g_GetVideoHeight, g_InputWasRead, g_GetAudioSampleCount;
static ptrfn g_GetAudio;
static ptrfn g_GetLoadError, g_GetVideoBgra;
static framefn g_FrameAdvance;
static u32fn g_GetGameTic;
static u64fn g_GetCycleCount, g_GetStateDigest;
typedef int32_t (MB_GUEST_ABI *i32fn)(void);
typedef uintptr_t (MB_GUEST_ABI *ptrfn_i32)(int32_t);
typedef int64_t (MB_GUEST_ABI *i64fn_i32)(int32_t);
static i32fn g_GetSaveDataFileCount;
static ptrfn_i32 g_GetSaveDataFileName, g_GetSaveDataFileBuffer;
static i64fn_i32 g_GetSaveDataFileSize;
typedef void (MB_GUEST_ABI *setfn)(int32_t, int32_t);
static setfn g_SetButton, g_SetAxis;
static intfn g_GetButtonCount, g_GetAxisCount;
static ptrfn_i32 g_GetButtonName, g_GetAxisName;
static ptrfn_i32 g_GetMemoryDomainPtr;
static i64fn_i32 g_GetMemoryDomainSize;
static ptrfn g_GetGameProperties;

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
static const int16_t *core_audio(int *n)
{
	*n = g_GetAudioSampleCount();
	return (const int16_t *)g_GetAudio();
}
static int core_input_was_read(void) { return g_InputWasRead(); }
static uint64_t core_clock(void) { return g_GetCycleCount(); }
static uint64_t core_state(void) { return g_GetStateDigest(); }
static int32_t core_sd_count(void) { return g_GetSaveDataFileCount(); }
static void core_set_button(int32_t i, int32_t s) { g_SetButton(i, s); }
static void core_set_axis(int32_t i, int32_t v) { g_SetAxis(i, v); }
static int core_button_count(void) { return g_GetButtonCount(); }
static const char *core_button_name(int32_t i) { return (const char *)g_GetButtonName(i); }
static int core_axis_count(void) { return g_GetAxisCount(); }
static const char *core_axis_name(int32_t i) { return (const char *)g_GetAxisName(i); }
static const char *core_sd_name(int32_t i) { return (const char *)g_GetSaveDataFileName(i); }
static int64_t core_sd_size(int32_t i) { return g_GetSaveDataFileSize(i); }
static const uint8_t *core_sd_buffer(int32_t i) { return (const uint8_t *)g_GetSaveDataFileBuffer(i); }
static const uint8_t *core_game_state(int64_t *size)
{
	*size = g_GetMemoryDomainSize(0);
	return (const uint8_t *)g_GetMemoryDomainPtr(0);
}
static const char *core_game_properties(void) { return (const char *)g_GetGameProperties(); }

typedef struct { uint8_t *b; size_t len, cap, pos; } membuf;
static int32_t mem_write(uintptr_t ud, const uint8_t *d, uintptr_t n)
{
	membuf *m = (membuf *)ud;
	if (m->len + n > m->cap)
	{
		m->cap = (m->len + n) * 2 + 64;
		m->b = realloc(m->b, m->cap);
	}
	memcpy(m->b + m->len, d, n);
	m->len += n;
	return 0;
}
static intptr_t mem_read(uintptr_t ud, uint8_t *d, uintptr_t n)
{
	membuf *m = (membuf *)ud;
	const uintptr_t avail = m->len - m->pos;
	if (n > avail)
		n = avail;
	memcpy(d, m->b + m->pos, n);
	m->pos += n;
	return (intptr_t)n;
}

static const char *g_wbx, *g_workdir;
static int g_rerecord;
static long g_session_at, g_stale_at;
static membuf g_state;

static void check(const mb_return *r, const char *what)
{
	if (r->error_message[0])
	{
		fprintf(stderr, "%s: %s\n", what, r->error_message);
		exit(1);
	}
}

/* the host: the core loaded, every file of the work folder mounted, the
 * exports found, the machine started (Init) and sealed */
static void build_host(void)
{
	FILE *wf = fopen(g_wbx, "rb");
	if (!wf) { perror(g_wbx); exit(1); }
	const uint32_t mib[] = { LAYOUT_MIB };
	mb_memory_layout_template layout = {
		(uintptr_t)mib[0] << 20, (uintptr_t)mib[1] << 20, (uintptr_t)mib[2] << 20,
		(uintptr_t)mib[3] << 20, (uintptr_t)mib[4] << 20 };
	freader fr = { wf };
	mb_return r;
	wbx_create_host(&layout, "core.wbx", file_read, (uintptr_t)&fr, &r);
	fclose(wf);
	check(&r, "create");
	g_host = (mb_host *)r.data;

	DIR *d = opendir(g_workdir);
	if (!d) { perror(g_workdir); exit(1); }
	struct dirent *de;
	while ((de = readdir(d)) != NULL)
	{
		char path[4096], real[4096];
		snprintf(path, sizeof path, "%s/%s", g_workdir, de->d_name);
		struct stat st;
		if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
			continue;
		if (!realpath(path, real)) { perror(path); exit(1); }
		wbx_mount_file_path(g_host, de->d_name, real, &r);
		check(&r, de->d_name);
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
	g_InputWasRead = (intfn)proc("InputWasRead");
	g_GetAudio = (ptrfn)proc("GetAudio");
	g_GetAudioSampleCount = (intfn)proc("GetAudioSampleCount");
	g_GetCycleCount = (u64fn)proc("GetCycleCount");
	g_GetStateDigest = (u64fn)proc("GetStateDigest");
	g_SetButton = (setfn)proc("SetButton");
	g_SetAxis = (setfn)proc("SetAxis");
	g_GetButtonCount = (intfn)proc("GetButtonCount");
	g_GetButtonName = (ptrfn_i32)proc("GetButtonName");
	g_GetAxisCount = (intfn)proc("GetAxisCount");
	g_GetAxisName = (ptrfn_i32)proc("GetAxisName");
	g_GetSaveDataFileCount = (i32fn)proc("GetSaveDataFileCount");
	g_GetSaveDataFileName = (ptrfn_i32)proc("GetSaveDataFileName");
	g_GetSaveDataFileSize = (i64fn_i32)proc("GetSaveDataFileSize");
	g_GetSaveDataFileBuffer = (ptrfn_i32)proc("GetSaveDataFileBuffer");
	g_GetMemoryDomainPtr = (ptrfn_i32)proc("GetMemoryDomainPtr");
	g_GetMemoryDomainSize = (i64fn_i32)proc("GetMemoryDomainSize");
	g_GetGameProperties = (ptrfn)proc("GetGameProperties");

	/* Init runs before Seal: the started machine is the sealed baseline */
	if (g_Init() != 1)
	{
		fprintf(stderr, "run-wbx: Init failed: %s\n", (const char *)g_GetLoadError());
		exit(1);
	}
	wbx_deactivate_host(g_host, &r);
	wbx_seal(g_host, &r);
	check(&r, "seal");
	wbx_activate_host(g_host, &r);
}

static void pre_frame(long step)
{
	mb_return r;
	if (g_stale_at && (step == g_stale_at || step == g_stale_at + 1))
	{
		if (step == g_stale_at)
		{
			g_state.len = 0;
			wbx_save_state(g_host, mem_write, (uintptr_t)&g_state, &r);
			check(&r, "save_state");
		}
		else
		{
			g_state.pos = 0;
			wbx_load_state(g_host, mem_read, (uintptr_t)&g_state, &r);
			check(&r, "load_state");
		}
	}
	if (g_rerecord || step == g_session_at)
	{
		g_state.len = 0;
		wbx_save_state(g_host, mem_write, (uintptr_t)&g_state, &r);
		check(&r, "save_state");
	}
	if (step == g_session_at)
	{
		/* the machine leaves in a state and arrives in a new host */
		wbx_deactivate_host(g_host, &r);
		wbx_destroy_host(g_host, &r);
		build_host();
	}
	if (g_rerecord || step == g_session_at)
	{
		g_state.pos = 0;
		wbx_load_state(g_host, mem_read, (uintptr_t)&g_state, &r);
		check(&r, "load_state");
	}
}

static int known(const char *arg)
{
	static long *want;
	if (want)
	{
		*want = atol(arg);
		want = NULL;
		return 1;
	}
	if (!strcmp(arg, "--rerecord"))
		return g_rerecord = 1;
	if (!strcmp(arg, "--session-at"))
		return (want = &g_session_at) != NULL;
	if (!strcmp(arg, "--stale-state"))
		return (want = &g_stale_at) != NULL;
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 3)
	{
		fprintf(stderr, "usage: run-wbx <core.wbx> <workdir> [options] [--rerecord] [--session-at N] [--stale-state N]\n");
		return 2;
	}
	static struct harness_opts o;
	if (!harness_parse(argc, argv, 3, &o, known))
		return 2;
	g_wbx = argv[1];
	g_workdir = argv[2];
	build_host();

	const struct harness_core c = {
		.init = core_init,
		.load_error = core_load_error,
		.frame = core_frame,
		.video = core_video,
		.audio = core_audio,
		.gametic = core_gametic,
		.input_was_read = core_input_was_read,
		.clock = core_clock,
		.state_digest = core_state,
		.set_button = core_set_button,
		.set_axis = core_set_axis,
		.button_count = core_button_count,
		.button_name = core_button_name,
		.axis_count = core_axis_count,
		.axis_name = core_axis_name,
		.savedata_count = core_sd_count,
		.savedata_name = core_sd_name,
		.savedata_size = core_sd_size,
		.savedata_buffer = core_sd_buffer,
		.game_state = core_game_state,
		.game_properties = core_game_properties,
		.pre_frame = pre_frame,
	};
	const int ret = harness_run(&c, &o);
	if (g_rerecord || g_session_at)
		fprintf(stderr, "stateBytes=%zu\n", g_state.len);
	mb_return r;
	wbx_deactivate_host(g_host, &r);
	wbx_destroy_host(g_host, &r);
	free(g_state.b);
	return ret;
}
