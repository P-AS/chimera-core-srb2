/* chimera-platform.h - between the platform layer (platform/) and the driver */
#ifndef CHIMERA_PLATFORM_H
#define CHIMERA_PLATFORM_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* the machine's clock (platform/i_system.c), in I_GetPrecisePrecision() units */
uint64_t chimera_clock_precise(void);
/* a step of the machine: its clock moves one tic */
void chimera_clock_step(void);
/* nonzero: the host's clock instead (the native reference's diagnostic) */
extern int chimera_host_clock;
/* a step's sound: frames of 44.1 kHz stereo signed 16-bit (platform/i_sound.c) */
void chimera_audio_mix(int16_t *out, int frames);
/* the machine's one video mode, set before the start (platform/i_video.c) */
void chimera_video_set_mode(int w, int h);
/* the machine's renderer, set before the start: 0 software, 1 OpenGL
 * (platform/i_video.c, platform/ogl_chimera.c) */
void chimera_video_set_renderer(int opengl);
/* which one draws: 1 OpenGL, 0 software (OpenGL asked for and not had) */
int chimera_video_opengl(void);
/* the engine's screen, through its palette, as BGRA (platform/i_video.c) */
int chimera_video_width(void);
int chimera_video_height(void);
void chimera_video_bgra(uint32_t *out);
/* the engine's exit - I_Error or a quit - which halts the machine (the driver);
 * it does not return. msg is I_Error's message, or NULL for a quit. */
void chimera_exit(int rc, const char *msg) __attribute__((noreturn));
/* the engine waits for time inside a tic (I_Sleep): the step ends (the driver) */
void chimera_wait(void);
/* a tic command was built: the step read input (the driver) */
void chimera_input_read(void);
/* the machine's filesystem (platform/files.c): a folder made, and the save
 * data - every file the game wrote but its configuration */
void chimera_mkdir(const char *path);
int chimera_savedata_count(void);
const char *chimera_savedata_name(int index);
int64_t chimera_savedata_size(int index);
const uint8_t *chimera_savedata_buffer(int index);
/* the seed of the bytes I_GetRandomBytes answers (the driver's) */
extern uint64_t chimera_random_seed;
#ifdef __cplusplus
}
#endif
#endif
