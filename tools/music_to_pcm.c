/* SPDX-License-Identifier: MIT
 * Build: cc -O2 tools/music_to_pcm.c -lm -o /tmp/ofp-music-to-pcm
 * Usage: ofp-music-to-pcm source.mp3 output.pcm
 * 11,025 Hz signed 8-bit mono; FIR low-pass, normalized, short loop crossfade.
 */
#define DR_MP3_IMPLEMENTATION
#include "../third_party/dr_mp3.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    drmp3_config cfg; drmp3_uint64 frames, j;
    float *input, *mono, peak = 0;
    size_t n, i, fade = 6615; FILE *f;
    if (argc != 3) return 2;
    input = drmp3_open_file_and_read_pcm_frames_f32(argv[1], &cfg, &frames, NULL);
    if (!input || !cfg.sampleRate || !frames) return 1;
    n = (size_t)(frames * 11025 / cfg.sampleRate);
    mono = calloc(n, sizeof *mono);
    if (!mono) return 1;
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
    if (n > 2 * fade) {
        for (j = 0; j < fade; j++) mono[j] = mono[n-fade+j] * (1-(float)j/fade) + mono[j] * ((float)j/fade);
        n -= fade;
    }
    for (i = 0; i < n; i++) if (fabsf(mono[i]) > peak) peak = fabsf(mono[i]);
    f = fopen(argv[2], "wb"); if (!f) return 1;
    for (i = 0; i < n; i++) { int v = peak ? (int)lrintf(mono[i] * 115 / peak) : 0; fputc(v & 255, f); }
    free(mono); return fclose(f) != 0;
}
