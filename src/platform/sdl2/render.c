/* SPDX-License-Identifier: MIT
 * SDL2 renderer. All art comes from the active Theme (PNG sheets); this file
 * only decides which cell goes where. */
#include <math.h>
#include "common/fx_budget.h"
#include <stdio.h>
#include <stdlib.h>
#include "platform/sdl2/render.h"
#include "platform/sdl2/surface_art.h"
#include "platform/sdl2/ui.h"
#include "platform/sdl2/fx.h"


static SDL_Texture *radar_tex, *rocket_rack;
static int ground_fires = 1;
void render_set_ground_fires(int on) { ground_fires = on; }

static int free_cam, free_cam_x, free_cam_y;

void render_free_camera(int on, int x, int y) { free_cam = on; free_cam_x = x; free_cam_y = y; }
static SDL_Texture *glow_tex, *cone_tex, *light_map;    /* lighting, see further down */
static int light_w, light_h, night;
static SDL_Texture *make_light_texture(SDL_Renderer *r, int cone);

static SDL_Texture *make_rocket_rack(SDL_Renderer *r)
{
    static const char *const art[] = {
        "1111111111", "1232323231", "1232323231", "1454545451", "1454545451",
        "1454545451", "1454545451", "1454545451", "1454545451", "1454545451",
        "1454545451", "1454545451", "1422222241", "1111111111"
    };
    static const Uint32 palette[] = { 0, 0xff16181c, 0xff9c9d8b, 0xff393c37, 0xff5d6556, 0xffc7af65 };
    Uint32 pixels[32][32] = {{0}};
    SDL_Texture *tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 32, 32);
    int x, y;
    if (!tex) return NULL;
    for (y = 0; y < (int)(sizeof art / sizeof art[0]); y++)
        for (x = 0; x < 10; x++) pixels[5 + y][11 + x] = palette[art[y][x] - '0'];
    SDL_UpdateTexture(tex, NULL, pixels, sizeof pixels[0]);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
    return tex;
}

void render_tank_icon(SDL_Renderer *r, SDL_Texture *tanks, int pick, int team, int x, int y, int size)
{
    int cls = tank_chassis(pick);
    int span = size * (cls == TANK_HEAVY ? 10 : cls == TANK_MEDIUM ? 8 : 7) / 10;
    SDL_Rect src = { 0, (cls * 2 + (team & 1)) * THEME_TANK_CELL, THEME_TANK_CELL, THEME_TANK_CELL };
    SDL_Rect dst = { x - span / 2, y - span / 2, span, span };
    if (tanks) SDL_RenderCopy(r, tanks, &src, &dst);
    if (pick == TANK_RHINO && rocket_rack) SDL_RenderCopy(r, rocket_rack, NULL, &dst);
}

int render_init(SDL_Renderer *r)
{
    radar_tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                  RADAR_SIZE, RADAR_SIZE);
    if (!radar_tex) return -1;
    SDL_SetTextureScaleMode(radar_tex, SDL_ScaleModeNearest);
    rocket_rack = make_rocket_rack(r);
    glow_tex = make_light_texture(r, 0);
    cone_tex = make_light_texture(r, 1);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    return 0;
}

static void free_lights(void);

void render_shutdown(void)
{
    free_lights();
    if (radar_tex) SDL_DestroyTexture(radar_tex);
    if (rocket_rack) SDL_DestroyTexture(rocket_rack);
    radar_tex = NULL;
    rocket_rack = NULL;
}

/* ---- tiles ------------------------------------------------------------------ */

static uint32_t hash2(uint32_t x, uint32_t y)
{
    uint32_t h = x * 374761393u + y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

/* Which tiles join visually with which. 0 = no autotiling (random variant). */
int render_connect_group(int type)
{
    switch (type) {
    case TILE_ROAD:     return 1;
    case TILE_WATER:    return 2;
    case TILE_ICE:      return 7;
    case TILE_OIL:      return 8;
    case TILE_CRATER:   return 9;
    case TILE_WALL:     return 3;
    case TILE_BUILDING: return 4;
    case TILE_TREE:     return 5;     /* neighbouring bushes merge into clumps */
    case TILE_CANOPY:   return 5;     /* (the trees overhead: the bushes' look, merged) */
    case TILE_RUINS:    return 6;     /* a fallen building's footprint reads as one ruin */
    default:            return 0;
    }
}

static int tile_column(const Map *m, int tx, int ty)
{
    int type = m->type[ty][tx], grp = render_connect_group(type), mask = 0;
    if (!grp)
        return (int)(hash2((uint32_t)tx, (uint32_t)ty) & 15);
    /* outside the map counts as connected so edges don't show along the border */
    if (ty == 0         || render_connect_group(m->type[ty - 1][tx]) == grp) mask |= 1;   /* N */
    if (tx == MAP_W - 1 || render_connect_group(m->type[ty][tx + 1]) == grp) mask |= 2;   /* E */
    if (ty == MAP_H - 1 || render_connect_group(m->type[ty + 1][tx]) == grp) mask |= 4;   /* S */
    if (tx == 0         || render_connect_group(m->type[ty][tx - 1]) == grp) mask |= 8;   /* W */
    return mask;
}

static void bollard(SDL_Renderer *r, int x, int y)
{
    SDL_Rect q = { x + 1, y + 1, 4, 4 };
    SDL_SetRenderDrawColor(r, 0, 0, 0, 80);             /* shadow */
    SDL_RenderFillRect(r, &q);
    q.x = x; q.y = y;
    SDL_SetRenderDrawColor(r, 40, 40, 40, 255);
    SDL_RenderFillRect(r, &q);
    q.w = q.h = 3;
    SDL_SetRenderDrawColor(r, 120, 120, 120, 255);
    SDL_RenderFillRect(r, &q);
    SDL_SetRenderDrawColor(r, 170, 170, 170, 255);
    SDL_RenderDrawLine(r, x, y, x + 2, y);
    SDL_RenderDrawLine(r, x, y, x, y + 2);
}

static int sides_mode;                  /* 0 blue/red, 1 blue/orange with marks */

void render_set_sides(int mode) { sides_mode = mode ? 1 : 0; }
int  render_sides(void) { return sides_mode; }

static int style_teams;                 /* the world's own sides' colours (its tanks'), if it has them */
static Rgb style_team[2];
void render_set_style_teams(const Style *st)
{
    style_teams = st && st->has_teams == 3;
    if (style_teams) {
        int k;
        for (k = 0; k < 2; k++) style_team[k] = (Rgb){ st->team[k][0], st->team[k][1], st->team[k][2] };
    }
}

Rgb render_side_rgb(int team)
{
    if (team == TEAM_INDEPENDENT) return (Rgb){ 210, 95, 240 };
    if (!sides_mode && style_teams) return style_team[team == TEAM_ENEMY];   /* (the world's: its tanks' colours) */
    if (team == TEAM_ENEMY) return sides_mode ? (Rgb){ 255, 150, 20 } : (Rgb){ 230, 40, 30 };
    return sides_mode ? (Rgb){ 50, 140, 255 } : (Rgb){ 70, 130, 255 };
}

static void draw_flag(SDL_Renderer *r, int x, int y, int team, uint32_t tick)
{
    /* a proper standard that stands out on any ground: black-outlined
     * waving cloth, ours royal blue with a gold star, theirs red with a
     * white bar, on a pole with a gold knob */
    static const Uint8 ours[3] = { 40, 90, 230 }, ours_lit[3] = { 110, 160, 255 };
    static const Uint8 red[3] = { 215, 25, 20 }, red_lit[3] = { 255, 90, 70 };
    static const Uint8 orange[3] = { 235, 130, 10 }, orange_lit[3] = { 255, 185, 70 };
    const Uint8 *theirs = sides_mode ? orange : red, *theirs_lit = sides_mode ? orange_lit : red_lit;
    const Uint8 *c = team == TEAM_ENEMY ? theirs : ours, *cl = team == TEAM_ENEMY ? theirs_lit : ours_lit;
    Uint8 wc[3], wl[3];
    if (!sides_mode && style_teams) {                    /* the world's sides' colours: the cloth (and lit, a shade up) */
        Rgb sc = style_team[team == TEAM_ENEMY];
        wc[0] = (Uint8)(sc.r * 3 / 4); wc[1] = (Uint8)(sc.g * 3 / 4); wc[2] = (Uint8)(sc.b * 3 / 4);
        wl[0] = sc.r; wl[1] = sc.g; wl[2] = sc.b;
        c = wc; cl = wl;
    }
    int ph = (int)((tick / 6) & 3), col, top = y - 14;
    SDL_Rect q;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 90);                      /* shadow on the ground */
    SDL_RenderDrawLine(r, x + 1, y + 1, x + 10, y - 2);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);                     /* pole, outlined */
    q.x = x - 1; q.y = top - 1; q.w = 3; q.h = y - top + 2;
    SDL_RenderFillRect(r, &q);
    SDL_SetRenderDrawColor(r, 200, 196, 186, 255);
    SDL_RenderDrawLine(r, x, top, x, y);
    for (col = 0; col < 10; col++) {                             /* cloth, one rippling column at a time */
        int dy = ((col + ph) & 3) == 0 ? 1 : 0, h = 7 - col / 5;
        int cx = x + 1 + col, cy = top + dy + col / 6;
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderDrawLine(r, cx, cy - 1, cx, cy + h);
        SDL_SetRenderDrawColor(r, dy ? cl[0] : c[0], dy ? cl[1] : c[1], dy ? cl[2] : c[2], 255);
        SDL_RenderDrawLine(r, cx, cy, cx, cy + h - 1);
    }
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);                     /* the fly end */
    SDL_RenderDrawLine(r, x + 11, top + 1, x + 11, top + 6);
    if (team == TEAM_ENEMY) {                                    /* white bar */
        SDL_SetRenderDrawColor(r, 250, 250, 245, 255);
        SDL_RenderDrawLine(r, x + 2, top + 3 + (ph == 1), x + 9, top + 4 + (ph == 1));
    } else {                                                     /* gold star */
        SDL_SetRenderDrawColor(r, 255, 215, 40, 255);
        SDL_RenderDrawLine(r, x + 4, top + 3, x + 6, top + 3);
        SDL_RenderDrawLine(r, x + 5, top + 2, x + 5, top + 4);
        SDL_RenderDrawPoint(r, x + 4, top + 5); SDL_RenderDrawPoint(r, x + 6, top + 5);
    }
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);                     /* gold knob on top */
    q.x = x - 1; q.y = top - 3; q.w = 3; q.h = 3;
    SDL_RenderFillRect(r, &q);
    SDL_SetRenderDrawColor(r, 255, 210, 60, 255);
    SDL_RenderDrawPoint(r, x, top - 2);
}

/* a flag lying out in the open: a pulsing ring round it so you can't miss it */
static void flag_marker(SDL_Renderer *r, int x, int y, int team, uint32_t tick)
{
    int k, ring, rad0 = 6 + (int)(tick / 3 % 10);
    for (ring = 0; ring < 2; ring++) {                     /* a dark edge under a bright ring */
        int rad = rad0 + ring, a = 255 - (rad0 - 6) * 22;
        if (ring) SDL_SetRenderDrawColor(r, 0, 0, 0, (Uint8)(a / 2));
        else {
            Rgb sc = render_side_rgb(team);
            SDL_SetRenderDrawColor(r, sc.r, sc.g, sc.b, (Uint8)(a < 50 ? 50 : a));
        }
        for (k = 0; k < 36; k++) {
            float an = (float)k * 0.1745f;
            SDL_RenderDrawPoint(r, x + (int)(cosf(an) * (float)rad), y + (int)(sinf(an) * (float)rad * 0.6f));
        }
    }
}

static void draw_ring_px(SDL_Renderer *r, int cx, int cy, int rad, int alpha)
{
    int k;
    SDL_SetRenderDrawColor(r, 20, 20, 20, (Uint8)alpha);
    for (k = 0; k < 48; k++) {
        float a = (float)k * 6.2831853f / 48.0f;
        SDL_RenderDrawPoint(r, cx + (int)(cosf(a) * rad), cy + (int)(sinf(a) * rad));
    }
}

static int is_water(const Map *m, int tx, int ty)
{
    return tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H || m->type[ty][tx] == TILE_WATER;
}

/* Living water, drawn over the water tiles every frame: deeper colour away
 * from the shore, drifting wave crests, sun glints, and foam breathing
 * along the banks. Ice only glints; murky water moves slowly. */
static void water_fx(SDL_Renderer *r, const Map *m, int tx, int ty, int sx, int sy, float t, int kind)
{
    int k, deep = 1, dx, dy;
    uint32_t h;
    for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++)
            deep &= is_water(m, tx + dx, ty + dy);
    if (deep && kind != WATER_ICE) {
        SDL_Rect q = { sx, sy, TILE_SIZE, TILE_SIZE };
        SDL_SetRenderDrawColor(r, 0, 10, 30, kind == WATER_MURKY ? 40 : 55);
        SDL_RenderFillRect(r, &q);
    }
    if (kind != WATER_ICE) {                             /* wave crests drifting across */
        float speed = kind == WATER_MURKY ? 0.35f : 1.0f;
        for (k = 0; k < 3; k++) {
            int wy = (ty * TILE_SIZE + k * 6 + (int)(t * 5 * speed)) % 18;
            float ph = sinf((float)(ty * TILE_SIZE + wy) * 0.45f + t * 1.6f * speed + (float)tx * 1.3f);
            int x0 = (int)((float)(k * 5 + tx * 7) + t * 9 * speed + ph * 3) % 16;
            int len = 4 + (int)(ph * 2 + 2);
            if (wy >= TILE_SIZE) continue;
            SDL_SetRenderDrawColor(r, 210, 232, 255, kind == WATER_MURKY ? 60 : 120);
            if (x0 + len <= TILE_SIZE)
                SDL_RenderDrawLine(r, sx + x0, sy + wy, sx + x0 + len - 1, sy + wy);
            else {
                SDL_RenderDrawLine(r, sx + x0, sy + wy, sx + TILE_SIZE - 1, sy + wy);
                SDL_RenderDrawLine(r, sx, sy + wy, sx + x0 + len - 1 - TILE_SIZE, sy + wy);
            }
            SDL_SetRenderDrawColor(r, 0, 0, 20, kind == WATER_MURKY ? 40 : 70);   /* trough below */
            if (wy + 1 < TILE_SIZE && x0 + len <= TILE_SIZE)
                SDL_RenderDrawLine(r, sx + x0, sy + wy + 1, sx + x0 + len - 1, sy + wy + 1);
        }
    }
    h = hash2((uint32_t)tx * 7u + (uint32_t)(t * (kind == WATER_ICE ? 2.0f : 5.0f)), (uint32_t)ty);
    if ((h & 7) == 0) {                                  /* a glint of sun */
        int gx = sx + 2 + (int)((h >> 3) & 11), gy = sy + 2 + (int)((h >> 7) & 11);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 220);
        SDL_RenderDrawPoint(r, gx, gy);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 110);
        SDL_RenderDrawPoint(r, gx - 1, gy); SDL_RenderDrawPoint(r, gx + 1, gy);
        SDL_RenderDrawPoint(r, gx, gy - 1); SDL_RenderDrawPoint(r, gx, gy + 1);
    }
    for (k = 0; k < 4; k++) {                            /* foam along the banks, breathing */
        static const int ox[4] = { 0, 1, 0, -1 }, oy[4] = { -1, 0, 1, 0 };
        int i;
        if (is_water(m, tx + ox[k], ty + oy[k])) continue;
        for (i = 1; i < TILE_SIZE - 1; i += 2) {
            int wx = k == 1 ? TILE_SIZE - 3 : k == 3 ? 2 : i;
            int wy = k == 0 ? 2 : k == 2 ? TILE_SIZE - 3 : i;
            float a = sinf(t * (kind == WATER_ICE ? 0.8f : 2.6f) + (float)(tx * 16 + ty * 16 + i) * 0.6f);
            if (a < 0.2f) continue;
            SDL_SetRenderDrawColor(r, 235, 245, 255, (Uint8)(a * (kind == WATER_ICE ? 70 : 140)));
            SDL_RenderDrawPoint(r, sx + wx, sy + wy);
        }
    }
}

/* round off the square corners of lakes: where two sides of a water tile
 * face land, cover the corner with ground and draw a curved bank */
static void water_corners(SDL_Renderer *r, const Style *st, const Map *m, int tx, int ty, int sx, int sy)
{
    static const int cxs[4] = { 0, 1, 0, 1 }, cys[4] = { 0, 0, 1, 1 };
    int c, x, y;
    for (c = 0; c < 4; c++) {
        int ex = cxs[c] ? 1 : -1, ey = cys[c] ? 1 : -1;
        if (is_water(m, tx + ex, ty) || is_water(m, tx, ty + ey)) continue;   /* not a convex corner */
        for (y = 0; y < 6; y++)
            for (x = 0; x < 6; x++) {
                /* distance from the centre of the rounding circle (radius 5.5) */
                float dx = 5.5f - (float)x, dy = 5.5f - (float)y, d = sqrtf(dx * dx + dy * dy);
                int px = cxs[c] ? TILE_SIZE - 1 - x : x, py = cys[c] ? TILE_SIZE - 1 - y : y;
                SDL_Rect src = { px, TILE_SAND * TILE_SIZE + py, 1, 1 }, dst = { sx + px, sy + py, 1, 1 };
                if (d > 5.6f) SDL_RenderCopy(r, st->tiles, &src, &dst);      /* bank */
                else if (d > 4.6f) {                                         /* outline */
                    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
                    SDL_RenderDrawPoint(r, sx + px, sy + py);
                }
            }
    }
}

/* ---- light ---------------------------------------------------------------------- */


/* the map's time of day rules; the editor asks for plain daylight */
void render_set_night(int on) { night = on; }            /* 1 = always draw in daylight (editor) */
int  render_night(void) { return night; }

static SDL_Texture *make_light_texture(SDL_Renderer *r, int cone)
{
    enum { N = 64 };
    static Uint32 px[N * N];
    int x, y;
    SDL_Texture *t;
    for (y = 0; y < N; y++)
        for (x = 0; x < N; x++) {
            float a;
            if (!cone) {                                   /* round glow, soft falloff */
                float dx = (x - N / 2 + 0.5f) / (N / 2), dy = (y - N / 2 + 0.5f) / (N / 2);
                float d = sqrtf(dx * dx + dy * dy);
                a = d >= 1 ? 0 : (1 - d) * (1 - d);
            } else {                                       /* beam from the bottom centre, pointing up */
                float dx = (x - N / 2 + 0.5f), dy = (float)(N - 1 - y) + 0.5f;
                float d = sqrtf(dx * dx + dy * dy) / N, ang = fabsf(atan2f(dx, dy));
                float spread = 0.42f;
                a = (d >= 1 || ang > spread) ? 0 : (1 - d) * (1 - ang / spread) * (0.5f + 0.5f * (1 - d));
            }
            px[y * N + x] = ((Uint32)(a * 255) << 24) | 0xFFFFFFu;
        }
    t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, N, N);
    if (t) {
        SDL_UpdateTexture(t, NULL, px, N * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
    }
    return t;
}

static void glow(SDL_Renderer *r, float x, float y, float rad, int cr, int cg, int cb, int a)
{
    SDL_Rect d = { (int)(x - rad), (int)(y - rad), (int)(rad * 2), (int)(rad * 2) };
    if (!glow_tex || rad < 1 || a <= 0) return;
    SDL_SetTextureColorMod(glow_tex, (Uint8)cr, (Uint8)cg, (Uint8)cb);
    SDL_SetTextureAlphaMod(glow_tex, (Uint8)(a > 255 ? 255 : a));
    SDL_RenderCopy(r, glow_tex, NULL, &d);
}

static void beam(SDL_Renderer *r, float x, float y, float angle_deg, float len, int cr, int cg, int cb, int a)
{
    SDL_Rect d = { (int)(x - len * 0.45f), (int)(y - len), (int)(len * 0.9f), (int)len };
    SDL_Point c = { (int)(len * 0.45f), (int)len };
    if (!cone_tex) return;
    SDL_SetTextureColorMod(cone_tex, (Uint8)cr, (Uint8)cg, (Uint8)cb);
    SDL_SetTextureAlphaMod(cone_tex, (Uint8)a);
    SDL_RenderCopyEx(r, cone_tex, NULL, &d, angle_deg, &c, SDL_FLIP_NONE);
}

static void fx_light_cb(void *ctx, float x, float y, float rad, int cr, int cg, int cb, int a)
{
    glow((SDL_Renderer *)ctx, x, y, rad, cr, cg, cb, a);
}

/* every light in view: fires and flashes from the particles, shots, and at
 * night headlights, searchlights, lamps and helicopter spotlights */
static void emit_lights(SDL_Renderer *r, const Game *g, int cam_x, int cam_y, int alpha256, int at_night)
{
    float t = (float)SDL_GetTicks() / 1000.0f;
    int i;
    fx_lights(fx_light_cb, r, cam_x, cam_y, at_night ? 255 : 110);
    for (i = 0; i < MAX_SHOTS; i++) {
        const Shot *s = &g->shots[i];
        float x, y;
        if (!s->active) continue;
        x = (float)(fx_to_int(fx_lerp(s->prev_x, s->x, alpha256)) - cam_x);
        y = (float)(fx_to_int(fx_lerp(s->prev_y, s->y, alpha256)) - cam_y);
        if (s->kind == SHOT_ROCKET) glow(r, x, y, 14, 255, 170, 70, at_night ? 230 : 120);
        else if (s->kind != SHOT_MORTAR) glow(r, x, y, s->kind == SHOT_BULLET ? 5 : 8, 255, 200, 110, at_night ? 200 : 70);
    }
    for (i = 0; i < g->map.num_st; i++) {               /* the warm glow of smouldering remains */
        const Structure *st = &g->map.st[i];
        const StructInfo *si = &struct_info[st->type];
        float age, cx, cy, fl;
        if (!ground_fires || !st->smoulder) continue;
        age = (float)(uint16_t)(g->tick - st->aim) / (float)EMBER_TICKS;
        if (age >= 1) continue;
        cx = (float)(st->x * TILE_SIZE + si->w * TILE_SIZE / 2 - cam_x);
        cy = (float)(st->y * TILE_SIZE + si->h * TILE_SIZE / 2 - cam_y);
        if (cx < -80 || cy < -80 || cx > 500 || cy > 320) continue;
        fl = 0.85f + 0.15f * sinf(t * 5.0f + (float)i);
        glow(r, cx, cy, (float)(si->w + si->h) * 9.0f, 255, 110, 40,
             (int)((at_night ? 200.0f : 70.0f) * (1 - age) * (1 - age) * fl));
    }
    if (!at_night) return;
    if (g->biome == BIOME_SCIFI || g->biome == BIOME_VOLCANIC) {   /* plasma pools, energy lanes, lava: they light the dark */
        int tx, ty, x0 = cam_x / TILE_SIZE - 1, y0 = cam_y / TILE_SIZE - 1;
        for (ty = y0; ty < y0 + 16 && ty < MAP_H; ty++)
            for (tx = x0; tx < x0 + 22 && tx < MAP_W; tx++) {
                if (tx < 0 || ty < 0 || ((tx + ty) & 1)) continue;
                if (g->map.type[ty][tx] == TILE_WATER)
                    glow(r, (float)(tx * TILE_SIZE + 8 - cam_x), (float)(ty * TILE_SIZE + 8 - cam_y), 20, 200, 60, 255, 90);
                else if (g->map.type[ty][tx] == TILE_ROAD)
                    glow(r, (float)(tx * TILE_SIZE + 8 - cam_x), (float)(ty * TILE_SIZE + 8 - cam_y), 14, 60, 200, 255, 45);
            }
    }
    for (i = 0; i < 2 && !(g->extras & EXTRA_NOFLAGS); i++)   /* flags lying out glow so you can find them */
        if (g->flags[i].state == FLAG_GROUND)
            glow(r, (float)(fx_to_int(g->flags[i].x) - cam_x), (float)(fx_to_int(g->flags[i].y) - cam_y - 6), 16,
                 i ? 255 : 120, i ? 90 : 160, i ? 70 : 255, 200);
    for (i = 0; i < g->num_tanks; i++) {                 /* headlights */
        const Tank *tk = &g->tanks[i];
        float x, y, a;
        if (!tk->active || tk->respawn) continue;
        x = (float)(fx_to_int(fx_lerp(tk->prev_x, tk->x, alpha256)) - cam_x);
        y = (float)(fx_to_int(fx_lerp(tk->prev_y, tk->y, alpha256)) - cam_y);
        a = (float)tk->angle * 360.0f / 65536.0f;
        beam(r, x + sinf(a * 0.0174533f) * 6, y - cosf(a * 0.0174533f) * 6, a, 110, 255, 248, 220, 255);
        beam(r, x + sinf(a * 0.0174533f) * 6, y - cosf(a * 0.0174533f) * 6, a, 60, 255, 250, 230, 200);
        glow(r, x, y, 18, 120, 120, 110, 120);            /* a little spill round the tank */
    }
    for (i = 0; i < g->map.num_st; i++) {                /* the camps after dark */
        const Structure *st = &g->map.st[i];
        const StructInfo *si = &struct_info[st->type];
        float cx = (float)(st->x * TILE_SIZE + si->w * TILE_SIZE / 2 - cam_x);
        float cy = (float)(st->y * TILE_SIZE + si->h * TILE_SIZE / 2 - cam_y);
        if (st->hp == 0 && si->hp) continue;
        if (cx < -120 || cy < -120 || cx > 520 || cy > 340) continue;
        switch (st->type) {
        case ST_TOWER: {                                  /* sweeping searchlight */
            float sweep = t * 40.0f + (float)i * 97.0f;
            beam(r, cx, cy, sweep, 80, 255, 255, 230, 170);
            glow(r, cx, cy, 10, 255, 250, 210, 200);
            break;
        }
        case ST_HELIPAD: {
            int k, on = (int)(t * 2 + i) % 2;
            for (k = 0; k < 4; k++) {
                static const int ox[4] = { 0, 20, 0, -20 }, oy[4] = { -20, 0, 20, 0 };
                glow(r, cx + (float)ox[k], cy + (float)oy[k], 6, on ? 255 : 120, on ? 80 : 120, 60, 220);
            }
            break;
        }
        case ST_GARAGE:                                   /* lamp over the door */
            glow(r, cx, (float)((st->y + 3) * TILE_SIZE - cam_y), 26, 255, 210, 140, 200);
            break;
        case ST_BARRACKS: case ST_HQ: case ST_WAREHOUSE: case ST_FIRSTAID: case ST_POWCAMP:
        case ST_RADAR: case ST_POWER: case ST_DOME: case ST_FLAGHOUSE: case ST_BUNKER:
            glow(r, cx, cy + (float)(si->h * TILE_SIZE / 2), 20 + si->w * 4, 255, 200, 120, 150);
            break;
        case ST_DEPOT:
            glow(r, cx, cy, 14, 255, 80, 60, (int)(t * 3 + i) % 2 ? 200 : 60);   /* warning beacon */
            break;
        default:
            break;
        }
    }
    for (i = 0; i < MAX_HELIS; i++) {                    /* helicopter spotlight and nav lights */
        const Heli *h = &g->helis[i];
        float x, y;
        if (!h->active) continue;
        x = (float)(fx_to_int(fx_lerp(h->prev_x, h->x, alpha256)) - cam_x);
        y = (float)(fx_to_int(fx_lerp(h->prev_y, h->y, alpha256)) - cam_y);
        glow(r, x + 10 + sinf((float)h->angle * 9.587e-5f) * 18, y + 10 - cosf((float)h->angle * 9.587e-5f) * 18,
             22, 230, 240, 255, 200);
        glow(r, x, y, 5, 255, 40, 30, (int)(t * 4) % 2 ? 255 : 0);
    }
    for (i = 0; i < MAX_MINES; i++) {                    /* armed mines blink red */
        const Mine *mn = &g->mines[i];
        if (mn->active && !mn->arm && (g->tick / 10) % 2)
            glow(r, (float)(fx_to_int(mn->x) - cam_x), (float)(fx_to_int(mn->y) - cam_y), 5, 255, 30, 20, 160);
    }
}

static void draw_lights(SDL_Renderer *r, const Game *g, const Style *style, const SDL_Rect *view,
                        int cam_x, int cam_y, int alpha256)
{
    int tod = night ? TIME_DAY : g->map.time;           /* the map's time of day */
    if (style->faithful) return;                         /* the original had no lighting */
    if (tod == TIME_DAY) {                               /* daytime: fire and flashes just glow */
        emit_lights(r, g, cam_x, cam_y, alpha256, 0);
        return;
    }
    if (!light_map || light_w != view->w || light_h != view->h) {
        if (light_map) SDL_DestroyTexture(light_map);
        light_map = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, view->w, view->h);
        light_w = view->w; light_h = view->h;
        if (light_map) SDL_SetTextureBlendMode(light_map, SDL_BLENDMODE_MOD);
    }
    if (!light_map) return;
    {
        SDL_Texture *prev = SDL_GetRenderTarget(r);
        SDL_Rect clip = { 0, 0, view->w, view->h };
        SDL_SetRenderTarget(r, light_map);                /* darkness (or dusk), then add the lights */
        if (tod == TIME_DUSK) SDL_SetRenderDrawColor(r, 255, 198, 150, 255);   /* low, warm sun */
        else SDL_SetRenderDrawColor(r, 38, 44, 72, 255);
        SDL_RenderClear(r);
        emit_lights(r, g, cam_x, cam_y, alpha256, 1);
        if (prev) SDL_SetRenderTarget(r, prev);
        else ui_screen_target(r);                         /* (back to the screen, its scaling and all) */
        ui_viewport(r, view);
        SDL_RenderSetClipRect(r, &clip);
        SDL_RenderCopy(r, light_map, NULL, &clip);        /* multiply the scene by it */
        emit_lights(r, g, cam_x, cam_y, alpha256, 0);     /* and let fires bloom on top */
    }
}

/* glowing embers in the remains of a fallen building: they pulse, and
 * slowly go dark over a minute and a half */
static void embers(SDL_Renderer *r, const Game *g, const Structure *st, int idx, int x0, int y0, int w, int h)
{
    float age = (float)(uint16_t)(g->tick - st->aim) / (float)EMBER_TICKS, fade, t;
    int k, n = (w / TILE_SIZE) * (h / TILE_SIZE) * 7;
    if (age >= 1) return;
    fade = (1 - age) * (1 - age);
    t = (float)SDL_GetTicks() / 1000.0f;
    for (k = 0; k < n; k++) {
        uint32_t hh = hash2((uint32_t)idx * 131u + (uint32_t)k, (uint32_t)k * 7u + 3u);
        int ex = x0 + 2 + (int)(hh % (uint32_t)(w - 4)), ey = y0 + 2 + (int)((hh >> 12) % (uint32_t)(h - 4));
        float fl = 0.5f + 0.5f * sinf(t * (2.0f + (float)(hh & 7) * 0.6f) + (float)k);
        float b = fade * (0.35f + 0.65f * fl);
        if (b < 0.08f || (((hh >> 24) & 3) == 0 && age > 0.6f)) continue;   /* some go out early */
        SDL_SetRenderDrawColor(r, 255, (Uint8)(60 + 170 * b * b), (Uint8)(30 * b), (Uint8)(90 + 165 * b));
        SDL_RenderDrawPoint(r, ex, ey);
        if (b > 0.6f) {                                   /* the hottest ones are bigger */
            SDL_SetRenderDrawColor(r, 255, 120, 30, (Uint8)(120 * b));
            SDL_RenderDrawPoint(r, ex + 1, ey);
            SDL_RenderDrawPoint(r, ex, ey + 1);
        }
    }
}

static int turret_row(int type)
{
    switch (type) {
    case ST_T_MG:     return 0;
    case ST_T_CANNON: return 1;
    case ST_T_ROCKET: return 2;
    case ST_T_HEAVY:  return 3;
    case ST_T_TWIN:   return 4;
    case ST_T_MORTAR: return 5;
    default:          return -1;
    }
}

static void outlined_trail(SDL_Renderer *r, int x, int y, int ex, int ey, Rgb color)
{
    int horizontal = abs(x - ex) >= abs(y - ey);
    int dx = !horizontal, dy = horizontal;
    SDL_SetRenderDrawColor(r, 18, 15, 16, 255);
    SDL_RenderDrawLine(r, x - dx, y - dy, ex - dx, ey - dy);
    SDL_RenderDrawLine(r, x + dx, y + dy, ex + dx, ey + dy);
    SDL_SetRenderDrawColor(r, color.r, color.g, color.b, 255);
    SDL_RenderDrawLine(r, x, y, ex, ey);
}

static void draw_shot(SDL_Renderer *r, const Shot *s, int x, int y)
{
    float len = 1.0f / 65536.0f;
    int ex = x - (int)((float)s->vx * len * 1.5f), ey = y - (int)((float)s->vy * len * 1.5f);
    SDL_Rect q;
    switch (s->kind) {
    case SHOT_BULLET:                                   /* tracer */
        outlined_trail(r, x, y, ex, ey, (Rgb){ 255, 230, 120 });
        break;
    case SHOT_ROCKET: {                                 /* body and a flickering flame */
        int fx_ = x - (int)((float)s->vx * len * 2.5f), fy = y - (int)((float)s->vy * len * 2.5f);
        SDL_SetRenderDrawColor(r, 255, (Uint8)(150 + (s->ttl * 37) % 100), 40, 255);
        SDL_RenderDrawLine(r, ex, ey, fx_, fy);
        outlined_trail(r, x, y, ex, ey, (Rgb){ 255, 250, 216 });
        break;
    }
    case SHOT_MORTAR: {                                 /* shell high in its arc, shadow below */
        int tot = s->life ? s->life : 1, done = tot - s->ttl;
        int h = 4 * 18 * done * (tot - done) / (tot * tot);   /* up to 18 px high */
        q.x = x - 1; q.y = y - 1; q.w = q.h = 2;
        SDL_SetRenderDrawColor(r, 0, 0, 0, 100);
        SDL_RenderFillRect(r, &q);
        q.x = x - 1; q.y = y - 1 - h; q.w = q.h = 3;
        SDL_SetRenderDrawColor(r, 20, 20, 20, 255);
        SDL_RenderFillRect(r, &q);
        SDL_SetRenderDrawColor(r, 150, 150, 140, 255);
        SDL_RenderDrawPoint(r, x - 1, y - 1 - h);
        break;
    }
    case SHOT_HEAVY:
        q.x = x - 2; q.y = y - 2; q.w = q.h = 4;
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderFillRect(r, &q);
        q.x++; q.y++; q.w = q.h = 2;
        SDL_SetRenderDrawColor(r, 255, 200, 120, 255);
        SDL_RenderFillRect(r, &q);
        break;
    default: {                                          /* tank shell: dark slug, burning tail */
        int tx2 = x - (int)((float)s->vx * len * 2.0f), ty2 = y - (int)((float)s->vy * len * 2.0f);
        outlined_trail(r, x, y, tx2, ty2, (Rgb){ 255, 168, 63 });
        ui_fill(r, x - 1, y - 1, 3, 3, (Rgb){ 18, 15, 16 });
        ui_fill(r, x, y, 1, 1, (Rgb){ 255, 250, 216 });
        break;
    }
    }
}

/* battle marks, as in the original: what a fight leaves on the ground for the rest of the match. A
 * scorch where something blew up, debris round a building that went, small red marks where soldiers fell
 * (render_set_red_marks(0): not those). Only drawn: the rules don't know of them */
#define MAX_MARKS 600
static struct { int16_t x, y; uint8_t kind, size; } marks[MAX_MARKS];
static int nmarks, mark_next, red_marks = 1;
static uint16_t mark_seen[MAX_EFFECTS];
static uint32_t marks_tick;
static const void *marks_map;
void render_set_red_marks(int on) { red_marks = on; }
static void marks_update(const Game *g)
{
    int i;
    if (g->tick < marks_tick || marks_map != (const void *)&g->map) {   /* (a new match: a clean field) */
        nmarks = mark_next = 0;
        memset(mark_seen, 0, sizeof mark_seen);
        for (i = 0; i < MAX_EFFECTS; i++) mark_seen[i] = g->effects[i].serial;   /* (what's on now isn't new) */
    }
    marks_tick = g->tick; marks_map = &g->map;
    for (i = 0; i < MAX_EFFECTS; i++) {
        const Effect *e = &g->effects[i];
        if (!e->active || e->delay || e->serial == mark_seen[i]) continue;
        mark_seen[i] = e->serial;
        if (e->kind != EFF_EXPLOSION && e->kind != EFF_BLAST && e->kind != EFF_SPLAT) continue;
        marks[mark_next].x = (int16_t)fx_to_int(e->x); marks[mark_next].y = (int16_t)fx_to_int(e->y);
        marks[mark_next].kind = e->kind; marks[mark_next].size = e->size;
        mark_next = (mark_next + 1) % MAX_MARKS;
        if (nmarks < MAX_MARKS) nmarks++;
    }
}
static void marks_draw(SDL_Renderer *r, int cam_x, int cam_y, int vw, int vh)
{
    int i, k;
    for (i = 0; i < nmarks; i++) {
        int x = marks[i].x - cam_x, y = marks[i].y - cam_y;
        uint32_t h = hash2((uint32_t)marks[i].x, (uint32_t)marks[i].y);
        if (x < -40 || y < -40 || x > vw + 40 || y > vh + 40) continue;
        if (marks[i].kind == EFF_SPLAT) {                 /* a soldier fell here */
            if (!red_marks) continue;
            for (k = 0; k < 5; k++) {
                SDL_Rect q = { x + (int)((h >> (k * 3)) & 7) - 3, y + (int)((h >> (k * 3 + 2)) & 7) - 3, 1 + (k & 1), 1 + (k & 1) };
                SDL_SetRenderDrawColor(r, (Uint8)(120 + (k * 20)), 18, 14, 200);
                SDL_RenderFillRect(r, &q);
            }
            continue;
        }
        {   /* a scorch: a few dark, soft patches (a blast's bigger) */
            int big = marks[i].kind == EFF_BLAST, rad = big ? 11 + (marks[i].size >> 5) : 5;
            for (k = 0; k < (big ? 5 : 3); k++) {
                int ox = (int)((h >> (k * 4)) & 7) - 3, oy = (int)((h >> (k * 4 + 2)) & 7) - 3, rr = rad - k * 2;
                SDL_Rect q = { x + ox - rr, y + oy - rr * 3 / 4, rr * 2, rr * 3 / 2 };
                if (rr < 2) break;
                SDL_SetRenderDrawColor(r, 20, 16, 12, (Uint8)(big ? 46 : 36));
                SDL_RenderFillRect(r, &q);
            }
            if (big)                                      /* debris round it: dark chunks */
                for (k = 0; k < 10; k++) {
                    uint32_t hk = hash2(h, (uint32_t)k);
                    int dx = (int)(hk % 41) - 20, dy = (int)((hk >> 8) % 41) - 20, sz = 1 + (int)((hk >> 16) % 3);
                    SDL_Rect q = { x + dx, y + dy, sz, sz };
                    SDL_SetRenderDrawColor(r, (Uint8)(50 + (hk >> 20) % 40), (Uint8)(46 + (hk >> 22) % 30), 40, 230);
                    SDL_RenderFillRect(r, &q);
                }
        }
    }
}

/* the trees overhead: a crown of their own (as the original's: round, leafy, a grove of them overlapping),
 * drawn once per world in its colours (dark, middle, light) */
#define CROWN 48
static SDL_Texture *crown_tex[BIOME_COUNT];
static SDL_Texture *crown_for(SDL_Renderer *r, int biome)
{
    static const Uint8 pal[BIOME_COUNT][3][3] = {
        [BIOME_CLASSIC]   = { { 22, 66, 20 },  { 58, 136, 40 }, { 128, 198, 70 } },
        [BIOME_DESERT]    = { { 30, 70, 22 },  { 72, 140, 44 }, { 150, 205, 80 } },
        [BIOME_JUNGLE]    = { { 14, 56, 18 },  { 40, 120, 36 }, { 110, 190, 60 } },
        [BIOME_WINTER]    = { { 18, 48, 34 },  { 46, 86, 60 },  { 214, 228, 230 } },
        [BIOME_SCIFI]     = { { 16, 56, 66 },  { 36, 128, 140 }, { 130, 226, 226 } },
        [BIOME_VOLCANIC]  = { { 36, 28, 24 },  { 78, 60, 40 },  { 150, 112, 60 } },
        [BIOME_WASTELAND] = { { 46, 52, 22 },  { 92, 98, 42 },  { 156, 156, 82 } },
        [BIOME_MOON]      = { { 56, 56, 68 },  { 108, 108, 124 }, { 194, 194, 208 } },
        [BIOME_SWAMP]     = { { 28, 46, 18 },  { 58, 88, 34 },  { 112, 142, 60 } },
    };
    static const int bx[5] = { 24, 15, 33, 18, 31 }, by[5] = { 22, 27, 27, 15, 16 }, br[5] = { 16, 12, 12, 11, 11 };
    SDL_Surface *sf;
    Uint32 *px;
    int x, y, k;
    if (biome < 0 || biome >= BIOME_COUNT) biome = 0;
    if (crown_tex[biome]) return crown_tex[biome];
    sf = SDL_CreateRGBSurfaceWithFormat(0, CROWN, CROWN, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!sf) return NULL;
    px = (Uint32 *)sf->pixels;
    for (y = 0; y < CROWN; y++)
        for (x = 0; x < CROWN; x++) {
            int in = 0, edge = 0;
            for (k = 0; k < 5; k++) {
                int dx = x - bx[k], dy = y - by[k], d2 = dx * dx + dy * dy;
                if (d2 <= br[k] * br[k]) in = 1;
                if (d2 <= (br[k] + 1) * (br[k] + 1)) edge = 1;
            }
            if (in) {
                /* light from the top left; leaves: a little noise; darker near the bottom right */
                int lit = (CROWN - x) + (CROWN - y) - CROWN;          /* -48 .. 48 */
                uint32_t h = (uint32_t)(x * 73856093u ^ y * 19349663u) * 2654435761u;
                int tone = lit / 2 + (int)((h >> 27) & 15) - 8, c;
                if (biome == BIOME_CLASSIC) {
                    int nearest = 9999;
                    for (k = 0; k < 5; k++) {
                        int dx = x - bx[k], dy = y - by[k];
                        int d = (dx * dx + dy * dy) * 100 / (br[k] * br[k]);
                        if (d < nearest) nearest = d;
                    }
                    /* Rounded foliage clusters like the credited top-down tree. */
                    tone = 15 - x / 3 - y / 3 + (100 - nearest) / 7;
                    if ((h & 7) == 0) tone -= 10;
                }
                const Uint8 *a = pal[biome][1], *bcol = tone > 6 ? pal[biome][2] : pal[biome][0];
                int f = tone > 6 ? (tone - 6) * 12 : (-tone) * 10;
                Uint8 rr, gg, bb;
                if (f > 220) f = 220;
                if (tone > -2 && tone <= 6) f = 0;
                rr = (Uint8)(a[0] + (bcol[0] - a[0]) * f / 256);
                gg = (Uint8)(a[1] + (bcol[1] - a[1]) * f / 256);
                bb = (Uint8)(a[2] + (bcol[2] - a[2]) * f / 256);
                c = (h >> 24) % 9 == 0;                                /* (a darker leaf here and there) */
                if (c) { rr = (Uint8)(rr * 3 / 4); gg = (Uint8)(gg * 3 / 4); bb = (Uint8)(bb * 3 / 4); }
                px[y * CROWN + x] = 0xFF000000u | (Uint32)rr << 16 | (Uint32)gg << 8 | bb;
            } else if (edge) {
                px[y * CROWN + x] = 0xFF000000u | (Uint32)(pal[biome][0][0] / 2) << 16 | (Uint32)(pal[biome][0][1] / 2) << 8 | (pal[biome][0][2] / 2);
            } else px[y * CROWN + x] = 0;
        }
    crown_tex[biome] = SDL_CreateTextureFromSurface(r, sf);
    SDL_FreeSurface(sf);
    if (crown_tex[biome]) SDL_SetTextureBlendMode(crown_tex[biome], SDL_BLENDMODE_BLEND);
    return crown_tex[biome];
}

/* the bases' gates, as in the original: a gap of 2..5 tiles in a straight run of wall, wall on both sides
 * (a gap a shot blew is rubble, not a gate). Square posts on the wall's ends; a striped barrier across,
 * raised while a tank is near. Only drawn: the gap is open to drive through, as it always was. */
static int is_wall(const Map *m, int x, int y) { return x >= 0 && y >= 0 && x < MAP_W && y < MAP_H && m->type[y][x] == TILE_WALL; }
static int gate_open_ground(const Map *m, int x, int y)
{
    int t = x >= 0 && y >= 0 && x < MAP_W && y < MAP_H ? m->type[y][x] : TILE_ROCK;
    if (t == TILE_ROAD && m->sid[y][x] && (struct_info[m->st[m->sid[y][x] - 1].type].flags & SF_GATE))
        return 0;
    return t == TILE_SAND || t == TILE_ROAD || t == TILE_FLOOR;
}
/* each gate's barrier: how far up (0 down .. 256 up), kept between frames so it swings */
#define MAX_GATES 48
typedef struct { int16_t tx, ty; uint8_t dir; int16_t up; Uint32 at; } GateVisual;
static GateVisual gates[MAX_GATES];
static GateVisual *gate_state(int tx, int ty, int dir)
{
    int k, free_k = -1;
    for (k = 0; k < MAX_GATES; k++) {
        if (gates[k].tx == tx && gates[k].ty == ty && gates[k].dir == dir && (gates[k].tx || gates[k].ty)) return &gates[k];
        if (free_k < 0 && !gates[k].tx && !gates[k].ty) free_k = k;
    }
    if (free_k < 0) free_k = (tx * 7 + ty * 13 + dir) % MAX_GATES;   /* (full: reuse one) */
    gates[free_k].tx = (int16_t)tx; gates[free_k].ty = (int16_t)ty; gates[free_k].dir = (uint8_t)dir; gates[free_k].up = 0;
    gates[free_k].at = 0;
    return &gates[free_k];
}

/* a gate post: the world's own wall, a pillar standing on its own (its look: the walls' and towers') */
static void gate_post(SDL_Renderer *r, const Style *style, int x, int y)
{
    SDL_Rect src = { 0, TILE_WALL * TILE_SIZE, TILE_SIZE, TILE_SIZE }, dst = { x - 9, y - 11, 18, 20 }, q;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 90);                                       /* its shadow */
    q = (SDL_Rect){ x - 6, y + 6, 15, 5 };
    SDL_RenderFillRect(r, &q);
    if (style && style->tiles) SDL_RenderCopy(r, style->tiles, &src, &dst);      /* (column 0: a piece on its own) */
    else { SDL_SetRenderDrawColor(r, 118, 118, 120, 255); SDL_RenderFillRect(r, &dst); }
    SDL_SetRenderDrawColor(r, 255, 255, 255, 40);                                 /* a lit top edge */
    q = (SDL_Rect){ x - 9, y - 11, 18, 2 };
    SDL_RenderFillRect(r, &q);
}

/* the barrier: a striped pole from its post across the gap; "up" turns it on its post, so from above
 * it shortens towards the post (and its shadow falls away from it) */
static void gate_pole(SDL_Renderer *r, int ax, int ay, int len, int dir, int up)
{
    int vis = len * (256 - up) / 256, i;                   /* (a turn of up/256 of a right angle, near enough) */
    int lift = up * 6 / 256;                               /* (raised: its shadow apart from it) */
    for (i = 0; i < vis; i += 4) {
        SDL_Rect q = dir ? (SDL_Rect){ ax - 2 + lift, ay + i + 2, 4, 4 } : (SDL_Rect){ ax + i + 2, ay - 1 + lift, 4, 4 };
        SDL_SetRenderDrawColor(r, 0, 0, 0, 70);            /* shadow */
        SDL_RenderFillRect(r, &q);
    }
    for (i = 0; i < vis; i += 4) {
        SDL_Rect q = dir ? (SDL_Rect){ ax - 2, ay + i, 4, 4 } : (SDL_Rect){ ax + i, ay - 2, 4, 4 };
        if ((i / 4) % 2) SDL_SetRenderDrawColor(r, 235, 232, 225, 255);
        else SDL_SetRenderDrawColor(r, 205, 40, 32, 255);
        SDL_RenderFillRect(r, &q);
    }
    {   /* the counterweight on the post's far side */
        SDL_Rect q = dir ? (SDL_Rect){ ax - 3, ay - 6, 6, 4 } : (SDL_Rect){ ax - 6, ay - 3, 4, 6 };
        SDL_SetRenderDrawColor(r, 60, 58, 54, 255);
        SDL_RenderFillRect(r, &q);
    }
}

static void draw_blast_gate(SDL_Renderer *r, const Game *g, const Structure *gate, int cam_x, int cam_y)
{
    const StructInfo *info = &struct_info[gate->type];
    int x = gate->x * TILE_SIZE, y = gate->y * TILE_SIZE;
    int vertical = gate->type == ST_GATE_V, offset;
    int w = info->w * TILE_SIZE, h = info->h * TILE_SIZE;
    int open = game_gate_open(g, x + w / 2, y + h / 2);
    int length = vertical ? h : w;
    if (!gate->hp) return;
    x -= cam_x; y -= cam_y;
    for (offset = 0; offset < length; offset += 8) {
        int end = offset == 0 || offset == length - 8;
        int half = end ? 6 : 4;
        Rgb metal = end || !(offset & 8) ? (Rgb){119, 127, 122} : (Rgb){76, 83, 80};
        if (open && !end) continue;
        if (vertical) {
            ui_fill(r, x + 8 - half, y + offset, 2 * half, 8, UI_BLACK);
            ui_fill(r, x + 9 - half, y + offset + 1, 2 * half - 2, 6, metal);
        } else {
            ui_fill(r, x + offset, y + 8 - half, 8, 2 * half, UI_BLACK);
            ui_fill(r, x + offset + 1, y + 9 - half, 6, 2 * half - 2, metal);
        }
    }
}

static void draw_gates(SDL_Renderer *r, const Style *style, const Game *g, int cam_x, int cam_y, int tx0, int ty0, int tx1, int ty1)
{
    const Map *m = &g->map;
    int tx, ty, dir;
    Uint32 now = SDL_GetTicks();
    for (ty = ty0 - 6; ty <= ty1 + 6; ty++)
        for (tx = tx0 - 6; tx <= tx1 + 6; tx++) {
            if (!is_wall(m, tx, ty)) continue;
            for (dir = 0; dir < 2; dir++) {              /* 0: a run east-west, 1: north-south */
                int sx = dir ? 0 : 1, sy = dir ? 1 : 0, n = 0;
                if (!is_wall(m, tx - sx, ty - sy)) continue;   /* (the wall runs on behind this end) */
                while (n < 6 && gate_open_ground(m, tx + sx * (n + 1), ty + sy * (n + 1))) n++;
                if (n < 2 || n > 5 || !is_wall(m, tx + sx * (n + 1), ty + sy * (n + 1))) continue;
                if (!is_wall(m, tx + sx * (n + 2), ty + sy * (n + 2))) continue;   /* (and on beyond the other) */
                {
                    int ax = tx * TILE_SIZE + TILE_SIZE / 2 - cam_x, ay = ty * TILE_SIZE + TILE_SIZE / 2 - cam_y;
                    int bx = (tx + sx * (n + 1)) * TILE_SIZE + TILE_SIZE / 2 - cam_x, by = (ty + sy * (n + 1)) * TILE_SIZE + TILE_SIZE / 2 - cam_y;
                    int mx = (tx * 2 + sx * (n + 1)) * TILE_SIZE / 2 + TILE_SIZE / 2, my = (ty * 2 + sy * (n + 1)) * TILE_SIZE / 2 + TILE_SIZE / 2;
                    GateVisual *gate = gate_state(tx, ty, dir);
                    Uint32 dt = gate->at ? now - gate->at : 0;
                    int near = game_gate_open(g, mx, my);
                    gate->at = now;
                    if (dt > 100) dt = 100;
                    gate->up = (int16_t)(gate->up + (near ? 1 : -1) * (int)(dt * 256 / 450));
                    if (gate->up < 0) gate->up = 0;
                    if (gate->up > 236) gate->up = 236;
                    gate_pole(r, ax, ay, dir ? by - ay : bx - ax, dir, gate->up);
                    gate_post(r, style, ax, ay);
                    gate_post(r, style, bx, by);
                }
            }
        }
}

/* Static surface detail is cached in the ground chunks, never redrawn per frame. */
static void surface_tile(SDL_Renderer *r, int kind, int mask, SDL_Rect dst)
{
    static const Uint8 colors[3][6][3] = {
        {{178,214,228},{239,252,255},{113,167,194},{99,130,145},{197,224,233},{255,255,255}},
        {{80,80,82},{17,18,24},{43,43,52},{94,96,107},{61,75,100},{80,80,82}},
        {{48,47,52},{17,16,23},{99,96,104},{166,162,170},{56,53,59},{23,21,27}}
    };
    int x, y;
    for (y = 0; y < 16; y++) for (x = 0; x < 16;) {
        int start = x, pen = surface_pixels[kind][mask][y][x];
        SDL_Rect dot;
        while (++x < 16 && surface_pixels[kind][mask][y][x] == pen) {}
        dot = (SDL_Rect){dst.x + start * dst.w / 16, dst.y + y * dst.h / 16,
                         x * dst.w / 16 - start * dst.w / 16, (y+1) * dst.h / 16 - y * dst.h / 16};
        SDL_SetRenderDrawColor(r, colors[kind][pen][0], colors[kind][pen][1], colors[kind][pen][2], 255);
        SDL_RenderFillRect(r, &dot);
    }
}

/* one ground tile at dst (the terrain under everything: what the view draws first) */
static void ground_tile(SDL_Renderer *r, const Style *style, const Game *g, int tx, int ty, SDL_Rect dst)
{
    int type = g->map.type[ty][tx], sid = g->map.sid[ty][tx];
    if (type >= TILE_ICE && type <= TILE_CRATER) {
        surface_tile(r, type - TILE_ICE, tile_column(&g->map, tx, ty), dst);
    } else if (sid && (struct_info[g->map.st[sid - 1].type].flags & SF_GATE) && type == TILE_BUILDING) {
        SDL_Rect src = { 15 * TILE_SIZE, TILE_ROAD * TILE_SIZE, TILE_SIZE, TILE_SIZE };
        SDL_RenderCopy(r, style->tiles, &src, &dst);
    } else if (type == TILE_CANOPY) {                   /* trees overhead: the ground here (they come after the tanks) */
        SDL_Rect gsrc = { (int)(hash2((uint32_t)tx, (uint32_t)ty) & 15) * TILE_SIZE, TILE_SAND * TILE_SIZE, TILE_SIZE, TILE_SIZE };
        SDL_RenderCopy(r, style->tiles, &gsrc, &dst);
    } else if ((type == TILE_BUILDING || type == TILE_FLOOR) && sid && style->structures) {
        /* just the ground here; the building comes after the shadows */
        SDL_Rect gsrc = { (int)(hash2((uint32_t)tx, (uint32_t)ty) & 15) * TILE_SIZE,
                          TILE_SAND * TILE_SIZE, TILE_SIZE, TILE_SIZE };
        SDL_RenderCopy(r, style->tiles, &gsrc, &dst);
    } else {
        SDL_Rect src = { tile_column(&g->map, tx, ty) * TILE_SIZE, type * TILE_SIZE, TILE_SIZE, TILE_SIZE };
        SDL_RenderCopy(r, style->tiles, &src, &dst);
    }
}

/* The ground, in chunks of 16 x 16 tiles drawn once into a texture and kept: a view then draws a few chunks,
 * not a few hundred tiles (and their neighbour lookups) every frame. A chunk is drawn again when anything
 * that decides its look changes: its tiles or the ones around it (a wall shot down, a bush flattened), its
 * buildings' kinds, or the style. Without textures to draw into, tile by tile as before. */
enum { CHUNK = 16, CHUNK_PX = CHUNK * TILE_SIZE, CHUNKS_KEPT = 24 };
static struct { SDL_Texture *tex; int cx, cy, used; uint32_t sig; const Style *style; } chunks[CHUNKS_KEPT];
static int chunk_clock, chunks_off;

static uint32_t chunk_sig(const Game *g, int cx, int cy)
{
    uint32_t h = 2166136261u ^ (uint32_t)g->map.w << 16 ^ (uint32_t)g->map.h;
    int x, y;
    for (y = cy * CHUNK - 1; y <= cy * CHUNK + CHUNK; y++)
        for (x = cx * CHUNK - 1; x <= cx * CHUNK + CHUNK; x++) {
            int t = -1, sid = 0;
            if (x >= 0 && y >= 0 && x < g->map.w && y < g->map.h && x < MAP_W && y < MAP_H) {
                t = g->map.type[y][x];
                sid = g->map.sid[y][x];
            }
            h = (h ^ (uint32_t)(t + 1)) * 16777619u;
            if (sid) h = (h ^ (uint32_t)sid << 8 ^ g->map.st[sid - 1].type) * 16777619u;
        }
    return h;
}

static void ground(SDL_Renderer *r, const Style *style, const Game *g, int cam_x, int cam_y,
                   int tx0, int ty0, int tx1, int ty1, const SDL_Rect *view)
{
    int cx, cy, tx, ty;
    if (!chunks_off && !getenv("OFP_NO_CHUNKS")) {
        for (cy = ty0 / CHUNK; cy <= ty1 / CHUNK; cy++)
            for (cx = tx0 / CHUNK; cx <= tx1 / CHUNK; cx++) {
                int k, pick = -1, oldest = 0;
                uint32_t sig = chunk_sig(g, cx, cy);
                SDL_Rect dst = { cx * CHUNK_PX - cam_x, cy * CHUNK_PX - cam_y, CHUNK_PX, CHUNK_PX };
                for (k = 0; k < CHUNKS_KEPT; k++) {
                    if (chunks[k].tex && chunks[k].cx == cx && chunks[k].cy == cy && chunks[k].style == style) { pick = k; break; }
                    if (chunks[k].used < chunks[oldest].used) oldest = k;
                }
                if (pick < 0 || chunks[pick].sig != sig) {   /* (not kept, or changed: drawn again) */
                    SDL_Texture *prev = SDL_GetRenderTarget(r);
                    if (pick < 0) {
                        pick = oldest;
                        if (!chunks[pick].tex)
                            chunks[pick].tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, CHUNK_PX, CHUNK_PX);
                        if (!chunks[pick].tex) { chunks_off = 1; goto by_tile; }
                        SDL_SetTextureBlendMode(chunks[pick].tex, SDL_BLENDMODE_BLEND);
                    }
                    if (SDL_SetRenderTarget(r, chunks[pick].tex) != 0) { chunks_off = 1; goto by_tile; }
                    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
                    SDL_RenderClear(r);
                    for (ty = cy * CHUNK; ty < cy * CHUNK + CHUNK && ty < MAP_H && ty < g->map.h; ty++)
                        for (tx = cx * CHUNK; tx < cx * CHUNK + CHUNK && tx < MAP_W && tx < g->map.w; tx++) {
                            SDL_Rect d = { (tx - cx * CHUNK) * TILE_SIZE, (ty - cy * CHUNK) * TILE_SIZE, TILE_SIZE, TILE_SIZE };
                            ground_tile(r, style, g, tx, ty, d);
                        }
                    if (prev) SDL_SetRenderTarget(r, prev);
                    else ui_screen_target(r);                 /* (back to the screen, its scaling and all) */
                    {
                        SDL_Rect clip = { 0, 0, view->w, view->h };
                        ui_viewport(r, view);
                        SDL_RenderSetClipRect(r, &clip);
                    }
                    chunks[pick].cx = cx; chunks[pick].cy = cy; chunks[pick].sig = sig; chunks[pick].style = style;
                }
                chunks[pick].used = ++chunk_clock;
                SDL_RenderCopy(r, chunks[pick].tex, NULL, &dst);
            }
        return;
    }
by_tile:
    for (ty = ty0; ty <= ty1 && ty < MAP_H; ty++)
        for (tx = tx0; tx <= tx1 && tx < MAP_W; tx++) {
            SDL_Rect dst = { tx * TILE_SIZE - cam_x, ty * TILE_SIZE - cam_y, TILE_SIZE, TILE_SIZE };
            ground_tile(r, style, g, tx, ty, dst);
        }
}

void render_view(SDL_Renderer *r, const Theme *t, const Style *style, const Game *g,
                 const SDL_Rect *view, int follow, int alpha256, int weather_effects)
{
    SDL_Texture *tanks = style->tanks ? style->tanks : t->tanks;
    SDL_Rect clip = { 0, 0, view->w, view->h };
    int cam_x, cam_y, tx, ty, i, shx, shy;
    int tx0, ty0, tx1, ty1;

    if (free_cam) {                                      /* watching: a camera of its own */
        cam_x = free_cam_x - view->w / 2; cam_y = free_cam_y - view->h / 2;
        if (cam_x > g->map.w * TILE_SIZE - view->w) cam_x = g->map.w * TILE_SIZE - view->w;   /* (the map's own edge) */
        if (cam_y > g->map.h * TILE_SIZE - view->h) cam_y = g->map.h * TILE_SIZE - view->h;
        if (cam_x < 0) cam_x = 0;
        if (cam_y < 0) cam_y = 0;
    } else game_camera(g, follow, view->w, view->h, alpha256, &cam_x, &cam_y);
    fx_shake(&shx, &shy);
    cam_x += shx;
    cam_y += shy;
    ui_viewport(r, view);
    SDL_RenderSetClipRect(r, &clip);

    tx0 = cam_x >> TILE_SHIFT;  tx1 = (cam_x + view->w - 1) >> TILE_SHIFT;
    ty0 = cam_y >> TILE_SHIFT;  ty1 = (cam_y + view->h - 1) >> TILE_SHIFT;
    if (tx0 < 0) tx0 = 0;
    if (ty0 < 0) ty0 = 0;
    ground(r, style, g, cam_x, cam_y, tx0, ty0, tx1, ty1, view);   /* the ground (kept in chunks) */

    marks_update(g);                                     /* what the fight's left on the ground */
    marks_draw(r, cam_x, cam_y, view->w, view->h);
    draw_gates(r, style, g, cam_x, cam_y, tx0, ty0, tx1, ty1);   /* the bases' gates: posts, a barrier that swings */
    if (style->bollards)                                 /* pairs of posts beside barrier walls */
        for (ty = ty0 - 1; ty <= ty1 + 1 && ty < MAP_H; ty++)
            for (tx = tx0 - 1; tx <= tx1 + 1 && tx < MAP_W; tx++) {
                int bx = tx * TILE_SIZE - cam_x, by = ty * TILE_SIZE - cam_y;
                if (tx < 0 || ty < 0 || g->map.type[ty][tx] != TILE_WALL) continue;
                if (tx % 3 == 0) {
                    if (ty > 0 && !(tile_info[g->map.type[ty - 1][tx]].flags & TF_SOLID)) {
                        bollard(r, bx + 2, by - 5); bollard(r, bx + 8, by - 5);
                    }
                    if (ty < MAP_H - 1 && !(tile_info[g->map.type[ty + 1][tx]].flags & TF_SOLID)) {
                        bollard(r, bx + 2, by + 17); bollard(r, bx + 8, by + 17);
                    }
                }
                if (ty % 3 == 0) {
                    if (tx > 0 && !(tile_info[g->map.type[ty][tx - 1]].flags & TF_SOLID)) {
                        bollard(r, bx - 5, by + 2); bollard(r, bx - 5, by + 8);
                    }
                    if (tx < MAP_W - 1 && !(tile_info[g->map.type[ty][tx + 1]].flags & TF_SOLID)) {
                        bollard(r, bx + 17, by + 2); bollard(r, bx + 17, by + 8);
                    }
                }
            }

    /* shadows: the sun is up and to the left, so everything tall throws a
     * soft shadow down and to the right onto whatever is next to it */
    for (ty = ty0; ty <= ty1 + 1 && ty < MAP_H; ty++)
        for (tx = tx0; tx <= tx1 + 1 && tx < MAP_W; tx++) {
            int bx = tx * TILE_SIZE - cam_x, by = ty * TILE_SIZE - cam_y;
            if (g->map.type[ty][tx] != TILE_WALL) continue;
            SDL_SetRenderDrawColor(r, 0, 0, 0, 70);
            if (ty + 1 < MAP_H && g->map.type[ty + 1][tx] != TILE_WALL) {
                SDL_Rect q = { bx + 3, by + TILE_SIZE, TILE_SIZE, 4 };
                SDL_RenderFillRect(r, &q);
            }
            if (tx + 1 < MAP_W && g->map.type[ty][tx + 1] != TILE_WALL) {
                SDL_Rect q = { bx + TILE_SIZE, by + 3, 4, TILE_SIZE - 3 };
                SDL_RenderFillRect(r, &q);
            }
        }
    for (i = 0; i < g->map.num_st; i++)
        if (struct_info[g->map.st[i].type].flags & SF_GATE)
            draw_blast_gate(r, g, &g->map.st[i], cam_x, cam_y);
    if (style->structures) {
        int pass;
        for (pass = 0; pass < 2; pass++)                 /* 0: shadows, 1: the buildings */
            for (i = 0; i < g->map.num_st; i++) {
                const Structure *st = &g->map.st[i];
                const StructInfo *si = &struct_info[st->type];
                int damaged = si->hp && st->hp * 2 <= si->hp, w = si->w * TILE_SIZE, h = si->h * TILE_SIZE;
                SDL_Rect src = { damaged * STRUCT_SLOT_W, (st->type ? st->type - 1 : 0) * STRUCT_SLOT_H, w, h };
                SDL_Rect dst = { st->x * TILE_SIZE - cam_x, st->y * TILE_SIZE - cam_y, w, h };
                if (st->type == ST_NONE || (si->flags & SF_GATE)) continue; /* gates are drawn separately */
                if (dst.x > view->w + 8 || dst.y > view->h + 8 || dst.x + w < -8 || dst.y + h < -8) continue;
                if (st->hp == 0 && si->hp) {                  /* fallen: what's left of it, glowing */
                    if (pass == 1 && g->map.type[st->y][st->x] == TILE_RUINS && !g->map.sid[st->y][st->x]) {
                        src.x = 2 * STRUCT_SLOT_W;
                        SDL_RenderCopy(r, style->structures, &src, &dst);
                        if (ground_fires && st->smoulder) embers(r, g, st, i, dst.x, dst.y, w, h);
                    }
                    continue;
                }
                if (st->x >= MAP_W || g->map.sid[st->y][st->x] != i + 1) continue;
                if (pass == 0) {
                    int off = st->type == ST_TOWER ? 7 : st->type == ST_HELIPAD ? 0 : 5;
                    if (!off) continue;
                    dst.x += off; dst.y += off;
                    SDL_SetTextureColorMod(style->structures, 0, 0, 0);
                    SDL_SetTextureAlphaMod(style->structures, 80);
                    SDL_RenderCopy(r, style->structures, &src, &dst);
                    SDL_SetTextureColorMod(style->structures, 255, 255, 255);
                    SDL_SetTextureAlphaMod(style->structures, 255);
                } else {
                    SDL_RenderCopy(r, style->structures, &src, &dst);
                }
            }
    }

    if (style->vehicles)                                 /* parked vehicles and their wrecks */
        for (i = 0; i < g->num_props; i++) {
            const Prop *p = &g->props[i];
            int x = fx_to_int(p->x) - cam_x, y = fx_to_int(p->y) - cam_y;
            SDL_Rect src = { ((p->angle + 0x400) >> 11 & 31) * 32,
                             (p->kind * 3 + (p->alive ? (p->team & 1) : 2)) * 32, 32, 32 };
            SDL_Rect dst = { x - 16, y - 16, 32, 32 };
            if (!p->used || x < -20 || y < -20 || x > view->w + 20 || y > view->h + 20) continue;
            SDL_RenderCopy(r, style->vehicles, &src, &dst);
        }

    {   /* water comes alive */
        float wt = (float)SDL_GetTicks() / 1000.0f;
        for (ty = ty0; ty <= ty1 && ty < MAP_H; ty++)
            for (tx = tx0; tx <= tx1 && tx < MAP_W; tx++)
                if (g->map.type[ty][tx] == TILE_WATER) {
                    int wx = tx * TILE_SIZE - cam_x, wy = ty * TILE_SIZE - cam_y;
                    if (!style->faithful) water_fx(r, &g->map, tx, ty, wx, wy, wt, style->water_fx);
                    water_corners(r, style, &g->map, tx, ty, wx, wy);
                }
    }

    fx_draw_ground(r, cam_x, cam_y);

    if (style->turrets)                                  /* rotating gun heads */
        for (i = 0; i < g->map.num_st; i++) {
            const Structure *st = &g->map.st[i];
            const StructInfo *si = &struct_info[st->type];
            int row = turret_row(st->type), cx, cy, frame;
            SDL_Rect src, dst;
            if (row < 0 || st->hp == 0) continue;
            cx = st->x * TILE_SIZE + si->w * TILE_SIZE / 2 - cam_x;
            cy = st->y * TILE_SIZE + si->h * TILE_SIZE / 2 - cam_y;
            if (cx < -40 || cy < -40 || cx > view->w + 40 || cy > view->h + 40) continue;
            frame = ((st->aim + 0x400) >> 11) & 31;
            src.x = frame * THEME_TANK_CELL; src.y = row * THEME_TANK_CELL;
            src.w = src.h = THEME_TANK_CELL;
            dst.x = cx - THEME_TANK_CELL / 2; dst.y = cy - THEME_TANK_CELL / 2;
            dst.w = dst.h = THEME_TANK_CELL;
            SDL_RenderCopy(r, style->turrets, &src, &dst);
            if (st->team == TEAM_INDEPENDENT || g->map.custom_bases) {
                int x = cx - si->w * TILE_SIZE / 2 + 2;
                int y = cy - si->h * TILE_SIZE / 2 + 2;
                ui_fill(r, x, y, 6, 6, (Rgb){ 15, 12, 20 });
                ui_fill(r, x + 1, y + 1, 4, 4, render_side_rgb(st->team));
            }
        }

    for (i = 0; i < g->map.num_st; i++) {                /* helicopters parked on their pads */
        const Structure *p = &g->map.st[i];
        SDL_Rect src = { 0, 0, THEME_HELI_CELL, THEME_HELI_CELL }, dst;
        int px, py;
        if (g->map.quiet || p->type != ST_HELIPAD || p->aim != 0) continue;
        px = p->x * TILE_SIZE + 24 - cam_x;
        py = p->y * TILE_SIZE + 24 - cam_y;
        if (px < -30 || py < -30 || px > view->w + 30 || py > view->h + 30) continue;
        dst.w = dst.h = THEME_HELI_CELL;
        dst.x = px - THEME_HELI_CELL / 2 + 2; dst.y = py - THEME_HELI_CELL / 2 + 2;
        SDL_SetTextureColorMod(t->heli, 0, 0, 0);
        SDL_SetTextureAlphaMod(t->heli, 90);
        SDL_RenderCopy(r, t->heli, &src, &dst);
        SDL_SetTextureColorMod(t->heli, 255, 255, 255);
        SDL_SetTextureAlphaMod(t->heli, 255);
        dst.x -= 2; dst.y -= 2;
        SDL_RenderCopy(r, t->heli, &src, &dst);
        SDL_SetRenderDrawColor(r, 30, 30, 30, 255);       /* rotor at rest */
        SDL_RenderDrawLine(r, px - 13, py - 13, px + 13, py + 13);
        SDL_RenderDrawLine(r, px - 13, py + 13, px + 13, py - 13);
    }

    for (i = 0; i < MAX_MINES; i++) {                    /* mines: dark disc, blinking when armed */
        const Mine *mn = &g->mines[i];
        SDL_Rect q;
        if (!mn->active) continue;
        q.x = fx_to_int(mn->x) - cam_x - 2; q.y = fx_to_int(mn->y) - cam_y - 2; q.w = q.h = 5;
        SDL_SetRenderDrawColor(r, 30, 30, 26, 255);
        SDL_RenderFillRect(r, &q);
        q.x++; q.y++; q.w = q.h = 3;
        if (mn->arm) SDL_SetRenderDrawColor(r, 108, 108, 99, 255);
        else if ((g->tick / 10) % 2) SDL_SetRenderDrawColor(r, 255, 56, 38, 255);
        else SDL_SetRenderDrawColor(r, 70, 20, 16, 255);
        SDL_RenderFillRect(r, &q);
        SDL_SetRenderDrawColor(r, 238, 232, 208, 255);
        SDL_RenderDrawLine(r, q.x, q.y, q.x + 1, q.y);
    }

    for (i = 0; i < 2 && !(g->extras & EXTRA_NOFLAGS); i++)   /* flags lying on the ground (not in modes without) */
        if (g->flags[i].state == FLAG_GROUND)
        {
            int fxp = fx_to_int(g->flags[i].x) - cam_x, fyp = fx_to_int(g->flags[i].y) - cam_y;
            flag_marker(r, fxp, fyp, i, g->tick);
            draw_flag(r, fxp, fyp, i, g->tick);
        }

    for (i = 0; i < MAX_MEN + MAX_EVACUEES; i++) {       /* soldiers, prisoners and first-aid arrivals */
        const Man *m = i < MAX_MEN ? &g->men[i] : &g->evacuees[i - MAX_MEN];
        int dir8, frame, mx, my;
        SDL_Rect src, dst;
        if (m->kind == MAN_NONE) continue;
        mx = fx_to_int(fx_lerp(m->prev_x, m->x, alpha256)) - cam_x;
        my = fx_to_int(fx_lerp(m->prev_y, m->y, alpha256)) - cam_y;
        if (mx < -8 || my < -8 || mx > view->w + 8 || my > view->h + 8) continue;
        dir8 = ((m->dir + 0x1000) >> 13) & 7;
        frame = (m->x != m->prev_x || m->y != m->prev_y || (m->kind == MAN_POW && m->state == 0))
                ? (int)((g->tick / 6 + (uint32_t)i) & 1) : 0;
        src.x = (dir8 * 2 + frame) * THEME_MAN_CELL; src.y = (m->kind == MAN_SOLDIER) * THEME_MAN_CELL;
        if (m->kind == MAN_SOLDIER && m->team == TEAM_PLAYER) {   /* our men: their own row, or tinted */
            int h = 0;
            SDL_QueryTexture(t->men, NULL, NULL, NULL, &h);
            if (h >= 3 * THEME_MAN_CELL) src.y = 2 * THEME_MAN_CELL;
            else SDL_SetTextureColorMod(t->men, 255, 215, 140);
        }
        src.w = src.h = THEME_MAN_CELL;
        dst.x = mx - THEME_MAN_CELL / 2; dst.y = my - THEME_MAN_CELL / 2; dst.w = dst.h = THEME_MAN_CELL;
        SDL_RenderCopy(r, t->men, &src, &dst);
        SDL_SetTextureColorMod(t->men, 255, 255, 255);
    }

    if (g->extras & EXTRA_HILL) {                        /* the hill: its edge, in the holder's colour */
        int hx = g->hill_tx * TILE_SIZE + TILE_SIZE / 2 - cam_x, hy = g->hill_ty * TILE_SIZE + TILE_SIZE / 2 - cam_y;
        int hr = HILL_R * TILE_SIZE, k;
        Rgb hc = g->hill_holder >= 0 ? render_side_rgb(g->hill_holder) : (Rgb){ 240, 230, 200 };
        SDL_Rect fill = { hx - hr, hy - hr, hr * 2, hr * 2 };
        SDL_SetRenderDrawColor(r, hc.r, hc.g, hc.b, g->hill_holder >= 0 ? 46 : 22);
        SDL_RenderFillRect(r, &fill);
        SDL_SetRenderDrawColor(r, hc.r, hc.g, hc.b, 200);
        for (k = 0; k < 2; k++) {
            SDL_Rect edge = { hx - hr + k, hy - hr + k, hr * 2 - 2 * k, hr * 2 - 2 * k };
            SDL_RenderDrawRect(r, &edge);
        }
    }
    if (g->extras & EXTRA_CONQUEST) {                    /* conquest: the points, A B C, in their owners' colours */
        int ci, k;
        for (ci = 0; ci < g->num_cp; ci++) {
            int hx = g->cp_tx[ci] * TILE_SIZE + TILE_SIZE / 2 - cam_x, hy = g->cp_ty[ci] * TILE_SIZE + TILE_SIZE / 2 - cam_y;
            int hr = CP_RADIUS * TILE_SIZE;
            Rgb hc = g->cp_owner[ci] >= 0 ? render_side_rgb(g->cp_owner[ci]) : (Rgb){ 240, 230, 200 };
            SDL_Rect fill = { hx - hr, hy - hr, hr * 2, hr * 2 };
            char letter[2] = { (char)('A' + ci), 0 };
            if (hx + hr < 0 || hy + hr < 0 || hx - hr > view->w || hy - hr > view->h) continue;
            SDL_SetRenderDrawColor(r, hc.r, hc.g, hc.b, g->cp_owner[ci] >= 0 ? 46 : 22);
            SDL_RenderFillRect(r, &fill);
            SDL_SetRenderDrawColor(r, hc.r, hc.g, hc.b, 200);
            for (k = 0; k < 2; k++) {
                SDL_Rect edge = { hx - hr + k, hy - hr + k, hr * 2 - 2 * k, hr * 2 - 2 * k };
                SDL_RenderDrawRect(r, &edge);
            }
            ui_text_shadow(r, hx - 3, hy - 4, 1, hc, letter);   /* (the view's own viewport: no offset) */
        }
        {   /* the score, along the top: each side's, towards CONQUEST_WIN */
            char sb[32];
            Rgb a = render_side_rgb(TEAM_PLAYER), b = render_side_rgb(TEAM_ENEMY);
            int wa, cx = view->w / 2;
            snprintf(sb, sizeof sb, "%d", g->cp_score[0]);
            wa = ui_text_width(sb, 1);
            ui_fill(r, cx - 46, 2, 92, 11, (Rgb){ 12, 12, 10 });
            ui_text_bold(r, cx - 6 - wa, 4, 1, a, sb);
            ui_text(r, cx - 3, 4, 1, (Rgb){ 200, 200, 190 }, "-");
            snprintf(sb, sizeof sb, "%d", g->cp_score[1]);
            ui_text_bold(r, cx + 6, 4, 1, b, sb);
        }
    }
    if (g->extras & EXTRA_WARLORD) {                  /* warlord: who the bosses are, and the score */
        int side;
        for (side = 0; side < 2; side++) {
            int jt = g->jug[side];
            const Tank *jt_t;
            int jx, jy, k;
            Rgb jc = render_side_rgb(side);
            if (jt < 0 || jt >= g->num_tanks) continue;
            jt_t = &g->tanks[jt];
            if (!jt_t->active || jt_t->respawn) continue;
            jx = fx_to_int(jt_t->x) - cam_x; jy = fx_to_int(jt_t->y) - cam_y;
            if (jx < -20 || jy < -20 || jx > view->w + 20 || jy > view->h + 20) continue;
            SDL_SetRenderDrawColor(r, jc.r, jc.g, jc.b, 220);
            for (k = 0; k < 2; k++) {
                SDL_Rect ring = { jx - 12 + k, jy - 12 + k, 24 - 2 * k, 24 - 2 * k };
                SDL_RenderDrawRect(r, &ring);
            }
            ui_text_shadow(r, jx - 11, jy - 22, 1, jc, "JUG");
        }
        {
            char sb[16];
            Rgb a = render_side_rgb(TEAM_PLAYER), b = render_side_rgb(TEAM_ENEMY);
            int cx = view->w / 2;
            ui_fill(r, cx - 46, 2, 92, 11, (Rgb){ 12, 12, 10 });
            snprintf(sb, sizeof sb, "%d", g->jug_score[0]);
            ui_text_bold(r, cx - 6 - ui_text_width(sb, 1), 4, 1, a, sb);
            ui_text(r, cx - 3, 4, 1, (Rgb){ 200, 200, 190 }, "-");
            snprintf(sb, sizeof sb, "%d", g->jug_score[1]);
            ui_text_bold(r, cx + 6, 4, 1, b, sb);
        }
    }
    if (g->extras & (EXTRA_HARDPOINT | EXTRA_KILLCONF)) {   /* hardpoint: the point that counts; kill confirmed: the tags */
        int k;
        uint16_t sa = g->extras & EXTRA_HARDPOINT ? g->hp_score[0] : g->kc_score[0], sb2 = g->extras & EXTRA_HARDPOINT ? g->hp_score[1] : g->kc_score[1];
        if ((g->extras & EXTRA_HARDPOINT) && g->num_cp) {
            int ci = g->hp_active, hx = g->cp_tx[ci] * TILE_SIZE + TILE_SIZE / 2 - cam_x, hy = g->cp_ty[ci] * TILE_SIZE + TILE_SIZE / 2 - cam_y;
            int hr = CP_RADIUS * TILE_SIZE, on[2] = { 0, 0 }, hi, left = (int)((HP_MOVE - g->tick % HP_MOVE) / TICK_RATE);
            Rgb hc;
            char lb[24];
            for (hi = 0; hi < g->num_tanks; hi++) {           /* (who's on it: the same test as the rules, in pixels) */
                const Tank *ht = &g->tanks[hi];
                int ddx = fx_to_int(ht->x) - (hx + cam_x), ddy = fx_to_int(ht->y) - (hy + cam_y);
                if (ht->active && !ht->respawn && ddx * ddx + ddy * ddy < hr * hr) on[ht->team & 1] = 1;
            }
            hc = on[0] != on[1] ? render_side_rgb(on[0] ? TEAM_PLAYER : TEAM_ENEMY) : (Rgb){ 240, 236, 220 };
            if (!(hx + hr < 0 || hy + hr < 0 || hx - hr > view->w || hy - hr > view->h)) {
                SDL_Rect fill = { hx - hr, hy - hr, hr * 2, hr * 2 };
                SDL_SetRenderDrawColor(r, hc.r, hc.g, hc.b, 40 + (int)((g->tick / 8) % 4) * 6);
                SDL_RenderFillRect(r, &fill);
                SDL_SetRenderDrawColor(r, hc.r, hc.g, hc.b, 220);
                for (k = 0; k < 3; k++) { SDL_Rect edge = { hx - hr + k, hy - hr + k, hr * 2 - 2 * k, hr * 2 - 2 * k }; SDL_RenderDrawRect(r, &edge); }
                snprintf(lb, sizeof lb, "HARDPOINT %d", left);
                ui_text_shadow(r, hx - ui_text_width(lb, 1) / 2, hy - hr - 10, 1, hc, lb);
            }
        }
        if (g->extras & EXTRA_KILLCONF)
            for (k = 0; k < MAX_TAGS; k++) {                 /* a tag: a little diamond in the fallen side's colour */
                int gx2, gy2, pulse;
                Rgb tc;
                if (!g->tags[k].active) continue;
                gx2 = fx_to_int(g->tags[k].x) - cam_x; gy2 = fx_to_int(g->tags[k].y) - cam_y;
                if (gx2 < -8 || gy2 < -8 || gx2 > view->w + 8 || gy2 > view->h + 8) continue;
                if (g->tags[k].ttl < 5 * TICK_RATE && (g->tick / 6) % 2) continue;   /* (going: blinking) */
                tc = render_side_rgb(g->tags[k].team);
                pulse = (int)((g->tick / 5) % 3);
                SDL_SetRenderDrawColor(r, 0, 0, 0, 120);
                { SDL_Rect sh = { gx2 - 3, gy2 - 1, 7, 5 }; SDL_RenderFillRect(r, &sh); }
                SDL_SetRenderDrawColor(r, tc.r, tc.g, tc.b, 255);
                { SDL_Rect a = { gx2 - 1, gy2 - 4 - pulse, 3, 9 + pulse * 2 }, b = { gx2 - 4 - pulse, gy2 - 1, 9 + pulse * 2, 3 }; SDL_RenderFillRect(r, &a); SDL_RenderFillRect(r, &b); }
                SDL_SetRenderDrawColor(r, 255, 255, 240, 255);
                { SDL_Rect c = { gx2 - 1, gy2 - 1, 3, 3 }; SDL_RenderFillRect(r, &c); }
            }
        {   /* the score along the top */
            char sb[16];
            Rgb a = render_side_rgb(TEAM_PLAYER), b = render_side_rgb(TEAM_ENEMY);
            int cx = view->w / 2;
            ui_fill(r, cx - 46, 2, 92, 11, (Rgb){ 12, 12, 10 });
            snprintf(sb, sizeof sb, "%d", sa);
            ui_text_bold(r, cx - 6 - ui_text_width(sb, 1), 4, 1, a, sb);
            ui_text(r, cx - 3, 4, 1, (Rgb){ 200, 200, 190 }, "-");
            snprintf(sb, sizeof sb, "%d", sb2);
            ui_text_bold(r, cx + 6, 4, 1, b, sb);
        }
    }
    if (g->drop.active) {                                /* an arena's supply drop: a crate under a parachute */
        int px = g->drop.tx * TILE_SIZE + TILE_SIZE / 2 - cam_x, py = g->drop.ty * TILE_SIZE + TILE_SIZE / 2 - cam_y;
        int sway = (int)((g->tick / 10) % 4) - 1;
        SDL_Rect crate = { px - 7, py - 6, 14, 12 }, band = { px - 7, py - 1, 14, 2 }, sh = { px - 6, py - 4, 14, 12 };
        SDL_SetRenderDrawColor(r, 20, 18, 12, 160); SDL_RenderFillRect(r, &sh);
        SDL_SetRenderDrawColor(r, 150, 105, 50, 255); SDL_RenderFillRect(r, &crate);
        SDL_SetRenderDrawColor(r, 240, 200, 60, 255); SDL_RenderFillRect(r, &band);
        SDL_SetRenderDrawColor(r, 90, 60, 30, 255); SDL_RenderDrawRect(r, &crate);
        SDL_SetRenderDrawColor(r, 235, 235, 225, 230);   /* (the parachute, still on it) */
        { SDL_Rect chute = { px - 9 + sway, py - 17, 18, 5 }; SDL_RenderFillRect(r, &chute); }
        SDL_RenderDrawLine(r, px - 8 + sway, py - 12, px - 6, py - 6);
        SDL_RenderDrawLine(r, px + 8 + sway, py - 12, px + 6, py - 6);
    }
    if (g->extras & EXTRA_PICKUPS)                       /* the pickups: repair, mines, fuel */
        for (i = 0; i < MAX_PICKUPS; i++) {
            const Pickup *p = &g->pickups[i];
            int px = p->tx * TILE_SIZE + TILE_SIZE / 2 - cam_x, py = p->ty * TILE_SIZE + TILE_SIZE / 2 - cam_y;
            int bob = (int)((g->tick / 8 + (uint32_t)i * 3) % 4 == 0);
            SDL_Rect box = { px - 6, py - 6 - bob, 12, 12 }, a, b;
            if (!p->active) continue;
            SDL_SetRenderDrawColor(r, 20, 18, 12, 255);
            { SDL_Rect sh = { box.x + 1, box.y + 2, 12, 12 }; SDL_RenderFillRect(r, &sh); }
            if (p->kind == PICKUP_REPAIR) {               /* white, a red cross */
                SDL_SetRenderDrawColor(r, 240, 240, 235, 255); SDL_RenderFillRect(r, &box);
                SDL_SetRenderDrawColor(r, 210, 40, 30, 255);
                a = (SDL_Rect){ px - 4, py - 1 - bob, 8, 2 }; b = (SDL_Rect){ px - 1, py - 4 - bob, 2, 8 };
                SDL_RenderFillRect(r, &a); SDL_RenderFillRect(r, &b);
            } else if (p->kind == PICKUP_MINES) {         /* olive, three mines */
                int k;
                SDL_SetRenderDrawColor(r, 90, 100, 50, 255); SDL_RenderFillRect(r, &box);
                SDL_SetRenderDrawColor(r, 30, 30, 26, 255);
                for (k = 0; k < 3; k++) { SDL_Rect m = { px - 5 + k * 4, py - 1 - bob, 3, 3 }; SDL_RenderFillRect(r, &m); }
            } else if (p->kind == PICKUP_FUEL) {              /* red, a fuel can */
                SDL_SetRenderDrawColor(r, 200, 40, 30, 255); SDL_RenderFillRect(r, &box);
                SDL_SetRenderDrawColor(r, 250, 210, 60, 255);
                a = (SDL_Rect){ px - 3, py - 5 - bob, 4, 2 }; SDL_RenderFillRect(r, &a);
                b = (SDL_Rect){ px - 4, py - 1 - bob, 8, 2 }; SDL_RenderFillRect(r, &b);
            }
            if (p->kind == PICKUP_BOOST || p->kind == PICKUP_RAPID) {
                int k;
                SDL_SetRenderDrawColor(r, p->kind == PICKUP_BOOST ? 50 : 240, p->kind == PICKUP_BOOST ? 135 : 170, p->kind == PICKUP_BOOST ? 225 : 45, 255);
                SDL_RenderFillRect(r, &box);
                SDL_SetRenderDrawColor(r, 15, 18, 24, 255);
                if (p->kind == PICKUP_BOOST) {
                    for (k = 0; k < 4; k++) SDL_RenderDrawLine(r, px - k, py - 4 - bob + k, px + k, py - 4 - bob + k);
                    a = (SDL_Rect){px-1, py-bob, 3, 4}; SDL_RenderFillRect(r, &a);
                } else for (k = -3; k <= 2; k += 5) {
                    a = (SDL_Rect){px+k, py-3-bob, 2, 7}; SDL_RenderFillRect(r, &a);
                }
            }
            SDL_SetRenderDrawColor(r, 255, 255, 255, 120);
            SDL_RenderDrawRect(r, &box);
        }

    for (i = 0; i < g->num_tanks; i++) {
        const Tank *tk = &g->tanks[i];
        int16_t da = (int16_t)(tk->angle - tk->prev_angle);
        angle16 a = (angle16)(tk->prev_angle + (da * alpha256) / 256);
        int frame = ((a + 0x400) >> 11) & 31;                     /* 32 directions */
        int sx = fx_to_int(fx_lerp(tk->prev_x, tk->x, alpha256)) - cam_x;
        int sy = fx_to_int(fx_lerp(tk->prev_y, tk->y, alpha256)) - cam_y;
        SDL_Rect src = { frame * THEME_TANK_CELL, (tk->cls * 2 + (tk->team & 1)) * THEME_TANK_CELL,
                         THEME_TANK_CELL, THEME_TANK_CELL };
        SDL_Rect dst = { sx - THEME_TANK_CELL / 2, sy - THEME_TANK_CELL / 2, THEME_TANK_CELL, THEME_TANK_CELL };
        if (!tk->active || tk->respawn) continue;
        SDL_RenderCopy(r, tanks, &src, &dst);
        if (tk->rocket_launcher && rocket_rack)
            SDL_RenderCopyEx(r, rocket_rack, NULL, &dst, frame * (360.0 / 32), NULL, SDL_FLIP_NONE);
        if (sides_mode) {                                /* marks: a ring over ours, a triangle over theirs */
            Rgb mc = render_side_rgb(tk->team);
            int mx = sx, my = sy - THEME_TANK_CELL / 2 - 4, k;
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
            if (tk->team == TEAM_ENEMY) {
                for (k = -1; k <= 4; k++) SDL_RenderDrawLine(r, mx - 4 + (k + 1) / 2, my - 3 + k, mx + 4 - (k + 1) / 2, my - 3 + k);
                SDL_SetRenderDrawColor(r, mc.r, mc.g, mc.b, 255);
                for (k = 0; k < 4; k++) SDL_RenderDrawLine(r, mx - 3 + k / 1, my - 2 + k, mx + 3 - k, my - 2 + k);
            } else {
                SDL_Rect ring = { mx - 3, my - 3, 7, 7 };
                SDL_RenderDrawRect(r, &ring);
                ring.x++; ring.y++; ring.w -= 2; ring.h -= 2;
                SDL_SetRenderDrawColor(r, mc.r, mc.g, mc.b, 255);
                SDL_RenderDrawRect(r, &ring);
            }
        }
        if (tk->boost_ticks || tk->rapid_ticks) {
            int k;
            for (k = 0; k < 2; k++) {
                int ticks = k ? tk->rapid_ticks : tk->boost_ticks;
                if (ticks) {
                    SDL_Rect bg = {sx-15, sy+17+k*9, 30, 8}, bar = {sx-6, sy+20+k*9, ticks*18/POWERUP_TICKS, 2};
                    SDL_SetRenderDrawColor(r, 0, 0, 0, 255); SDL_RenderFillRect(r, &bg);
                    SDL_SetRenderDrawColor(r, k ? 255 : 70, k ? 190 : 175, k ? 45 : 255, 255); SDL_RenderFillRect(r, &bar);
                    ui_text(r, sx-13, sy+18+k*9, 1, UI_WHITE, k ? "R" : "B");
                }
            }
        }
        if (tk->has_flag)                                /* flag streaming from the tank */
            draw_flag(r, sx + 2, sy - 2, 1 - (tk->team & 1), g->tick);
    }

    {   /* the trees overhead, over the tanks under them (as in the original); near the tank this view follows, see-through */
        int fx0 = follow >= 0 && follow < g->num_tanks ? fx_to_int(g->tanks[follow].x) >> TILE_SHIFT : -99;
        int fy0 = follow >= 0 && follow < g->num_tanks ? fx_to_int(g->tanks[follow].y) >> TILE_SHIFT : -99;
        SDL_Texture *cr = crown_for(r, g->biome);
        if (cr) {
            for (ty = ty0 > 0 ? ty0 - 1 : 0; ty <= ty1 + 1 && ty < g->map.h; ty++)
                for (tx = tx0 > 0 ? tx0 - 1 : 0; tx <= tx1 + 1 && tx < g->map.w; tx++) {
                    int span, size, near;
                    SDL_Rect dst, shadow;
                    if (g->map.type[ty][tx] != TILE_CANOPY) continue;
                    span = map_canopy_block(&g->map, tx, ty) ? 2 : 1;
                    if (span == 2 && ((tx | ty) & 1)) continue;
                    size = TILE_SIZE * span;
                    dst = (SDL_Rect){ tx * TILE_SIZE - cam_x, ty * TILE_SIZE - cam_y, size, size };
                    shadow = (SDL_Rect){ dst.x + span, dst.y + span, size, size };
                    near = fx0 >= tx - 1 && fx0 <= tx + span && fy0 >= ty - 1 && fy0 <= ty + span;
                    SDL_SetTextureColorMod(cr, 0, 0, 0);
                    SDL_SetTextureAlphaMod(cr, near ? 30 : 70);
                    SDL_RenderCopy(r, cr, NULL, &shadow);
                    SDL_SetTextureColorMod(cr, 255, 255, 255);
                    SDL_SetTextureAlphaMod(cr, near ? 110 : 255);
                    SDL_RenderCopy(r, cr, NULL, &dst);
                }
        }
        if (cr) SDL_SetTextureAlphaMod(cr, 255);
    }
    if (style->structures)                               /* garage roofs over tanks parked inside */
        for (i = 0; i < g->map.num_st; i++) {
            const Structure *st = &g->map.st[i];
            const StructInfo *si = &struct_info[st->type];
            SDL_Rect src, dst;
            if (st->type != ST_GARAGE) continue;
            dst.x = st->x * TILE_SIZE - cam_x; dst.y = st->y * TILE_SIZE - cam_y;
            dst.w = si->w * TILE_SIZE; dst.h = (si->h - 1) * TILE_SIZE;    /* all but the door row */
            if (dst.x > view->w || dst.y > view->h || dst.x + dst.w < 0 || dst.y + dst.h < 0) continue;
            src.x = 0; src.y = (st->type - 1) * STRUCT_SLOT_H; src.w = dst.w; src.h = dst.h;
            SDL_RenderCopy(r, style->structures, &src, &dst);
        }

    fx_draw_air(r, cam_x, cam_y);

    for (i = 0; i < MAX_HELIS; i++) {                    /* helicopters fly above everything */
        const Heli *h = &g->helis[i];
        int16_t da = (int16_t)(h->angle - h->prev_angle);
        angle16 a = (angle16)(h->prev_angle + (da * alpha256) / 256);
        int frame = ((a + 0x400) >> 11) & 31, hx, hy, k;
        SDL_Rect src = { frame * THEME_HELI_CELL, 0, THEME_HELI_CELL, THEME_HELI_CELL }, dst;
        if (!h->active) continue;
        hx = fx_to_int(fx_lerp(h->prev_x, h->x, alpha256)) - cam_x;
        hy = fx_to_int(fx_lerp(h->prev_y, h->y, alpha256)) - cam_y;
        dst.w = dst.h = THEME_HELI_CELL;
        dst.x = hx - THEME_HELI_CELL / 2 + 14; dst.y = hy - THEME_HELI_CELL / 2 + 14;   /* shadow far below */
        SDL_SetTextureColorMod(t->heli, 0, 0, 0);
        SDL_SetTextureAlphaMod(t->heli, 80);
        SDL_RenderCopy(r, t->heli, &src, &dst);
        SDL_SetTextureColorMod(t->heli, 255, 255, 255);
        SDL_SetTextureAlphaMod(t->heli, 255);
        dst.x = hx - THEME_HELI_CELL / 2; dst.y = hy - THEME_HELI_CELL / 2;
        SDL_RenderCopy(r, t->heli, &src, &dst);
        for (k = 0; k < 2; k++) {                        /* spinning rotor blur */
            float ra = (float)(g->tick * 0.9 + k * 1.5708 + i) + (float)alpha256 / 256.0f * 0.9f;
            int ex = (int)(cosf(ra) * 17), ey = (int)(sinf(ra) * 17);
            SDL_SetRenderDrawColor(r, 30, 30, 30, 170);
            SDL_RenderDrawLine(r, hx - ex, hy - ey, hx + ex, hy + ey);
        }
        draw_ring_px(r, hx, hy, 17, 40);
    }

    draw_lights(r, g, style, view, cam_x, cam_y, alpha256);
    if (weather_effects >= 0)
        render_weather(r, &clip, g->tick, weather_visual((Biome)g->biome, g->weather), g->map.time, weather_effects);
    for (i = 0; i < MAX_SHOTS; i++) {
        const Shot *s = &g->shots[i];
        if (!s->active) continue;
        draw_shot(r, s, fx_to_int(fx_lerp(s->prev_x, s->x, alpha256)) - cam_x,
                  fx_to_int(fx_lerp(s->prev_y, s->y, alpha256)) - cam_y);
    }
    if (g->extras & EXTRA_CONQUEST) {
        for (i = 0; i < g->num_cp; i++) {
            int take = g->cp_take[i], amount = abs(take);
            int x = g->cp_tx[i] * TILE_SIZE + TILE_SIZE / 2 - cam_x;
            int y = g->cp_ty[i] * TILE_SIZE + TILE_SIZE / 2 - cam_y + 19;
            int w = (amount * 48 + CP_TAKE - 1) / CP_TAKE;
            if (!amount || amount >= CP_TAKE) continue;
            ui_fill(r, x - 26, y - 1, 52, 8, (Rgb){ 12, 12, 10 });
            ui_fill(r, x - 25, y, 50, 6, (Rgb){ 236, 230, 206 });
            ui_fill(r, x - 24, y + 1, 48, 4, (Rgb){ 54, 54, 51 });
            ui_fill(r, x - 24, y + 1, w, 4, render_side_rgb(take > 0 ? TEAM_PLAYER : TEAM_ENEMY));
        }
    }

    if (SDL_GetRenderTarget(r)) {
        SDL_RenderSetClipRect(r, NULL);
        SDL_RenderSetViewport(r, NULL);
    } else ui_screen_target(r);
}

static void free_lights(void)
{
    if (glow_tex) SDL_DestroyTexture(glow_tex);
    if (cone_tex) SDL_DestroyTexture(cone_tex);
    if (light_map) SDL_DestroyTexture(light_map);
    glow_tex = cone_tex = light_map = NULL;
}

/* ---- HUD ------------------------------------------------------------------------ */

static Uint32 radar_color(int type)
{
    switch (type) {
    case TILE_WALL:     return 0xFF363636u;
    case TILE_WATER:    return 0xFF101C38u;
    case TILE_ICE:      return 0xFF859CAAu;
    case TILE_CRATER:   return 0xFF12121Au;
    case TILE_OIL:      return 0xFF282828u;
    case TILE_ROAD:     return 0xFF282828u;
    default:            return 0xFF000000u;
    }
}

static int radar_zoom = 1;                         /* 1 = local map, 2 = local tank dots */
void render_set_radar_zoom(int z) { radar_zoom = z == 2 ? 2 : 1; }
int  render_radar_zoom(void) { return radar_zoom; }

void render_draw_radar(SDL_Renderer *r, const Game *g, int tank, int x, int y, uint32_t frame)
{
    const Tank *t = tank >= 0 ? &g->tanks[tank] : NULL;
    int h = RADAR_SIZE / 2;
    int w = g->map.w, ht = g->map.h;
    int ctx = t ? fx_to_int(t->x) >> TILE_SHIFT : g->spawn_tx;
    int cty = t ? fx_to_int(t->y) >> TILE_SHIFT : g->spawn_ty;
    void *pixels;
    int pitch, rx, ry, i, k;
    SDL_Rect dst = { x, y, RADAR_SIZE, RADAR_SIZE };
    (void)frame;                                     /* blinking runs on the clock */
    /* Follow the tank, stopping at the map edges so the radar square stays filled. */
    if (ctx > w - h) ctx = w - h;
    if (cty > ht - h) cty = ht - h;
    if (ctx < h) ctx = h;
    if (cty < h) cty = h;
#define RX(tx) ((tx) - ctx + h)
#define RY(ty) ((ty) - cty + h)
#define RXX(v) RX(fx_to_int(v) >> TILE_SHIFT)
#define RYY(v) RY(fx_to_int(v) >> TILE_SHIFT)

    if (SDL_LockTexture(radar_tex, NULL, &pixels, &pitch) != 0) return;
    for (ry = 0; ry < RADAR_SIZE; ry++) {
        Uint32 *row = (Uint32 *)((Uint8 *)pixels + ry * pitch);
        int my = cty + ry - h;
        for (rx = 0; rx < RADAR_SIZE; rx++) {
            int mx = ctx + rx - h;
            row[rx] = (mx < 0 || my < 0 || mx >= w || my >= ht)
                    ? 0xFF101018u : radar_zoom == 2 ? 0xFF000000u :
                    g->map.sid[my][mx] && g->map.st[g->map.sid[my][mx] - 1].hp ? 0xFF888888u : radar_color(g->map.type[my][mx]);
        }
    }
    SDL_UnlockTexture(radar_tex);
    SDL_RenderCopy(r, radar_tex, NULL, &dst);

    for (k = 0; k < 2 && !g->map.arena && radar_zoom == 1; k++) {         /* both camps, outlined in their colours */
        const Base *b = &g->bases[k];
        int x0 = RX(b->x0), x1 = RX(b->x1), y0 = RY(b->y0), y1 = RY(b->y1);
        Rgb c = render_side_rgb(k);
        for (i = x0; i <= x1; i++) {
            if (i < 0 || i >= RADAR_SIZE) continue;
            if (y0 >= 0 && y0 < RADAR_SIZE) ui_fill(r, x + i, y + y0, 1, 1, c);
            if (y1 >= 0 && y1 < RADAR_SIZE) ui_fill(r, x + i, y + y1, 1, 1, c);
        }
        for (i = y0; i <= y1; i++) {
            if (i < 0 || i >= RADAR_SIZE) continue;
            if (x0 >= 0 && x0 < RADAR_SIZE) ui_fill(r, x + x0, y + i, 1, 1, c);
            if (x1 >= 0 && x1 < RADAR_SIZE) ui_fill(r, x + x1, y + i, 1, 1, c);
        }
    }
    if (g->extras & EXTRA_WARLORD) {                  /* the Warlords: big blips */
        int side;
        for (side = 0; side < 2; side++) {
            int jt = g->jug[side];
            if (jt >= 0 && jt < g->num_tanks && g->tanks[jt].active && !g->tanks[jt].respawn) {
                int jx = RXX(g->tanks[jt].x), jy = RYY(g->tanks[jt].y);
                if (jx >= 3 && jy >= 3 && jx < RADAR_SIZE - 3 && jy < RADAR_SIZE - 3) {
                    ui_fill(r, x + jx - 3, y + jy - 3, 7, 7, (Rgb){ 255, 255, 255 });
                    ui_fill(r, x + jx - 2, y + jy - 2, 5, 5, render_side_rgb(side));
                }
            }
        }
    }
    if (g->drop.active) {                                /* the supply drop, blinking yellow */
        int hx = RX(g->drop.tx), hy = RY(g->drop.ty);
        if (hx >= 2 && hy >= 2 && hx < RADAR_SIZE - 2 && hy < RADAR_SIZE - 2 && (g->tick / 15) % 2)
            ui_fill(r, x + hx - 2, y + hy - 2, 4, 4, (Rgb){ 255, 220, 60 });
    }
    if (g->extras & EXTRA_HILL) {                        /* the hill: a small square, the holder's colour */
        int hx = RX(g->hill_tx), hy = RY(g->hill_ty);
        Rgb hc = g->hill_holder >= 0 ? render_side_rgb(g->hill_holder) : (Rgb){ 240, 230, 200 };
        if (hx >= 2 && hy >= 2 && hx < RADAR_SIZE - 2 && hy < RADAR_SIZE - 2) {
            ui_fill(r, x + hx - 2, y + hy - 2, 5, 1, hc); ui_fill(r, x + hx - 2, y + hy + 2, 5, 1, hc);
            ui_fill(r, x + hx - 2, y + hy - 2, 1, 5, hc); ui_fill(r, x + hx + 2, y + hy - 2, 1, 5, hc);
        }
    }
    if (g->extras & EXTRA_PICKUPS)                       /* pickups: white dots */
        for (i = 0; i < MAX_PICKUPS; i++) {
            const Pickup *p = &g->pickups[i];
            int px = RX(p->tx), py = RY(p->ty);
            if (p->active && px >= 0 && py >= 0 && px < RADAR_SIZE && py < RADAR_SIZE) ui_fill(r, x + px, y + py, 1, 1, (Rgb){ 255, 255, 255 });
        }
    for (i = 0; i < g->num_tanks; i++) {               /* blips */
        const Tank *o = &g->tanks[i];
        int bx = RXX(o->x), by = RYY(o->y);
        if (!o->active || o->out || o->respawn || bx < 1 || by < 1 || bx >= RADAR_SIZE - 1 || by >= RADAR_SIZE - 1) continue;
        if (i == tank && !ui_blink(800)) continue;      /* own blip blinks */
        if (sides_mode && o->team == TEAM_ENEMY) {     /* (marks: an enemy is a cross) */
            Rgb ec = render_side_rgb(TEAM_ENEMY);
            ui_fill(r, x + bx - 1, y + by - 1, 1, 1, ec); ui_fill(r, x + bx + 1, y + by - 1, 1, 1, ec);
            ui_fill(r, x + bx, y + by, 1, 1, ec);
            ui_fill(r, x + bx - 1, y + by + 1, 1, 1, ec); ui_fill(r, x + bx + 1, y + by + 1, 1, 1, ec);
        } else ui_fill(r, x + bx - 1, y + by - 1, 2, 2, sides_mode ? render_side_rgb(o->team)
                                                                  : render_side_rgb(o->team))   /* (the world's: its tanks' colours) */;
    }
    if (g->extras & EXTRA_CONQUEST) {
        for (i = 0; i < g->num_cp; i++) {
            int dy, bx = RX(g->cp_tx[i]), by = RY(g->cp_ty[i]);
            Rgb color = g->cp_owner[i] < 0 ? UI_WHITE : render_side_rgb(g->cp_owner[i]);
            if ((unsigned)bx >= RADAR_SIZE || (unsigned)by >= RADAR_SIZE) continue;
            /* A hollow diamond keeps a tank on the checkpoint visible. */
            for (dy = -2; dy <= 2; dy++) {
                int dx = 2 - abs(dy);
                if ((unsigned)(by + dy) >= RADAR_SIZE) continue;
                if (bx - dx >= 0) ui_fill(r, x + bx - dx, y + by + dy, 1, 1, color);
                if (dx && bx + dx < RADAR_SIZE) ui_fill(r, x + bx + dx, y + by + dy, 1, 1, color);
            }
        }
    }
    if (!t) {
        int bx = RX(ctx), by = RY(cty);
        ui_fill(r, x + bx - 1, y + by - 1, 3, 3, render_side_rgb(TEAM_PLAYER));
    } else if (!g->map.arena && !(g->extras & (EXTRA_NOFLAGS | EXTRA_KILLCONF | EXTRA_HARDPOINT))) {
        for (k = 0; k < 2; k++) {
            const Flag *f = &g->flags[k];
            int bx = RXX(f->x), by = RYY(f->y);
            if (bx >= 0 && by >= 0 && bx < RADAR_SIZE && by < RADAR_SIZE) {
                SDL_Rect marker = { x + bx - 1, y + by - 2, 5, 6 }, clipped;
                if (SDL_IntersectRect(&marker, &dst, &clipped))
                    ui_fill(r, clipped.x, clipped.y, clipped.w, clipped.h, (Rgb){ 0, 0, 0 });
                marker = (SDL_Rect){ x + bx, y + by - 1, 1, 4 };
                if (SDL_IntersectRect(&marker, &dst, &clipped))
                    ui_fill(r, clipped.x, clipped.y, clipped.w, clipped.h, UI_WHITE);
                marker = (SDL_Rect){ x + bx + 1, y + by - 1, 2, 2 };
                if (SDL_IntersectRect(&marker, &dst, &clipped))
                    ui_fill(r, clipped.x, clipped.y, clipped.w, clipped.h, render_side_rgb(k));
            }
        }
    }
    if (t && game_order_active(g, t->team & 1)) {
        const TeamOrder *o = &g->team_order[t->team & 1];
        int bx = RX(o->kind == ORDER_ESCORT ? fx_to_int(g->tanks[o->issuer].x) >> TILE_SHIFT : o->tx);
        int by = RY(o->kind == ORDER_ESCORT ? fx_to_int(g->tanks[o->issuer].y) >> TILE_SHIFT : o->ty);
        if (bx >= 3 && by >= 3 && bx < RADAR_SIZE - 3 && by < RADAR_SIZE - 3) {
            ui_fill(r, x + bx - 3, y + by, 7, 1, UI_WHITE);
            ui_fill(r, x + bx, y + by - 3, 1, 7, UI_WHITE);
        }
    }
#undef RXX
#undef RYY
#undef RX
#undef RY
}

static void label_box(SDL_Renderer *r, int x, int y, int w, const char *label)
{
    ui_box(r, x, y, w, 12);
    ui_text_bold(r, x + (w - ui_text_width(label, 1) - 1) / 2, y + 2, 1, ui_colors()->label, label);
}

static void value_box(SDL_Renderer *r, int x, int y, int w, int icon, Rgb icon_col, const char *val)
{
    ui_box(r, x, y, w, 12);
    if (icon >= 0) {
        ui_icon(r, icon, x + 3, y + 2, icon_col);
        ui_text(r, x + w - 3 - ui_text_width(val, 1), y + 2, 1, ui_colors()->value, val);
    } else {
        ui_text(r, x + (w - ui_text_width(val, 1)) / 2, y + 2, 1, ui_colors()->value, val);
    }
}

void render_hud(SDL_Renderer *r, const Theme *th, const Game *g, int tank, int x, int y, uint32_t frame)
{
    const Tank *t = &g->tanks[tank];
    const TankStats *st = world_tank_stats(g->vehicle_world, (TankClass)t->cls);
    const HudColors *c = &th->hud;
    char buf[32];
    int w = 88, bw = 64, bx = x + (w - bw) / 2, sx = x + (w - 44) / 2;
    int low_fuel = t->fuel < st->fuel * TICK_RATE / 5;

    ui_panel(r, x, y, w, 208);

    label_box(r, bx, y + 8, bw, "SCORE");
    snprintf(buf, sizeof buf, "%06u", (unsigned)(t->score % 1000000u));
    value_box(r, bx, y + 22, bw, -1, c->value, buf);

    snprintf(buf, sizeof buf, "%u/%u", t->pows, st->pows);
    value_box(r, sx - 6, y + 38, 56, ICON_POW, t->pows == st->pows && !ui_blink(1200) ? UI_WHITE : c->frame, buf);
    snprintf(buf, sizeof buf, t->lives == 255 ? "INF" : "%u", t->lives);
    value_box(r, sx, y + 54, 44, ICON_TANK, c->frame, buf);
    if (t->has_flag && !ui_blink(1200)) {
        value_box(r, sx, y + 68, 44, -1, c->value, "FLAG");
    } else {
        snprintf(buf, sizeof buf, "%02u", t->mines);
        value_box(r, sx, y + 68, 44, ICON_MINE, UI_GREY, buf);
    }

    label_box(r, bx, y + 84, bw, (low_fuel && !ui_blink(1200)) ? "" : "FUEL");
    ui_box(r, bx, y + 97, bw, 10);
    ui_bar(r, bx + 4, y + 100, bw - 8, 4, t->fuel, st->fuel * TICK_RATE, c->bar_fuel);

    label_box(r, bx, y + 110, bw, "ARMOR");
    ui_box(r, bx, y + 123, bw, 10);
    ui_bar(r, bx + 4, y + 126, bw - 8, 4, t->armor, game_armor_max(g, tank), c->bar_armor);

    ui_box(r, x + (w - RADAR_SIZE - 6) / 2, y + 138, RADAR_SIZE + 6, RADAR_SIZE + 6);
    render_draw_radar(r, g, tank, x + (w - RADAR_SIZE) / 2, y + 141, frame);
}

/* the two-player panel under each half of the screen: 188 x 64 */
void render_hud_small(SDL_Renderer *r, const Theme *th, const Game *g, int tank, int x, int y,
                      uint32_t frame, const char *who)
{
    const Tank *t = &g->tanks[tank];
    const TankStats *st = world_tank_stats(g->vehicle_world, (TankClass)t->cls);
    const HudColors *c = &th->hud;
    char buf[32];
    int low_fuel = t->fuel < st->fuel * TICK_RATE / 5;
    int rx = x + 188 - RADAR_SIZE - 6;

    ui_panel(r, x, y, 188, 64);
    snprintf(buf, sizeof buf, "%06u", (unsigned)(t->score % 1000000u));
    label_box(r, x + 5, y + 4, 34, who);
    value_box(r, x + 41, y + 4, 78, -1, c->value, buf);

    snprintf(buf, sizeof buf, t->lives == 255 ? "INF" : "%u", t->lives);
    value_box(r, x + 4, y + 18, 30, ICON_TANK, c->frame, buf);
    snprintf(buf, sizeof buf, "%u/%u", t->pows, st->pows);
    value_box(r, x + 35, y + 18, 54, ICON_POW, t->pows == st->pows && !ui_blink(1200) ? UI_WHITE : c->frame, buf);
    if (t->has_flag && !ui_blink(1200)) {
        value_box(r, x + 91, y + 18, 32, -1, c->value, "FLAG");
    } else {
        snprintf(buf, sizeof buf, "%02u", t->mines);
        value_box(r, x + 91, y + 18, 32, ICON_MINE, UI_GREY, buf);
    }

    ui_box(r, x + 5, y + 33, 114, 12);
    ui_text_bold(r, x + 9, y + 35, 1, ui_colors()->label, (low_fuel && !ui_blink(1200)) ? "" : "FUEL");
    ui_bar(r, x + 54, y + 37, 60, 4, t->fuel, st->fuel * TICK_RATE, c->bar_fuel);
    ui_box(r, x + 5, y + 47, 114, 12);
    ui_text_bold(r, x + 9, y + 49, 1, ui_colors()->label, "ARMOR");
    ui_bar(r, x + 54, y + 51, 60, 4, t->armor, game_armor_max(g, tank), c->bar_armor);

    ui_box(r, rx - 3, y + 1, RADAR_SIZE + 6, RADAR_SIZE + 6);
    render_draw_radar(r, g, tank, rx, y + 4, frame);
}

/* a map's colours, like a printed map: the ground in its world's colour */
static Uint32 map_color(int type, int biome)
{
    static const Uint32 ground[BIOME_COUNT] = { 0xFFC8A86Eu, 0xFFC88C50u, 0xFF4A7A30u, 0xFFDCE0EAu, 0xFF3E4654u,
                                                0xFF333333u, 0xFF8A5A34u, 0xFF969696u, 0xFF4A5530u };   /* volcanic, wasteland, moon, swamp */
    switch (type) {
    case TILE_ROAD:     return 0xFF5A5A5Eu;
    case TILE_ICE:      return 0xFFB5D9E8u;
    case TILE_OIL:      return 0xFF22232Bu;
    case TILE_CRATER:   return 0xFF302F37u;
    case TILE_WATER:    return biome == BIOME_SCIFI ? 0xFF8C30A8u : biome == BIOME_VOLCANIC ? 0xFFF06010u   /* lava */
                             : biome == BIOME_WASTELAND ? 0xFF64B428u : biome == BIOME_MOON ? 0xFFA8C0D8u   /* toxic, crater ice */
                             : biome == BIOME_SWAMP ? 0xFF28463Au : 0xFF2E5CB0u;
    case TILE_ROCK:     return 0xFF6E645Au;
    case TILE_CANOPY:   return 0xFF1E5A22u;   /* (the trees overhead: a deeper green) */
    case TILE_TREE:     return biome == BIOME_WINTER ? 0xFF2E5A3Eu : biome == BIOME_SCIFI ? 0xFF46B4E6u
                             : biome == BIOME_VOLCANIC ? 0xFF4A3A30u : biome == BIOME_WASTELAND ? 0xFF6A4A26u
                             : biome == BIOME_MOON ? 0xFFDCE4F0u : biome == BIOME_SWAMP ? 0xFF243A20u : 0xFF2C7A2Cu;
    case TILE_STUMP:    return 0xFF6E8A40u;
    case TILE_WALL:     return 0xFF9A9A9Au;
    case TILE_BUILDING: return 0xFF8A4A26u;
    case TILE_FLOOR:    return 0xFF8C8C8Cu;
    case TILE_RUBBLE: case TILE_RUINS: return 0xFF5A4A3Au;
    default:            return ground[biome < BIOME_COUNT ? biome : 0];
    }
}

static int overview_bright;

void render_map_preview(SDL_Renderer *r, const Game *g, const SDL_Rect *view, uint32_t frame)
{
    overview_bright = 1;
    render_overview(r, g, view, frame);
    overview_bright = 0;
}

/* the whole battlefield at a glance, for a free quarter of a 3-player screen */
void render_overview(SDL_Renderer *r, const Game *g, const SDL_Rect *view, uint32_t frame)
{
    (void)frame;                                     /* blinking runs on the clock */
    static SDL_Texture *tex;
    void *pixels;
    int pitch, x, y, i, side = view->w < view->h ? view->w : view->h;
    SDL_Rect dst = { view->x + (view->w - side) / 2, view->y + (view->h - side) / 2, side, side };
    if (!tex) {
        tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, MAP_W, MAP_H);
        if (!tex) return;
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
    }
    ui_fill(r, view->x, view->y, view->w, view->h, (Rgb){ 16, 16, 24 });
    if (SDL_LockTexture(tex, NULL, &pixels, &pitch) != 0) return;
    for (y = 0; y < MAP_H; y++) {
        Uint32 *row = (Uint32 *)((Uint8 *)pixels + y * pitch);
        for (x = 0; x < MAP_W; x++)
            row[x] = overview_bright ? map_color(g->map.type[y][x], g->biome) : radar_color(g->map.type[y][x]);
    }
    SDL_UnlockTexture(tex);
    {   /* the map's own area, its whole side on the square */
        SDL_Rect src = { 0, 0, g->map.w, g->map.h };
        SDL_RenderCopy(r, tex, &src, &dst);
    }
#define MX(v) (dst.x + (fx_to_int(v) >> TILE_SHIFT) * side / g->map.w)
#define MY(v) (dst.y + (fx_to_int(v) >> TILE_SHIFT) * side / g->map.h)
    for (i = 0; i < 2 && !(g->extras & EXTRA_NOFLAGS) && !g->map.arena; i++) {   /* both flags (not in modes without, nor arenas) */
        const Flag *f = &g->flags[i];
        if (f->state != FLAG_CARRIED) {
            ui_fill(r, MX(f->x) - 2, MY(f->y) - 2, 5, 5, (Rgb){ 0, 0, 0 });
            ui_fill(r, MX(f->x) - 1, MY(f->y) - 1, 3, 3, i ? (Rgb){ 230, 30, 24 } : (Rgb){ 60, 120, 255 });
        }
    }
    for (i = 0; i < MAX_HELIS; i++)
        if (g->helis[i].active) ui_fill(r, MX(g->helis[i].x) - 1, MY(g->helis[i].y) - 1, 2, 2, (Rgb){ 255, 60, 40 });
    for (i = 0; i < g->num_tanks; i++) {                      /* every tank, the carrier blinking */
        const Tank *t = &g->tanks[i];
        if (!t->active || t->respawn || (t->has_flag && !ui_blink(800))) continue;
        ui_fill(r, MX(t->x) - 2, MY(t->y) - 2, 4, 4, (Rgb){ 0, 0, 0 });
        ui_fill(r, MX(t->x) - 1, MY(t->y) - 1, 3, 3, render_side_rgb(t->team));
    }
#undef MX
#undef MY
}

/* a player's tag in the corner of their view: dark plate, side-coloured rim, white letters */
void render_player_tag(SDL_Renderer *r, int x, int y, int player, int team)
{
    char who[4] = { 'P', (char)('1' + player), 0, 0 };
    Rgb rim = render_side_rgb(team);
    SDL_Rect q = { x, y, 21, 12 };
    SDL_SetRenderDrawColor(r, rim.r, rim.g, rim.b, 255);
    SDL_RenderFillRect(r, &q);
    q.x++; q.y++; q.w -= 2; q.h -= 2;
    SDL_SetRenderDrawColor(r, 12, 12, 16, 230);
    SDL_RenderFillRect(r, &q);
    ui_text_bold(r, x + 3, y + 2, 1, UI_WHITE, who);
}

static void render_snow_or_sand(SDL_Renderer *ren, const SDL_Rect *v, uint32_t tick, int weather)
{
    int i, snowstorm = weather == WEATHER_SNOWSTORM, snow = weather != WEATHER_SANDSTORM;
    int count = snowstorm ? 115 : snow ? 52 : 65;
    SDL_SetRenderDrawColor(ren, 247, snow ? 250 : 190, snow ? 255 : 114, snow ? 225 : 180);
    for (i = 0; i < count; i += 1 + (int)fx_budget_quality) {
        uint32_t h = (uint32_t)(i + 1) * 2654435761u;
        unsigned sway = (h + tick / 3) & 31u;
        unsigned dx = snowstorm ? tick * (7 + i % 6) : snow ? (sway < 16 ? sway : 31 - sway) : tick * (12 + i % 6);
        unsigned dy = snow ? tick * ((snowstorm ? 3 : 1) + i % 3) : tick / 3;
        int x = (int)(((h >> 8) + dx) % (unsigned)v->w);
        int y = (int)(((h >> 20) + dy) % (unsigned)v->h);
        int width = snowstorm ? 2 + i % 4 : snow ? 1 + (i % 4 == 0) : 3 + i % 6;
        int height = snow && width == 2 ? 2 : 1;
        SDL_Rect particle = { v->x + x, v->y + y, width < v->w - x ? width : v->w - x,
                              height < v->h - y ? height : v->h - y };
        SDL_RenderFillRect(ren, &particle);
    }
}

static void render_eruption(SDL_Renderer *ren, const SDL_Rect *v, uint32_t tick)
{
    int i;
    SDL_SetRenderDrawColor(ren, 104, 33, 26, 74);
    SDL_RenderFillRect(ren, v);
    for (i = 0; i < 52; i++) {
        uint32_t h = (uint32_t)(i + 1) * 2654435761u;
        int ember = i < 16;
        unsigned up = ember ? 0u - tick * (1 + i % 3) : tick / 3;
        int x = (int)(((h >> 8) + tick * (1 + i % 3)) % (unsigned)v->w);
        int y = (int)(((h >> 20) + up) % (unsigned)v->h);
        if (ember) {
            SDL_SetRenderDrawColor(ren, 226, 74, 20, 160);
            SDL_RenderDrawLine(ren, v->x + (x > 2 ? x - 2 : 0), v->y + (y + 2 < v->h ? y + 2 : v->h - 1),
                               v->x + x, v->y + y);
            SDL_SetRenderDrawColor(ren, 255, 199, 79, 245);
        } else SDL_SetRenderDrawColor(ren, 178, 161, 154, 165);
        SDL_RenderDrawPoint(ren, v->x + x, v->y + y);
    }
}

static void render_meteors(SDL_Renderer *ren, const SDL_Rect *v, uint32_t tick, int at_night)
{
    int i;
    for (i = 0; i < (at_night ? 8 : 14); i++) {
        uint32_t h = (uint32_t)(i + 1) * 2654435761u;
        unsigned phase = (tick + (h >> 16)) % 96u;
        int x, y, length = at_night ? 19 : 11;
        if (phase >= 28) continue;
        x = (int)(((h >> 8) + phase * 6) % (unsigned)v->w);
        y = (int)(((h >> 21) + phase * 3) % (unsigned)v->h);
        SDL_SetRenderDrawColor(ren, at_night ? 155 : 228, at_night ? 198 : 157, at_night ? 248 : 91, 192);
        SDL_RenderDrawLine(ren, v->x + (x > length ? x - length : 0), v->y + (y > length / 2 ? y - length / 2 : 0),
                           v->x + x, v->y + y);
        SDL_SetRenderDrawColor(ren, 255, 249, 224, 255);
        SDL_RenderDrawPoint(ren, v->x + x, v->y + y);
    }
}

/* A moving screen-space overlay, independent of the battlefield simulation. */
void render_weather(SDL_Renderer *ren, const SDL_Rect *v, uint32_t tick, int weather, int time, int flashes)
{
    int i, storm = weather == WEATHER_STORM;
    SDL_BlendMode old;
    if (weather == WEATHER_CLEAR || weather == WEATHER_CHANGING) return;
    SDL_GetRenderDrawBlendMode(ren, &old);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    if (weather == WEATHER_FOG || weather == WEATHER_SANDSTORM || weather == WEATHER_SNOWSTORM) {
        if (weather == WEATHER_FOG) SDL_SetRenderDrawColor(ren, 178, 189, 199, 116);
        else if (weather == WEATHER_SNOWSTORM) SDL_SetRenderDrawColor(ren, 169, 190, 215, 83);
        else SDL_SetRenderDrawColor(ren, 187, 141, 76, 87);
        SDL_RenderFillRect(ren, v);
    }
    if (weather == WEATHER_CLOUDY) {
        SDL_SetRenderDrawColor(ren, 32, 39, 54, 75);
        for (i = 0; i < 3; i++) {
            int x = (int)((tick / (4 + i) + 81 + 117 * i) % (unsigned)(v->w + 108)) - 54;
            int y = v->h * (i + 1) / 4;
            int row;
            for (row = 0; row < WEATHER_CLOUD_ROWS; row++) {
                int span = 2 * weather_cloud_span[row], left = x - span, right = x + span;
                int yy = y + row * 2;
                if (left < 0) left = 0;
                if (right > v->w) right = v->w;
                if (left < right && yy < v->h) {
                    SDL_Rect shadow = { v->x + left, v->y + yy, right - left, yy + 1 < v->h ? 2 : 1 };
                    SDL_RenderFillRect(ren, &shadow);
                }
            }
        }
    }
    if (!storm && weather != WEATHER_RAIN) {
        if (weather == WEATHER_SNOW || weather == WEATHER_SNOWSTORM || weather == WEATHER_SANDSTORM)
            render_snow_or_sand(ren, v, tick, weather);
        else if (weather == WEATHER_ERUPTION) render_eruption(ren, v, tick);
        else if (weather == WEATHER_METEOR) render_meteors(ren, v, tick, time == TIME_NIGHT);
        SDL_SetRenderDrawBlendMode(ren, old);
        return;
    }
    if (storm) {
        SDL_SetRenderDrawColor(ren, 35, 44, 65, 64);
        SDL_RenderFillRect(ren, v);
    }
    SDL_SetRenderDrawColor(ren, 195, 218, 248, storm ? 185 : 150);
    for (i = 0; i < (storm ? 100 : 48); i += 1 + (int)fx_budget_quality) {
        uint32_t h = (uint32_t)(i + 1) * 2654435761u;
        int speed = (storm ? 10 : 5) + i % 4;
        int x = (int)(((h >> 8) + tick * (storm ? 4u : 2u)) % (uint32_t)v->w);
        int y = (int)(((h >> 20) + tick * (uint32_t)speed) % (uint32_t)v->h);
        int end_x = x + (storm ? 4 : 2), end_y = y + (storm ? 9 : 5) + i % 3;
        SDL_RenderDrawLine(ren, v->x + x, v->y + y,
                           v->x + (end_x < v->w ? end_x : v->w - 1),
                           v->y + (end_y < v->h ? end_y : v->h - 1));
    }
    if (storm && flashes && weather_lightning(tick)) {
        SDL_SetRenderDrawColor(ren, 210, 228, 255, 82);
        SDL_RenderFillRect(ren, v);
    }
    SDL_SetRenderDrawBlendMode(ren, old);
}

