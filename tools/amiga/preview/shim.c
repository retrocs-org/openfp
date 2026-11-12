/* SPDX-License-Identifier: MIT
 * The AmigaOS calls the Amiga frontend makes, done on the desktop over plain planar bitmaps, so
 * what it draws can be looked at without an Amiga (README.md). Environment:
 *   OFP_SHOT=file.ppm   where the picture goes (default amiga-preview.ppm)
 *   OFP_SHOT_MS=N       how long it runs before the picture (default 3000)
 *   OFP_KEYS="ms:code,ms:vN"   keys to send: raw codes (0x80+: a release), vN a character (v13: Return)
 *   OFP_JOY="ms:bits,..."  a CD32 pad in port 1 (JPF_ bits from then on)
 *   OFP_AGA=0|1         whether the machine says it has the AGA chipset (default 1) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "amiga_shim.h"
#define font8x8_basic shim_font8x8
#include "../../../third_party/font8x8_basic.h"
#undef font8x8_basic
#define font8x8_basic shim_font8x8

static uint32_t palette[256];                       /* 0xRRGGBB */
static struct BitMap *shown;                         /* what the display shows */
static struct ExecBase exec_base = { { { 0 }, 40 }, AFF_68020 | AFF_68030 };
struct ExecBase *SysBase = &exec_base;
static struct GfxBase gfx_base;
static struct IntuitionBase intuition_base;
static struct Library lowlevel_base, other_base;
static uint32_t start_ms;

static uint32_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000u + t.tv_nsec / 1000000);
}

/* ---- exec ---- */
APTR AllocMem(ULONG size, ULONG flags) { return (flags & MEMF_CLEAR) ? calloc(1, size) : malloc(size); }
void FreeMem(APTR p, ULONG size) { (void)size; free(p); }
APTR AllocVec(ULONG size, ULONG flags) { return AllocMem(size, flags); }
void FreeVec(APTR p) { free(p); }
ULONG AvailMem(ULONG flags) { (void)flags; return 2u << 20; }
struct Library *OpenLibrary(CONST_STRPTR name, ULONG version)
{
    (void)version;
    if (!start_ms) {
        const char *aga = getenv("OFP_AGA");
        start_ms = now_ms();
        gfx_base.ChipRevBits0 = (UBYTE)((!aga || atoi(aga)) ? (GFXF_AA_ALICE | GFXF_AA_LISA | GFXF_HR_AGNUS | GFXF_HR_DENISE) : (GFXF_HR_AGNUS | GFXF_HR_DENISE));
        gfx_base.LibNode.lib_Version = 40;
    }
    if (!strcmp((const char *)name, "graphics.library")) return &gfx_base.LibNode;
    if (!strcmp((const char *)name, "intuition.library")) return &intuition_base.LibNode;
    if (!strcmp((const char *)name, "lowlevel.library")) return &lowlevel_base;
    return &other_base;
}
void CloseLibrary(struct Library *l) { (void)l; }
struct MsgPort *CreateMsgPort(void) { return calloc(1, sizeof(struct MsgPort)); }
void DeleteMsgPort(struct MsgPort *p) { free(p); }
static void put_msg(struct MsgPort *p, struct Message *m) { if (p) p->head = m; }
struct Message *GetMsg(struct MsgPort *p);
void ReplyMsg(struct Message *m) { (void)m; }
struct Message *WaitPort(struct MsgPort *p) { return p ? p->head : NULL; }
ULONG Wait(ULONG s) { return s; }
void Forbid(void) {}
void Permit(void) {}

/* ---- the picture ---- */
static int pixel(const struct BitMap *bm, int x, int y)
{
    int p, v = 0;
    if (x < 0 || y < 0 || x >= bm->BytesPerRow * 8 || y >= bm->Rows) return 0;
    for (p = 0; p < bm->Depth; p++)
        if (bm->Planes[p] && (bm->Planes[p][y * bm->BytesPerRow + (x >> 3)] & (0x80 >> (x & 7)))) v |= 1 << p;
    return v;
}
static void plot(struct BitMap *bm, int x, int y, int v, unsigned mask)
{
    int p;
    if (x < 0 || y < 0 || x >= bm->BytesPerRow * 8 || y >= bm->Rows) return;
    for (p = 0; p < bm->Depth; p++) {
        UBYTE *b;
        if (!bm->Planes[p] || !(mask & (1u << p))) continue;
        b = &bm->Planes[p][y * bm->BytesPerRow + (x >> 3)];
        if (v & (1 << p)) *b |= (UBYTE)(0x80 >> (x & 7)); else *b &= (UBYTE)~(0x80 >> (x & 7));
    }
}
/* ---- hardware sprites: their lists, drawn over the picture ---- */
enum { V0 = 0x2c, H0 = 0x80 };                        /* (where a PAL screen's corner is) */
static struct SimpleSprite *sprites[8];
static int sprite_base = 16;
WORD GetSprite(struct SimpleSprite *s, WORD num)
{
    if (num < 0 || num > 7 || sprites[num]) return -1;
    sprites[num] = s; s->num = (UWORD)num; s->posctldata = NULL;
    return num;
}
void FreeSprite(WORD num) { if (num >= 0 && num < 8) sprites[num] = NULL; }
void MoveSprite(struct ViewPort *vp, struct SimpleSprite *s, WORD x, WORD y)
{
    int vstart = V0 + y, vstop = vstart + s->height, hstart = H0 + x;
    (void)vp;
    s->x = (UWORD)x; s->y = (UWORD)y;
    if (!s->posctldata) return;
    s->posctldata[0] = (UWORD)((vstart & 255) << 8 | (hstart >> 1 & 255));
    s->posctldata[1] = (UWORD)((vstop & 255) << 8 | (vstart >> 8 & 1) << 2 | (vstop >> 8 & 1) << 1 | (hstart & 1));
}
void ChangeSprite(struct ViewPort *vp, struct SimpleSprite *s, void *data)
{ s->posctldata = (UWORD *)data; MoveSprite(vp, s, (WORD)s->x, (WORD)s->y); }
ULONG VideoControl(struct ColorMap *cm, struct TagItem *tags)
{
    (void)cm;
    for (; tags->ti_Tag; tags++) if (tags->ti_Tag == VTAG_SPEVEN_BASE_SET) sprite_base = (int)tags->ti_Data;
    return 0;
}
LONG MakeScreen(struct Screen *s) { (void)s; return 0; }
LONG RethinkDisplay(void) { return 0; }
static void draw_sprites(uint32_t *pic, int w, int h)
{
    int n;
    for (n = 7; n >= 0; n--) {                        /* (sprite 0 in front: drawn last) */
        const UWORD *d;
        int guard = 0;
        if (!sprites[n] || !(d = sprites[n]->posctldata)) continue;
        while ((d[0] || d[1]) && guard++ < 512) {
            int vstart = (d[0] >> 8) | (d[1] & 4) << 6, vstop = (d[1] >> 8) | (d[1] & 2) << 7;
            int hstart = (d[0] & 255) << 1 | (d[1] & 1), line, bit;
            d += 2;
            for (line = 0; line < vstop - vstart; line++, d += 2)
                for (bit = 0; bit < 16; bit++) {
                    int c = (d[0] >> (15 - bit) & 1) | (d[1] >> (15 - bit) & 1) << 1;
                    int x = hstart - H0 + bit, y = vstart - V0 + line;
                    if (c && x >= 0 && x < w && y >= 0 && y < h) pic[y * w + x] = palette[sprite_base + (n / 2) * 4 + c];
                }
        }
    }
}

void shim_dump(const char *path)
{
    FILE *f = fopen(path, "wb");
    int x, y, w = 320, h = 256;
    if (!f || !shown) { if (f) fclose(f); return; }
    static uint32_t pic[320 * 256];
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) pic[y * w + x] = palette[pixel(shown, x, y)];
    if (!getenv("OFP_NO_SPRITES")) draw_sprites(pic, w, h);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            uint32_t c = pic[y * w + x];
            fputc((int)(c >> 16) & 255, f); fputc((int)(c >> 8) & 255, f); fputc((int)c & 255, f);
        }
    fclose(f);
}
static void maybe_finish(void)
{
    const char *ms = getenv("OFP_SHOT_MS");
    uint32_t limit = ms ? (uint32_t)atoi(ms) : 3000;
    if (now_ms() - start_ms >= limit) {
        const char *path = getenv("OFP_SHOT");
        shim_dump(path ? path : "amiga-preview.ppm");
        exit(0);
    }
}

/* ---- graphics ---- */
struct BitMap *AllocBitMap(ULONG w, ULONG h, ULONG depth, ULONG flags, struct BitMap *fr)
{
    struct BitMap *bm = calloc(1, sizeof *bm);
    ULONG p;
    (void)flags; (void)fr;
    InitBitMap(bm, (LONG)depth, (LONG)w, (LONG)h);
    for (p = 0; p < depth; p++) bm->Planes[p] = calloc(1, (size_t)bm->BytesPerRow * h);
    return bm;
}
void FreeBitMap(struct BitMap *bm) { int p; if (!bm) return; for (p = 0; p < bm->Depth; p++) free(bm->Planes[p]); free(bm); }
void InitBitMap(struct BitMap *bm, LONG depth, LONG w, LONG h)
{
    memset(bm, 0, sizeof *bm);
    bm->Depth = (UBYTE)depth; bm->BytesPerRow = (UWORD)(((w + 15) >> 4) << 1); bm->Rows = (UWORD)h;
}
ULONG GetBitMapAttr(struct BitMap *bm, ULONG a)
{ return a == BMA_DEPTH ? bm->Depth : a == BMA_WIDTH ? bm->BytesPerRow * 8u : a == BMA_HEIGHT ? bm->Rows : 0; }
void InitRastPort(struct RastPort *rp) { memset(rp, 0, sizeof *rp); rp->FgPen = 1; rp->Mask = (UBYTE)0xff; rp->DrawMode = JAM2; }

/* minterm, per pixel and plane: A the mask (all set when there's none), B the source, C the destination */
static int mt(ULONG minterm, int a, int b, int c) { return (int)(minterm >> (a << 2 | b << 1 | c)) & 1; }
LONG BltBitMap(struct BitMap *s, LONG sx, LONG sy, struct BitMap *d, LONG dx, LONG dy, LONG w, LONG h,
               ULONG minterm, ULONG mask, PLANEPTR temp)
{
    static int *buf;
    static size_t cap;
    LONG x, y;
    int p;
    (void)temp;
    if (w <= 0 || h <= 0) return 0;
    if ((size_t)(w * h) > cap) { cap = (size_t)(w * h); buf = realloc(buf, cap * sizeof *buf); }
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) buf[y * w + x] = pixel(s, sx + x, sy + y);   /* (overlap: read first) */
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int sv = buf[y * w + x], dv = pixel(d, dx + x, dy + y), out = 0;
            for (p = 0; p < 8; p++) out |= mt(minterm, 1, (sv >> p) & 1, (dv >> p) & 1) << p;
            plot(d, dx + x, dy + y, out, (unsigned)mask);
        }
    return 0;
}
void BltBitMapRastPort(struct BitMap *s, LONG sx, LONG sy, struct RastPort *rp, LONG dx, LONG dy, LONG w, LONG h, ULONG m)
{ BltBitMap(s, sx, sy, rp->BitMap, dx, dy, w, h, m, rp->Mask, NULL); }
void BltMaskBitMapRastPort(struct BitMap *s, LONG sx, LONG sy, struct RastPort *rp, LONG dx, LONG dy, LONG w, LONG h,
                           ULONG minterm, PLANEPTR mask)
{   /* (the autodoc's two: 0xe0 copies the source through the mask, 0x20 its inverse) */
    LONG x, y;
    int bpr = s->BytesPerRow;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int mx = sx + x, my = sy + y, sv;
            if (!(mask[my * bpr + (mx >> 3)] & (0x80 >> (mx & 7)))) continue;
            sv = pixel(s, mx, my);
            if (minterm == 0x20) sv = ~sv & ((1 << rp->BitMap->Depth) - 1);
            plot(rp->BitMap, dx + x, dy + y, sv, rp->Mask);
        }
}
void BltTemplate(const void *src, LONG srcx, LONG modulo, struct RastPort *rp, LONG dx, LONG dy, LONG w, LONG h)
{
    const UBYTE *t = src;
    LONG x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int bx = srcx + x, on = (t[y * modulo + (bx >> 3)] >> (7 - (bx & 7))) & 1;
            if (on) plot(rp->BitMap, dx + x, dy + y, (UBYTE)rp->FgPen, rp->Mask);
            else if (rp->DrawMode & JAM2) plot(rp->BitMap, dx + x, dy + y, (UBYTE)rp->BgPen, rp->Mask);
        }
}
void RectFill(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1)
{
    LONG x, y;
    for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++) {
        if (rp->DrawMode & COMPLEMENT) plot(rp->BitMap, x, y, pixel(rp->BitMap, x, y) ^ 0xff, rp->Mask);
        else plot(rp->BitMap, x, y, (UBYTE)rp->FgPen, rp->Mask);
    }
}
LONG WritePixel(struct RastPort *rp, LONG x, LONG y) { plot(rp->BitMap, x, y, (UBYTE)rp->FgPen, rp->Mask); return 0; }
void SetAPen(struct RastPort *rp, ULONG pen) { rp->FgPen = (BYTE)pen; }
void SetBPen(struct RastPort *rp, ULONG pen) { rp->BgPen = (BYTE)pen; }
void SetDrMd(struct RastPort *rp, ULONG m) { rp->DrawMode = (BYTE)m; }
void Draw(struct RastPort *rp, LONG x, LONG y)
{
    int x0 = rp->cp_x, y0 = rp->cp_y;
    int dx = abs((int)x - x0), sx = x0 < x ? 1 : -1;
    int dy = -abs((int)y - y0), sy = y0 < y ? 1 : -1, err = dx + dy;
    for (;;) {
        int twice;
        WritePixel(rp, x0, y0);
        if (x0 == x && y0 == y) break;
        twice = 2 * err;
        if (twice >= dy) { err += dy; x0 += sx; }
        if (twice <= dx) { err += dx; y0 += sy; }
    }
    rp->cp_x = (WORD)x; rp->cp_y = (WORD)y;
}
void Move(struct RastPort *rp, LONG x, LONG y) { rp->cp_x = (WORD)x; rp->cp_y = (WORD)y; }
LONG SetFont(struct RastPort *rp, struct TextFont *f) { rp->Font = f; return 0; }
LONG Text(struct RastPort *rp, CONST_STRPTR s, ULONG n)
{   /* (topaz 8 stood in for by font8x8: the baseline 6 down) */
    ULONG i;
    int row, col, top = rp->cp_y - 6;
    for (i = 0; i < n; i++, rp->cp_x += 8) {
        const unsigned char *g = (const unsigned char *)font8x8_basic[s[i] & 127];
        for (row = 0; row < 8; row++) for (col = 0; col < 8; col++) {
            if (g[row] & (1 << col)) plot(rp->BitMap, rp->cp_x + col, top + row, (UBYTE)rp->FgPen, rp->Mask);
            else if (rp->DrawMode & JAM2) plot(rp->BitMap, rp->cp_x + col, top + row, (UBYTE)rp->BgPen, rp->Mask);
        }
    }
    return 0;
}
void WaitBlit(void) {}
void WaitTOF(void) { maybe_finish(); }
void LoadRGB4(struct ViewPort *vp, const UWORD *c, LONG n)
{
    LONG i;
    (void)vp;
    for (i = 0; i < n && i < 256; i++)
        palette[i] = (uint32_t)(((c[i] >> 8) & 15) * 17) << 16 | (uint32_t)(((c[i] >> 4) & 15) * 17) << 8 | (uint32_t)((c[i] & 15) * 17);
}
void LoadRGB32(struct ViewPort *vp, const ULONG *t)
{
    (void)vp;
    while (*t) {
        ULONG n = *t >> 16, first = *t & 0xffff, i;
        t++;
        for (i = 0; i < n; i++, t += 3)
            if (first + i < 256) palette[first + i] = (t[0] >> 24) << 16 | (t[1] >> 24) << 8 | (t[2] >> 24);
    }
}
void SetRGB32(struct ViewPort *vp, ULONG n, ULONG r, ULONG g, ULONG b)
{ (void)vp; if (n < 256) palette[n] = (r >> 24) << 16 | (g >> 24) << 8 | (b >> 24); }

/* ---- intuition ---- */
/* (each tag and value read as a whole word: on the desktop a pointer is wider than a ULONG) */
static uintptr_t tag(va_list ap, ULONG want, uintptr_t dflt)
{
    for (;;) {
        ULONG t = (ULONG)va_arg(ap, uintptr_t);
        uintptr_t v;
        if (t == TAG_DONE) return dflt;
        v = va_arg(ap, uintptr_t);
        if (t == want) return v;
    }
}
#define TAGVAL(first, want, dflt, out) do { va_list ap; va_start(ap, first); out = tag(ap, want, dflt); va_end(ap); } while (0)
static struct TextFont topaz = { { { 0 } }, 8, 0, 0, 8, 6 };
struct Screen *OpenScreenTags(void *ns, ...)
{
    struct Screen *s = calloc(1, sizeof *s);
    uintptr_t w, h, d;
    TAGVAL(ns, SA_Width, 320, w); TAGVAL(ns, SA_Height, 256, h); TAGVAL(ns, SA_Depth, 4, d);
    w &= 0xffffffffu; h &= 0xffffffffu; d &= 0xffffffffu;
    s->Width = (WORD)w; s->Height = (WORD)h;
    s->bm = AllocBitMap((ULONG)w, (ULONG)h, (ULONG)d, BMF_CLEAR, NULL);
    InitRastPort(&s->RastPort);
    s->RastPort.BitMap = s->bm; s->RastPort.Font = &topaz;
    shown = s->bm;
    return s;
}
void CloseScreen(struct Screen *s) { if (s) { FreeBitMap(s->bm); free(s); } }
struct Window *OpenWindowTags(void *nw, ...)
{
    struct Window *w = calloc(1, sizeof *w);
    uintptr_t scr;
    TAGVAL(nw, WA_CustomScreen, 0, scr);
    w->WScreen = (struct Screen *)(uintptr_t)scr;
    w->RPort = &w->WScreen->RastPort;
    w->UserPort = CreateMsgPort();
    return w;
}
void CloseWindow(struct Window *w) { if (w) { free(w->UserPort); free(w); } }
struct ScreenBuffer *AllocScreenBuffer(struct Screen *s, struct BitMap *bm, ULONG flags)
{
    struct ScreenBuffer *sb = calloc(1, sizeof *sb);
    (void)bm;
    sb->sb_BitMap = (flags & SB_SCREEN_BITMAP) ? s->bm : AllocBitMap(s->bm->BytesPerRow * 8u, s->bm->Rows, s->bm->Depth, BMF_CLEAR, NULL);
    sb->sb_DBufInfo = calloc(1, sizeof *sb->sb_DBufInfo);
    return sb;
}
void FreeScreenBuffer(struct Screen *s, struct ScreenBuffer *sb) { (void)s; if (sb) { free(sb->sb_DBufInfo); free(sb); } }
ULONG ChangeScreenBuffer(struct Screen *s, struct ScreenBuffer *sb)
{
    (void)s;
    shown = sb->sb_BitMap;
    put_msg(sb->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort, &sb->sb_DBufInfo->dbi_SafeMessage);
    put_msg(sb->sb_DBufInfo->dbi_DispMessage.mn_ReplyPort, &sb->sb_DBufInfo->dbi_DispMessage);
    maybe_finish();
    return 1;
}
void SetPointer(struct Window *w, UWORD *p, LONG h, LONG wd, LONG x, LONG y) { (void)w; (void)p; (void)h; (void)wd; (void)x; (void)y; }
void ClearPointer(struct Window *w) { (void)w; }
void ReportMouse(LONG flag, struct Window *w) { (void)flag; (void)w; }
BOOL ModifyIDCMP(struct Window *w, ULONG f) { (void)w; (void)f; return TRUE; }
void ScreenToFront(struct Screen *s) { (void)s; }

/* scripted keys: the window's messages */
struct Message *GetMsg(struct MsgPort *p)
{
    static struct IntuiMessage im;
    static int next;
    struct Message *m;
    if (!p) return NULL;
    if (p->head) { m = p->head; p->head = NULL; return m; }
    {
        const char *keys = getenv("OFP_KEYS");
        int i = 0;
        maybe_finish();
        while (keys && *keys) {
            unsigned ms = (unsigned)strtoul(keys, (char **)&keys, 10), code, vanilla = 0;
            if (*keys == ':') keys++;
            if (*keys == 'v') { vanilla = 1; keys++; }   /* (v13: a character, as IDCMP_VANILLAKEY brings it) */
            code = (unsigned)strtoul(keys, (char **)&keys, 0);
            if (*keys == ',') keys++;
            if (i++ == next && now_ms() - start_ms >= ms) {
                next++;
                memset(&im, 0, sizeof im);
                im.Class = vanilla ? IDCMP_VANILLAKEY : IDCMP_RAWKEY; im.Code = (UWORD)code;
                return &im.ExecMessage;
            }
        }
    }
    return NULL;
}

/* OFP_JOY="ms:bits,ms:bits": port 1 from then on (bits: the JPF_ ones, e.g. 0x20000 PLAY); a CD32 pad */
ULONG ReadJoyPort(ULONG port)
{
    const char *s = getenv("OFP_JOY");
    ULONG state = 0;
    if (port != 1 || !s) return JP_TYPE_NOTAVAIL;
    while (*s) {
        unsigned ms = (unsigned)strtoul(s, (char **)&s, 10);
        ULONG bits;
        if (*s == ':') s++;
        bits = (ULONG)strtoul(s, (char **)&s, 0);
        if (*s == ',') s++;
        if (now_ms() - start_ms >= ms) state = bits;
    }
    return JP_TYPE_GAMECTLR | state;
}

/* ---- dos, keymap ---- */
BPTR Lock(CONST_STRPTR n, LONG m) { (void)n; (void)m; return 0; }
void UnLock(BPTR l) { (void)l; }
BPTR CreateDir(CONST_STRPTR n) { (void)n; return 0; }
LONG Examine(BPTR l, struct FileInfoBlock *f) { (void)l; (void)f; return 0; }
LONG ExNext(BPTR l, struct FileInfoBlock *f) { (void)l; (void)f; return 0; }
APTR AllocDosObject(ULONG t, void *tags) { (void)t; (void)tags; return calloc(1, sizeof(struct FileInfoBlock)); }
void FreeDosObject(ULONG t, APTR p) { (void)t; free(p); }
void Delay(LONG ticks) { struct timespec t = { 0, ticks * 20000000L }; nanosleep(&t, NULL); }
WORD MapRawKey(struct InputEvent *e, STRPTR buf, WORD len, void *km) { (void)e; (void)buf; (void)len; (void)km; return 0; }

/* ---- the sound: silence ---- */
int sound_open(void) { return -1; }
void sound_play(int id, int volume) { (void)id; (void)volume; }
void sound_engine(int step) { (void)step; }
void sound_close(void) {}

#ifdef OFP_AMIGA_ONLINE
/* ---- the online build's extras: no public list here ---- */
#include "platform/amiga/public_servers.h"
int amiga_public_worker(void) { return 0; }
int amiga_public_start(AmigaPublicList *list) { (void)list; return -1; }
int amiga_public_poll(AmigaPublicList *list) { (void)list; return -1; }
int amiga_share_worker(const char *p, const char *a) { (void)p; (void)a; return 20; }
int amiga_share_start(AmigaPublicList *job, const char *p, const char *a) { (void)job; (void)p; (void)a; return -1; }
int amiga_share_poll(AmigaPublicList *job) { (void)job; return 0; }
#endif

/* ---- the music (amiga_music.c's): which song would play, nothing heard ---- */
static int shim_song = -1;
void music_play(int song) { shim_song = song; }
int music_song(void) { return shim_song; }
void music_close(void) { shim_song = -1; }
void music_update(void) {}
int sound_share(int music) { (void)music; return 1; }

/* Preview-only replacements; native builds use the real timer and audio code. */
uint32_t amiga_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)((uint64_t)t.tv_sec * 1000000u + t.tv_nsec / 1000u);
}
void sound_powerup(int kind) { (void)kind; }
void sound_volume(int level) { (void)level; }
void music_volume(int level) { (void)level; }
#ifdef OFP_AMIGA_ONLINE
void amiga_worker_workbench(const struct WBStartup *startup) { (void)startup; }
#endif
