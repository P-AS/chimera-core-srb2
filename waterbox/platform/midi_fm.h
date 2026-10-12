/* midi_fm.h - MIDI played on emulated YM2612s (midi_fm.cpp): the music
 * mixer's (i_sound.c) MIDI decoder. 44.1 kHz stereo signed 16-bit. */
#ifndef MIDI_FM_H
#define MIDI_FM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct midi_fm;

/* a MIDI file's magic, as SDL_mixer_X (upstream's Windows builds) knows one:
 * Standard MIDI (MThd), RIFF MIDI (RMID), DMX MUS and XMI */
int midi_fm_is_midi(const void *data, size_t len);

/* a player for the song, which reads from data until it is closed; NULL when
 * it is not a song the sequencer reads */
struct midi_fm *midi_fm_open(const void *data, size_t len);
void midi_fm_close(struct midi_fm *m);

/* from the start: looping at the song's loop points (its whole length when it
 * has none) forever, or playing it once */
void midi_fm_start(struct midi_fm *m, int looping);

/* frames of the song into out (2 per frame); fewer at its end */
long midi_fm_render(struct midi_fm *m, int16_t *out, long frames);

/* seconds */
double midi_fm_length(struct midi_fm *m);
double midi_fm_tell(struct midi_fm *m);
void midi_fm_seek(struct midi_fm *m, double seconds);

#ifdef __cplusplus
}
#endif

#endif
