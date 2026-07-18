//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	Music for PureDarwin: a real MUS-format parser (the lump format
//	DOOM's music actually ships in) driving a simple square-wave
//	synthesizer per MIDI channel, mixed into the same /dev/dsp0 stream
//	i_sound_pd.c's sfx mixer writes. This is not trying to emulate the
//	OPL2/AdLib timbre real DOOM used (that's DBOPL-scale work - see
//	fbDOOM's own unused i_oplmusic.c for the real thing); it plays the
//	actual notes/timing/volume from the song at the right pitches, just
//	with a plain tone instead of an FM instrument patch.
//
//	Called from i_sound_pd.c's I_UpdateSound() once per output sample so
//	MUS event timing (in its own native tic rate, independent of DOOM's
//	35Hz game tic) stays sample-accurate.
//
//-----------------------------------------------------------------------------

#include "doomtype.h"
#include "i_sound.h"
#include <string.h>
#include <math.h>

#define MUS_MAGIC "MUS\x1a"
#define MUS_NUM_CHANNELS 16
#define MUS_PERCUSSION_CHANNEL 15
#define MUS_TICRATE 140 /* DMX's native MUS clock */
#define OUT_RATE 48000

struct mus_header {
    char id[4];
    unsigned short score_len;
    unsigned short score_start;
    unsigned short channels;
    unsigned short sec_channels;
    unsigned short num_instruments;
};

enum {
    MUS_EV_RELEASE = 0,
    MUS_EV_PLAY = 1,
    MUS_EV_PITCHBEND = 2,
    MUS_EV_SYSTEM = 3,
    MUS_EV_CONTROLLER = 4,
    MUS_EV_UNUSED = 5,
    MUS_EV_SCOREEND = 6,
};

struct mus_channel {
    int note;    /* -1 = silent */
    int volume;  /* 0-127 */
    unsigned phase; /* Q16.16 wave phase */
    unsigned step;  /* Q16.16 phase increment per output sample */
};

static const unsigned char *mus_data;
static unsigned mus_len;
static unsigned mus_pos;     /* byte offset into mus_data, from score_start */
static unsigned mus_start;
static boolean mus_playing;
static boolean mus_looping;
static int mus_volume = 127; /* 0-127, set by I_SetMusicVolume */

static struct mus_channel mus_channels[MUS_NUM_CHANNELS];

static unsigned mus_sample_countdown; /* output samples until next event batch */
static unsigned mus_tic_accum_milli;  /* fractional output-samples-per-mus-tic carry */

/* MIDI note number -> frequency (A4=440Hz at note 69), scaled to a Q16.16
 * phase step for a given output sample rate. */
static unsigned
note_step(int note)
{
    if (note < 0)
        note = 0;
    if (note > 127)
        note = 127;

    /* freq = 440 * 2^((note-69)/12); step = freq / OUT_RATE, in Q16.16. */
    double freq = 440.0 * pow(2.0, ((double)note - 69.0) / 12.0);
    double step = (freq / (double)OUT_RATE) * 65536.0;
    if (step < 0.0)
        step = 0.0;
    if (step > 4294967295.0)
        step = 4294967295.0;
    return (unsigned)step;
}

static unsigned char
mus_read_byte(void)
{
    if (mus_pos >= mus_len)
        return 0xff; /* treat as score-end-ish if we run off the end */
    return mus_data[mus_pos++];
}

/* MUS variable-length time delta: 7 bits per byte, MSB=continue. */
static unsigned
mus_read_vlq(void)
{
    unsigned val = 0;
    unsigned char b;
    do {
        b = mus_read_byte();
        val = (val << 7) | (b & 0x7f);
    } while (b & 0x80);
    return val;
}

static void
mus_stop_all_notes(void)
{
    for (int i = 0; i < MUS_NUM_CHANNELS; i++) {
        mus_channels[i].note = -1;
        mus_channels[i].volume = 0;
    }
}

static void
mus_restart(void)
{
    mus_pos = mus_start;
    mus_sample_countdown = 0;
    mus_tic_accum_milli = 0;
    mus_stop_all_notes();
}

/* Process every event at the current score position until we hit one
 * followed by a time delta, then schedule the next batch. */
static void
mus_process_events(void)
{
    for (;;) {
        if (mus_pos >= mus_len) {
            mus_playing = false;
            return;
        }

        unsigned char evbyte = mus_read_byte();
        int channel = evbyte & 0x0f;
        int evtype = (evbyte >> 4) & 0x07;
        boolean has_delay = (evbyte & 0x80) != 0;

        switch (evtype) {
        case MUS_EV_RELEASE: {
            unsigned char note = mus_read_byte() & 0x7f;
            (void)note;
            if (channel < MUS_NUM_CHANNELS) {
                mus_channels[channel].note = -1;
            }
            break;
        }
        case MUS_EV_PLAY: {
            unsigned char b = mus_read_byte();
            int note = b & 0x7f;
            boolean has_vol = (b & 0x80) != 0;
            int vol = channel < MUS_NUM_CHANNELS ? mus_channels[channel].volume : 100;
            if (has_vol)
                vol = mus_read_byte() & 0x7f;
            if (channel < MUS_NUM_CHANNELS && channel != MUS_PERCUSSION_CHANNEL) {
                mus_channels[channel].note = note;
                mus_channels[channel].volume = vol;
                mus_channels[channel].step = note_step(note);
            }
            break;
        }
        case MUS_EV_PITCHBEND:
            mus_read_byte();
            break;
        case MUS_EV_SYSTEM:
            mus_read_byte();
            break;
        case MUS_EV_CONTROLLER: {
            unsigned char ctrl = mus_read_byte();
            unsigned char val = mus_read_byte();
            if (ctrl == 3 && channel < MUS_NUM_CHANNELS) /* volume controller */
                mus_channels[channel].volume = val & 0x7f;
            break;
        }
        case MUS_EV_SCOREEND:
            if (mus_looping) {
                mus_restart();
            } else {
                mus_playing = false;
            }
            return;
        default:
            break;
        }

        if (has_delay) {
            unsigned delay_tics = mus_read_vlq();
            /* Convert MUS tics to output samples, Q1000 fixed-point carry
             * so 48000/140 (not integer) doesn't drift. */
            unsigned long long milli = (unsigned long long)delay_tics * OUT_RATE * 1000ull;
            mus_tic_accum_milli += (unsigned)(milli / MUS_TICRATE);
            mus_sample_countdown = mus_tic_accum_milli / 1000u;
            mus_tic_accum_milli -= mus_sample_countdown * 1000u;
            return;
        }
        /* No delay: more events belong to this same instant, keep going. */
    }
}

/* Called once per output sample from i_sound_pd.c's mixer. Returns a signed
 * sample (already volume-scaled, summed across channels) to add into the
 * output mix. */
int
I_PD_MixMusicSample(void)
{
    if (!mus_playing)
        return 0;

    if (mus_sample_countdown == 0)
        mus_process_events();

    if (mus_sample_countdown > 0)
        mus_sample_countdown--;

    int mix = 0;
    for (int i = 0; i < MUS_NUM_CHANNELS; i++) {
        struct mus_channel *ch = &mus_channels[i];
        if (ch->note < 0 || ch->volume <= 0)
            continue;

        /* Simple bandlimited-ish square wave: +amp for first half phase,
         * -amp for second half. */
        int amp = (ch->volume * mus_volume * 60) / (127 * 127);
        mix += (ch->phase < 0x80000000u) ? amp : -amp;
        ch->phase += ch->step;
    }
    return mix;
}

void I_PD_InitMusicPlayer(void)
{
    mus_stop_all_notes();
}

void I_InitMusic(void)
{
    I_PD_InitMusicPlayer();
}

void I_ShutdownMusic(void)
{
    mus_playing = false;
}

void I_SetMusicVolume(int volume)
{
    mus_volume = volume;
}

void I_PauseSong(void)
{
    mus_playing = false;
}

void I_ResumeSong(void)
{
    if (mus_data)
        mus_playing = true;
}

void *I_RegisterSong(void *data, int len)
{
    const struct mus_header *hdr = (const struct mus_header *)data;

    if (len < (int)sizeof(struct mus_header) || memcmp(hdr->id, MUS_MAGIC, 4) != 0)
        return NULL;

    mus_data = (const unsigned char *)data;
    mus_len = (unsigned)len;
    mus_start = hdr->score_start;
    mus_playing = false;
    return (void *)data;
}

void I_UnRegisterSong(void *handle)
{
    if ((const void *)handle == (const void *)mus_data) {
        mus_playing = false;
        mus_data = NULL;
    }
}

void I_PlaySong(void *handle, boolean looping)
{
    if ((const void *)handle != (const void *)mus_data)
        return;
    mus_looping = looping;
    mus_restart();
    mus_playing = true;
}

void I_StopSong(void)
{
    mus_playing = false;
    mus_stop_all_notes();
}

boolean I_MusicIsPlaying(void)
{
    return mus_playing;
}
