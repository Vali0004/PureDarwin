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
//	Sound effects for PureDarwin, real: a small mixer over the DMX-format
//	sfx lumps (8-bit unsigned PCM, 11025 Hz mono, an 8-byte header we
//	skip - see fbDOOM's own getsfx()/W_CacheLumpNum usage in
//	i_sound_dummy.c for the same access pattern), resampled and
//	panned to 16-bit/48000 Hz/stereo and streamed to /dev/dsp0 (real
//	Intel HDAudio via RavynHDAudio - see src/Userspace/pcmplay, the
//	existing minimal CLI player for the same device/format). Music
//	(MUS parsing + synthesis) lives in i_music_pd.c; I_PD_MixMusicSample()
//	is mixed in here alongside the sfx channels each output sample.
//
//-----------------------------------------------------------------------------

#include "doomtype.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define SRC_RATE 11025
#define OUT_RATE 48000
#define MAX_CHANNELS 16
/* (SRC_RATE << 16) / OUT_RATE, Q16.16 fixed-point per-output-sample advance. */
#define SRC_STEP ((SRC_RATE << 16) / OUT_RATE)

struct pd_channel {
    const unsigned char *data; /* DMX sample data, header already skipped */
    unsigned length;           /* sample count */
    unsigned pos;               /* Q16.16 read position into data */
    int leftvol, rightvol;      /* 0-127 */
};

static struct pd_channel channels[MAX_CHANNELS];
static int dsp_fd = -1;

extern int I_PD_MixMusicSample(void); /* i_music_pd.c */

int snd_sfxdevice = 0;
int snd_musicdevice = 0;
int snd_samplerate = SRC_RATE;

void I_InitSound(boolean use_sfx_prefix)
{
    (void)use_sfx_prefix;
    memset(channels, 0, sizeof(channels));
    dsp_fd = open("/dev/dsp0", O_WRONLY);
}

void I_ShutdownSound(void)
{
    if (dsp_fd >= 0) {
        close(dsp_fd);
        dsp_fd = -1;
    }
}

int I_GetSfxLumpNum(sfxinfo_t *sfxinfo)
{
    char namebuf[9];
    snprintf(namebuf, sizeof(namebuf), "ds%s", sfxinfo->name);
    return W_GetNumForName(namebuf);
}

static void
compute_pan(int sep, int vol, int *leftvol, int *rightvol)
{
    /* sep: 0 = full left, 128 = center, 255 = full right (as passed by
     * s_sound.c). vol: 0-127. */
    *leftvol = (vol * (255 - sep)) / 255;
    *rightvol = (vol * sep) / 255;
}

int I_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep)
{
    if (channel < 0 || channel >= MAX_CHANNELS)
        return -1;

    int lumpnum = I_GetSfxLumpNum(sfxinfo);
    if (lumpnum < 0)
        return -1;

    int lumplen = W_LumpLength(lumpnum);
    if (lumplen <= 8)
        return -1;

    const unsigned char *raw = (const unsigned char *)W_CacheLumpNum(lumpnum, PU_CACHE);

    channels[channel].data = raw + 8;
    channels[channel].length = lumplen - 8;
    channels[channel].pos = 0;
    compute_pan(sep, vol, &channels[channel].leftvol, &channels[channel].rightvol);

    return channel;
}

void I_UpdateSoundParams(int channel, int vol, int sep)
{
    if (channel < 0 || channel >= MAX_CHANNELS)
        return;
    compute_pan(sep, vol, &channels[channel].leftvol, &channels[channel].rightvol);
}

void I_StopSound(int channel)
{
    if (channel < 0 || channel >= MAX_CHANNELS)
        return;
    channels[channel].data = NULL;
}

boolean I_SoundIsPlaying(int channel)
{
    if (channel < 0 || channel >= MAX_CHANNELS)
        return false;
    return channels[channel].data != NULL;
}

void I_PrecacheSounds(sfxinfo_t *sounds, int num_sounds)
{
    (void)sounds;
    (void)num_sounds;
}

void I_UpdateSound(void)
{
    /* Called once per game tic (35 Hz); write that tic's worth of mixed
     * audio. Accumulate the fractional frame count so 48000/35 (not an
     * integer) doesn't drift over time. */
    static unsigned accum_milli = 0; /* fractional frames, in thousandths */
    unsigned frames_milli = 48000000u / 35u;
    accum_milli += frames_milli;
    unsigned frames = accum_milli / 1000u;
    accum_milli -= frames * 1000u;

    if (dsp_fd < 0 || frames == 0)
        return;

    short buf[2048]; /* interleaved stereo; 1024 frames per pass */
    unsigned done = 0;
    while (done < frames) {
        unsigned chunk = frames - done;
        if (chunk > 1024)
            chunk = 1024;

        for (unsigned i = 0; i < chunk; i++) {
            int music = I_PD_MixMusicSample();
            int left = music, right = music;

            for (int c = 0; c < MAX_CHANNELS; c++) {
                struct pd_channel *ch = &channels[c];
                if (!ch->data)
                    continue;

                unsigned idx = ch->pos >> 16;
                if (idx >= ch->length) {
                    ch->data = NULL;
                    continue;
                }

                int sample = ((int)ch->data[idx] - 128) << 8;
                left += (sample * ch->leftvol) / 127;
                right += (sample * ch->rightvol) / 127;

                ch->pos += SRC_STEP;
            }

            if (left > 32767) left = 32767;
            if (left < -32768) left = -32768;
            if (right > 32767) right = 32767;
            if (right < -32768) right = -32768;

            buf[i * 2] = (short)left;
            buf[i * 2 + 1] = (short)right;
        }

        ssize_t want = (ssize_t)chunk * 4;
        ssize_t off = 0;
        while (off < want) {
            ssize_t w = write(dsp_fd, (unsigned char *)buf + off, want - off);
            if (w <= 0)
                break;
            off += w;
        }

        done += chunk;
    }
}

void I_BindSoundVariables(void)
{
}
