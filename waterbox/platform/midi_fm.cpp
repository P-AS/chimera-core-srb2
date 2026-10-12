/* midi_fm.cpp - MIDI on emulated YM2612s: the music mixer's MIDI player
 * (i_sound.c's MU_MID_EX).
 *
 * Upstream's Windows builds play MIDI with SDL_mixer_X's OPNMIDI: libOPNMIDI
 * on MAME's YM2612, six chips, with its XG bank. libOPNMIDI's player is
 * GPL-3.0-or-later and SRB2 is GPL-2.0 only, so the player here is the core's
 * own (GPL-2.0, as SRB2), built on the parts of libOPNMIDI (extern/opnmidi)
 * whose terms allow it:
 *   - the MIDI sequencer (midiseq/, MIT): the file formats (Standard MIDI,
 *     RIFF MIDI, MUS, XMI...), the events in time, tempo, loop points,
 *     seeking, the song's length and position
 *   - the bank loader (wopn/, MIT) and its XG bank (fm_banks/xg.wopn, MIT),
 *     the one SDL_mixer_X embeds, embedded here too (build/opnbank/)
 *   - the frequency and volume models (models/, MIT)
 *   - the chip interface (chips/opn_chip_base, mame_opn2: LGPL-2.1+) and
 *     MAME's YM2612 (chips/mame: GPL-2.0+)
 * What is the core's own is the synthesizer between them: which chip channel
 * plays which note, and what it writes to the chip for the MIDI channel's
 * program, volume, pan, pitch bend, modulation and sustain. It is written from
 * the YM2612's registers and the MIDI specification, not from libOPNMIDI's
 * player, so the music sounds like upstream's MIDI - the same chip, bank and
 * instruments - but is not sample for sample the same.
 *
 * Six chips, 36 voices, as SDL_mixer_X runs OPNMIDI; each chip at its native
 * rate (53267 Hz), resampled to 44.1 kHz by the chip interface. Everything is
 * the machine's: no host time, no randomness, and the libm calls (MAME's
 * tables, the volume model's log, the vibrato's sine) are the core's
 * deterministic math (platform/detmath.c). */
#include <new>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "midiseq/midi_sequencer_impl.hpp"
#include "chips/mame_opn2.h"
#include "wopn/wopn_file.h"
#include "models/opn_models.h"

#include "midi_fm.h"
#include "opn_bank.h" /* g_xg_wopn: fm_banks/xg.wopn's bytes */

namespace
{

const int CHIPS = 6;
const int VOICES = CHIPS * 6;
const uint32_t RATE = 44100;
const uint32_t CLOCK = 7670454; /* the NTSC Mega Drive's */
const int DRUMS = 9;            /* MIDI channel 10 */
const int BLOCK = 64;           /* frames between the vibrato's steps */

/* the carriers - the operators heard, which the volume scales - of each
 * algorithm, a bit per operator in the bank's order, which is the registers'
 * (S1 at +0, S3 at +4, S2 at +8, S4 at +12) */
const uint8_t carriers[8] = { 0x8, 0x8, 0x8, 0x8, 0xC, 0xE, 0xE, 0xF };

/* the vibrato the modulation wheel gives: 5.5 Hz, up to half a semitone
 * either way at its top */
const uint32_t VIBRATO_STEP = (uint32_t)(5.5 * BLOCK / RATE * 4294967296.0);
const double VIBRATO_DEPTH = 0.5 / 127.0;

WOPNFile *g_bank;
double g_sine[256];

struct Voice
{
	int channel; /* the MIDI channel, -1 never used */
	int note;
	bool on;   /* keyed */
	bool held; /* released by the player, kept on by the sustain pedal */
	const WOPNInstrument *ins;
	double tone; /* the note's, before the bend and the vibrato */
	uint8_t velocity;
	uint32_t mul; /* the frequency model's multiplier offset written */
	uint64_t start, release;
};

struct Channel
{
	uint8_t program, bank_msb, bank_lsb, volume, expression, pan, modulation;
	bool sustain;
	int bend; /* -8192..8191 */
	uint8_t bend_semitones, bend_cents;
	uint8_t rpn_msb, rpn_lsb;
};

} // namespace

struct midi_fm
{
	BW_MidiSequencer seq;
	BW_MidiRtInterface rt;
	MameOPN2 *chip[CHIPS];
	Voice voice[VOICES];
	Channel ch[16];
	uint64_t now; /* frames rendered */
	uint32_t vibrato;
};

/* ------------------------------------------------------------ the chips */

/* a register of a voice's channel: voices 0-5 are the first chip's channels,
 * 1-3 in its first part, 4-6 in its second */
static void reg(midi_fm *m, int v, uint16_t r, uint8_t d)
{
	const int c = v % 6;
	m->chip[v / 6]->writeReg((uint32_t)(c / 3), (uint16_t)(r + c % 3), d);
}

static void key(midi_fm *m, int v, bool on)
{
	const int c = v % 6;
	m->chip[v / 6]->writeReg(0, 0x28, (uint8_t)((on ? 0xF0 : 0x00) | (c / 3) << 2 | c % 3));
}

static void chips_reset(midi_fm *m)
{
	for (int c = 0; c < CHIPS; c++)
	{
		MameOPN2 *chip = m->chip[c];
		chip->reset();
		chip->writeReg(0, 0x22, g_bank->lfo_freq); /* the LFO, as the bank has it */
		chip->writeReg(0, 0x27, 0x00);             /* channel 3 normal, no timers */
		chip->writeReg(0, 0x2B, 0x00);             /* no DAC */
	}
	for (int v = 0; v < VOICES; v++)
	{
		key(m, v, false);
		reg(m, v, 0xB4, 0xC0);
		m->voice[v].channel = -1;
		m->voice[v].on = m->voice[v].held = false;
	}
}

/* ------------------------------------------------------------ a voice */

static void voice_volume(midi_fm *m, int v)
{
	const Voice &vo = m->voice[v];
	const Channel &c = m->ch[vo.channel];
	OPNVolume_t vol;
	vol.vel = vo.velocity;
	vol.chVol = c.volume;
	vol.chExpr = c.expression;
	vol.masterVolume = 127;
	vol.algorithm = vo.ins->fbalg & 7;
	for (int k = 0; k < 4; k++)
	{
		vol.tlOp[k] = vo.ins->operators[k].level_40 & 127;
		vol.doOp[k] = carriers[vol.algorithm] >> k & 1;
	}
	opnModel_genericVolume(&vol);
	for (int k = 0; k < 4; k++)
		reg(m, v, (uint16_t)(0x40 + 4 * k), (uint8_t)vol.tlOp[k]);
}

static void voice_pitch(midi_fm *m, int v)
{
	Voice &vo = m->voice[v];
	const Channel &c = m->ch[vo.channel];
	double tone = vo.tone + c.bend * (c.bend_semitones + c.bend_cents / 100.0) / 8192.0;
	if (c.modulation)
		tone += g_sine[m->vibrato >> 24] * c.modulation * VIBRATO_DEPTH;
	uint32_t mul;
	const uint16_t f = opnModel_genericFreqOPN2(tone, &mul);
	if (mul != vo.mul)
	{
		/* a note above the block's range: the operators' multipliers raised */
		vo.mul = mul;
		for (int k = 0; k < 4; k++)
		{
			const uint8_t dtfm = vo.ins->operators[k].dtfm_30;
			uint32_t ml = (dtfm & 0x0F) + mul;
			reg(m, v, (uint16_t)(0x30 + 4 * k), (uint8_t)((dtfm & 0x70) | (ml > 15 ? 15 : ml)));
		}
	}
	reg(m, v, 0xA4, (uint8_t)(f >> 8)); /* block and the high bits first: they latch */
	reg(m, v, 0xA0, (uint8_t)(f & 0xFF));
}

static void voice_pan(midi_fm *m, int v)
{
	/* MAME's per-channel pan (the chip interface's extension): 0 left, 127 right */
	m->chip[v / 6]->writePan((uint16_t)(v % 6), m->ch[m->voice[v].channel].pan);
}

static void voice_program(midi_fm *m, int v)
{
	Voice &vo = m->voice[v];
	const WOPNInstrument *ins = vo.ins;
	for (int k = 0; k < 4; k++)
	{
		const WOPNOperator &op = ins->operators[k];
		reg(m, v, (uint16_t)(0x30 + 4 * k), op.dtfm_30);
		reg(m, v, (uint16_t)(0x50 + 4 * k), op.rsatk_50);
		reg(m, v, (uint16_t)(0x60 + 4 * k), op.amdecay1_60);
		reg(m, v, (uint16_t)(0x70 + 4 * k), op.decay2_70);
		reg(m, v, (uint16_t)(0x80 + 4 * k), op.susrel_80);
		reg(m, v, (uint16_t)(0x90 + 4 * k), op.ssgeg_90);
	}
	vo.mul = 0;
	reg(m, v, 0xB0, ins->fbalg);
	reg(m, v, 0xB4, (uint8_t)(0xC0 | ins->lfosens));
	voice_pan(m, v);
	voice_volume(m, v);
	voice_pitch(m, v);
}

static void voice_off(midi_fm *m, int v)
{
	Voice &vo = m->voice[v];
	vo.on = vo.held = false;
	vo.release = m->now;
	key(m, v, false);
}

/* every voice of a channel, sounding or releasing */
template <class F>
static void each_voice(midi_fm *m, int channel, F f)
{
	for (int v = 0; v < VOICES; v++)
		if (m->voice[v].channel == channel)
			f(v);
}

/* ------------------------------------------------------------ MIDI */

static void channels_reset(midi_fm *m)
{
	for (int i = 0; i < 16; i++)
	{
		Channel &c = m->ch[i];
		c.program = c.bank_msb = c.bank_lsb = 0;
		c.volume = 100;
		c.expression = 127;
		c.pan = 64;
		c.modulation = 0;
		c.sustain = false;
		c.bend = 0;
		c.bend_semitones = 2;
		c.bend_cents = 0;
		c.rpn_msb = c.rpn_lsb = 127;
	}
}

static const WOPNBank *find_bank(const WOPNBank *banks, int count, int msb, int lsb)
{
	for (int i = 0; i < count; i++)
		if (banks[i].bank_midi_msb == msb && banks[i].bank_midi_lsb == lsb)
			return &banks[i];
	return NULL;
}

/* the instrument a note plays: the channel's program in its bank (bank 0's
 * when that bank has none), or on the drum channel the note in the kit the
 * program selects (as GS and XG number their kits; kit 0's when it has none) */
static const WOPNInstrument *instrument(const Channel &c, int channel, int note)
{
	const WOPNInstrument *ins;
	if (channel == DRUMS)
	{
		const WOPNBank *kit = find_bank(g_bank->banks_percussive, g_bank->banks_count_percussion, 0, c.program);
		ins = kit ? &kit->ins[note] : NULL;
		if (!ins || (ins->inst_flags & WOPN_Ins_IsBlank))
			ins = &g_bank->banks_percussive[0].ins[note];
	}
	else
	{
		const WOPNBank *bank = find_bank(g_bank->banks_melodic, g_bank->banks_count_melodic, c.bank_msb, c.bank_lsb);
		ins = bank ? &bank->ins[c.program] : NULL;
		if (!ins || (ins->inst_flags & WOPN_Ins_IsBlank))
			ins = &g_bank->banks_melodic[0].ins[c.program];
	}
	return (ins->inst_flags & WOPN_Ins_IsBlank) ? NULL : ins;
}

/* the voice for a new note: the same note still sounding on the channel
 * (struck again), else one never used, else the one released longest ago,
 * else the one keyed longest ago */
static int voice_for(midi_fm *m, int channel, int note)
{
	int released = -1, keyed = -1;
	for (int v = 0; v < VOICES; v++)
	{
		const Voice &vo = m->voice[v];
		if (vo.channel == channel && vo.note == note && vo.on)
			return v;
	}
	for (int v = 0; v < VOICES; v++)
	{
		const Voice &vo = m->voice[v];
		if (vo.channel < 0)
			return v;
		if (!vo.on)
		{
			if (released < 0 || vo.release < m->voice[released].release)
				released = v;
		}
		else if (keyed < 0 || vo.start < m->voice[keyed].start)
			keyed = v;
	}
	return released >= 0 ? released : keyed;
}

static void note_off(void *u, uint8_t channel, uint8_t note)
{
	midi_fm *m = static_cast<midi_fm *>(u);
	channel &= 15;
	const bool sustain = m->ch[channel].sustain;
	each_voice(m, channel, [&](int v) {
		Voice &vo = m->voice[v];
		if (vo.note != note || !vo.on || vo.held)
			return;
		if (sustain)
			vo.held = true;
		else
			voice_off(m, v);
	});
}

static void note_on(void *u, uint8_t channel, uint8_t note, uint8_t velocity)
{
	midi_fm *m = static_cast<midi_fm *>(u);
	channel &= 15;
	note &= 127;
	if (!velocity)
	{
		note_off(u, channel, note);
		return;
	}
	const WOPNInstrument *ins = instrument(m->ch[channel], channel, note);
	if (!ins)
		return;
	const int v = voice_for(m, channel, note);
	Voice &vo = m->voice[v];
	if (vo.on)
		key(m, v, false);
	vo.channel = channel;
	vo.note = note;
	vo.on = true;
	vo.held = false;
	vo.ins = ins;
	vo.tone = (channel == DRUMS && ins->percussion_key_number ? ins->percussion_key_number : note) + ins->note_offset;
	vo.velocity = velocity & 127;
	vo.start = m->now;
	voice_program(m, v);
	key(m, v, true);
}

static void note_aftertouch(void *, uint8_t, uint8_t, uint8_t) {}
static void channel_aftertouch(void *, uint8_t, uint8_t) {}

static void release_held(midi_fm *m, int channel)
{
	each_voice(m, channel, [&](int v) {
		if (m->voice[v].held)
			voice_off(m, v);
	});
}

static void controller(void *u, uint8_t channel, uint8_t type, uint8_t value)
{
	midi_fm *m = static_cast<midi_fm *>(u);
	channel &= 15;
	value &= 127;
	Channel &c = m->ch[channel];
	switch (type)
	{
		case 0: c.bank_msb = value; break;
		case 32: c.bank_lsb = value; break;
		case 1:
			c.modulation = value;
			each_voice(m, channel, [&](int v) { voice_pitch(m, v); });
			break;
		case 7:
		case 11:
			(type == 7 ? c.volume : c.expression) = value;
			each_voice(m, channel, [&](int v) { voice_volume(m, v); });
			break;
		case 10:
			c.pan = value;
			each_voice(m, channel, [&](int v) { voice_pan(m, v); });
			break;
		case 64:
			c.sustain = value >= 64;
			if (!c.sustain)
				release_held(m, channel);
			break;
		case 101: c.rpn_msb = value; break;
		case 100: c.rpn_lsb = value; break;
		case 99:
		case 98: c.rpn_msb = c.rpn_lsb = 127; break; /* an NRPN: data entry is not the RPN's */
		case 6:
		case 38:
			/* RPN 0: the pitch bend's range, semitones and cents */
			if (c.rpn_msb == 0 && c.rpn_lsb == 0)
			{
				(type == 6 ? c.bend_semitones : c.bend_cents) = value;
				each_voice(m, channel, [&](int v) { voice_pitch(m, v); });
			}
			break;
		case 120: /* all sound off */
			each_voice(m, channel, [&](int v) {
				if (m->voice[v].on)
					voice_off(m, v);
			});
			break;
		case 121: /* reset all controllers */
			c.modulation = 0;
			c.expression = 127;
			c.sustain = false;
			c.bend = 0;
			c.rpn_msb = c.rpn_lsb = 127;
			release_held(m, channel);
			each_voice(m, channel, [&](int v) {
				voice_volume(m, v);
				voice_pitch(m, v);
			});
			break;
		case 123: /* all notes off, and the modes that imply it */
		case 124:
		case 125:
		case 126:
		case 127:
			each_voice(m, channel, [&](int v) {
				Voice &vo = m->voice[v];
				if (vo.on && !vo.held)
				{
					if (c.sustain)
						vo.held = true;
					else
						voice_off(m, v);
				}
			});
			break;
		default: break;
	}
}

static void patch(void *u, uint8_t channel, uint8_t program)
{
	static_cast<midi_fm *>(u)->ch[channel & 15].program = program & 127;
}

static void pitch_bend(void *u, uint8_t channel, uint8_t msb, uint8_t lsb)
{
	midi_fm *m = static_cast<midi_fm *>(u);
	channel &= 15;
	m->ch[channel].bend = ((msb & 127) << 7 | (lsb & 127)) - 8192;
	each_voice(m, channel, [&](int v) { voice_pitch(m, v); });
}

/* GM System On, GS Reset and XG System On reset every channel */
static void sysex(void *u, const uint8_t *msg, size_t size)
{
	midi_fm *m = static_cast<midi_fm *>(u);
	if (size && msg[0] == 0xF0)
	{
		msg++;
		size--;
	}
	const bool gm = size >= 4 && msg[0] == 0x7E && msg[2] == 0x09 && msg[3] == 0x01;
	const bool gs = size >= 7 && msg[0] == 0x41 && msg[2] == 0x42 && msg[3] == 0x12 && msg[4] == 0x40 && msg[5] == 0x00
		&& msg[6] == 0x7F;
	const bool xg = size >= 6 && msg[0] == 0x43 && (msg[1] & 0xF0) == 0x10 && msg[2] == 0x4C && msg[3] == 0x00
		&& msg[4] == 0x00 && msg[5] == 0x7E;
	if (!gm && !gs && !xg)
		return;
	channels_reset(m);
	for (int v = 0; v < VOICES; v++)
		if (m->voice[v].channel >= 0)
		{
			if (m->voice[v].on)
				voice_off(m, v);
			voice_volume(m, v);
			voice_pitch(m, v);
			voice_pan(m, v);
		}
}

/* ------------------------------------------------------------ sound */

static void vibrato_step(midi_fm *m)
{
	m->vibrato += VIBRATO_STEP;
	for (int v = 0; v < VOICES; v++)
	{
		const Voice &vo = m->voice[v];
		if (vo.channel >= 0 && m->ch[vo.channel].modulation)
			voice_pitch(m, v);
	}
}

/* the sequencer's render hook: the chips' frames, summed, between its events */
static void render(void *u, uint8_t *stream, size_t length)
{
	midi_fm *m = static_cast<midi_fm *>(u);
	int16_t *out = reinterpret_cast<int16_t *>(stream);
	size_t frames = length / 4;
	while (frames)
	{
		if (m->now % BLOCK == 0)
			vibrato_step(m);
		size_t n = (size_t)(BLOCK - m->now % BLOCK);
		if (n > frames)
			n = frames;
		int32_t mix[2 * BLOCK];
		memset(mix, 0, n * 2 * sizeof mix[0]);
		for (int c = 0; c < CHIPS; c++)
			m->chip[c]->generateAndMix32(mix, n);
		for (size_t i = 0; i < 2 * n; i++)
			out[i] = (int16_t)(mix[i] > 32767 ? 32767 : mix[i] < -32768 ? -32768 : mix[i]);
		out += 2 * n;
		frames -= n;
		m->now += n;
	}
}

/* every voice quiet at once: keyed off and released at the fastest rate */
static void silence(midi_fm *m)
{
	for (int v = 0; v < VOICES; v++)
	{
		for (int k = 0; k < 4; k++)
			reg(m, v, (uint16_t)(0x80 + 4 * k), 0xFF);
		key(m, v, false);
		m->voice[v].channel = -1;
		m->voice[v].on = m->voice[v].held = false;
	}
}

/* ------------------------------------------------------------ the interface */

static bool bank_load(void)
{
	if (g_bank)
		return true;
	for (int i = 0; i < 256; i++)
		g_sine[i] = sin(i * (2.0 * 3.14159265358979323846 / 256.0));
	void *copy = malloc(sizeof g_xg_wopn);
	if (!copy)
		return false;
	memcpy(copy, g_xg_wopn, sizeof g_xg_wopn);
	g_bank = WOPN_LoadBankFromMem(copy, sizeof g_xg_wopn, NULL);
	free(copy);
	return g_bank && g_bank->banks_count_melodic && g_bank->banks_count_percussion;
}

int midi_fm_is_midi(const void *data, size_t len)
{
	const uint8_t *p = static_cast<const uint8_t *>(data);
	if (len >= 4 && !memcmp(p, "MThd", 4))
		return 1;
	if (len >= 12 && !memcmp(p, "RIFF", 4) && !memcmp(p + 8, "RMID", 4))
		return 1;
	if (len >= 4 && !memcmp(p, "MUS\x1A", 4))
		return 1;
	if (len >= 12 && !memcmp(p, "FORM", 4) && !memcmp(p + 8, "XDIR", 4))
		return 1;
	return 0;
}

struct midi_fm *midi_fm_open(const void *data, size_t len)
{
	if (!bank_load())
		return NULL;
	midi_fm *m = new (std::nothrow) midi_fm();
	if (!m)
		return NULL;
	memset(&m->rt, 0, sizeof m->rt);
	m->rt.rtUserData = m;
	m->rt.rt_noteOn = note_on;
	m->rt.rt_noteOff = note_off;
	m->rt.rt_noteAfterTouch = note_aftertouch;
	m->rt.rt_channelAfterTouch = channel_aftertouch;
	m->rt.rt_controllerChange = controller;
	m->rt.rt_patchChange = patch;
	m->rt.rt_pitchBend = pitch_bend;
	m->rt.rt_systemExclusive = sysex;
	m->rt.onPcmRender = render;
	m->rt.onPcmRender_userData = m;
	m->rt.pcmSampleRate = RATE;
	m->rt.pcmFrameSize = 4;
	bool ok = true;
	try
	{
		m->seq.setInterface(&m->rt);
		ok = m->seq.loadMIDI(data, len);
	}
	catch (...)
	{
		ok = false;
	}
	for (int c = 0; c < CHIPS; c++)
	{
		m->chip[c] = ok ? new (std::nothrow) MameOPN2(OPNChip_OPN2) : NULL;
		if (m->chip[c])
			m->chip[c]->setRate(RATE, CLOCK);
		else
			ok = false;
	}
	if (!ok)
	{
		midi_fm_close(m);
		return NULL;
	}
	channels_reset(m);
	chips_reset(m);
	return m;
}

void midi_fm_close(struct midi_fm *m)
{
	if (!m)
		return;
	for (int c = 0; c < CHIPS; c++)
		delete m->chip[c];
	delete m;
}

void midi_fm_start(struct midi_fm *m, int looping)
{
	m->seq.setLoopEnabled(looping != 0);
	m->seq.setLoopsCount(-1);
	midi_fm_seek(m, 0.0);
}

long midi_fm_render(struct midi_fm *m, int16_t *out, long frames)
{
	if (frames <= 0)
		return 0;
	return m->seq.playStream(reinterpret_cast<uint8_t *>(out), (size_t)frames * 4) / 4;
}

double midi_fm_length(struct midi_fm *m) { return m->seq.timeLength(); }
double midi_fm_tell(struct midi_fm *m) { return m->seq.tell(); }

/* the song from a point: its controllers as the events before it set them
 * (the sequencer replays them, every note-on skipped), every voice quiet */
void midi_fm_seek(struct midi_fm *m, double seconds)
{
	silence(m);
	channels_reset(m);
	if (seconds > 0.0)
		m->seq.seek(seconds, 1.0 / RATE);
	else
		m->seq.rewind();
}
