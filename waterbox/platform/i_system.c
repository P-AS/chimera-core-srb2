/* i_system.c - SRB2's system layer without a host (upstream's sdl/i_system.c,
 * less SDL; its shape is upstream's dummy/i_system.c):
 *
 *   - no window, keyboard, mouse, joystick, clipboard or console of the
 *     host's: the frontend's input arrives as the driver's, never as events
 *   - I_Error and I_Quit halt the machine (chimera_exit, the driver's) rather
 *     than end a process
 *   - the files are the machine's: srb2.pk3 and the rest are where the host
 *     mounts them ("."), and no folder is made
 *   - the "operating system's" random bytes, which seed the game's RNG at
 *     start (M_RandomSeedFromOS), are the driver's seed's
 *   - the clock is the host's monotonic clock (milestone 0); the machine's
 *     own comes with the virtual-time seam */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "doomdef.h"
#include "doomtype.h"
#include "i_system.h"
#include "i_time.h"
#include "i_joy.h"

#include "chimera-platform.h"

UINT8 graphics_started = 0;
UINT8 keyboard_started = 0;

/* ---- time */

#define PRECISION 1000000000ull

uint64_t chimera_clock_precise(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * PRECISION + (uint64_t)ts.tv_nsec;
}

precise_t I_GetPreciseTime(void) { return chimera_clock_precise(); }
UINT64 I_GetPrecisePrecision(void) { return PRECISION; }
void I_StartupTimer(void) {}

void I_Sleep(UINT32 ms)
{
	struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
	nanosleep(&ts, NULL);
}

void I_SleepDuration(precise_t duration)
{
	struct timespec ts = { (time_t)(duration / PRECISION), (long)(duration % PRECISION) };
	nanosleep(&ts, NULL);
}

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
INT32 I_mkdir(const char *dirname, INT32 unixright)
{
	(void)dirname; (void)unixright;
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
 * empty, for now */
static ticcmd_t g_basecmd, g_basecmd2;
ticcmd_t *I_BaseTiccmd(void) { return &g_basecmd; }
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
