/* SPDX-License-Identifier: MIT */
#include "common/recovery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static uint32_t digest(const char *s, size_t n) {
    uint32_t h = 2166136261u;
    while (n--) h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}
static void filename(char out[1100], const char *prefix, int slot) {
    snprintf(out, 1100, "%s.%d.draft", prefix, slot);
}
static unsigned scan(const char *prefix, char *text, size_t cap, uint32_t *hash) {
    unsigned newest = 0; int i;
    for (i = 0; i < 3; i++) {
        char path[1100], header[100], block[256]; FILE *f;
        unsigned seq, len, sum, remaining; uint32_t actual = 2166136261u;
        int used = 0, valid = 1; long body;
        filename(path, prefix, i);
        if (!(f = fopen(path, "rb"))) continue;
        if (!fgets(header, sizeof header, f) ||
            sscanf(header, "OFPDRAFT 1 %u %u %x %n", &seq, &len, &sum, &used) != 3 ||
            !used || header[used] || !seq || !len || len > 1024 * 1024 ||
            (body = ftell(f)) < 0) { fclose(f); continue; }
        remaining = len;
        while (remaining) {
            size_t want = remaining < sizeof block ? remaining : sizeof block, n, k;
            n = fread(block, 1, want, f);
            if (n != want || memchr(block, 0, n)) { valid = 0; break; }
            for (k = 0; k < n; k++) actual = (actual ^ (unsigned char)block[k]) * 16777619u;
            remaining -= (unsigned)n;
        }
        if (valid && fgetc(f) == EOF && !ferror(f) && actual == sum && seq > newest &&
            (!text || len < cap)) {
            if (!text || (!fseek(f, body, SEEK_SET) && fread(text, 1, len, f) == len)) {
                newest = seq; *hash = sum;
                if (text) text[len] = 0;
            }
        }
        fclose(f);
    }
    return newest;
}
int recovery_read(const char *prefix, char *text, size_t cap) {
    uint32_t hash; if (cap) text[0] = 0;
    return cap && scan(prefix, text, cap, &hash) != 0;
}
int recovery_write(const char *prefix, const char *text) {
    uint32_t oldhash = 0, hash; unsigned seq; size_t len = strlen(text);
    char path[1100], temporary[1110]; FILE *f; int ok;
    if (!len || len > 1024 * 1024) return -1;
    seq = scan(prefix, NULL, 0, &oldhash); hash = digest(text, len);
    if (seq && hash == oldhash) return 0;
    if (seq == UINT32_MAX) return -1;
    filename(path, prefix, (int)((seq + 1) % 3));
    snprintf(temporary, sizeof temporary, "%s.tmp", path);
    if (!(f = fopen(temporary, "wb"))) return -1;
    ok = fprintf(f, "OFPDRAFT 1 %u %u %08x\n", seq + 1, (unsigned)len, (unsigned)hash) > 0 &&
         fwrite(text, 1, len, f) == len;
    if (fclose(f)) ok = 0;
    /* AmigaDOS and Windows require the destination to be removed first.
     * The other two valid generations remain intact if this rename fails. */
    if (ok) { remove(path); if (rename(temporary, path)) ok = 0; }
    if (!ok) remove(temporary);
    return ok ? 0 : -1;
}
void recovery_clear(const char *prefix) {
    int i; char path[1100];
    for (i = 0; i < 3; i++) { filename(path, prefix, i); remove(path); }
}
