/* chimera-platform.h - between the platform layer (platform/) and the driver */
#ifndef CHIMERA_PLATFORM_H
#define CHIMERA_PLATFORM_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* the machine's clock (platform/i_system.c), in I_GetPrecisePrecision() units */
uint64_t chimera_clock_precise(void);
/* the engine's screen, through its palette, as BGRA (platform/i_video.c) */
int chimera_video_width(void);
int chimera_video_height(void);
void chimera_video_bgra(uint32_t *out);
/* the engine's exit - I_Error or a quit - which halts the machine (the driver);
 * it does not return. msg is I_Error's message, or NULL for a quit. */
void chimera_exit(int rc, const char *msg) __attribute__((noreturn));
/* the seed of the bytes I_GetRandomBytes answers (the driver's) */
extern uint64_t chimera_random_seed;
#ifdef __cplusplus
}
#endif
#endif
