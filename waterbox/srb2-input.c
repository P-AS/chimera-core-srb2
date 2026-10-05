/* srb2-input.c - the controller: SRB2's own keyboard, and axes for what a key
 * cannot say exactly.
 *
 * The buttons are the game's controls in its default keyboard scheme (the
 * "FPS" one, gamecontroldefault[gcs_fps]: WASD, the arrows, Space, Shift...),
 * and pressing one is pressing its key: a key event goes to the engine
 * (D_PostEvent) when a button changes, so the menus, the title screen and the
 * game all read it as they read a keyboard, and the game builds its tic
 * command from the held keys with all its own logic (accelerative turning, the
 * Simple control style's camera). Enter and escape are the menus' own keys,
 * and a prompt's too (Enter or Space is yes, escape no).
 *
 * The names are chosen so that Chimera's TAStudio, which takes a control's
 * letter from a table of its own or else from the name's last word, tells
 * every one apart without a table for SRB2: Turn left/right (l, r) beside
 * Strafe Left/Right (L, R), Reset Camera (C), Enter (E) beside escape (e,
 * lowercase for its letter). docs/chimera-proposal.patch has a table Chimera could
 * adopt (proposed upstream, not required). The ring-slinger controls - Fire, Fire
 * Normal, Toss Flag, the weapons - are left out for now (commented below).
 * The bindings are forced to that scheme at start (srb2_input_bind), so no
 * configuration can change what a button means.
 *
 * The axes are added into the tic command through upstream's seam for an
 * external driver (I_BaseTiccmd, which G_BuildTiccmd starts from):
 *   Forward Move, Side Move  -50..50 (MAXPLMOVE), as a key's full press is
 *                            50. The game adds its keys' amount without
 *                            clamping the sum, so an axis counts only while its
 *                            keys are not held (Forward/Backward, Strafe
 *                            Left/Right): the command stays in the game's range.
 *   Turn                     an angle delta, -32768..32767 (1/65536 turn),
 *                            added to what the turn keys give; positive
 *                            turns right (the command's angleturn negated)
 *   Aim                      the look pitch, as the command's aiming (angle >>
 *                            16) negated: positive looks down, as a stick's
 *                            Y; clipped as the game clips it; 0 leaves the
 *                            game's own look (patches/0004)
 */
#include <string.h>

#include "doomdef.h"
#include "g_game.h"
#include "d_event.h"
#include "d_main.h"
#include "d_ticcmd.h"
#include "g_input.h"
#include "keys.h"

#define SRB2_INPUT_TICCMD
#include "srb2-input.h"

static const struct srb2_button g_buttons[] = {
	{ "Forward", GC_FORWARD, 'w' },
	{ "Backward", GC_BACKWARD, 's' },
	{ "Strafe Left", GC_STRAFELEFT, 'a' },
	{ "Strafe Right", GC_STRAFERIGHT, 'd' },
	{ "Turn left", GC_TURNLEFT, KEY_LEFTARROW },
	{ "Turn right", GC_TURNRIGHT, KEY_RIGHTARROW },
	{ "Look Up", GC_LOOKUP, KEY_UPARROW },
	{ "Look Down", GC_LOOKDOWN, KEY_DOWNARROW },
	{ "Jump", GC_JUMP, KEY_SPACE },
	{ "Spin", GC_SPIN, KEY_LSHIFT },
	/* the ring-slinger controls (match, CTF): left out for now, to keep the
	 * controller to what a single-player game reads (user-decided 2026-10-04)
	{ "Fire", GC_FIRE, KEY_RCTRL },
	{ "Fire Normal", GC_FIRENORMAL, KEY_RALT },
	{ "Toss Flag", GC_TOSSFLAG, '\'' },
	*/
	{ "Center View", GC_CENTERVIEW, KEY_LCTRL },
	{ "Reset Camera", GC_CAMRESET, 'r' },
	{ "Camera Toggle", GC_CAMTOGGLE, 'v' },
	/* the ring-slinger weapons, left out with them
	{ "Weapon Next", GC_WEAPONNEXT, KEY_MOUSEWHEELUP },
	{ "Weapon Prev", GC_WEAPONPREV, KEY_MOUSEWHEELDOWN },
	{ "Weapon 1", GC_WEPSLOT1, '1' },
	{ "Weapon 2", GC_WEPSLOT2, '2' },
	{ "Weapon 3", GC_WEPSLOT3, '3' },
	{ "Weapon 4", GC_WEPSLOT4, '4' },
	{ "Weapon 5", GC_WEPSLOT5, '5' },
	{ "Weapon 6", GC_WEPSLOT6, '6' },
	{ "Weapon 7", GC_WEPSLOT7, '7' },
	*/
	{ "Custom 1", GC_CUSTOM1, 'z' },
	{ "Custom 2", GC_CUSTOM2, 'x' },
	{ "Custom 3", GC_CUSTOM3, 'c' },
	{ "Pause", GC_PAUSE, 'p' },
	/* the menus' own keys, a prompt's too (M_Responder: Enter or Space is yes,
	 * escape no) */
	{ "Enter", GC_NULL, KEY_ENTER },
	{ "escape", GC_NULL, KEY_ESCAPE },
};
#define NBUTTONS ((int)(sizeof g_buttons / sizeof g_buttons[0]))

static const struct srb2_axis g_axes[] = {
	{ "Forward Move", -MAXPLMOVE, MAXPLMOVE },
	{ "Side Move", -MAXPLMOVE, MAXPLMOVE },
	{ "Turn", -32768, 32767 },
	{ "Aim", -32768, 32767 },
};
#define NAXES ((int)(sizeof g_axes / sizeof g_axes[0]))

/* the controller's state: what the frontend set for the next step, and what
 * the engine was last told (machine memory: a state restores both) */
static uint8_t g_held[NBUTTONS], g_down[NBUTTONS];
static int32_t g_axis[NAXES];

int srb2_input_button_count(void) { return NBUTTONS; }
const struct srb2_button *srb2_input_button(int i) { return i >= 0 && i < NBUTTONS ? &g_buttons[i] : NULL; }
int srb2_input_axis_count(void) { return NAXES; }
const struct srb2_axis *srb2_input_axis(int i) { return i >= 0 && i < NAXES ? &g_axes[i] : NULL; }

void srb2_input_set_button(int i, int held)
{
	if (i >= 0 && i < NBUTTONS)
		g_held[i] = held != 0;
}

void srb2_input_set_axis(int i, int32_t value)
{
	if (i < 0 || i >= NAXES)
		return;
	if (value < g_axes[i].min)
		value = g_axes[i].min;
	if (value > g_axes[i].max)
		value = g_axes[i].max;
	g_axis[i] = value;
}

/* the default keyboard scheme, whatever the configuration says */
void srb2_input_bind(void)
{
	G_CopyControls(gamecontrol, gamecontroldefault[gcs_fps], NULL, 0);
	G_CopyControls(gamecontrolbis, gamecontrolbisdefault[gcs_fps], NULL, 0);
	for (int i = 0; i < NBUTTONS; i++)
		if (g_buttons[i].control != GC_NULL)
		{
			gamecontrol[g_buttons[i].control][0] = g_buttons[i].key;
			gamecontrol[g_buttons[i].control][1] = KEY_NULL;
		}
}

/* a step's buttons as key events: releases first, then presses, each in the
 * controller's order */
void srb2_input_post(void)
{
	event_t ev;
	memset(&ev, 0, sizeof ev);
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < NBUTTONS; i++)
		{
			const int release = pass == 0;
			if (release ? (g_down[i] && !g_held[i]) : (!g_down[i] && g_held[i]))
			{
				ev.type = release ? ev_keyup : ev_keydown;
				ev.key = g_buttons[i].key;
				D_PostEvent(&ev);
				g_down[i] = g_held[i];
			}
		}
}

static int held(int control)
{
	for (int i = 0; i < NBUTTONS; i++)
		if (g_buttons[i].control == control)
			return g_held[i];
	return 0;
}

/* the axes, as the command G_BuildTiccmd starts from */
void srb2_input_base(ticcmd_t *cmd)
{
	memset(cmd, 0, sizeof *cmd);
	if (!held(GC_FORWARD) && !held(GC_BACKWARD))
		cmd->forwardmove = (SINT8)g_axis[0];
	if (!held(GC_STRAFELEFT) && !held(GC_STRAFERIGHT))
		cmd->sidemove = (SINT8)g_axis[1];
	/* the command's angle is counter-clockwise, its pitch up: negated, with
	 * -32768 kept in range */
	cmd->angleturn = (INT16)(g_axis[2] == -32768 ? 32767 : -g_axis[2]);
	cmd->aiming = (INT16)(g_axis[3] == -32768 ? 32767 : -g_axis[3]);
}
