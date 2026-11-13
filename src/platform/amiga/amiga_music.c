/* SPDX-License-Identifier: MIT
 * Stream mono signed 8-bit loops to Paula through audio.device. The original
 * tracker player remains available for missing tracks and the Sci-Fi world. */
#ifdef __amigaos__
#include <string.h>
#include <stdio.h>
#include <exec/io.h>
#include <devices/audio.h>
#include <clib/alib_protos.h>
#include "common/soundtrack.h"
#include "platform/amiga/music_levels.h"
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/interrupts.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>
#include <proto/exec.h>
#include "platform/amiga/amiga_music.h"
#include "common/volume.h"
#include "platform/amiga/amiga_sound.h"
#include "platform/amiga/modplay.h"
#include "platform/amiga/music_mods.h"

#define CUSTOM ((volatile struct Custom *)0xdff000)
enum { MUSIC_CHANNELS = 3, CHANNEL_MASK = (1 << MUSIC_CHANNELS) - 1, PAULA_VOLUME_MAX = 64 };
#define MUSIC_GAIN(level) (((level) * PAULA_VOLUME_MAX + VOLUME_MAX / 2) / VOLUME_MAX)

static ModPlayer player;
static struct Interrupt server;
static UBYTE *chip, *silence;                     /* the song's samples; two words of nothing, for "no loop" */
static ULONG chip_size;
static volatile int running;                      /* the interrupt plays only while this is set */
static int installed, song = -1;

enum { PCM_BLOCK = 8192, PCM_BUFFERS = 3, PCM_PERIOD = 3546895 / 11025 };
static volatile UWORD gain = MUSIC_GAIN(AMIGA_MUSIC_DEFAULT_LEVEL);  /* Also read by the MOD interrupt. */
static FILE *pcm_file;
static struct MsgPort *pcm_port;
static UBYTE *pcm_chip;
static struct IOAudio *pcm_io[PCM_BUFFERS];
static int pcm_queued[PCM_BUFFERS];
static UWORD pcm_level = 64;

static const char *const pcm_name[SOUNDTRACK_COUNT] = {
    [BIOME_CLASSIC] = "classic", [BIOME_DESERT] = "desert", [BIOME_JUNGLE] = "jungle",
    [BIOME_WINTER] = "winter", [BIOME_VOLCANIC] = "volcanic", [BIOME_WASTELAND] = "wasteland",
    [BIOME_MOON] = "moon", [BIOME_SWAMP] = "swamp", [SOUNDTRACK_MENU] = "menu", [SOUNDTRACK_CREDITS] = "credits",
    [SOUNDTRACK_CANNON_TUBE] = "cannon-tube", [SOUNDTRACK_SNOW_THEME] = "snow-theme",
    [SOUNDTRACK_SPACE_ATMOSPHERE] = "space-atmosphere",
    [SOUNDTRACK_NEW_BEGINNING] = "new-beginning",
    [SOUNDTRACK_LOST_DESERT] = "lost-in-the-desert",
    [SOUNDTRACK_DESERT_DREAMS] = "desert-of-dreams-mix",
    [SOUNDTRACK_URBAN_JUNGLE] = "urban-jungle-2061",
    [SOUNDTRACK_JUNGLE_PRO_SENSORY] = "jungle-pro-sensory",
    [SOUNDTRACK_IRON_WASTELAND] = "iron-wasteland"
};

static void pcm_stop(void)
{
    int i;
    /* Cancel waiting writes before stopping the one Paula is currently playing. */
    for (i = PCM_BUFFERS - 1; i >= 0; i--)
        if (pcm_queued[i]) AbortIO((struct IORequest *)pcm_io[i]);
    for (i = 0; i < PCM_BUFFERS; i++) {
        if (pcm_queued[i]) WaitIO((struct IORequest *)pcm_io[i]);
        pcm_queued[i] = 0;
        if (pcm_io[i]) { DeleteIORequest((struct IORequest *)pcm_io[i]); pcm_io[i] = NULL; }
    }
    if (pcm_port) { DeleteMsgPort(pcm_port); pcm_port = NULL; }
    if (pcm_chip) { FreeMem(pcm_chip, PCM_BLOCK * PCM_BUFFERS); pcm_chip = NULL; }
    if (pcm_file) { fclose(pcm_file); pcm_file = NULL; }
}

static int pcm_queue(int index)
{
    struct IOAudio *io = pcm_io[index];
    UBYTE *data = pcm_chip + index * PCM_BLOCK;
    size_t bytes = 0;
    int rewound = 0;
    while (bytes < PCM_BLOCK) {
        size_t read = fread(data + bytes, 1, PCM_BLOCK - bytes, pcm_file);
        bytes += read;
        if (bytes < PCM_BLOCK) {
            if ((!read && rewound) || ferror(pcm_file) || fseek(pcm_file, 0, SEEK_SET)) return 0;
            rewound = 1;
        }
    }
    io->ioa_Request.io_Command = CMD_WRITE;
    io->ioa_Request.io_Flags = ADIOF_PERVOL;
    io->ioa_Data = data;
    io->ioa_Length = PCM_BLOCK;
    io->ioa_Period = PCM_PERIOD;
    io->ioa_Volume = (gain * pcm_level + 32) / 64;
    io->ioa_Cycles = 1;
    BeginIO((struct IORequest *)io);
    pcm_queued[index] = 1;
    return 1;
}

static int pcm_start(int which)
{
    char path[64];
    int i;
    if (!pcm_name[which]) return 0;
    pcm_level = amiga_music_level[which];
    snprintf(path, sizeof path, "PROGDIR:music/%s.pcm", pcm_name[which]);
    pcm_file = fopen(path, "rb");
    if (!pcm_file) return 0;
    pcm_port = CreateMsgPort();
    pcm_chip = (UBYTE *)AllocMem(PCM_BLOCK * PCM_BUFFERS, MEMF_CHIP);
    if (!pcm_port || !pcm_chip || !sound_share(1)) goto fail;
    for (i = 0; i < PCM_BUFFERS; i++) {
        pcm_io[i] = sound_music_request(pcm_port);
        if (!pcm_io[i] || !pcm_queue(i)) goto fail;
    }
    return 1;

fail:
    pcm_stop();
    return 0;
}

void music_update(void)
{
    struct Message *msg;
    if (!pcm_port) return;
    while ((msg = GetMsg(pcm_port))) {
        int i;
        for (i = 0; i < PCM_BUFFERS; i++) if (msg == (struct Message *)pcm_io[i]) break;
        if (i == PCM_BUFFERS) continue;
        pcm_queued[i] = 0;
        if (pcm_io[i]->ioa_Request.io_Error || !pcm_queue(i)) {
            pcm_stop();
            sound_share(0);
            return;
        }
    }
}

/* (wait for the beam to move on a few lines: Paula needs a moment between stopping a channel's DMA and
 *  starting it again, and between starting it and being given its loop) */
static void lines(int n)
{
    while (n-- > 0) {
        UBYTE line = (UBYTE)(CUSTOM->vhposr >> 8);
        while ((UBYTE)(CUSTOM->vhposr >> 8) == line) ;
    }
}

/* the interrupt: a tick of the song, into the registers (an interrupt server: returns 0, the chain goes on) */
static LONG vblank(void)
{
    int c;
    UWORD start = 0;
    if (!running) return 0;
    mod_tick(&player);
    for (c = 0; c < MUSIC_CHANNELS; c++) if (player.voice[c].trigger) start |= (UWORD)(1 << c);
    if (start) {
        CUSTOM->dmacon = start;                       /* (those channels stopped) */
        lines(2);
    }
    for (c = 0; c < MUSIC_CHANNELS; c++) {
        const ModVoice *v = &player.voice[c];
        if (start & (1 << c)) {
            CUSTOM->aud[c].ac_ptr = (UWORD *)v->data;
            CUSTOM->aud[c].ac_len = (UWORD)(v->length / 2 ? v->length / 2 : 1);
        }
        CUSTOM->aud[c].ac_per = v->period ? v->period : 428;
        UWORD volume = (UWORD)(v->volume * gain / 28);
        CUSTOM->aud[c].ac_vol = !v->period ? 0 : volume > PAULA_VOLUME_MAX ? PAULA_VOLUME_MAX : volume;
    }
    if (start) {
        CUSTOM->dmacon = (UWORD)(DMAF_SETCLR | start);
        lines(2);
        for (c = 0; c < MUSIC_CHANNELS; c++)          /* (Paula has the sample: now its loop, or nothing) */
            if (start & (1 << c)) {
                const ModVoice *v = &player.voice[c];
                CUSTOM->aud[c].ac_ptr = (UWORD *)(v->loop ? v->loop : silence);
                CUSTOM->aud[c].ac_len = (UWORD)(v->loop ? v->loop_length / 2 : 1);
            }
    }
    return 0;
}

static void stop_channels(void)
{
    int c;
    running = 0;
    CUSTOM->dmacon = CHANNEL_MASK;
    for (c = 0; c < MUSIC_CHANNELS; c++) CUSTOM->aud[c].ac_vol = 0;
}

void music_play(int which)
{
    if (which == song) return;
    static const int fallback[SOUNDTRACK_COUNT] = {
        MUSIC_CLASSIC, MUSIC_DESERT, MUSIC_JUNGLE, MUSIC_WINTER, MUSIC_SCIFI,
        MUSIC_DESERT, MUSIC_DESERT, MUSIC_SCIFI, MUSIC_JUNGLE, MUSIC_MENU, MUSIC_MENU, MUSIC_DESERT, MUSIC_WINTER,
        MUSIC_SCIFI, MUSIC_DESERT, MUSIC_DESERT, MUSIC_DESERT, MUSIC_JUNGLE, MUSIC_JUNGLE, MUSIC_DESERT
    };
    if (which < 0 || which >= SOUNDTRACK_COUNT) which = -1;
    if (pcm_file) pcm_stop();
    if (running) stop_channels();
    if (chip) { FreeMem(chip, chip_size); chip = NULL; chip_size = 0; }
    song = which;
    if (which < 0) { sound_share(0); return; }       /* (the effects: all four channels again) */
    if (pcm_start(which)) return;
    which = fallback[which];
    if (!sound_share(CHANNEL_MASK)) { return; }       /* (no audio.device: no channels, no music) */
    if (!silence && !(silence = (UBYTE *)AllocMem(4, MEMF_CHIP | MEMF_CLEAR))) { sound_share(0); return; }
    if (mod_open(&player, music_mods[which], music_sizes[which], NULL) != 0) { sound_share(0); return; }
    chip_size = mod_sample_bytes(&player);
    if (!(chip = (UBYTE *)AllocMem(chip_size ? chip_size : 4, MEMF_CHIP))) { sound_share(0); return; }
    memcpy(chip, player.sample_data, chip_size);
    mod_open(&player, music_mods[which], music_sizes[which], chip);
    if (!installed) {
        server.is_Node.ln_Type = NT_INTERRUPT;
        server.is_Node.ln_Pri = -60;                  /* (after graphics.library's own) */
        server.is_Node.ln_Name = (char *)"OpenFP music";
        server.is_Data = NULL;
        server.is_Code = (void (*)(void))vblank;
        AddIntServer(INTB_VERTB, &server);
        installed = 1;
    }
    running = 1;
}

void music_volume(int level)
{
    gain = MUSIC_GAIN(volume_clamp(level));
    /* PCM reads the new gain when queuing the next block. */
    if (!gain) music_play(-1);
}

int music_song(void) { return song; }

void music_close(void)
{
    music_play(-1);
    if (installed) { RemIntServer(INTB_VERTB, &server); installed = 0; }
    if (silence) { FreeMem(silence, 4); silence = NULL; }
}
#endif
