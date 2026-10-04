/* srb2-driver.c - the machine: SRB2's engine started as upstream's main
 * starts it (D_SRB2Main, then what D_SRB2Loop does before its first frame:
 * patches/0001), and stepped a pass of its loop at a time (D_RunFrame).
 *
 * An exit of the engine - I_Error, a quit - does not end a process: it halts
 * the machine where it stands (chimera_exit longjmps out of the engine), which
 * keeps answering, silent and still, with I_Error's message kept. */
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include "doomdef.h"
#include "d_main.h"
#include "m_argv.h"

#include "chimera-platform.h"
#include "srb2-driver.h"

static jmp_buf g_exit_jump;
static int g_in_engine;
static int g_halted;
static char g_error[1024];

void chimera_exit(int rc, const char *msg)
{
	if (msg)
	{
		snprintf(g_error, sizeof g_error, "%s", msg);
		fprintf(stderr, "srb2: I_Error: %s\n", msg);
	}
	else
		fprintf(stderr, "srb2: quit (%d)\n", rc);
	g_halted = 1;
	if (g_in_engine)
		longjmp(g_exit_jump, 1);
	/* an exit outside the engine's calls (an atexit, say) has nowhere to go */
	for (;;) {}
}

int srb2_start(int argc, char **argv)
{
	myargc = argc;
	myargv = argv;
	g_in_engine = 1;
	if (setjmp(g_exit_jump) == 0)
	{
		D_SRB2Main();
		D_SRB2LoopSetup();
	}
	g_in_engine = 0;
	return g_halted ? -1 : 0;
}

void srb2_frame(void)
{
	if (g_halted)
		return;
	g_in_engine = 1;
	if (setjmp(g_exit_jump) == 0)
		D_RunFrame();
	g_in_engine = 0;
}

int srb2_halted(void) { return g_halted; }
const char *srb2_error(void) { return g_error; }
