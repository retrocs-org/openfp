/* SPDX-License-Identifier: MIT
 * Build: cc -O2 tools/music_to_pcm.c -lm -o /tmp/ofp-music-to-pcm
 * Usage: ofp-music-to-pcm source.mp3|source.ogg|source.wav output.pcm [seconds] [--seamless]
 * 11,025 Hz signed 8-bit mono; FIR low-pass, normalized, short loop crossfade.
 */
#define DR_MP3_IMPLEMENTATION
#include "../third_party/dr_mp3.h"
#include "../third_party/stb_vorbis.c"
#include <math.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
/* The uncompressed 16-bit WAV downloads use little-endian PCM. */
static unsigned wav_u16(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t wav_u32(const unsigned char *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static float *wav_pcm16(const char *path, drmp3_config *cfg, drmp3_uint64 *frames)
{
    FILE *f = fopen(path, "rb");
    unsigned char head[16], *pcm = NULL;
    unsigned channels = 0, rate = 0, align = 0;
    uint32_t bytes = 0;
    size_t samples, i;
    float *out = NULL;
    if (!f) return NULL;
    if (fread(head, 1, 12, f) != 12 || memcmp(head, "RIFF", 4) || memcmp(head + 8, "WAVE", 4)) goto done;
    while (fread(head, 1, 8, f) == 8) {
        uint32_t length = wav_u32(head + 4);
        if (!memcmp(head, "fmt ", 4)) {
            if (length < 16 || fread(head, 1, 16, f) != 16 || wav_u16(head) != 1 || wav_u16(head + 14) != 16) goto done;
            channels = wav_u16(head + 2); rate = wav_u32(head + 4); align = wav_u16(head + 12);
            if (!channels || channels > 8 || !rate || align != channels * 2) goto done;
            length -= 16;
        } else if (!memcmp(head, "data", 4)) {
            if (!align || !length || length % align) goto done;
            bytes = length;
            break;
        }
        if ((uint64_t)length + (length & 1u) > LONG_MAX || fseek(f, (long)length + (length & 1u), SEEK_CUR)) goto done;
    }
    samples = bytes / 2;
    if (!samples || samples > SIZE_MAX / sizeof *out) goto done;
    pcm = malloc(bytes); out = malloc(samples * sizeof *out);
    if (!pcm || !out || fread(pcm, 1, bytes, f) != bytes) { free(out); out = NULL; goto done; }
    for (i = 0; i < samples; i++) {
        int sample = (int)wav_u16(pcm + i * 2);
        if (sample >= 32768) sample -= 65536;
        out[i] = (float)sample / 32768.0f;
    }
    cfg->channels = channels; cfg->sampleRate = rate; *frames = samples / channels;
done:
    free(pcm); fclose(f); return out;
}
int main(int argc, char **argv)
{
    drmp3_config cfg; drmp3_uint64 frames, j;
    float *input = NULL, *mono, peak = 0;
    const char *extension;
    size_t n, i, fade = 6615; FILE *f;
    double seconds = 0;
    int arg, seamless = 0;
    if (argc < 3 || argc > 5) return 2;
    for (arg = 3; arg < argc; arg++) {
        char *end;
        if (!strcmp(argv[arg], "--seamless")) { seamless = 1; continue; }
        seconds = strtod(argv[arg], &end);
        if (*end || !isfinite(seconds) || seconds <= 0) return 2;
    }
    extension = strrchr(argv[1], '.');
    if (!extension) return 1;
    /* MP3 probing can mistake PCM bytes for a short MP3 stream. */
    if (!strcmp(extension, ".wav")) input = wav_pcm16(argv[1], &cfg, &frames);
    else if (!strcmp(extension, ".mp3")) input = drmp3_open_file_and_read_pcm_frames_f32(argv[1], &cfg, &frames, NULL);
    else if (!strcmp(extension, ".ogg")) {
        stb_vorbis *v = stb_vorbis_open_filename(argv[1], NULL, NULL);
        stb_vorbis_info info;
        unsigned length;
        if (!v) return 1;
        info = stb_vorbis_get_info(v);
        length = stb_vorbis_stream_length_in_samples(v);
        cfg.sampleRate = info.sample_rate; cfg.channels = info.channels;
        if (!length || !cfg.channels || (size_t)length > SIZE_MAX / cfg.channels / sizeof *input ||
            length > INT_MAX / cfg.channels) { stb_vorbis_close(v); return 1; }
        input = malloc((size_t)length * cfg.channels * sizeof *input);
        if (!input) { stb_vorbis_close(v); return 1; }
        frames = stb_vorbis_get_samples_float_interleaved(v, cfg.channels, input, length * cfg.channels);
        stb_vorbis_close(v);
    }
    if (!input || !cfg.sampleRate || !frames) { free(input); return 1; }
    if (seconds && seconds < (double)frames / cfg.sampleRate) {
        frames = (drmp3_uint64)(seconds * cfg.sampleRate);
        seamless = 0; /* a clipped excerpt needs its own loop transition */
    }
    n = (size_t)(frames * 11025 / cfg.sampleRate);
    mono = calloc(n, sizeof *mono);
    if (!mono) { free(input); return 1; }
    for (i = 0; i < n; i++) {
        double at = (double)i * cfg.sampleRate / 11025, sum = 0, weights = 0;
        double cutoff = 0.45 * 11025 / cfg.sampleRate;
        int k, ch;
        for (k = -24; k <= 24; k++) {
            double dx = floor(at) + k - at, v, weight;
            long index = (long)floor(at) + k;
            if (index < 0 || (drmp3_uint64)index >= frames) continue;
            v = fabs(dx) < 1e-9 ? 2 * cutoff : sin(6.28318530718 * cutoff * dx) / (3.14159265359 * dx);
            weight = v * (0.5 + 0.5 * cos(3.14159265359 * dx / 25));
            for (ch = 0; ch < (int)cfg.channels; ch++) sum += input[index * cfg.channels + ch] * weight / cfg.channels;
            weights += weight;
        }
        mono[i] = (float)(sum / weights);
    }
    drmp3_free(input, NULL);
    if (!seamless && n > 2 * fade) {
        for (j = 0; j < fade; j++) mono[j] = mono[n-fade+j] * (1-(float)j/fade) + mono[j] * ((float)j/fade);
        n -= fade;
    }
    for (i = 0; i < n; i++) if (fabsf(mono[i]) > peak) peak = fabsf(mono[i]);
    f = fopen(argv[2], "wb"); if (!f) { free(mono); return 1; }
    for (i = 0; i < n; i++) { int v = peak ? (int)lrintf(mono[i] * 115 / peak) : 0; fputc(v & 255, f); }
    free(mono);
    fprintf(stderr, "%s: %.2f seconds, %zu bytes\n", argv[2], (double)n / 11025, n);
    { int failed = ferror(f); return fclose(f) != 0 || failed; }
}
