/* srb2-ghosts.c - ghosts: a recorded run of a level, drawn as a see-through
 * player that moves as the recording did, to race against. SRB2 shows them in
 * Record Attack and NiGHTS Mode only; the core adds three things, through the
 * engine's hooks (patches/0009):
 *
 *   - the project's own ghost files (file_slots.json's "ghosts" slot): each is
 *     shown on the map it was recorded on, in Record Attack, NiGHTS Mode and,
 *     with the setting "Ghosts outside Record Attack", in normal play. A file
 *     named as Record Attack names its replays (MAP01-sonic-time-best.lmp,
 *     MAP01-sonic-last.lmp, MAP01-guest.lmp...) is also found in the replay
 *     folder the game uses, whatever it is called (platform/files.c): "main"
 *     for the game itself, the add-on's own name when an add-on brings its own
 *     game data. Such a file is the game's to show, by the ghost settings
 *     (SRB2's console variables ghost_besttime and the others); any other name
 *     is always shown.
 *   - ghosts in normal play (the setting): the replay folder's, as Record
 *     Attack picks them, and the project's own.
 *   - a recording of every level played outside Record Attack (the setting
 *     "Record ghosts"), saved as ghosts/<n>-<map>-<character>.lmp, a file the
 *     game wrote, so Export Save Data has it. The level being played is in the
 *     export too, as far as it went: a movie that ends at the goal does not
 *     have to play on into the score tally. SRB2's own recording
 *     (demorecording) is not used, because it also stops the game saving
 *     progress; the engine writes the same ghost data while
 *     demoghostrecording is set.
 *
 * A ghost does not change the game: it is scenery that nothing touches
 * (MF_NOBLOCKMAP, MF_NOCLIP, MF_SCENERY), and recording only writes what the
 * player did. The test script checks both (run-gate.sh's ghosts test).
 *
 * Everything here is the machine's: in its memory, in every savestate. The
 * export's copy of the level being recorded is output, made when the export
 * asks, in invisible memory (wbx-entry.c gives it). */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_player.h"
#include "g_demo.h"
#include "g_game.h"
#include "g_state.h"
#include "m_misc.h"
#include "md5.h"
#include "p_setup.h"
#include "r_skins.h"

#include "chimera-platform.h"
#include "srb2-driver.h"

static int g_normal_play, g_record;
/* the recordings made so far, which numbers them */
static unsigned g_count;
/* the recording: the file it is saved as, and its buffer (G_RecordDemo's) */
static char g_name[64];
static UINT8 *g_buf;
/* the first ghost tic after a level starts catches up with its clock */
static int g_catch_up;
/* the export's copy of the level being recorded (invisible: output) */
static UINT8 *g_pending;
static size_t g_pending_cap;

/* SRB2's demo format (g_demo.c) */
#define DEMOHEADER "\xF0" "SRB2Replay" "\x0F"
#define DEMOMARKER 0x80
#define DF_GHOST 0x01
/* a recording stops early when this little room is left: more than a tic of
 * the most a tic can write */
#define LOW_ROOM (64u << 10)

void srb2_set_ghosts(int normal_play, int record)
{
	g_normal_play = normal_play;
	g_record = record;
}

void srb2_set_ghost_pending_buffer(unsigned char *buf, size_t size)
{
	g_pending = buf;
	g_pending_cap = size;
}

boolean I_GhostsInNormalPlay(void)
{
	return g_normal_play && !modeattacking && !demoplayback && !titlemapinaction && playerstarts[0];
}

/* the names Record Attack gives its replays: MAPxx-guest.lmp and
 * MAPxx-<skin>-{last,time-best,score-best,rings-best}.lmp */
static int record_attack_name(const char *n)
{
	static const char *const ends[] = { "-guest.lmp", "-last.lmp", "-time-best.lmp", "-score-best.lmp", "-rings-best.lmp" };
	const size_t len = strlen(n);
	if (len < 6 || strncasecmp(n, "MAP", 3) || n[5] != '-')
		return 0;
	for (size_t i = 0; i < sizeof ends / sizeof ends[0]; i++)
	{
		const size_t e = strlen(ends[i]);
		if (len >= 5 + e && !strcasecmp(n + len - e, ends[i]))
			return 1;
	}
	return 0;
}

/* a ghost file's map, from its header; 0 if it is no ghost */
static int ghost_map(const char *name, UINT8 md5[16])
{
	UINT8 h[55];
	FILE *f = fopen(name, "rb");
	if (!f)
		return 0;
	const size_t n = fread(h, 1, sizeof h, f);
	fclose(f);
	/* G_AddGhost's own checks come later; these find the map: the header, a
	 * demo version with a 16-bit map number (0x000c and up), "PLAY" */
	if (n < sizeof h || memcmp(h, DEMOHEADER, 12) || (h[14] | h[15] << 8) < 0x000c || memcmp(h + 32, "PLAY", 4))
		return 0;
	if (!(h[54] & DF_GHOST))
		return 0;
	memcpy(md5, h + 38, 16);
	return h[36] | h[37] << 8;
}

void I_LoadGhosts(void)
{
	g_catch_up = 1;
	if (demoplayback || titlemapinaction || !playerstarts[0])
		return;
	if (!modeattacking && !I_GhostsInNormalPlay())
		return;
	for (int i = 0; i < chimera_ghost_file_count(); i++)
	{
		char name[256];
		UINT8 md5[16];
		snprintf(name, sizeof name, "%s", chimera_ghost_file_name(i));
		/* the replay folder's: the game loaded it already, or chose not to */
		if (record_attack_name(name))
			continue;
		const int map = ghost_map(name, md5);
		if (map != gamemap)
			continue;
		if (memcmp(md5, mapmd5, 16))
		{
			CONS_Alert(CONS_NOTICE, "Ghost %s: recorded on another version of %s\n", name, G_BuildMapName(gamemap));
			continue;
		}
		G_AddGhost(name);
	}
}

/* the ghosts' tic. A ghost's tics count from its level's start; a level
 * entered at a starpost starts at the starpost's time, so on the first tic
 * the ghosts are run on to it */
void I_GhostTicker(void)
{
	if (g_catch_up)
	{
		g_catch_up = 0;
		for (tic_t t = 1; t < leveltime && G_HaveGhosts(); t++)
			G_GhostTicker();
	}
	G_GhostTicker();
}

/* ---- the recording */

static void stop_recording(void)
{
	size_t len, room;
	if (!demoghostrecording)
		return;
	demoghostrecording = false;
	if (G_DemoRecorded(&len, &room) != g_buf)
	{
		/* another demo took the buffer: nothing left to save */
		CONS_Alert(CONS_WARNING, "Ghost %s was not saved: another demo started\n", g_name);
		g_buf = NULL;
		return;
	}
	/* SRB2's own end of a recording: the end marker, the checksum, the file
	 * (a file the game wrote), the buffer freed; and the ghosts shown, which
	 * are the level's */
	demorecording = true;
	G_CheckDemoStatus();
	g_buf = NULL;
}

void I_LevelUnloading(void)
{
	stop_recording();
	G_FreeGhosts();
}

void I_LevelLoaded(void)
{
	player_t *p = &players[consoleplayer];
	size_t len, room;
	if (!g_record || modeattacking || demoplayback || demorecording || metalrecording || titlemapinaction
		|| gamestate != GS_LEVEL || !playeringame[consoleplayer] || !p->mo)
		return;
	char base[48];
	snprintf(base, sizeof base, "ghosts/%03u-%s-%s", ++g_count, G_BuildMapName(gamemap), skins[p->skin]->name);
	snprintf(g_name, sizeof g_name, "%s.lmp", base);
	/* G_RecordDemo sets demorecording, which is what this is not */
	G_RecordDemo(base);
	demorecording = false;
	/* G_BeginRecording sets the player's flags from the controls' options;
	 * the game set them already, and a recording must not change the game */
	const pflags_t keep = p->pflags;
	G_BeginRecording();
	p->pflags = keep;
	g_buf = G_DemoRecorded(&len, &room);
	demoghostrecording = g_buf != NULL;
}

/* after every step: a level that has ended (the score tally, the title, a
 * cutscene) ends its recording; so does a buffer about to be full */
void srb2_ghosts_step(void)
{
	size_t len, room;
	if (!demoghostrecording)
		return;
	const UINT8 *b = G_DemoRecorded(&len, &room);
	if (gamestate != GS_LEVEL || b != g_buf || room < LOW_ROOM)
	{
		if (room < LOW_ROOM && b == g_buf && gamestate == GS_LEVEL)
			CONS_Alert(CONS_WARNING, "Ghost %s is full: its recording ends here\n", g_name);
		stop_recording();
	}
}

/* the level being recorded, as the file it will be: what it has so far, the
 * end marker and the checksum (g_demo.c's WriteDemoChecksum: the MD5 of
 * everything after the checksum, at byte 16). NULL when nothing is recorded */
const unsigned char *srb2_ghost_pending(const char **name, size_t *size)
{
	size_t len, room;
	if (!demoghostrecording || !g_pending)
		return NULL;
	const UINT8 *b = G_DemoRecorded(&len, &room);
	if (b != g_buf || len < 32 || len + 1 > g_pending_cap)
		return NULL;
	memcpy(g_pending, b, len);
	g_pending[len] = DEMOMARKER;
	md5_buffer((const char *)g_pending + 32, len + 1 - 32, g_pending + 16);
	*name = g_name;
	*size = len + 1;
	return g_pending;
}

/* the ghosts' positions and the replay folder's name, for the native
 * reference's diagnostics */
const char *chimera_ghosts_summary(void)
{
	static char out[1024];
	fixed_t xyz[3 * 16];
	const INT32 n = G_GhostPositions(xyz, 16);
	int k = snprintf(out, sizeof out, "ghosts %d folder %s", (int)n, timeattackfolder);
	for (INT32 i = 0; i < n && k < (int)sizeof out; i++)
		k += snprintf(out + k, sizeof out - (size_t)k, " %d,%d,%d", (int)xyz[3 * i], (int)xyz[3 * i + 1], (int)xyz[3 * i + 2]);
	return out;
}
