/* i_sound.c - SRB2's sound and music without an audio device: a mixer of the
 * core's own, in the machine's memory, that renders one step's samples after
 * the step (chimera_audio_mix) - so the sound is the same in every build and
 * on every host, and what the game asks of it (a song's position, whether a
 * sound still plays, a fade's end) is the machine's.
 *
 * It does what upstream's SDL_mixer backend (sdl/mixer_sound.c) does, as
 * nearly as that can be said of a mixer:
 *
 *   - sound effects: DMX (ds2chunk, ported), GME's formats (rendered for their
 *     length), WAV (PCM) and Ogg Vorbis (libvorbis), converted at load to 44.1 kHz stereo; volume and panning a
 *     channel as Mix_Volume and Mix_SetPanning give them; pitch ignored, as
 *     SDL_mixer ignores it;
 *   - music: GME's formats (VGM/VGZ, NSF, SPC, GBS, HES, KSS, AY, SAP, GYM),
 *     tracker modules (libopenmpt: MOD, S3M, XM, IT, MPTM...), Ogg Vorbis and
 *     WAV, tried in upstream's order, with the loop point of the song's
 *     LOOPPOINT= (samples, read as upstream reads it) or LOOPMS= tag, or the
 *     one the game sets; fades stepped every 10 ms of output, as upstream's
 *     SDL timer steps them, with the callback run from I_UpdateSound; the
 *     length and position the decoder gives (as SDL_mixer_X, upstream's
 *     Windows builds, gives them). MIDI is not played: the game prefers the
 *     digital songs, which the data has for every one.
 *
 * Every output frame is 44.1 kHz stereo signed 16-bit; a step (a tic) is
 * 1260 of them. */
#include <stdlib.h>
#include <string.h>

#define OV_EXCLUDE_STATIC_CALLBACKS
#include <vorbis/vorbisfile.h>
#include <gme/gme.h>
#include <zlib.h>

#include "doomdef.h"
#include "doomtype.h"
#include "i_sound.h"
#include "s_sound.h" /* with HAVE_OPENMPT: openmpt_mhandle, cv_modfilter */
#include "sounds.h"
#include "w_wad.h"
#include "z_zone.h"
#include "m_fixed.h"
#include "byteptr.h"
#include "console.h"

#include "chimera-platform.h"

#define RATE 44100
#define CHANNELS 256
/* SDL_mixer's fade timer: 10 ms */
#define FADE_TICK (RATE / 100)

UINT8 sound_started = false;

/* ------------------------------------------------------------ decoding */

/* a sound, ready to mix: 44.1 kHz stereo */
struct pcm
{
	UINT32 frames;
	INT16 *data; /* frames * 2 */
};

/* linear resampling to 44.1 kHz stereo, in 16.16 fixed point */
static int to_pcm(struct pcm *out, const INT16 *in, UINT32 frames, int channels, UINT32 rate)
{
	if (!frames || !rate || channels < 1)
		return 0;
	const UINT64 outframes = rate == RATE ? frames : ((UINT64)frames * RATE + rate - 1) / rate;
	out->data = malloc((size_t)outframes * 4);
	if (!out->data)
		return 0;
	out->frames = (UINT32)outframes;
	const UINT64 step = ((UINT64)rate << 16) / RATE;
	for (UINT64 i = 0; i < outframes; i++)
	{
		const UINT64 pos = rate == RATE ? (i << 16) : i * step;
		const UINT32 a = (UINT32)(pos >> 16), b = a + 1 < frames ? a + 1 : a;
		const INT32 f = (INT32)(pos & 0xFFFF);
		for (int c = 0; c < 2; c++)
		{
			const int ch = channels == 1 ? 0 : c;
			const INT32 sa = in[(size_t)a * channels + ch], sb = in[(size_t)b * channels + ch];
			out->data[i * 2 + c] = (INT16)(sa + (((sb - sa) * f) >> 16));
		}
	}
	return 1;
}

/* upstream's ds2chunk: a Doom sound, 8-bit at its rate, to 44.1 kHz stereo */
static int dmx_pcm(struct pcm *out, const UINT8 *lump, size_t len)
{
	if (len < 8)
		return 0;
	const UINT16 ver = (UINT16)(lump[0] | lump[1] << 8);
	const UINT16 freq = (UINT16)(lump[2] | lump[3] << 8);
	UINT32 samples = (UINT32)lump[4] | (UINT32)lump[5] << 8 | (UINT32)lump[6] << 16 | (UINT32)lump[7] << 24;
	UINT32 newsamples;
	if (ver != 3)
		return 0;
	if (samples > len - 8)
		samples = (UINT32)(len - 8);
	switch (freq)
	{
		case 44100: newsamples = samples; break;
		case 22050: newsamples = samples << 1; break;
		case 11025: newsamples = samples << 2; break;
		default:
		{
			const fixed_t frac = (44100 << FRACBITS) / (UINT32)(freq ? freq : 1);
			if (!(frac & 0xFFFF))
				newsamples = samples * (frac >> FRACBITS);
			else
				newsamples = FixedMul(FixedDiv(samples, freq), 44100) + 1;
		}
	}
	if (newsamples >= UINT32_MAX >> 2)
		return 0;
	INT16 *d = malloc((size_t)newsamples * 4 + 8);
	if (!d)
		return 0;
	out->data = d;
	const SINT8 *s = (const SINT8 *)(lump + 8);
	UINT32 i = 0;
	INT16 o;
	switch (freq)
	{
		case 44100:
		case 22050:
		case 11025:
		{
			const int rep = freq == 44100 ? 1 : freq == 22050 ? 2 : 4;
			while (i++ < samples)
			{
				o = (INT16)(((INT16)(*s++) + 0x80) << 8);
				for (int r = 0; r < rep; r++)
				{
					*d++ = o;
					*d++ = o;
				}
			}
			break;
		}
		default:
		{
			fixed_t step = 0;
			const fixed_t frac = ((UINT32)freq << FRACBITS) / 44100 + 1;
			while (i < samples && (UINT32)(d - out->data) / 2 < newsamples)
			{
				o = (INT16)((INT16)(*s + 0x80) << 8);
				while (step < FRACUNIT && (UINT32)(d - out->data) / 2 < newsamples)
				{
					*d++ = o;
					*d++ = o;
					step += frac;
				}
				do
				{
					i++;
					s++;
					step -= FRACUNIT;
				} while (step >= FRACUNIT);
			}
		}
	}
	out->frames = (UINT32)(d - out->data) / 2;
	return 1;
}

/* a RIFF WAVE of PCM, 8- or 16-bit, mono or stereo */
struct wave
{
	const UINT8 *data;
	UINT32 bytes, rate;
	int channels, bits;
};

static int wave_parse(struct wave *w, const UINT8 *p, size_t len)
{
	if (len < 12 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4))
		return 0;
	int fmt = 0;
	memset(w, 0, sizeof *w);
	for (size_t at = 12; at + 8 <= len;)
	{
		const UINT32 size = (UINT32)p[at + 4] | (UINT32)p[at + 5] << 8 | (UINT32)p[at + 6] << 16 | (UINT32)p[at + 7] << 24;
		const UINT8 *body = p + at + 8;
		const size_t avail = len - at - 8;
		if (!memcmp(p + at, "fmt ", 4) && size >= 16 && avail >= 16)
		{
			if ((body[0] | body[1] << 8) != 1)
				return 0; /* not PCM */
			w->channels = body[2] | body[3] << 8;
			w->rate = (UINT32)body[4] | (UINT32)body[5] << 8 | (UINT32)body[6] << 16 | (UINT32)body[7] << 24;
			w->bits = body[14] | body[15] << 8;
			fmt = 1;
		}
		else if (!memcmp(p + at, "data", 4) && fmt)
		{
			w->data = body;
			w->bytes = (UINT32)(size < avail ? size : avail);
			return (w->bits == 8 || w->bits == 16) && (w->channels == 1 || w->channels == 2) && w->rate;
		}
		at += 8 + (size_t)size + (size & 1);
	}
	return 0;
}

/* a wave's frames as signed 16-bit, interleaved */
static INT16 *wave_s16(const struct wave *w, UINT32 *frames)
{
	const int bps = w->bits / 8;
	*frames = w->bytes / (UINT32)(bps * w->channels);
	INT16 *s = malloc((size_t)*frames * w->channels * 2 + 2);
	if (!s)
		return NULL;
	for (UINT32 i = 0; i < *frames * (UINT32)w->channels; i++)
		s[i] = bps == 1 ? (INT16)((w->data[i] - 0x80) << 8) : (INT16)(w->data[2 * i] | w->data[2 * i + 1] << 8);
	return s;
}

/* Ogg Vorbis from memory */
struct membuf
{
	const UINT8 *data;
	size_t size, pos;
};

static size_t mem_read(void *ptr, size_t size, size_t nmemb, void *src)
{
	struct membuf *m = src;
	size_t n = size * nmemb;
	if (n > m->size - m->pos)
		n = m->size - m->pos;
	memcpy(ptr, m->data + m->pos, n);
	m->pos += n;
	return size ? n / size : 0;
}

static int mem_seek(void *src, ogg_int64_t offset, int whence)
{
	struct membuf *m = src;
	const ogg_int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (ogg_int64_t)m->pos : (ogg_int64_t)m->size;
	if (base + offset < 0 || base + offset > (ogg_int64_t)m->size)
		return -1;
	m->pos = (size_t)(base + offset);
	return 0;
}

static long mem_tell(void *src) { return (long)((struct membuf *)src)->pos; }

static const ov_callbacks mem_callbacks = { mem_read, mem_seek, NULL, mem_tell };

static int ogg_pcm(struct pcm *out, const UINT8 *data, size_t len)
{
	struct membuf m = { data, len, 0 };
	OggVorbis_File vf;
	if (len < 4 || memcmp(data, "OggS", 4) || ov_open_callbacks(&m, &vf, NULL, 0, mem_callbacks) != 0)
		return 0;
	const vorbis_info *vi = ov_info(&vf, -1);
	const int channels = vi->channels;
	const UINT32 rate = (UINT32)vi->rate;
	size_t cap = 1 << 16, used = 0;
	char *buf = malloc(cap);
	int ok = buf && (channels == 1 || channels == 2);
	while (ok)
	{
		if (cap - used < 8192)
		{
			char *grown = realloc(buf, cap * 2);
			if (!grown)
			{
				ok = 0;
				break;
			}
			buf = grown;
			cap *= 2;
		}
		int section;
		const long n = ov_read(&vf, buf + used, (int)(cap - used), 0, 2, 1, &section);
		if (n <= 0)
			break;
		used += (size_t)n;
	}
	ov_clear(&vf);
	if (ok)
		ok = to_pcm(out, (const INT16 *)buf, (UINT32)(used / (2 * (size_t)channels)), channels, rate);
	free(buf);
	return ok;
}

/* a VGZ: upstream inflates it itself (its last 4 bytes are the inflated
 * size, typically) and gives GME the result */
static UINT8 *inflate_vgz(const UINT8 *data, size_t len, size_t *outlen)
{
	if (len < 8 || data[0] != 0x1F || data[1] != 0x8B)
		return NULL;
	const size_t inflated = (size_t)data[len - 4] | (size_t)data[len - 3] << 8 | (size_t)data[len - 2] << 16
		| (size_t)data[len - 1] << 24;
	UINT8 *out = calloc(inflated ? inflated : 1, 1);
	z_stream stream;
	memset(&stream, 0, sizeof stream);
	stream.avail_in = (uInt)len;
	stream.next_in = (Bytef *)data;
	stream.avail_out = (uInt)inflated;
	stream.next_out = out;
	int ok = out && inflateInit2(&stream, 32 + MAX_WBITS) == Z_OK;
	if (ok)
	{
		ok = inflate(&stream, Z_FINISH) == Z_STREAM_END;
		if (!ok)
			CONS_Alert(CONS_ERROR, "Encountered an error when running inflate: %s\n", stream.msg ? stream.msg : "");
		inflateEnd(&stream);
	}
	if (!ok)
	{
		free(out);
		return NULL;
	}
	*outlen = inflated;
	return out;
}

/* GME's equalizer, as upstream sets it (sdl/mixer_sound.c's values) */
#define GME_TREBLE 5.0f
#define GME_BASS 1.0f
static void gme_eq(Music_Emu *emu)
{
	gme_equalizer_t eq = { GME_TREBLE, GME_BASS, 0, 0, 0, 0, 0, 0, 0, 0 };
	gme_set_equalizer(emu, &eq);
}

/* a GME sound effect: its first track for its play length, as upstream renders it */
static int gme_pcm(struct pcm *out, const UINT8 *data, size_t len)
{
	size_t vlen = 0;
	UINT8 *vgz = inflate_vgz(data, len, &vlen);
	Music_Emu *emu;
	const int opened = vgz ? !gme_open_data(vgz, (long)vlen, &emu, RATE) : !gme_open_data(data, (long)len, &emu, RATE);
	free(vgz);
	if (!opened)
		return 0;
	gme_info_t *info;
	gme_start_track(emu, 0);
	gme_eq(emu);
	gme_track_info(emu, &info, 0);
	const UINT32 bytes = (UINT32)((info->play_length * 441 / 10) << 2);
	gme_free_info(info);
	out->frames = bytes / 4;
	out->data = malloc((size_t)bytes + 4);
	if (out->data)
		gme_play(emu, (int)(out->frames * 2), out->data);
	gme_delete(emu);
	return out->data != NULL;
}

/* ------------------------------------------------------------ sound effects */

struct channel
{
	const struct pcm *pcm;
	UINT32 pos;
	int volume;      /* 0..128, Mix_Volume's */
	int left, right; /* 0..255, Mix_SetPanning's */
};

static struct channel g_chan[CHANNELS];
static UINT8 sfx_volume;

void *I_GetSfx(sfxinfo_t *sfx)
{
	if (sfx->lumpnum == LUMPERROR)
		sfx->lumpnum = S_GetSfxLumpNum(sfx);
	sfx->length = W_LumpLength(sfx->lumpnum);
	UINT8 *lump = W_CacheLumpNum(sfx->lumpnum, PU_SOUND);
	struct pcm *pcm = calloc(1, sizeof *pcm);
	int ok = 0;
	if (pcm)
	{
		struct wave w;
		ok = dmx_pcm(pcm, lump, sfx->length) || gme_pcm(pcm, lump, sfx->length) || ogg_pcm(pcm, lump, sfx->length);
		if (!ok && wave_parse(&w, lump, sfx->length))
		{
			UINT32 frames;
			INT16 *s = wave_s16(&w, &frames);
			ok = s && to_pcm(pcm, s, frames, w.channels, w.rate);
			free(s);
		}
	}
	Z_Free(lump);
	if (!ok)
	{
		free(pcm);
		return NULL;
	}
	return pcm;
}

void I_FreeSfx(sfxinfo_t *sfx)
{
	struct pcm *pcm = sfx->data;
	if (pcm)
	{
		for (int i = 0; i < CHANNELS; i++)
			if (g_chan[i].pcm == pcm)
				g_chan[i].pcm = NULL;
		free(pcm->data);
		free(pcm);
	}
	sfx->data = NULL;
	sfx->lumpnum = LUMPERROR;
}

static void set_params(struct channel *c, UINT8 vol, UINT8 sep)
{
	c->volume = (((UINT16)vol + 1) * (UINT16)sfx_volume) / 62; /* (256 * 31) / 62 == 127 */
	c->left = min((UINT16)(0xff - sep) << 1, 0xff);
	c->right = min((UINT16)sep << 1, 0xff);
}

INT32 I_StartSound(sfxenum_t id, UINT8 vol, UINT8 sep, UINT8 pitch, UINT8 priority, INT32 channel)
{
	(void)pitch; /* as SDL_mixer: no pitch */
	(void)priority;
	if (channel < 0 || channel >= CHANNELS)
	{
		for (channel = 0; channel < CHANNELS && g_chan[channel].pcm; channel++) {}
		if (channel == CHANNELS)
			return -1;
	}
	struct channel *c = &g_chan[channel];
	c->pcm = S_sfx[id].data;
	c->pos = 0;
	set_params(c, vol, sep);
	return c->pcm ? channel : -1;
}

void I_StopSound(INT32 handle)
{
	if (handle >= 0 && handle < CHANNELS)
		g_chan[handle].pcm = NULL;
}

boolean I_SoundIsPlaying(INT32 handle)
{
	return handle >= 0 && handle < CHANNELS && g_chan[handle].pcm != NULL;
}

void I_UpdateSoundParams(INT32 handle, UINT8 vol, UINT8 sep, UINT8 pitch)
{
	(void)pitch;
	if (handle >= 0 && handle < CHANNELS)
		set_params(&g_chan[handle], vol, sep);
}

void I_SetSfxVolume(UINT8 volume) { sfx_volume = volume; }

/* ------------------------------------------------------------ music */

/* a song: a decoder behind a few calls, so the formats beside Vorbis (WAV
 * here; GME and modules later) are one more source each */
struct song
{
	musictype_t type;
	UINT32 rate;
	int channels;
	/* frames decoded into buf as signed 16-bit interleaved, 0 at the end */
	long (*read)(struct song *s, INT16 *buf, long frames);
	int (*seek)(struct song *s, UINT64 frame);
	UINT64 (*tell)(struct song *s);
	UINT64 (*length)(struct song *s); /* frames, 0 unknown */
	void (*close)(struct song *s);
	/* Vorbis */
	struct membuf mem;
	OggVorbis_File vf;
	/* GME */
	Music_Emu *gme;
	int track;
	/* WAV */
	INT16 *pcm;
	UINT64 frames, pos;
	/* the resampler (to 44.1 kHz): the last two frames and the fraction */
	INT16 prev[2], next[2];
	UINT32 frac;
	int primed;
};

static long vorbis_read(struct song *s, INT16 *buf, long frames)
{
	long got = 0;
	while (got < frames)
	{
		int section;
		const long n = ov_read(&s->vf, (char *)(buf + got * s->channels), (int)((frames - got) * 2 * s->channels), 0, 2,
			1, &section);
		if (n <= 0)
			break;
		got += n / (2 * s->channels);
	}
	return got;
}
static int vorbis_seek(struct song *s, UINT64 frame) { return ov_pcm_seek(&s->vf, (ogg_int64_t)frame) == 0; }
static UINT64 vorbis_tell(struct song *s)
{
	const ogg_int64_t t = ov_pcm_tell(&s->vf);
	return t > 0 ? (UINT64)t : 0;
}
static UINT64 vorbis_length(struct song *s)
{
	const ogg_int64_t t = ov_pcm_total(&s->vf, -1);
	return t > 0 ? (UINT64)t : 0;
}
static void vorbis_close(struct song *s) { ov_clear(&s->vf); }

static long wave_read(struct song *s, INT16 *buf, long frames)
{
	if ((UINT64)frames > s->frames - s->pos)
		frames = (long)(s->frames - s->pos);
	memcpy(buf, s->pcm + s->pos * s->channels, (size_t)frames * s->channels * 2);
	s->pos += (UINT64)frames;
	return frames;
}
static int wave_seek(struct song *s, UINT64 frame)
{
	s->pos = frame < s->frames ? frame : s->frames;
	return 1;
}
static UINT64 wave_tell(struct song *s) { return s->pos; }
static UINT64 wave_length(struct song *s) { return s->frames; }
static void wave_close(struct song *s) { free(s->pcm); }

static struct song *g_song;
/* a song loaded and started: upstream keeps a loaded one stopped until
 * I_PlaySong (and I_SongPlaying says a loaded one is playing) */
static boolean g_playing;
static UINT8 *g_song_data; /* the lump, which the decoder reads from */
static UINT8 music_volume, internal_volume;
static float loop_point; /* seconds, as upstream keeps it */
static boolean songpaused, is_looping;

static boolean is_fading;
static UINT8 fading_source, fading_target;
static INT32 fading_timer, fading_duration;
static void (*fading_callback)(void);
static boolean fading_do_callback, fading_nocleanup;
static UINT32 fade_frames; /* output frames toward the next 10 ms */

static void var_cleanup(void)
{
	loop_point = 0.0f;
	fading_source = fading_target = 0;
	fading_timer = fading_duration = 0;
	songpaused = is_looping = is_fading = false;
	if (!fading_nocleanup)
	{
		fading_callback = NULL;
		fading_do_callback = false;
	}
	else
		fading_nocleanup = false;
	internal_volume = 100;
	fade_frames = 0;
}

void I_StartupSound(void)
{
	if (sound_started)
		return;
	fading_nocleanup = false;
	var_cleanup();
	music_volume = sfx_volume = 0;
	sound_started = true;
	songpaused = false;
}

void I_ShutdownSound(void) {}

void I_UpdateSound(void)
{
	if (fading_do_callback)
	{
		if (fading_callback)
			(*fading_callback)();
		fading_callback = NULL;
		fading_do_callback = false;
	}
}

void I_InitMusic(void) {}
void I_ShutdownMusic(void) { I_UnloadSong(); }

musictype_t I_SongType(void) { return g_song ? g_song->type : MU_NONE; }
boolean I_SongPlaying(void) { return g_song != NULL; }
boolean I_SongPaused(void) { return songpaused; }

/* SDL_mixer has no tempo for a stream: neither has this */
/* a stream has no tempo in SDL_mixer; GME's has (upstream's) */
boolean I_SetSongSpeed(float speed)
{
	if (speed > 250.0f)
		speed = 250.0f;
	if (g_song && g_song->type == MU_GME)
	{
		gme_set_tempo(g_song->gme, speed);
		return true;
	}
	if (g_song && g_song->type == MU_MOD_EX)
	{
		if (speed > 4.0f)
			speed = 4.0f; /* upstream's limit */
		openmpt_module_ctl_set_floatingpoint(openmpt_mhandle, "play.tempo_factor", (double)speed);
		return true;
	}
	return false;
}

/* GME's length: intro + one loop, as upstream reconstructs it */
static INT32 gme_length(void)
{
	gme_info_t *info;
	INT32 length;
	if (gme_track_info(g_song->gme, &info, g_song->track))
		return 0;
	length = info->length;
	if (length <= 0)
	{
		length = info->intro_length + info->loop_length;
		if (length <= 0)
			length = 150 * 1000;
	}
	gme_free_info(info);
	return max(length, 0);
}

UINT32 I_GetSongLength(void)
{
	if (!g_song)
		return 0;
	if (g_song->type == MU_GME)
		return (UINT32)gme_length();
	if (g_song->type == MU_MOD_EX)
		return (UINT32)(openmpt_module_get_duration_seconds(openmpt_mhandle) * 1000.);
	return (UINT32)(g_song->length(g_song) * 1000 / g_song->rate);
}

boolean I_SetSongLoopPoint(UINT32 looppoint)
{
	if (!g_song || g_song->type == MU_GME || g_song->type == MU_MOD_EX || !is_looping)
		return false;
	const UINT32 length = I_GetSongLength();
	if (length > 0)
		looppoint %= length;
	loop_point = max((float)(looppoint / 1000.0L), 0);
	return true;
}

UINT32 I_GetSongLoopPoint(void)
{
	if (g_song && g_song->type == MU_GME)
	{
		gme_info_t *info;
		INT32 looppoint = 0;
		if (!gme_track_info(g_song->gme, &info, g_song->track))
		{
			looppoint = info->intro_length > 0 ? info->intro_length : 0;
			gme_free_info(info);
		}
		return (UINT32)max(looppoint, 0);
	}
	if (g_song && g_song->type == MU_MOD_EX)
		return 0;
	return g_song ? (UINT32)(loop_point * 1000) : 0;
}

boolean I_SetSongPosition(UINT32 position)
{
	if (!g_song)
		return false;
	if (g_song->type == MU_GME)
		return true; /* upstream: "this is unstable, so fail silently" */
	const UINT32 length = I_GetSongLength();
	const UINT32 looppoint = I_GetSongLoopPoint();
	if (length && position >= length)
		position = length > looppoint ? position % (length - looppoint) : 0;
	if (g_song->type == MU_MOD_EX)
	{
		openmpt_module_set_position_seconds(openmpt_mhandle, (double)(position / 1000.0L));
		return true;
	}
	g_song->seek(g_song, (UINT64)position * g_song->rate / 1000);
	g_song->primed = 0;
	return true;
}

UINT32 I_GetSongPosition(void)
{
	if (g_song && g_song->type == MU_GME)
	{
		INT32 position = gme_tell(g_song->gme);
		gme_info_t *info;
		if (gme_track_info(g_song->gme, &info, g_song->track))
			return (UINT32)position;
		/* GME's counter keeps going past the loop */
		if (info->length > 0)
			position %= info->length;
		else if (info->intro_length + info->loop_length > 0)
			position = position >= (info->intro_length + info->loop_length) ? (position % info->loop_length) : position;
		else
			position %= 150 * 1000;
		gme_free_info(info);
		return (UINT32)max(position, 0);
	}
	if (g_song && g_song->type == MU_MOD_EX)
		return (UINT32)(openmpt_module_get_position_seconds(openmpt_mhandle) * 1000.);
	return g_song ? (UINT32)(g_song->tell(g_song) * 1000 / g_song->rate) : 0;
}

/* the loop point upstream reads from the file: LOOPPOINT= (samples at 44.1
 * kHz, with its own odd 44.1) or LOOPMS=, the first one in the data */
static float find_loop_point(const char *data, size_t len)
{
	static const char key1[] = "LOOP", key2[] = "POINT=", key3[] = "MS=";
	for (size_t i = 0; i + 4 < len; i++)
	{
		if (strncmp(data + i, key1, 4))
			continue;
		const char *p = data + i + 4;
		const size_t left = len - i - 4;
		if (left > 6 && !strncmp(p, key2, 6))
		{
			const float lp = (float)((44.1L + atoi(p + 6)) / 44100.0L);
			if (lp != 0.0f)
				return lp;
		}
		else if (left > 3 && !strncmp(p, key3, 3))
		{
			const float lp = (float)(atoi(p + 3) / 1000.0L);
			if (lp != 0.0f)
				return lp;
		}
	}
	return 0.0f;
}

boolean I_LoadSong(char *data, size_t len)
{
	if (g_song)
		I_UnloadSong();
	var_cleanup();

	struct song *s = calloc(1, sizeof *s);
	UINT8 *copy = malloc(len ? len : 1);
	if (!s || !copy)
	{
		free(s);
		free(copy);
		return false;
	}
	memcpy(copy, data, len);
	struct wave w;
	s->mem = (struct membuf){ copy, len, 0 };
	size_t vlen = 0;
	UINT8 *vgz = inflate_vgz(copy, len, &vlen);
	if (len >= 2 && copy[0] == 0x1F && copy[1] == 0x8B && !vgz)
	{
		/* a VGZ that does not inflate: upstream gives up on it */
		free(s);
		free(copy);
		return false;
	}
	if (vgz ? !gme_open_data(vgz, (long)vlen, &s->gme, RATE) : !gme_open_data(copy, (long)len, &s->gme, RATE))
	{
		s->type = MU_GME;
		s->rate = RATE;
		s->channels = 2;
		s->track = -1;
	}
	else if (openmpt_probe_file_header(OPENMPT_PROBE_FILE_HEADER_FLAGS_DEFAULT, copy,
		len > openmpt_probe_file_header_get_recommended_size() ? openmpt_probe_file_header_get_recommended_size() : len,
		len, NULL, NULL, NULL, NULL, NULL, NULL) == OPENMPT_PROBE_FILE_HEADER_RESULT_SUCCESS)
	{
		/* a module: upstream keeps its handle in s_sound.c's openmpt_mhandle */
		openmpt_mhandle = openmpt_module_create_from_memory2(copy, len, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
		if (!openmpt_mhandle)
		{
			CONS_Alert(CONS_ERROR, "openmpt_module_create_from_memory2: could not load the module\n");
			free(vgz);
			free(s);
			free(copy);
			return false;
		}
		s->type = MU_MOD_EX;
		s->rate = RATE;
		s->channels = 2;
		s->track = -1;
	}
	else if (len >= 4 && !memcmp(copy, "OggS", 4) && ov_open_callbacks(&s->mem, &s->vf, NULL, 0, mem_callbacks) == 0)
	{
		const vorbis_info *vi = ov_info(&s->vf, -1);
		s->type = MU_OGG;
		s->rate = (UINT32)vi->rate;
		s->channels = vi->channels;
		s->read = vorbis_read;
		s->seek = vorbis_seek;
		s->tell = vorbis_tell;
		s->length = vorbis_length;
		s->close = vorbis_close;
		if (s->channels != 1 && s->channels != 2)
		{
			ov_clear(&s->vf);
			s->type = MU_NONE;
		}
	}
	else if (wave_parse(&w, copy, len))
	{
		UINT32 frames;
		s->pcm = wave_s16(&w, &frames);
		s->frames = frames;
		s->type = s->pcm ? MU_WAV : MU_NONE;
		s->rate = w.rate;
		s->channels = w.channels;
		s->read = wave_read;
		s->seek = wave_seek;
		s->tell = wave_tell;
		s->length = wave_length;
		s->close = wave_close;
	}
	free(vgz);
	if (s->type == MU_NONE || !s->rate)
	{
		CONS_Alert(CONS_ERROR, "I_LoadSong: not a song the core plays (GME's formats, Ogg Vorbis or WAV)\n");
		free(s);
		free(copy);
		return false;
	}
	g_song = s;
	g_song_data = copy;
	loop_point = find_loop_point(data, len);
	return true;
}

void I_UnloadSong(void)
{
	I_StopSong();
	if (g_song)
	{
		if (g_song->type == MU_GME)
			gme_delete(g_song->gme);
		else if (g_song->type == MU_MOD_EX)
		{
			openmpt_module_destroy(openmpt_mhandle);
			openmpt_mhandle = NULL;
		}
		else
			g_song->close(g_song);
		free(g_song);
		g_song = NULL;
	}
	free(g_song_data);
	g_song_data = NULL;
}

boolean I_PlaySong(boolean looping)
{
	if (!g_song)
		return false;
	if (g_song->type == MU_GME)
	{
		if (looping)
			gme_set_autoload_playback_limit(g_song->gme, 0);
		gme_eq(g_song->gme);
		gme_start_track(g_song->gme, 0);
		g_song->track = 0;
		g_playing = true;
		return true;
	}
	if (g_song->type == MU_MOD_EX)
	{
		openmpt_module_select_subsong(openmpt_mhandle, 0);
		openmpt_module_set_render_param(openmpt_mhandle, OPENMPT_MODULE_RENDER_INTERPOLATIONFILTER_LENGTH, cv_modfilter.value);
		if (looping)
			openmpt_module_set_repeat_count(openmpt_mhandle, -1);
		g_song->track = 0;
		g_playing = true;
		return true;
	}
	g_song->seek(g_song, 0);
	g_song->primed = 0;
	is_looping = looping;
	g_playing = true;
	I_SetMusicVolume(music_volume);
	return true;
}

void I_StopSong(void)
{
	if (!fading_nocleanup)
		I_StopFadingSong();
	if (g_song && (g_song->type == MU_GME || g_song->type == MU_MOD_EX))
		g_song->track = -1;
	g_playing = false;
	var_cleanup();
}

void I_PauseSong(void) { songpaused = true; }
void I_ResumeSong(void) { songpaused = false; }

void I_SetMusicVolume(UINT8 volume)
{
	if (!I_SongPlaying())
		return;
	music_volume = volume;
}

boolean I_SetSongTrack(INT32 track)
{
	if (g_song && g_song->type == MU_MOD_EX)
	{
		if (g_song->track == track)
			return false;
		if (track >= 0 && track < openmpt_module_get_num_subsongs(openmpt_mhandle))
		{
			openmpt_module_select_subsong(openmpt_mhandle, track);
			g_song->track = track;
			return true;
		}
		return false;
	}
	if (!g_song || g_song->type != MU_GME || g_song->track == track)
		return false;
	if (track >= 0 && track < gme_track_count(g_song->gme) - 1)
	{
		gme_err_t e = gme_start_track(g_song->gme, track);
		if (e)
		{
			CONS_Alert(CONS_ERROR, "GME error: %s\n", e);
			return false;
		}
		g_song->track = track;
		return true;
	}
	return false;
}

void I_SetInternalMusicVolume(UINT8 volume) { internal_volume = volume; }

void I_StopFadingSong(void)
{
	is_fading = false;
	fading_source = fading_target = 0;
	fading_timer = fading_duration = 0;
}

boolean I_FadeSongFromVolume(UINT8 target_volume, UINT8 source_volume, UINT32 ms, void (*callback)(void))
{
	source_volume = min(source_volume, 100);
	const INT16 volume_delta = (INT16)(target_volume - source_volume);

	I_StopFadingSong();

	if (!ms && volume_delta)
	{
		I_SetInternalMusicVolume(target_volume);
		if (callback)
			(*callback)();
		return true;
	}
	else if (!volume_delta)
	{
		if (callback)
			(*callback)();
		return true;
	}

	/* to the nearest 10 ms, as upstream rounds it */
	ms = (ms - ((ms / 10) * 10) > (((ms / 10) * 10) + 10) - ms) ? (((ms / 10) * 10) + 10) : ((ms / 10) * 10);

	if (!ms)
		I_SetInternalMusicVolume(target_volume);
	else if (source_volume != target_volume)
	{
		is_fading = true;
		fading_timer = fading_duration = (INT32)ms;
		fading_source = source_volume;
		fading_target = target_volume;
		fading_callback = callback;
		fade_frames = 0;
		if (internal_volume != source_volume)
			I_SetInternalMusicVolume(source_volume);
	}
	return is_fading;
}

boolean I_FadeSong(UINT8 target_volume, UINT32 ms, void (*callback)(void))
{
	return I_FadeSongFromVolume(target_volume, internal_volume, ms, callback);
}

boolean I_FadeOutStopSong(UINT32 ms) { return I_FadeSongFromVolume(0, internal_volume, ms, &I_StopSong); }

boolean I_FadeInPlaySong(UINT32 ms, boolean looping)
{
	if (I_PlaySong(looping))
		return I_FadeSongFromVolume(100, 0, ms, NULL);
	return false;
}

/* upstream's music_fade, once every 10 ms of output */
static void fade_tick(void)
{
	if (!is_fading || internal_volume == fading_target || fading_duration == 0)
	{
		I_StopFadingSong();
		fading_do_callback = true;
	}
	else if (songpaused)
		return;
	else if ((fading_timer -= 10) <= 0)
	{
		internal_volume = fading_target;
		I_StopFadingSong();
		fading_do_callback = true;
	}
	else
	{
		const UINT8 delta = (UINT8)abs(fading_target - fading_source);
		const fixed_t factor = FixedDiv(fading_duration - fading_timer, fading_duration);
		if (fading_target < fading_source)
			internal_volume = (UINT8)max(min(internal_volume, fading_source - FixedMul(delta, factor)), fading_target);
		else if (fading_target > fading_source)
			internal_volume = (UINT8)min(max(internal_volume, fading_source + FixedMul(delta, factor)), fading_target);
	}
}

/* the song's end: loop to the loop point, or stop (upstream's music_loop) */
static int song_ended(void)
{
	if (is_looping)
	{
		g_song->seek(g_song, (UINT64)(loop_point * g_song->rate));
		return 1;
	}
	fading_nocleanup = true;
	I_StopSong();
	return 0;
}

/* one source frame, stereo */
static int song_frame(INT16 out[2])
{
	INT16 buf[2];
	long n = g_song->read(g_song, buf, 1);
	if (n == 0)
	{
		if (!song_ended())
			return 0;
		n = g_song->read(g_song, buf, 1);
		if (n == 0)
		{
			fading_nocleanup = true;
			I_StopSong();
			return 0;
		}
	}
	out[0] = buf[0];
	out[1] = g_song->channels == 2 ? buf[1] : buf[0];
	return 1;
}

/* the music's next output frame at 44.1 kHz: the source's own frames when it
 * is 44.1 kHz, else linear between two */
static int music_frame(INT32 *l, INT32 *r)
{
	if (g_song->rate == RATE)
	{
		INT16 f[2];
		if (!song_frame(f))
			return 0;
		*l = f[0];
		*r = f[1];
		return 1;
	}
	if (!g_song->primed)
	{
		if (!song_frame(g_song->prev) || !song_frame(g_song->next))
			return 0;
		g_song->frac = 0;
		g_song->primed = 1;
	}
	const INT32 f = (INT32)g_song->frac;
	*l = g_song->prev[0] + (((g_song->next[0] - g_song->prev[0]) * f) >> 16);
	*r = g_song->prev[1] + (((g_song->next[1] - g_song->prev[1]) * f) >> 16);
	g_song->frac += (UINT32)(((UINT64)g_song->rate << 16) / RATE);
	while (g_song->frac >= 0x10000)
	{
		g_song->frac -= 0x10000;
		memcpy(g_song->prev, g_song->next, sizeof g_song->prev);
		if (!song_frame(g_song->next))
			return 1;
	}
	return 1;
}

static INT16 clip(INT32 v) { return (INT16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

/* the step's raw music: frames of the song at 44.1 kHz, before volume;
 * returns how many it gave (fewer when a song stops) */
static INT16 g_raw[2 * RATE];
static int music_raw(int frames)
{
	if (!g_song || !g_playing || songpaused)
		return 0;
	if (g_song->type == MU_GME)
	{
		/* upstream's mix_gme: nothing once the track has ended */
		if (gme_track_ended(g_song->gme))
			return 0;
		gme_play(g_song->gme, frames * 2, g_raw);
		return frames;
	}
	if (g_song->type == MU_MOD_EX)
	{
		/* upstream's mix_openmpt: what the module renders (nothing past its end) */
		const size_t got = openmpt_module_read_interleaved_stereo(openmpt_mhandle, RATE, (size_t)frames, g_raw);
		memset(g_raw + got * 2, 0, (size_t)(frames - (int)got) * 4);
		return frames;
	}
	for (int i = 0; i < frames; i++)
	{
		INT32 l, r;
		if (!g_song || !g_playing || !music_frame(&l, &r))
			return i;
		g_raw[2 * i] = (INT16)l;
		g_raw[2 * i + 1] = (INT16)r;
	}
	return frames;
}

/* a raw music sample at the volume of the moment: get_real_volume for a
 * stream (Mix_VolumeMusic), upstream's own scale for GME and modules (which
 * SDL_mixer hooks past the music volume, limiting it to 18) */
static INT32 music_gain(INT32 sample, musictype_t type)
{
	if (type == MU_GME || type == MU_MOD_EX)
	{
		if (music_volume >= 18)
			music_volume = 18;
		return (INT32)(INT16)(sample * music_volume * internal_volume / 100 / 20);
	}
	const INT32 mv = (INT32)((UINT32)music_volume * 128 / 31) * (INT32)internal_volume / 100;
	return sample * mv / 128;
}

/* a step's output: the music, then every channel, summed and clipped */
void chimera_audio_mix(INT16 *out, int frames)
{
	const musictype_t type = g_song ? g_song->type : MU_NONE;
	const int music = music_raw(frames);
	for (int i = 0; i < frames; i++)
	{
		INT32 l = 0, r = 0;
		if (i < music)
		{
			l += music_gain(g_raw[2 * i], type);
			r += music_gain(g_raw[2 * i + 1], type);
		}
		for (int c = 0; c < CHANNELS; c++)
		{
			struct channel *ch = &g_chan[c];
			if (!ch->pcm)
				continue;
			if (ch->pos >= ch->pcm->frames)
			{
				ch->pcm = NULL;
				continue;
			}
			const INT16 *s = ch->pcm->data + (size_t)ch->pos * 2;
			l += s[0] * ch->volume / 128 * ch->left / 255;
			r += s[1] * ch->volume / 128 * ch->right / 255;
			ch->pos++;
		}
		out[2 * i] = clip(l);
		out[2 * i + 1] = clip(r);
		if (is_fading && ++fade_frames >= FADE_TICK)
		{
			fade_frames = 0;
			fade_tick();
		}
	}
}
