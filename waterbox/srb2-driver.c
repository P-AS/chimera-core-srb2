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
#include <stddef.h>
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
#include "m_menu.h"

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

void srb2_set_unlocks(int modes, int skins, int all, int maps)
{
	g_unlock_modes = modes;
	g_unlock_skins = skins;
	g_unlock_all = all;
	/* the menu's own check, not the game data (patches/0006) */
	menu_allmapsavailable = maps ? true : false;
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
	/* the maps Record Attack offers: m_menu.c's list mode (an enum private to
	 * it, LLM_RECORDATTACK 2) set for the count, then put back */
	extern int levellistmode;
	const int mode = levellistmode;
	int ramaps = 0;
	levellistmode = 2;
	for (int i = 0; i < NUMMAPS; i++)
		ramaps += M_CanShowLevelInList(i, -1) ? 1 : 0;
	levellistmode = mode;
	snprintf(out, sizeof out, "recordattack %d nights %d skins %d/%d all %d/%d ramaps %d", ra, nights, skins, skinsall, n, all, ramaps);
	return out;
}

/* the player's skin, for the native reference's diagnostics */
const char *chimera_player_skin(void)
{
	return gamestate == GS_LEVEL && playeringame[consoleplayer] ? skins[players[consoleplayer].skin]->name : "(none)";
}

/* ---- the Game State domain (Chimera's docs/game-cores.md, "Properties"): a
 * copy of what a TASer watches, made after every step - the player's object
 * (its position, momentum and angle), the speed the game reckons, the
 * powers' timers, and what a conveyor or a moving platform adds to the
 * player's momentum. Read-only: the game would overwrite a poke on its next tic.
 * In the machine's memory, so a savestate carries it. */
struct game_state
{
	INT32 tic;            /* 0: gametic */
	INT32 level_time;     /* 4: leveltime */
	INT32 game_state;     /* 8: gamestate */
	INT32 map;            /* 12: gamemap */
	UINT8 in_level;       /* 16: the player has an object in a level */
	UINT8 pad[3];
	INT32 x, y, z;        /* 20: fixed point, 16.16 */
	INT32 momx, momy, momz; /* 32 */
	UINT32 angle;         /* 44: the object's angle, 2^32 a turn */
	INT32 speed;          /* 48: player->speed, 16.16 */
	UINT16 shoes;         /* 52: pw_sneakers, tics left */
	UINT16 invincibility; /* 54: pw_invulnerability */
	UINT16 space;         /* 56: pw_spacetime */
	UINT16 air;           /* 58: pw_underwater */
	INT32 cmomx, cmomy;   /* 60: player->cmomx/cmomy: a conveyor's or a platform's, 16.16 */
	INT32 pmomz;          /* 68: mo->pmomz: the moving floor's, 16.16 */
};

static struct game_state g_state;

static void update_game_state(void)
{
	memset(&g_state, 0, sizeof g_state);
	g_state.tic = (INT32)gametic;
	g_state.level_time = (INT32)leveltime;
	g_state.game_state = (INT32)gamestate;
	g_state.map = (INT32)gamemap;
	const player_t *p = &players[consoleplayer];
	const mobj_t *mo = gamestate == GS_LEVEL && playeringame[consoleplayer] ? p->mo : NULL;
	if (!mo)
		return;
	g_state.in_level = 1;
	g_state.x = mo->x;
	g_state.y = mo->y;
	g_state.z = mo->z;
	g_state.momx = mo->momx;
	g_state.momy = mo->momy;
	g_state.momz = mo->momz;
	g_state.angle = mo->angle;
	g_state.speed = p->speed;
	g_state.shoes = p->powers[pw_sneakers];
	g_state.invincibility = p->powers[pw_invulnerability];
	g_state.space = p->powers[pw_spacetime];
	g_state.air = p->powers[pw_underwater];
	g_state.cmomx = p->cmomx;
	g_state.cmomy = p->cmomy;
	g_state.pmomz = mo->pmomz;
}

int srb2_domain_count(void) { return 1; }
const char *srb2_domain_name(int i) { return i == 0 ? "Game State" : ""; }
UINT8 *srb2_domain_ptr(int i) { return i == 0 ? (UINT8 *)&g_state : NULL; }
long long srb2_domain_size(int i) { return i == 0 ? (long long)sizeof g_state : 0; }

/* the property table: what the Game State block holds, by name */
const char *srb2_game_properties(void)
{
	static char json[8192];
	if (json[0])
		return json;
	int n = 0, first = 1;
#define P(...) n += snprintf(json + n, sizeof json - (size_t)n, __VA_ARGS__)
#define GS(name, field, type, group, desc) \
	P("%s    { \"name\": \"%s\", \"domain\": \"Game State\", \"offset\": %d, \"type\": \"%s\", \"group\": \"%s\", " \
	  "\"writable\": false, \"description\": \"%s\" }", first ? "" : ",\n", \
	  name, (int)offsetof(struct game_state, field), type, group, desc), first = 0
	P("{\n  \"properties\": [\n");
	GS("Game.Tic", tic, "s32", "Game", "The game's tic (gametic)");
	GS("Game.Level Time", level_time, "s32", "Game", "Tics on this level (leveltime)");
	GS("Game.State", game_state, "s32", "Game", "gamestate (1: in a level)");
	GS("Game.Map", map, "s32", "Game", "gamemap");
	GS("Player.In Level", in_level, "bool", "Player", "The player has an object in a level; the rest of Player and Timers is 0 when not");
	GS("Player.X", x, "s32", "Player", "Position, 16.16 fixed point: 65536 is one unit");
	GS("Player.Y", y, "s32", "Player", "Position, 16.16 fixed point");
	GS("Player.Z", z, "s32", "Player", "Height, 16.16 fixed point");
	GS("Player.Momentum X", momx, "s32", "Player", "Units a tic, 16.16 fixed point");
	GS("Player.Momentum Y", momy, "s32", "Player", "Units a tic, 16.16 fixed point");
	GS("Player.Momentum Z", momz, "s32", "Player", "Units a tic, 16.16 fixed point");
	GS("Player.Angle", angle, "u32", "Player", "The player object's angle: 2^32 is a full turn, 0 is east, counterclockwise");
	GS("Player.Speed", speed, "s32", "Player", "Horizontal speed as the game reckons it (player->speed, against the floor it stands on), 16.16 fixed point");
	GS("Timers.Speed Shoes", shoes, "u16", "Timers", "Tics of speed shoes left (pw_sneakers)");
	GS("Timers.Invincibility", invincibility, "u16", "Timers", "Tics of invincibility left (pw_invulnerability)");
	GS("Timers.Space", space, "u16", "Timers", "Tics of air left in space (pw_spacetime)");
	GS("Timers.Air", air, "u16", "Timers", "Tics of air left underwater (pw_underwater)");
	GS("Player.Conveyor Momentum X", cmomx, "s32", "Player", "What a conveyor or a moving platform carrying the player adds to Momentum X (player->cmomx), 16.16 fixed point");
	GS("Player.Conveyor Momentum Y", cmomy, "s32", "Player", "What a conveyor or a moving platform carrying the player adds to Momentum Y (player->cmomy), 16.16 fixed point");
	GS("Player.Platform Momentum Z", pmomz, "s32", "Player", "The vertical momentum of the moving floor the player stands on (mo->pmomz), kept on leaving it; 16.16 fixed point");
	P("\n  ]\n}\n");
#undef GS
#undef P
	return json;
}

void srb2_frame(void)
{
	g_input_read = 0;
	if (g_halted)
		return;
	unlocks();
	chimera_gl_step();
	chimera_clock_step();
	srb2_input_post();
	co_switch(g_engine);
	update_game_state();
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
