/* srb2-input.h - the controller (srb2-input.c) */
#ifndef SRB2_INPUT_H
#define SRB2_INPUT_H
#include <stdint.h>

struct srb2_button
{
	const char *name;
	int control; /* the game control (gamecontrols_e), GC_NULL for a menu key */
	int key;     /* the key it presses */
};

struct srb2_axis
{
	const char *name;
	int32_t min, max;
};

int srb2_input_button_count(void);
const struct srb2_button *srb2_input_button(int i);
int srb2_input_axis_count(void);
const struct srb2_axis *srb2_input_axis(int i);

/* what the frontend holds for the next step */
void srb2_input_set_button(int i, int held);
void srb2_input_set_axis(int i, int32_t value);

/* the default keyboard scheme bound (after the engine's start) */
void srb2_input_bind(void);
/* the buttons that changed, as key events (before each step) */
void srb2_input_post(void);

#ifdef SRB2_INPUT_TICCMD
/* the axes, as the tic command G_BuildTiccmd starts from (I_BaseTiccmd) */
void srb2_input_base(ticcmd_t *cmd);
#endif
#endif
