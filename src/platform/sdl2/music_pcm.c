/* SPDX-License-Identifier: MIT */
#include <SDL.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "platform/sdl2/music_pcm.h"

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#include "dr_mp3.h"

int16_t *music_pcm_load(const char *path, int rate, int *samples)
{
    const char *extension = strrchr(path, '.');
    SDL_AudioSpec source = { 0 };
    void *input = NULL;
    int16_t *output = NULL;
    Uint32 size = 0;
    SDL_AudioStream *stream;
    if (!extension) return NULL;
    if (!strcmp(extension, ".wav")) {
        Uint8 *wav;
        if (!SDL_LoadWAV(path, &source, &wav, &size)) return NULL;
        input = wav;
    } else if (!strcmp(extension, ".ogg")) {
        short *decoded = NULL;
        int channels, frames = stb_vorbis_decode_filename(path, &channels, &source.freq, &decoded);
        input = decoded;
        source.format = AUDIO_S16SYS;
        if (frames > 0 && channels > 0 && channels <= 8 && frames <= INT_MAX / (2 * channels)) {
            source.channels = (Uint8)channels;
            size = (Uint32)(frames * channels * 2);
        }
    } else if (!strcmp(extension, ".mp3")) {
        drmp3_config config;
        drmp3_uint64 frames;
        input = drmp3_open_file_and_read_pcm_frames_s16(path, &config, &frames, NULL);
        source.format = AUDIO_S16SYS;
        if (input && config.channels > 0 && config.channels <= 2 && frames <= INT_MAX / (2 * config.channels)) {
            source.channels = (Uint8)config.channels;
            source.freq = (int)config.sampleRate;
            size = (Uint32)(frames * config.channels * 2);
        }
    }
    if (!input) return NULL;
    stream = size && size <= INT_MAX ?
        SDL_NewAudioStream(source.format, source.channels, source.freq, AUDIO_S16SYS, 1, rate) : NULL;
    if (stream && SDL_AudioStreamPut(stream, input, (int)size) == 0 && SDL_AudioStreamFlush(stream) == 0) {
        int bytes = SDL_AudioStreamAvailable(stream);
        if (bytes > 1) {
            output = SDL_malloc((size_t)bytes);
            if (output && SDL_AudioStreamGet(stream, output, bytes) == bytes) *samples = bytes / 2;
            else { SDL_free(output); output = NULL; }
        }
    }
    if (stream) SDL_FreeAudioStream(stream);
    if (!strcmp(extension, ".wav")) SDL_FreeWAV(input);
    else if (!strcmp(extension, ".mp3")) drmp3_free(input, NULL);
    else free(input);
    return output;
}
