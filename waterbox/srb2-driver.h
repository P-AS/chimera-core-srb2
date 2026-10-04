/* srb2-driver.h - the machine (srb2-driver.c), for the exports and harnesses */
#ifndef SRB2_DRIVER_H
#define SRB2_DRIVER_H
#ifdef __cplusplus
extern "C" {
#endif
/* the engine's start: upstream's main before its loop; nonzero if it halted */
int srb2_start(int argc, char **argv);
/* one pass of the engine's loop */
void srb2_frame(void);
/* whether the engine has exited (I_Error, a quit), and I_Error's message */
int srb2_halted(void);
const char *srb2_error(void);
#ifdef __cplusplus
}
#endif
#endif
