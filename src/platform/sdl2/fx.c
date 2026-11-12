/* SPDX-License-Identifier: MIT */
#include <math.h>
#include "common/fx_budget.h"
#include <stdlib.h>
#include "platform/sdl2/fx.h"
#include "platform/sdl2/settings.h"

#define MAX_PARTICLES 3000
#define MAX_DECALS    320
#define MAX_BURNS     48

enum { P_FLASH, P_RING, P_FIRE, P_SMOKE, P_SPARK, P_DEBRIS, P_EMBER };
static int explosion_style = EXPLOSIONS_FULL, fires_on = 1;
static int shake_on = 1;                             /* EFFECTS: FULL (shake and flashes) or CALM */

typedef struct {
    float x, y, z, vx, vy, vz;
    float life, max, size, grow, drag;
    SDL_Color c0, c1;
    unsigned char kind, alive;
} Particle;

typedef struct {
    float x, y, r, life, max;
    unsigned char shape, alive, splat;
} Decal;

typedef struct {                       /* a wreck that keeps burning after the blast */
    float x, y, life, max, size, acc;
    unsigned char alive;
} Burn;

static Particle parts[MAX_PARTICLES];
static Burn     burns[MAX_BURNS];
static int      next_burn;
static Decal    decals[MAX_DECALS];
/* run-over soldiers (kept apart from scorch marks) and tank tracks */
#define MAX_SPLATS 96
static Decal splats[MAX_SPLATS];
static int   next_splat;
#define MAX_TRACKS 2400
typedef struct { float x, y, life; signed char ox, oy; unsigned char alive, soft, ice; } Track;
static Track tracks[MAX_TRACKS];
static int   next_track, fx_biome, tracks_on = 1;
static float track_x[MAX_TANKS], track_y[MAX_TANKS];
#define TRACK_LIFE 30.0f

static int      next_part, next_decal;
static uint16_t seen[MAX_EFFECTS];      /* serial of the effect we last spawned per slot */
static float    shake;

static float frand(float a, float b) { return a + (b - a) * (float)rand() / (float)RAND_MAX; }

static SDL_Color col(int r, int g, int b, int a)
{
    SDL_Color c;
    c.r = (Uint8)r; c.g = (Uint8)g; c.b = (Uint8)b; c.a = (Uint8)a;
    return c;
}

void fx_reset(void)
{
    int i;
    for (i = 0; i < MAX_PARTICLES; i++) parts[i].alive = 0;
    for (i = 0; i < MAX_DECALS; i++) decals[i].alive = 0;
    for (i = 0; i < MAX_SPLATS; i++) splats[i].alive = 0;
    for (i = 0; i < MAX_TRACKS; i++) tracks[i].alive = 0;
    for (i = 0; i < MAX_TANKS; i++) track_x[i] = track_y[i] = -1000;
    for (i = 0; i < MAX_BURNS; i++) burns[i].alive = 0;
    for (i = 0; i < MAX_EFFECTS; i++) seen[i] = 0;
    shake = 0;
}

static Particle *emit(int kind, float x, float y, float life)
{
    static Particle discarded;
    static unsigned decoration;
    Particle *p;
    if (kind >= P_SMOKE && fx_budget_quality && (++decoration % (fx_budget_quality + 1))) {
        discarded.alive = 0; return &discarded;
    }
    p = &parts[next_part];
    next_part = (next_part + 1) % MAX_PARTICLES;       /* oldest goes first when full */
    p->kind = (unsigned char)kind;
    p->x = x; p->y = y; p->z = 0;
    p->vx = p->vy = p->vz = 0;
    p->life = p->max = life;
    p->size = 1; p->grow = 0; p->drag = 0;
    p->c0 = col(0, 0, 0, 0);
    p->alive = 1;
    return p;
}

static void scorch(float x, float y, float r, int shape)
{
    Decal *d = &decals[next_decal];
    next_decal = (next_decal + 1) % MAX_DECALS;
    d->x = x; d->y = y; d->r = r;
    d->life = d->max = 35.0f;                            /* seconds */
    d->shape = (unsigned char)shape;
    d->splat = 0;
    d->alive = 1;
}

/* run-over soldiers leave a stain that stays for minutes, as in the original:
 * kept apart from the scorch marks so explosions don't wipe them */

static void splat_decal(float x, float y)
{
    Decal *d = &splats[next_splat];
    next_splat = (next_splat + 1) % MAX_SPLATS;
    d->x = x; d->y = y; d->r = 3;
    d->life = d->max = 240.0f;
    d->shape = (unsigned char)(rand() & 255);
    d->splat = 1;
    d->alive = 1;
}

/* ---- track marks: two rows of dents behind every tank, fading away ------------- */

static void lay_tracks(const Game *g)
{
    int i;
    fx_biome = g->biome;
    for (i = 0; i < g->num_tanks; i++) {
        const Tank *t = &g->tanks[i];
        float x = (float)fx_to_int(t->x), y = (float)fx_to_int(t->y), dx = x - track_x[i], dy = y - track_y[i];
        TileType tt;
        if (!t->active || t->respawn) { track_x[i] = x; track_y[i] = y; continue; }
        if (dx * dx + dy * dy < 4.0f) continue;                  /* a dent every 2 px: a solid rut */
        if (dx * dx + dy * dy > 400.0f) { track_x[i] = x; track_y[i] = y; continue; }   /* teleported */
        track_x[i] = x; track_y[i] = y;
        tt = map_get(&g->map, (int)x >> TILE_SHIFT, (int)y >> TILE_SHIFT);
        if (tt != TILE_SAND && tt != TILE_STUMP && tt != TILE_TREE && tt != TILE_RUBBLE &&
            !(g->enhanced_rules && (tt == TILE_ICE || tt == TILE_OIL))) continue;  /* soft ground only */
        {
            Track *k = &tracks[next_track];
            float a = (float)t->angle * 9.5874e-5f;              /* angle16 -> radians */
            next_track = (next_track + 1) % MAX_TRACKS;
            k->x = x; k->y = y;
            k->ox = (signed char)(cosf(a) * 4.5f); k->oy = (signed char)(sinf(a) * 4.5f);   /* the two treads */
            k->life = TRACK_LIFE;
            k->soft = tt == TILE_SAND; k->ice = tt == TILE_ICE;
            k->alive = 1;
        }
    }
}

static void burst(float x, float y, float k, int with_scorch)
{
    int i, n;
    Particle *p;
    if (with_scorch) scorch(x, y, 5 + 7 * k, rand() & 3);
    if (explosion_style == EXPLOSIONS_OFF) return;
    if (explosion_style == EXPLOSIONS_SIMPLE) {
        for (i = 0; i < 3; i++) {
            p = emit(P_FIRE, x + (i - 1) * 2, y, 0.18f);
            p->size = 2 + k; p->grow = 8; p->vy = -6;
        }
        return;
    }
    if (shake_on) {                                      /* white-hot flash (EFFECTS: CALM, none: no sudden */
        p = emit(P_FLASH, x, y, 0.08f);                  /*  bright flashes, for eyes sensitive to them) */
        p->size = 6 * k + 3;
    }
    p = emit(P_RING, x, y, 0.28f);                       /* shock ring */
    p->size = 2; p->grow = 70 * k + 20;
    for (i = 0, n = (int)(10 * k) + 4; i < n; i++) {     /* many small fireballs */
        float a = frand(0, 6.283f), s = frand(15, 60) * k;
        p = emit(P_FIRE, x + frand(-3, 3) * k, y + frand(-3, 3) * k, frand(0.2f, 0.5f) * (0.8f + 0.2f * k));
        p->vx = cosf(a) * s; p->vy = sinf(a) * s - 6;
        p->size = frand(1.5f, 3.5f) * k + 1; p->grow = 4 * k; p->drag = 4;
    }
    for (i = 0, n = (int)(8 * k) + 3; i < n; i++) {      /* smoke that drifts and rises */
        float a = frand(0, 6.283f), s = frand(4, 18) * k;
        p = emit(P_SMOKE, x + frand(-4, 4) * k, y + frand(-4, 4) * k, frand(1.0f, 2.4f) * (0.7f + 0.3f * k));
        p->vx = cosf(a) * s + 3; p->vy = sinf(a) * s - 9;
        p->size = frand(1.5f, 3.5f) * k + 1.5f; p->grow = frand(2.5f, 5.0f) * k; p->drag = 1.6f;
        p->c0 = col(55 + rand() % 25, 50 + rand() % 20, 46 + rand() % 18, 170);
    }
    for (i = 0, n = (int)(14 * k) + 4; i < n; i++) {     /* sparks */
        float a = frand(0, 6.283f), s = frand(70, 230) * (0.6f + 0.4f * k);
        p = emit(P_SPARK, x, y, frand(0.15f, 0.45f));
        p->vx = cosf(a) * s; p->vy = sinf(a) * s; p->drag = 4;
    }
    for (i = 0, n = (int)(12 * k) + 2; i < n; i++) {     /* debris that flies, falls and bounces */
        static const SDL_Color deb[] = { { 90, 88, 84, 255 }, { 130, 126, 118, 255 },
                                         { 70, 50, 30, 255 }, { 30, 26, 22, 255 }, { 160, 150, 130, 255 } };
        float a = frand(0, 6.283f), s = frand(30, 130) * (0.6f + 0.4f * k);
        p = emit(P_DEBRIS, x, y, frand(1.2f, 2.5f));
        p->vx = cosf(a) * s; p->vy = sinf(a) * s;
        p->vz = frand(50, 170) * (0.6f + 0.4f * k);
        p->size = (float)(1 + rand() % 2); p->drag = 0.6f;
        p->c0 = deb[rand() % 5];
    }
    for (i = 0, n = (int)(6 * k); i < n; i++) {          /* embers floating up */
        p = emit(P_EMBER, x + frand(-6, 6) * k, y + frand(-6, 6) * k, frand(0.8f, 2.2f));
        p->vx = frand(-10, 10); p->vy = frand(-30, -10); p->drag = 0.5f;
    }
    shake += 1.5f * k * k;
}

/* something landed in the water: rings, spray and droplets, no scorch */
static void splash(float x, float y, float k)
{
    int i, n;
    Particle *p;
    for (i = 0; i < 3; i++) {                            /* spreading rings */
        p = emit(P_RING, x, y, 0.6f + 0.25f * (float)i);
        p->size = 1 + (float)i; p->grow = (16 + 10 * k) / (1 + 0.5f * (float)i);
        p->c0 = col(220, 235, 255, 255);
    }
    for (i = 0, n = (int)(10 * k) + 6; i < n; i++) {     /* spray thrown up, falling back */
        float a = frand(0, 6.283f), sp = frand(10, 50) * (0.6f + 0.4f * k);
        p = emit(P_DEBRIS, x, y, frand(0.4f, 0.9f));
        p->vx = cosf(a) * sp; p->vy = sinf(a) * sp; p->vz = frand(40, 120) * (0.6f + 0.4f * k);
        p->size = 1; p->drag = 1.5f;
        p->c0 = rand() & 1 ? col(230, 240, 255, 255) : col(150, 190, 230, 255);
    }
    p = emit(P_SMOKE, x, y, 0.6f);                       /* a puff of spray */
    p->size = 2 + 2 * k; p->grow = 8 * k; p->c0 = col(220, 230, 245, 120);
}

static int wet(const Game *g, float x, float y)
{
    return map_get(&g->map, (int)x >> TILE_SHIFT, (int)y >> TILE_SHIFT) == TILE_WATER;
}

static void spawn(const Game *g, const Effect *e)
{
    float x = (float)fx_to_int(e->x), y = (float)fx_to_int(e->y);
    if ((e->kind == EFF_PUFF || e->kind == EFF_HIT || e->kind == EFF_EXPLOSION) && wet(g, x, y)) {
        splash(x, y, e->kind == EFF_EXPLOSION ? 1.2f : 0.5f);
        return;
    }
    float dir = (float)e->dir * 6.2831853f / 65536.0f;      /* 0 = north, clockwise */
    float fx_ = sinf(dir), fy = -cosf(dir);
    float k = e->size ? (float)e->size / 100.0f : 1.0f;
    int i;
    Particle *p;
    switch (e->kind) {
    case EFF_BLAST: {
        float kk = 0.9f + 0.9f * (float)e->size / 255.0f;   /* 0.9 .. 1.8 */
        if (fires_on) {
            Burn *b = &burns[next_burn];                         /* it keeps burning a while */
            next_burn = (next_burn + 1) % MAX_BURNS;
            b->x = x; b->y = y; b->size = kk;
            b->life = b->max = frand(12, 25) * (0.6f + 0.4f * kk);
            b->acc = 0; b->alive = 1;
        }
        burst(x, y, kk, 1);
        for (i = 0; explosion_style == EXPLOSIONS_FULL && i < 3; i++) /* secondary pops */
            burst(x + frand(-10, 10) * kk, y + frand(-8, 8) * kk, 0.45f, 0);
        break;
    }
    case EFF_EXPLOSION:
        burst(x, y, 0.4f + 0.45f * (k > 1 ? 1 : k), 1);
        break;
    case EFF_HIT:
        for (i = 0; i < 6; i++) {
            float a = dir + 3.14159f + frand(-1.2f, 1.2f), s = frand(40, 120);
            p = emit(P_SPARK, x, y, frand(0.1f, 0.3f));
            p->vx = sinf(a) * s; p->vy = -cosf(a) * s; p->drag = 5;
        }
        p = emit(P_SMOKE, x, y, 0.7f);
        p->size = 2; p->grow = 5; p->vy = -8; p->c0 = col(120, 112, 100, 160);
        for (i = 0; i < 2; i++) {
            p = emit(P_DEBRIS, x, y, 0.8f);
            p->vx = frand(-50, 50); p->vy = frand(-50, 50); p->vz = frand(30, 80);
            p->size = 1; p->c0 = col(110, 104, 96, 255);
        }
        break;
    case EFF_SPARK:                                          /* shot rings off a tank */
        for (i = 0; i < 12; i++) {
            float a = dir + 3.14159f + frand(-1.6f, 1.6f), s = frand(60, 200);
            p = emit(P_SPARK, x, y, frand(0.15f, 0.4f));
            p->vx = sinf(a) * s; p->vy = -cosf(a) * s; p->drag = 4;
        }
        if (shake_on) {                                  /* (CALM: no flash; the sparks still show the hit) */
            p = emit(P_FLASH, x, y, 0.06f);
            p->size = 4 + 3 * k;
        }
        break;
    case EFF_MUZZLE:
        p = emit(P_FLASH, x, y, 0.05f);
        p->size = 2 + 3 * k;
        for (i = 0; i < 2 + (int)(2 * k); i++) {
            p = emit(P_SMOKE, x + fx_ * 2, y + fy * 2, frand(0.4f, 0.9f));
            p->vx = fx_ * frand(15, 40) + frand(-6, 6); p->vy = fy * frand(15, 40) + frand(-6, 6);
            p->size = 1.5f; p->grow = 4 + 3 * k; p->drag = 3;
            p->c0 = col(150, 145, 135, 150);
        }
        shake += 0.15f * k;
        break;
    case EFF_SPLAT:
        splat_decal(x, y);
        for (i = 0; i < 6; i++) {
            p = emit(P_DEBRIS, x, y, frand(0.3f, 0.6f));
            p->vx = frand(-40, 40); p->vy = frand(-40, 40); p->vz = frand(20, 50);
            p->size = 1; p->c0 = col(120, 10, 8, 255);
        }
        break;
    case EFF_CHEER:                                          /* a man safe home */
        for (i = 0; i < 5; i++) {
            p = emit(P_EMBER, x + frand(-8, 8), y + frand(-6, 6), frand(0.6f, 1.2f));
            p->vx = frand(-6, 6); p->vy = frand(-30, -15);
        }
        break;
    default:                                                 /* puff, rocket trail */
        p = emit(P_SMOKE, x, y, frand(0.5f, 1.1f));
        p->size = 1.5f; p->grow = 5; p->vx = frand(-5, 5); p->vy = frand(-12, -4); p->drag = 1;
        p->c0 = col(170, 165, 155, 140);
        break;
    }
}

void fx_update(const Game *g, float dt)
{
    int i;
    if (dt > 0.1f) dt = 0.1f;
    if (tracks_on) lay_tracks(g);
    for (i = 0; i < MAX_TRACKS; i++)
        if (tracks[i].alive && (tracks[i].life -= dt) <= 0) tracks[i].alive = 0;
    for (i = 0; i < MAX_SPLATS; i++)
        if (splats[i].alive && (splats[i].life -= dt) <= 0) splats[i].alive = 0;
    for (i = 0; i < MAX_EFFECTS; i++) {                      /* effects that just started */
        const Effect *e = &g->effects[i];
        if (!e->active || e->delay || seen[i] == e->serial) continue;
        seen[i] = e->serial;
        spawn(g, e);
    }
    for (i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &parts[i];
        float damp;
        if (!p->alive) continue;
        if ((p->life -= dt) <= 0) { p->alive = 0; continue; }
        damp = 1.0f / (1.0f + p->drag * dt);
        p->vx *= damp; p->vy *= damp;
        p->x += p->vx * dt; p->y += p->vy * dt;
        p->size += p->grow * dt;
        if (p->kind == P_DEBRIS) {                          /* height with gravity and bounces */
            p->vz -= 320 * dt;
            p->z += p->vz * dt;
            if (p->z < 0) {
                p->z = 0;
                p->vz = -p->vz * 0.35f;
                p->vx *= 0.6f; p->vy *= 0.6f;
            }
        }
    }
    for (i = 0; i < MAX_DECALS; i++)
        if (decals[i].alive && (decals[i].life -= dt) <= 0)
            decals[i].alive = 0;
    for (i = 0; fires_on && i < g->map.num_st; i++) {        /* wisps of smoke off smouldering remains */
        const Structure *st = &g->map.st[i];
        const StructInfo *si = &struct_info[st->type];
        float age;
        if (!st->smoulder) continue;
        age = (float)(uint16_t)(g->tick - st->aim) / (float)EMBER_TICKS;
        if (age >= 1 || frand(0, 1) > dt * 3.0f * (float)(si->w * si->h) * (1 - age)) continue;
        {
            Particle *p = emit(P_SMOKE, (float)(st->x * TILE_SIZE) + frand(2, (float)(si->w * TILE_SIZE - 2)),
                               (float)(st->y * TILE_SIZE) + frand(2, (float)(si->h * TILE_SIZE - 2)), frand(1.5f, 3.0f));
            p->vx = frand(2, 6); p->vy = frand(-10, -5);
            p->size = 1; p->grow = frand(2, 4); p->drag = 0.5f;
            p->c0 = col(70, 66, 60, (int)(110 * (1 - age)));
            if (frand(0, 1) < 0.25f) {                       /* a spark lifts off now and then */
                p = emit(P_EMBER, p->x, p->y, frand(0.5f, 1.2f));
                p->vx = frand(-6, 6); p->vy = frand(-25, -12);
            }
        }
    }
    for (i = 0; i < MAX_BURNS; i++) {                        /* flames and smoke off burning wrecks */
        Burn *b = &burns[i];
        float f;
        if (!b->alive) continue;
        if ((b->life -= dt) <= 0) { b->alive = 0; continue; }
        f = b->life / b->max;
        b->acc += dt * (6 + 10 * b->size) * f;
        while (b->acc >= 1) {
            Particle *p;
            b->acc -= 1;
            p = emit(P_FIRE, b->x + frand(-6, 6) * b->size, b->y + frand(-5, 5) * b->size, frand(0.25f, 0.5f));
            p->vy = frand(-14, -6); p->vx = frand(-3, 3);
            p->size = frand(1, 2.2f) * (0.5f + 0.5f * f) * b->size; p->grow = 1.5f; p->drag = 1;
            if (rand() % 3 == 0) {
                p = emit(P_SMOKE, b->x + frand(-4, 4), b->y - 2, frand(1.5f, 3.0f));
                p->vx = frand(2, 7); p->vy = frand(-14, -8);
                p->size = 1.5f; p->grow = frand(3, 5) * b->size; p->drag = 0.6f;
                p->c0 = col(40 + rand() % 20, 36 + rand() % 16, 32, 150);
            }
        }
    }
    shake *= expf(-9.0f * dt);
    if (shake > 6) shake = 6;
}

void fx_set_shake(int on) { shake_on = on; }

void fx_set_details(int explosions, int ground_fires)
{
    int i;
    if (explosion_style == explosions && fires_on == ground_fires) return;
    explosion_style = explosions;
    fires_on = ground_fires;
    for (i = 0; i < MAX_PARTICLES; i++) parts[i].alive = 0;
    if (!fires_on) for (i = 0; i < MAX_BURNS; i++) burns[i].alive = 0;
}

void fx_shake(int *dx, int *dy)
{
    if (!shake_on || shake < 0.3f) { *dx = *dy = 0; return; }   /* (EFFECTS: CALM, none) */
    *dx = (int)lroundf(frand(-shake, shake));
    *dy = (int)lroundf(frand(-shake, shake));
}

/* ---- drawing -------------------------------------------------------------------- */

static void disc(SDL_Renderer *r, float cx, float cy, float rad, SDL_Color c)
{
    enum { SEG = 12 };
    SDL_Vertex v[SEG + 1];
    int idx[SEG * 3], i;
    if (rad < 1.2f) {
        SDL_Rect px = { (int)cx, (int)cy, rad < 0.8f ? 1 : 2, rad < 0.8f ? 1 : 2 };
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_RenderFillRect(r, &px);
        return;
    }
    for (i = 0; i <= SEG; i++) {
        v[i].color = c;
        v[i].tex_coord.x = v[i].tex_coord.y = 0;
    }
    v[0].position.x = cx; v[0].position.y = cy;
    for (i = 0; i < SEG; i++) {
        float a = (float)i * 6.2831853f / SEG;
        v[i + 1].position.x = cx + cosf(a) * rad;
        v[i + 1].position.y = cy + sinf(a) * rad;
        idx[i * 3] = 0; idx[i * 3 + 1] = i + 1; idx[i * 3 + 2] = (i + 1) % SEG + 1;
    }
    SDL_RenderGeometry(r, NULL, v, SEG + 1, idx, SEG * 3);
}

static void ring(SDL_Renderer *r, float cx, float cy, float rad, SDL_Color c)
{
    enum { SEG = 20 };
    SDL_Vertex v[SEG * 2];
    int idx[SEG * 6], i;
    float w = rad > 6 ? 2.0f : 1.0f;
    for (i = 0; i < SEG; i++) {
        float a = (float)i * 6.2831853f / SEG, ca = cosf(a), sa = sinf(a);
        v[i * 2].position.x = cx + ca * rad;       v[i * 2].position.y = cy + sa * rad;
        v[i * 2 + 1].position.x = cx + ca * (rad - w); v[i * 2 + 1].position.y = cy + sa * (rad - w);
        v[i * 2].color = v[i * 2 + 1].color = c;
        v[i * 2].tex_coord.x = v[i * 2].tex_coord.y = v[i * 2 + 1].tex_coord.x = v[i * 2 + 1].tex_coord.y = 0;
    }
    for (i = 0; i < SEG; i++) {
        int a0 = i * 2, a1 = i * 2 + 1, b0 = ((i + 1) % SEG) * 2, b1 = b0 + 1;
        idx[i * 6] = a0; idx[i * 6 + 1] = b0; idx[i * 6 + 2] = a1;
        idx[i * 6 + 3] = a1; idx[i * 6 + 4] = b0; idx[i * 6 + 5] = b1;
    }
    SDL_RenderGeometry(r, NULL, v, SEG * 2, idx, SEG * 6);
}

void fx_draw_ground(SDL_Renderer *r, int cam_x, int cam_y)
{
    int i;
    /* track marks: darker dents in sand and earth, blue-grey ruts in snow */
    static const Uint8 tint[BIOME_COUNT][3] = { { 90, 70, 40 }, { 110, 60, 30 }, { 30, 26, 14 }, { 110, 125, 150 }, { 20, 26, 36 } };
    const Uint8 *tc = tint[fx_biome < BIOME_COUNT ? fx_biome : 0];
    for (i = 0; i < MAX_TRACKS; i++) {
        const Track *k = &tracks[i];
        float x, y, f;
        SDL_Rect q;
        if (!k->alive) continue;
        x = k->x - (float)cam_x; y = k->y - (float)cam_y;
        if (x < -8 || y < -8 || x > 500 || y > 300) continue;
        f = k->life / TRACK_LIFE;
        SDL_SetRenderDrawColor(r, k->ice ? 210 : tc[0], k->ice ? 230 : tc[1], k->ice ? 245 : tc[2], (Uint8)((k->soft ? 150 : 90) * (f < 0.5f ? f * 2 : 1)));
        q.w = q.h = 2;
        q.x = (int)(x + k->ox) - 1; q.y = (int)(y + k->oy) - 1; SDL_RenderFillRect(r, &q);
        q.x = (int)(x - k->ox) - 1; q.y = (int)(y - k->oy) - 1; SDL_RenderFillRect(r, &q);
    }
    for (i = 0; i < MAX_SPLATS; i++) {                       /* squashed soldiers, drying slowly */
        const Decal *d = &splats[i];
        float a, x, y, dry;
        int k;
        if (!d->alive) continue;
        x = d->x - (float)cam_x; y = d->y - (float)cam_y;
        if (x < -10 || y < -10 || x > 500 || y > 300) continue;
        a = d->life / d->max;
        dry = 1.0f - a;                                      /* red at first, brown later */
        a = a > 0.2f ? 1.0f : a / 0.2f;
        for (k = 0; k < 7; k++) {
            float ox = (float)(((d->shape >> k) & 3) - 1.5f) * 2.2f, oy = (float)(((d->shape >> ((k + 3) & 7)) & 3) - 1.5f) * 2.0f;
            disc(r, x + ox, y + oy, 1.3f + (float)(k & 1) * 0.6f,
                 col((int)(130 - 60 * dry), (int)(14 + 24 * dry), (int)(10 + 10 * dry), (int)(220 * a)));
        }
        disc(r, x, y, 2.0f, col((int)(80 - 30 * dry), (int)(8 + 14 * dry), 6, (int)(230 * a)));
    }
    for (i = 0; i < MAX_DECALS; i++) {
        const Decal *d = &decals[i];
        float a, x, y;
        if (!d->alive) continue;
        a = d->life / d->max;
        a = a > 0.3f ? 1.0f : a / 0.3f;                      /* fade out at the end */
        x = d->x - (float)cam_x; y = d->y - (float)cam_y;
        if (x < -40 || y < -40 || x > 500 || y > 300) continue;
        if (d->splat) {                                      /* squashed soldier */
            int k;
            for (k = 0; k < 5; k++) {
                float ox = (float)(((d->shape >> k) & 3) - 1.5f) * 1.6f, oy = (float)(((d->shape >> (k + 2)) & 3) - 1.5f) * 1.6f;
                disc(r, x + ox, y + oy, 1.4f, col(110, 12, 10, (int)(220 * a)));
            }
            disc(r, x, y, 1.6f, col(70, 8, 6, (int)(230 * a)));
            continue;
        }
        disc(r, x, y, d->r, col(20, 16, 12, (int)(70 * a)));
        disc(r, x + (float)(d->shape & 1) * 2 - 1, y + (float)(d->shape >> 1) * 2 - 1, d->r * 0.65f,
             col(10, 8, 6, (int)(90 * a)));
        disc(r, x, y, d->r * 0.3f, col(0, 0, 0, (int)(70 * a)));
    }
    for (i = 0; i < MAX_PARTICLES; i++) {                    /* shadows of flying debris */
        const Particle *p = &parts[i];
        if (!p->alive || p->kind != P_DEBRIS || p->z < 1) continue;
        disc(r, p->x - (float)cam_x + 1, p->y - (float)cam_y + 1, 0.9f, col(0, 0, 0, 90));
    }
}

void fx_draw_air(SDL_Renderer *r, int cam_x, int cam_y)
{
    int i, pass;
    /* smoke first, then fire and sparks on top */
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < MAX_PARTICLES; i++) {
            const Particle *p = &parts[i];
            float t, x, y;
            if (!p->alive) continue;
            if ((pass == 0) != (p->kind == P_SMOKE)) continue;
            t = 1.0f - p->life / p->max;                    /* 0 -> 1 over its life */
            x = p->x - (float)cam_x; y = p->y - (float)cam_y;
            if (x < -60 || y < -60 || x > 520 || y > 320) continue;
            switch (p->kind) {
            case P_FLASH:
                disc(r, x, y, p->size, col(255, 250, 220, (int)(230 * (1 - t))));
                break;
            case P_RING:
                if (p->c0.a)                                 /* water ring */
                    ring(r, x, y, p->size, col(p->c0.r, p->c0.g, p->c0.b, (int)(170 * (1 - t))));
                else
                    ring(r, x, y, p->size, col(255, 230, 180, (int)(150 * (1 - t))));
                break;
            case P_FIRE: {                                  /* yellow -> orange -> red -> soot */
                int rr = 255, gg = (int)(230 - 200 * t), bb = (int)(120 * (1 - t) * (1 - t));
                disc(r, x, y, p->size, col(rr, gg < 0 ? 0 : gg, bb, (int)(255 * (1 - t * t))));
                if (t < 0.4f)
                    disc(r, x, y, p->size * 0.5f, col(255, 250, 200, (int)(255 * (1 - t / 0.4f))));
                break;
            }
            case P_SMOKE:
                disc(r, x, y, p->size, col(p->c0.r, p->c0.g, p->c0.b, (int)(p->c0.a * (1 - t) * (t < 0.1f ? t * 10 : 1))));
                break;
            case P_SPARK: {
                SDL_SetRenderDrawColor(r, 255, (Uint8)(240 - 120 * t), (Uint8)(150 * (1 - t)), (Uint8)(255 * (1 - t)));
                SDL_RenderDrawLine(r, (int)x, (int)y, (int)(x - p->vx * 0.02f), (int)(y - p->vy * 0.02f));
                break;
            }
            case P_DEBRIS: {
                SDL_Rect q = { (int)x, (int)(y - p->z * 0.15f), (int)p->size, (int)p->size };
                SDL_SetRenderDrawColor(r, p->c0.r, p->c0.g, p->c0.b, 255);
                SDL_RenderFillRect(r, &q);
                break;
            }
            case P_EMBER: {
                SDL_Rect q = { (int)x, (int)y, 1, 1 };
                SDL_SetRenderDrawColor(r, 255, (Uint8)(160 - 100 * t), 40, (Uint8)(255 * (1 - t)));
                SDL_RenderFillRect(r, &q);
                break;
            }
            }
        }
}

void fx_lights(FxLightFn cb, void *ctx, int cam_x, int cam_y, int strength)
{
    int i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        const Particle *p = &parts[i];
        float t, x, y;
        if (!p->alive) continue;
        if (p->kind != P_FIRE && p->kind != P_FLASH && p->kind != P_EMBER) continue;
        t = 1.0f - p->life / p->max;
        x = p->x - (float)cam_x; y = p->y - (float)cam_y;
        if (x < -80 || y < -80 || x > 540 || y > 360) continue;
        if (p->kind == P_FLASH)
            cb(ctx, x, y, p->size * 6 + 10, 255, 240, 200, strength * (int)(255 * (1 - t)) / 255);
        else if (p->kind == P_FIRE)
            cb(ctx, x, y, p->size * 4 + 6, 255, 140, 50, strength * (int)(170 * (1 - t)) / 255);
        else
            cb(ctx, x, y, 3, 255, 120, 40, strength * (int)(120 * (1 - t)) / 255);
    }
    for (i = 0; i < MAX_BURNS; i++) {                        /* the flicker of a burning wreck */
        const Burn *b = &burns[i];
        float f, flick;
        if (!b->alive) continue;
        f = b->life / b->max;
        flick = 0.75f + 0.25f * sinf(b->life * 23.0f) * sinf(b->life * 7.0f + b->x);
        cb(ctx, b->x - (float)cam_x, b->y - (float)cam_y, (16 + 14 * b->size) * (0.6f + 0.4f * f),
           255, 150, 60, (int)((float)strength * 0.7f * f * flick));
    }
}

void fx_set_tracks(int on) { tracks_on = on; }
