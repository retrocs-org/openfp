/* SPDX-License-Identifier: MIT
 * The Amiga client's sound: the game's effects (sounds_8bit.h, from tools/amiga/gen_sounds.py) on Paula's
 * four channels through audio.device, system-friendly: all four allocated once, each sound to the next in
 * turn (a new one stops the oldest: no queue in a heavy fight). No audio.device (or no channels free):
 * silence, and the game goes on. */
#ifdef __amigaos__
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/audio.h>
#include <proto/exec.h>
#include <clib/alib_protos.h>                     /* (BeginIO: amiga.lib's, not exec's) */
#include "platform/amiga/amiga_sound.h"
#include "common/volume.h"
#include "platform/amiga/sounds_8bit.h"

#define PAL_CLOCK 3546895UL                       /* (Paula's clock, PAL: the period for a rate is clock / rate) */
static struct MsgPort *port;
static struct IOAudio *alloc_io, *ch_io[4];
static UBYTE *chip;                               /* the samples, in chip memory (Paula reads only that) */
static ULONG chip_size, offset[SND_COUNT];
static int busy[4], next_ch, open_ok, engine_step;   /* engine_step: the engine's loop on channel 3 (0: off) */
static unsigned music_mask;                       /* Paula channels reserved by the music */
static int gain = 64;                             /* Normal preserves each effect's mix. */
static int laugh_channel = -1;                    /* let the skull finish over routine effects */

int sound_open(void)
{
    static UBYTE all_four[] = { 15 };
    int k, s;
    ULONG at = 0;
    if (open_ok) return 0;
    for (s = 0; s < SND_COUNT; s++) chip_size += snd_len[s];
    if (!(chip = (UBYTE *)AllocMem(chip_size, MEMF_CHIP | MEMF_PUBLIC))) return -1;
    for (s = 0; s < SND_COUNT; s++) { offset[s] = at; memcpy(chip + at, snd_data[s], snd_len[s]); at += snd_len[s]; }
    if (!(port = CreateMsgPort())) { sound_close(); return -1; }
    if (!(alloc_io = (struct IOAudio *)CreateIORequest(port, sizeof(struct IOAudio)))) { sound_close(); return -1; }
    alloc_io->ioa_Request.io_Message.mn_Node.ln_Pri = 0;
    alloc_io->ioa_Data = all_four;
    alloc_io->ioa_Length = sizeof all_four;
    if (OpenDevice((CONST_STRPTR)AUDIONAME, 0, (struct IORequest *)alloc_io, 0) != 0) {   /* (and the channels: allocated) */
        DeleteIORequest((struct IORequest *)alloc_io); alloc_io = NULL;
        sound_close();
        return -1;
    }
    open_ok = 1;
    for (k = 0; k < 4; k++) {
        if (!(ch_io[k] = (struct IOAudio *)CreateIORequest(port, sizeof(struct IOAudio)))) { sound_close(); return -1; }
        *ch_io[k] = *alloc_io;                        /* (the device, the allocation key) */
        ch_io[k]->ioa_Request.io_Message.mn_ReplyPort = port;
        ch_io[k]->ioa_Request.io_Unit = (struct Unit *)((ULONG)alloc_io->ioa_Request.io_Unit & (1UL << k));
    }
    return 0;
}

static void finished(void)                        /* (sounds done: their channels free again) */
{
    struct Message *m;
    int k;
    if (!port) return;
    while ((m = GetMsg(port)) != NULL)
        for (k = 0; k < 4; k++) if (m == (struct Message *)ch_io[k]) {
            busy[k] = 0;
            if (laugh_channel == k) laugh_channel = -1;
        }
}

void sound_volume(int level)
{
    /* Keep the old 32, 64 and 96 gains at their migrated levels. */
    static const int gains[VOLUME_MAX + 1] = { 0, 11, 21, 32, 40, 48, 56, 64, 75, 85, 96 };
    int next = gains[volume_clamp(level)];
    int i;
    if (gain == next) return;
    gain = next;
    if (gain) return;
    finished();
    for (i = 0; i < 4; i++) {
        if (!busy[i] || (music_mask & (1u << i))) continue;
        AbortIO((struct IORequest *)ch_io[i]);
        WaitIO((struct IORequest *)ch_io[i]);
        busy[i] = 0;
    }
    laugh_channel = -1;
    engine_step = 0;
}

static void play_pitch(int id, int volume, unsigned pitch)
{
    struct IOAudio *io;
    int c, tries;
    volume = volume * gain / 64;
    if (!open_ok || id < 0 || id >= SND_COUNT || volume <= 0) return;
    finished();
    if (laugh_channel >= 0 && id == SND_SKULL_LAUGH) return;
    {
        int n = engine_step && music_mask != 7 ? 3 : 4;
        int steal = -1;
        for (tries = 0; tries < n; tries++) {
            c = (next_ch + tries) % n;
            if ((music_mask & (1u << c)) || c == laugh_channel) continue;
            if (steal < 0) steal = c;
            if (!busy[c]) break;
        }
        if (tries == n) c = steal;
        if (c < 0) return;
        next_ch = (c + 1) % n;
    }
    io = ch_io[c];
    if (!io || !io->ioa_Request.io_Unit) return;     /* (that channel wasn't ours) */
    if (busy[c]) { AbortIO((struct IORequest *)io); WaitIO((struct IORequest *)io); busy[c] = 0; }
    io->ioa_Request.io_Command = CMD_WRITE;
    io->ioa_Request.io_Flags = ADIOF_PERVOL;
    io->ioa_Data = chip + offset[id];
    io->ioa_Length = snd_len[id];
    io->ioa_Period = (UWORD)((PAL_CLOCK / SND_RATE) * 256 / pitch);
    io->ioa_Volume = (UWORD)(volume > 64 ? 64 : volume);
    io->ioa_Cycles = 1;
    BeginIO((struct IORequest *)io);
    busy[c] = 1;
    if (id == SND_SKULL_LAUGH) laugh_channel = c;
}

void sound_play(int id, int volume) { play_pitch(id, volume, 256); }
void sound_powerup(int kind) { play_pitch(SND_PICKUP, 56, kind ? 448 : 340); }

void sound_engine(int step)
{
    struct IOAudio *io = ch_io[3];
    if (!gain || !open_ok || !io || !io->ioa_Request.io_Unit) return;
    finished();
    if (laugh_channel == 3) return;
    if (step == engine_step && !(music_mask == 7 && step && !busy[3])) return;   /* (sharing: back after an effect) */
    if (busy[3]) { AbortIO((struct IORequest *)io); WaitIO((struct IORequest *)io); busy[3] = 0; }
    engine_step = step;
    if (!step) return;
    io->ioa_Request.io_Command = CMD_WRITE;
    io->ioa_Request.io_Flags = ADIOF_PERVOL;
    io->ioa_Data = chip + offset[SND_ENGINE];
    io->ioa_Length = snd_len[SND_ENGINE];
    io->ioa_Period = (UWORD)(PAL_CLOCK / SND_RATE * 4 / (3 + step));   /* (faster: higher) */
    io->ioa_Volume = (UWORD)(28 * gain / 64);
    io->ioa_Cycles = 0;                                  /* (round and round, until stopped) */
    BeginIO((struct IORequest *)io);
    busy[3] = 1;
}

int sound_share(int channels)
{
    int k;
    if (!open_ok) return 0;
    for (k = 0; k < 3; k++)
        if ((channels & (1 << k)) && !(music_mask & (1u << k)) && ch_io[k] && busy[k]) {
            AbortIO((struct IORequest *)ch_io[k]);
            WaitIO((struct IORequest *)ch_io[k]);
            busy[k] = 0;
            if (laugh_channel == k) laugh_channel = -1;
        }
    music_mask = (unsigned)channels;
    finished();
    return 1;
}

struct IOAudio *sound_music_request(struct MsgPort *reply_port)
{
    struct IOAudio *io;
    if (!open_ok || !ch_io[0]->ioa_Request.io_Unit) return NULL;
    io = (struct IOAudio *)CreateIORequest(reply_port, sizeof *io);
    if (io) {
        *io = *ch_io[0];
        io->ioa_Request.io_Message.mn_ReplyPort = reply_port;
        io->ioa_Request.io_Message.mn_Node.ln_Type = NT_REPLYMSG;
    }
    return io;
}

void sound_close(void)
{
    engine_step = 0;
    laugh_channel = -1;
    int k;
    for (k = 0; k < 4; k++) {
        if (ch_io[k] && busy[k]) { AbortIO((struct IORequest *)ch_io[k]); WaitIO((struct IORequest *)ch_io[k]); busy[k] = 0; }
        if (ch_io[k]) { DeleteIORequest((struct IORequest *)ch_io[k]); ch_io[k] = NULL; }
    }
    if (alloc_io) {
        if (open_ok) CloseDevice((struct IORequest *)alloc_io);   /* (the channels: free for others again) */
        DeleteIORequest((struct IORequest *)alloc_io); alloc_io = NULL;
    }
    open_ok = 0;
    music_mask = 0;
    if (port) { DeleteMsgPort(port); port = NULL; }
    if (chip) { FreeMem(chip, chip_size); chip = NULL; chip_size = 0; }
}
#endif
