/* srb2-driver.h - the machine (srb2-driver.c), for the exports and harnesses */
#ifndef SRB2_DRIVER_H
#define SRB2_DRIVER_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* the unlocks the project starts with (set before srb2_start): Record Attack,
 * NiGHTS Mode and Marathon Run; every character; every unlockable */
void srb2_set_unlocks(int modes, int skins, int all, int maps);
/* the ghosts (srb2-ghosts.c), set before srb2_start: shown in normal play;
 * every level recorded as one. And the buffer the export's copy of the level
 * being recorded is made in (invisible memory: output, not the machine) */
void srb2_set_ghosts(int normal_play, int record);
void srb2_set_ghost_pending_buffer(unsigned char *buf, size_t size);
/* after every step: a level's recording ends with the level */
void srb2_ghosts_step(void);
/* the engine's start: upstream's main before its loop; nonzero if it halted */
int srb2_start(int argc, char **argv);
/* a step: the machine's clock a tic further, the engine run until it next
 * waits for time (the end of a pass of its loop, or a sleep inside a tic) */
void srb2_frame(void);
/* whether the engine has exited (I_Error, a quit), and I_Error's message */
int srb2_halted(void);
const char *srb2_error(void);
/* whether the last step built a tic command: the game read its input */
int srb2_input_was_read(void);
/* the engine's tic counter */
unsigned srb2_gametic(void);
/* the memory domains (one: Game State, the step's copy of what a TASer
 * watches) and the property table that names what is in it */
int srb2_domain_count(void);
const char *srb2_domain_name(int i);
unsigned char *srb2_domain_ptr(int i);
long long srb2_domain_size(int i);
const char *srb2_game_properties(void);
#ifdef __cplusplus
}
#endif
#endif
