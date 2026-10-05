/* srb2-driver.c - the machine: SRB2's engine started as upstream's main
 * starts it (D_SRB2Main, then what D_SRB2Loop does before its first frame:
 * patches/0001), and stepped a tic at a time.
 *
 * The engine runs on a cothread of its own (libco, miniBox's): a step moves
 * the machine's clock one tic and resumes it, and it runs until it next waits
 * for time. That is either the top of its loop, a pass of D_RunFrame done, or
 * a sleep inside a tic (I_Sleep, platform/i_system.c). SRB2 sleeps there in
 * the loops that draw a frame a tic without running the game: the wipes, the
 * level's title card, the special stage's white, the intro's. So each of those
 * frames is a step of its own, with its own picture, and a step in which no
 * tic command was built is lag: the game read no input.
 *
 * The controller (srb2-input.c) is bound after the start, and its buttons
 * that changed are posted as key events before each step.
 *
 * An exit of the engine - I_Error, a quit - does not end a process: it halts
 * the machine where it stands. The engine's cothread is never resumed again,
 * and the machine keeps answering, silent and still, with I_Error's message
 * kept. */
#include <stdio.h>
#include <string.h>

#include <libco.h>

#include "doomdef.h"
#include "d_main.h"
#include "m_argv.h"
#include "doomstat.h"
#include "netcode/d_clisrv.h"
#include "command.h"
#include "g_game.h"
#include "p_local.h"
#include "p_tick.h"
#include "m_random.h"
#include "m_cond.h"
#include "r_skins.h"

#include "chimera-platform.h"
#include "srb2-driver.h"
#include "srb2-input.h"

/* the engine's stack: its BSP walk, Lua and the netcode nest deep */
#define ENGINE_STACK (16u << 20)

static cothread_t g_host, g_engine;
static int g_started;
static int g_halted;
static int g_input_read;
static char g_error[1024];

static void to_host(void) { co_switch(g_host); }
static void unlocks(void);

static void engine_main(void)
{
	D_SRB2Main();
	D_SRB2LoopSetup();
	g_started = 1;
	for (;;)
	{
		to_host();
		D_RunFrame();
	}
}

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
	/* on the engine's cothread (where every exit comes from): leave it for
	 * good; elsewhere there is nowhere to go */
	for (;;)
	{
		if (g_engine && co_active() == g_engine)
			to_host();
	}
}

/* the engine waits for time inside a tic: the step ends here */
void chimera_wait(void)
{
	if (g_engine && co_active() == g_engine)
		to_host();
}

void chimera_input_read(void) { g_input_read = 1; }

int srb2_start(int argc, char **argv)
{
	myargc = argc;
	myargv = argv;
	g_host = co_active();
	g_engine = co_create(ENGINE_STACK, engine_main);
	if (!g_engine)
	{
		snprintf(g_error, sizeof g_error, "no memory for the engine's stack");
		g_halted = 1;
		return -1;
	}
	/* the start runs to the loop; were it to wait for time, time passes */
	for (;;)
	{
		co_switch(g_engine);
		if (g_started || g_halted)
			break;
		chimera_clock_step();
	}
	if (g_halted)
		return -1;
	srb2_input_bind();
	unlocks();
	return 0;
}

/* ---- the unlocks a project starts with (its settings): set in the game data
 * the game has loaded, client's and server's, before every step - so a
 * reload of it (an add-on's own game data, dehacked.c) keeps them. The
 * game's own updates only ever unlock, so nothing takes them back. */
static int g_unlock_modes, g_unlock_skins, g_unlock_all;

void srb2_set_unlocks(int modes, int skins, int all)
{
	g_unlock_modes = modes;
	g_unlock_skins = skins;
	g_unlock_all = all;
}

static void apply_unlocks(gamedata_t *d)
{
	if (!d)
		return;
	for (int i = 0; i < MAXUNLOCKABLES; i++)
	{
		const INT16 t = unlockables[i].type;
		if (g_unlock_all
			|| (g_unlock_modes && (t == SECRET_RECORDATTACK || t == SECRET_NIGHTSMODE))
			|| (g_unlock_skins && t == SECRET_SKIN))
			d->unlocked[i] = true;
	}
}

static void unlocks(void)
{
	if (!g_unlock_modes && !g_unlock_skins && !g_unlock_all)
		return;
	apply_unlocks(clientGamedata);
	apply_unlocks(serverGamedata);
}

/* what is unlocked, for the native reference's diagnostics */
const char *chimera_unlocks_summary(void)
{
	static char out[128];
	int ra = 0, nights = 0, skins = 0, skinsall = 0, n = 0, all = 0;
	for (int i = 0; i < MAXUNLOCKABLES; i++)
	{
		const INT16 t = unlockables[i].type;
		const int u = clientGamedata && clientGamedata->unlocked[i];
		if (!unlockables[i].name[0])
			continue;
		all++;
		n += u;
		ra |= t == SECRET_RECORDATTACK && u;
		nights |= t == SECRET_NIGHTSMODE && u;
		if (t == SECRET_SKIN)
		{
			skinsall++;
			skins += u;
		}
	}
	snprintf(out, sizeof out, "recordattack %d nights %d skins %d/%d all %d/%d", ra, nights, skins, skinsall, n, all);
	return out;
}

/* the player's skin, for the native reference's diagnostics */
const char *chimera_player_skin(void)
{
	return gamestate == GS_LEVEL && playeringame[consoleplayer] ? skins[players[consoleplayer].skin]->name : "(none)";
}

void srb2_frame(void)
{
	g_input_read = 0;
	if (g_halted)
		return;
	unlocks();
	chimera_clock_step();
	srb2_input_post();
	co_switch(g_engine);
}

int srb2_halted(void) { return g_halted; }
const char *srb2_error(void) { return g_error; }
int srb2_input_was_read(void) { return g_input_read; }
unsigned srb2_gametic(void) { return (unsigned)gametic; }

/* an engine option's value by name, for the native reference's diagnostics */
const char *chimera_cvar_string(const char *name)
{
	consvar_t *v = CV_FindVar(name);
	return v ? v->string : "(none)";
}

/* ---- the game's state, as a digest: what the game IS, apart from how it is
 * drawn or heard - the tic, the level's time, the RNG, the game state and map,
 * the player, the camera (the tic command is built from it), and every object
 * in the level. The same at every resolution (the gate's resolution leg). */
static UINT64 g_digest;
static void mix(const void *p, size_t n)
{
	const UINT8 *b = p;
	for (size_t i = 0; i < n; i++)
		g_digest = (g_digest ^ b[i]) * 0x100000001b3ull;
}
#define MIX(v) do { __typeof__(v) mix_v = (v); mix(&mix_v, sizeof mix_v); } while (0)

UINT64 chimera_state_digest(void)
{
	g_digest = 0xcbf29ce484222325ull;
	MIX(gametic);
	MIX(leveltime);
	MIX(P_GetRandSeed());
	MIX(gamestate);
	MIX(gamemap);
	const player_t *p = &players[consoleplayer];
	MIX(p->rings);
	MIX(p->score);
	MIX(p->lives);
	MIX(p->pflags);
	MIX(p->speed);
	MIX(p->playerstate);
	MIX(camera.x);
	MIX(camera.y);
	MIX(camera.z);
	MIX(camera.angle);
	MIX(camera.aiming);
	if (gamestate == GS_LEVEL)
	{
		for (thinker_t *th = thlist[THINK_MOBJ].next; th && th != &thlist[THINK_MOBJ]; th = th->next)
		{
			if (th->function == (actionf_p1)P_RemoveThinkerDelayed)
				continue;
			const mobj_t *mo = (const mobj_t *)th;
			MIX(mo->x);
			MIX(mo->y);
			MIX(mo->z);
			MIX(mo->momx);
			MIX(mo->momy);
			MIX(mo->momz);
			MIX(mo->angle);
			MIX(mo->type);
			MIX(mo->health);
			MIX((INT32)(mo->state ? mo->state - states : -1));
		}
	}
	return g_digest;
}
