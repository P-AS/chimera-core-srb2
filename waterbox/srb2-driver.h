/* srb2-driver.h - the machine (srb2-driver.c), for the exports and harnesses */
#ifndef SRB2_DRIVER_H
#define SRB2_DRIVER_H
#ifdef __cplusplus
extern "C" {
#endif
/* the unlocks the project starts with (set before srb2_start): Record Attack,
 * NiGHTS Mode and Marathon Run; every character; every unlockable */
void srb2_set_unlocks(int modes, int skins, int all);
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
#ifdef __cplusplus
}
#endif
#endif
