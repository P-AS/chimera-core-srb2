/* i_system.c - SRB2's system layer without a host (upstream's sdl/i_system.c,
 * less SDL; its shape is upstream's dummy/i_system.c):
 *
 *   - no window, keyboard, mouse, joystick, clipboard or console of the
 *     host's: the frontend's input arrives as the driver's, never as events
 *   - I_Error and I_Quit halt the machine (chimera_exit, the driver's) rather
 *     than end a process
 *   - the files are the machine's (platform/files.c): srb2.pk3 and the rest
 *     are where the host mounts them ("."), and what the game writes and the
 *     folders it makes are the machine's memory
 *   - the "operating system's" random bytes, which seed the game's RNG at
 *     start (M_RandomSeedFromOS), are the driver's seed's
 *   - the clock is the machine's (below): nothing reads the host's */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "doomdef.h"
#include "doomtype.h"
#include "i_system.h"
#include "i_time.h"
#include "i_joy.h"

#include "chimera-platform.h"
#define SRB2_INPUT_TICCMD
#include "srb2-input.h"

int __real_clock_gettime(clockid_t clk, struct timespec *tp);

UINT8 graphics_started = 0;
UINT8 keyboard_started = 0;

/* ---- time: the machine's
 *
 * The precise clock counts TIC_UNITS a tic (35 MHz: a microsecond is 35 units,
 * as m_anigif and Lua's getTimeMicros divide by I_GetPrecisePrecision()/1e6).
 * It moves only when the machine does, a tic a step (chimera_clock_step, the
 * driver's). When the engine sleeps - the loops that wait inside a tic for
 * I_GetTime to move: the wipes, the title card, the intro - the step ends
 * there (chimera_wait), and the next one brings the tic it waits for: each
 * frame of a wipe is a step. The frame cap's sleep (I_SleepDuration) is
 * pacing, which is the frontend's: nothing.
 *
 * It starts half a tic in and stays on half-tics: I_UpdateTime turns deltas
 * into tics with a double accumulator and a strict ">", and a clock on whole
 * tics sits exactly on that threshold (1/35 is not more than 1/35), giving
 * no tic on the first step.
 *
 * chimera_host_clock (the native reference's --host-clock, never the core's)
 * puts the host's monotonic clock back, for the gate's teeth. */

#define TIC_UNITS 1000000ull
#define PRECISION (TICRATE * TIC_UNITS)
/* what the libc clocks answer: 2000-01-01 00:00:00 UTC, plus the machine's time */
#define EPOCH 946684800ull

static uint64_t g_clock = TIC_UNITS / 2;
int chimera_host_clock;

static uint64_t host_precise(void)
{
	struct timespec ts;
	__real_clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * PRECISION + (uint64_t)ts.tv_nsec * (PRECISION / 1000000ull) / 1000ull;
}

uint64_t chimera_clock_precise(void)
{
	return chimera_host_clock ? host_precise() : g_clock;
}

void chimera_clock_step(void) { g_clock += TIC_UNITS; }

precise_t I_GetPreciseTime(void) { return chimera_clock_precise(); }
UINT64 I_GetPrecisePrecision(void) { return PRECISION; }
void I_StartupTimer(void) {}

void I_Sleep(UINT32 ms)
{
	if (chimera_host_clock)
	{
		struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
		nanosleep(&ts, NULL);
		return;
	}
	chimera_wait();
}

void I_SleepDuration(precise_t duration)
{
	if (chimera_host_clock)
	{
		struct timespec ts = { (time_t)(duration / PRECISION),
			(long)(duration % PRECISION * 1000ull / (PRECISION / 1000000ull)) };
		nanosleep(&ts, NULL);
	}
}

/* the libc clocks the engine calls (the link wraps them): the machine's
 * time since EPOCH. localtime is UTC: the host's time zone is not the
 * machine's (Lua's os.date). */
static uint64_t machine_us(void)
{
	return chimera_clock_precise() / (PRECISION / 1000000ull);
}

int __wrap_clock_gettime(clockid_t clk, struct timespec *tp)
{
	const uint64_t us = machine_us();
	tp->tv_sec = (time_t)(us / 1000000ull + (clk == CLOCK_REALTIME ? EPOCH : 0));
	tp->tv_nsec = (long)(us % 1000000ull) * 1000L;
	return 0;
}

time_t __wrap_time(time_t *t)
{
	const time_t now = (time_t)(machine_us() / 1000000ull + EPOCH);
	if (t)
		*t = now;
	return now;
}

int __wrap_gettimeofday(struct timeval *tv, void *tz)
{
	(void)tz;
	const uint64_t us = machine_us();
	tv->tv_sec = (time_t)(us / 1000000ull + EPOCH);
	tv->tv_usec = (suseconds_t)(us % 1000000ull);
	return 0;
}

clock_t __wrap_clock(void)
{
	return (clock_t)(machine_us() * (CLOCKS_PER_SEC / 1000000));
}

struct tm *__wrap_localtime(const time_t *t)
{
	static struct tm tm;
	return gmtime_r(t, &tm);
}

/* rand: glibc's and musl's differ, so the core's own (C's example LCG) */
static uint32_t g_rand = 1;
int __wrap_rand(void)
{
	g_rand = g_rand * 1103515245u + 12345u;
	return (int)((g_rand / 65536u) % 32768u);
}
void __wrap_srand(unsigned seed) { g_rand = seed; }

/* ---- the end of the program: the machine's (the driver's) */

void I_Quit(void)
{
	chimera_exit(0, NULL);
}

void I_Error(const char *error, ...)
{
	char msg[8192];
	va_list ap;
	va_start(ap, error);
	vsnprintf(msg, sizeof msg, error, ap);
	va_end(ap);
	chimera_exit(-1, msg);
}

/* the functions upstream runs at exit save the configuration and the game
 * data, shut the sound and the network down: none of it outlives the machine */
void I_AddExitFunc(void (*func)()) { (void)func; }
void I_RemoveExitFunc(void (*func)()) { (void)func; }
INT32 I_StartupSystem(void) { return 0; }
void I_ShutdownSystem(void) {}

void I_OutputMsg(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

/* ---- the "operating system's" random bytes: splitmix64 of the driver's
 * seed, so the game's RNG starts where the machine says */

uint64_t chimera_random_seed;

size_t I_GetRandomBytes(char *destination, size_t count)
{
	uint64_t x = chimera_random_seed;
	for (size_t i = 0; i < count; i++)
	{
		if (i % 8 == 0)
		{
			uint64_t z = (x += 0x9E3779B97F4A7C15ull);
			z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
			z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
			x = z ^ (z >> 31);
		}
		destination[i] = (char)(x >> (8 * (i % 8)));
	}
	return count;
}

/* ---- the host: none */

size_t I_GetFreeMem(size_t *total)
{
	*total = 0;
	return 0;
}

void I_GetDiskFreeSpace(INT64 *freespace) { *freespace = 0; }
char *I_GetUserName(void) { return NULL; }
/* a folder of the machine's (platform/files.c): never the host's */
INT32 I_mkdir(const char *dirname, INT32 unixright)
{
	(void)unixright;
	chimera_mkdir(dirname);
	return 0;
}
const CPUInfoFlags *I_CPUInfo(void) { return NULL; }
const char *I_LocateWad(void) { return "."; }
char *I_GetEnv(const char *name) { (void)name; return NULL; }
INT32 I_PutEnv(char *variable) { (void)variable; return -1; }
INT32 I_ClipboardCopy(const char *data, size_t size) { (void)data; (void)size; return -1; }
const char *I_ClipboardPaste(void) { return NULL; }
void I_RegisterSysCommands(void) {}
const char *I_GetSysName(void) { return "Chimera"; }

/* ---- input: the driver's, never the host's events */

void I_GetEvent(void) {}
INT32 I_GetKey(void) { return 0; }
void I_OsPolling(void) {}
/* the tic command G_BuildTiccmd starts from ("empty, or external driver"):
 * the controller's axes (srb2-input.c). Asked for, the step has read input.
 * The second is splitscreen's player, whom the controller does not drive. */
static ticcmd_t g_basecmd, g_basecmd2;
ticcmd_t *I_BaseTiccmd(void)
{
	chimera_input_read();
	srb2_input_base(&g_basecmd);
	return &g_basecmd;
}
ticcmd_t *I_BaseTiccmd2(void) { return &g_basecmd2; }
void I_Tactile(FFType Type, const JoyFF_t *Effect) { (void)Type; (void)Effect; }
void I_Tactile2(FFType Type, const JoyFF_t *Effect) { (void)Type; (void)Effect; }
void I_JoyScale(void) {}
void I_JoyScale2(void) {}
void I_InitJoystick(void) {}
void I_InitJoystick2(void) {}
INT32 I_NumJoys(void) { return 0; }
const char *I_GetJoyName(INT32 joyindex) { (void)joyindex; return NULL; }
void I_StartupMouse(void) {}
void I_StartupMouse2(void) {}
void I_GetJoystickEvents(void) {}
void I_GetJoystick2Events(void) {}
void I_GetMouseEvents(void) {}
void I_UpdateMouseGrab(void) {}
void I_SetMouseGrab(boolean grab) { (void)grab; }
void I_GetCursorPosition(INT32 *x, INT32 *y)
{
	*x = 0;
	*y = 0;
}
void I_SetTextInputMode(boolean active) { (void)active; }
boolean I_GetTextInputMode(void) { return false; }
