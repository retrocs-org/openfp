/* SPDX-License-Identifier: MIT
 * .ofpmap: maps as text, read and written in memory. See world.h. */
#include <string.h>
#include "core/world.h"

static const char TILE_CH[TILE_COUNT] = {
    [TILE_SAND] = '.', [TILE_ROAD] = '=', [TILE_WATER] = '~', [TILE_ROCK] = '^', [TILE_TREE] = 'T',
    [TILE_STUMP] = ',', [TILE_WALL] = '#', [TILE_RUBBLE] = ':', [TILE_BUILDING] = '.', [TILE_RUINS] = 'x',
    [TILE_FLOOR] = '.', [TILE_CANOPY] = 'P',
    [TILE_ICE] = 'I', [TILE_OIL] = 'o', [TILE_CRATER] = 'O',
};

static TileType tile_of_char(char c)
{
    int t;
    for (t = 0; t < TILE_COUNT; t++)
        if (TILE_CH[t] == c && t != TILE_BUILDING && t != TILE_FLOOR) return (TileType)t;
    return TILE_SAND;
}

static int struct_by_name(const char *s, int n)
{
    int t;
    for (t = 1; t < ST_TYPE_COUNT; t++)
        if (struct_info[t].name && (int)strlen(struct_info[t].name) == n && !strncmp(struct_info[t].name, s, (size_t)n))
            return t;
    return -1;
}

/* the next line of text: start and length (without the newline) */
static const char *line_end(const char *p) { while (*p && *p != '\n' && *p != '\r') p++; return p; }
static const char *next_line(const char *p) { p = line_end(p); while (*p == '\r' || *p == '\n') p++; return p; }

static int read_int(const char **p)
{
    int v = 0, neg = 0;
    while (**p == ' ' || **p == '\t') (*p)++;
    if (**p == '-') { neg = 1; (*p)++; }
    while (**p >= '0' && **p <= '9') {                    /* (a huge number: held at a million, no overflow) */
        if (v < 1000000) v = v * 10 + (**p - '0');
        (*p)++;
    }
    return neg ? -v : v;
}

static void read_detail(char *out, unsigned cap, const char *value)
{
    unsigned n = 0;
    while (*value && *value != '\r' && *value != '\n' && n + 1 < cap) {
        unsigned char ch = (unsigned char)*value++;
        if (ch >= ' ' && ch <= '~') out[n++] = (char)ch;
    }
    out[n] = 0;
}

static int starts(const char *p, const char *word) { return !strncmp(p, word, strlen(word)); }

Biome mapfile_biome(const char *text)
{
    const char *p;
    Biome biome = BIOME_CLASSIC;
    if (!text) return biome;
    for (p = next_line(text); *p && !starts(p, "tiles"); p = next_line(p)) {
        if (starts(p, "biome=")) {
            char name[16];
            read_detail(name, sizeof name, p + 6);
            biome = biome_from_name(name);
        }
    }
    return biome;
}

static int fits(const Map *m, int type, int x, int y)
{
    const StructInfo *si = &struct_info[type];
    int i, j;
    if (x < 1 || y < 1 || x + si->w > m->w - 1 || y + si->h > m->h - 1) return 0;
    for (j = y; j < y + si->h; j++)
        for (i = x; i < x + si->w; i++)
            if (m->sid[j][i] || m->type[j][i] == TILE_WATER || m->type[j][i] == TILE_ROCK) return 0;
    return 1;
}

/* buildings never go inside a camp, its walls or the road out: the bases are
 * built by the game and must stay drivable (a map converted from the original
 * may have a pillbox exactly where our garages end up) */
static int near_base(const Base bases[2], int type, int x, int y)
{
    const StructInfo *si = &struct_info[type];
    int k;
    for (k = 0; k < 2; k++) {
        const Base *b = &bases[k];
        if (x + si->w > b->x0 - 2 && x < b->x1 + 3 && y + si->h > b->y0 - 2 && y < b->y1 + 7) return 1;
    }
    return 0;
}

/* Imported maps retain their original terrain. Each side gets one garage;
 * the open tile directly outside it is also the flag home. */
static void map_garage_base(Map *m, Base *base, int x, int y, int side)
{
    int i;
    memset(base, 0, sizeof *base);
    base->x0 = x; base->x1 = x + 2;
    base->y0 = y; base->y1 = y + 3;
    base->garage = map_place_structure(m, ST_GARAGE, x, y, side);
    base->num_garages = 1;
    base->spawn_tx = x + 1; base->spawn_ty = y + 1;
    base->flag_house = -1;
    for (i = 0; i < MAX_GARAGES; i++) {
        base->class_tx[i] = base->spawn_tx;
        base->class_ty[i] = base->spawn_ty;
        base->garages[i] = i == 0 ? base->garage : -1;
    }
    if ((tile_info[m->type[y + 3][x + 1]].flags & TF_SOLID))
        map_set(m, x + 1, y + 3, TILE_SAND);
}

static int garage_base_valid(const Map *m, int x, int y)
{
    return x >= 1 && y >= 1 && x <= m->w - 4 && y <= m->h - 5;
}

const char *const map_mode_names[MAP_MODE_COUNT] = {
    "CLASSIC", "CAPTURE THE FLAG", "CONQUEST", "TEAM DEATHMATCH", "ANNIHILATION", "WARLORD", "HARDPOINT", "KILL CONFIRMED"
};
static const char *const mode_ids[MAP_MODE_COUNT] = {
    "classic", "flag", "conquest", "deathmatch", "annihilation", "warlord", "hardpoint", "killconfirmed"
};

int map_mode_from_name(const char *name)
{
    int i, len = (int)(line_end(name) - name);
    for (i = 0; i < MAP_MODE_COUNT; i++)
        if (len == (int)strlen(mode_ids[i]) && starts(name, mode_ids[i])) return i;
    return -1;
}

int map_mode_has_flags(int mode)
{
    return mode == MAP_MODE_CLASSIC || mode == MAP_MODE_FLAG;
}

int map_allows_weather(int mode, Biome biome)
{
    return mode < 0 ? biome != BIOME_CLASSIC : mode != MAP_MODE_CLASSIC;
}

int map_editor_mode_for_world(int mode, Biome biome)
{
    return mode == MAP_MODE_CLASSIC && biome != BIOME_CLASSIC ? MAP_MODE_FLAG : mode;
}

int map_editor_allows_weather(int mode, Biome biome)
{
    return biome != BIOME_CLASSIC && map_allows_weather(mode, biome);
}

/* Clamp to the selected size, or cycle between the counts that size supports. */
int map_custom_players(int size, int players, int direction)
{
    static const int min_players[MAP_SIZES] = { 2, 2, 4, 8 };
    static const int max_players[MAP_SIZES] = { 2, 4, 6, 8 };
    int min, max;
    if (size < 0 || size >= MAP_SIZES) size = SIZE_LARGE;
    min = min_players[size]; max = max_players[size];
    if (players < min) players = min;
    if (players > max) players = max;
    players = (players & ~1) + 2 * direction;
    return players < min ? max : players > max ? min : players;
}

static int live_structure(const Map *m, int index)
{
    const Structure *st = &m->st[index];
    return st->type != ST_NONE && m->sid[st->y][st->x] == index + 1;
}

int map_custom_count(const Map *m, int team, int type)
{
    int i, count = 0;
    for (i = 0; i < m->num_st; i++)
        if (m->st[i].team == team && m->st[i].type == type && live_structure(m, i)) count++;
    return count;
}

int map_custom_contains(const Base *base, int type, int x, int y)
{
    const StructInfo *s = &struct_info[type];
    return x >= base->x0 && y >= base->y0 && x + s->w - 1 <= base->x1 && y + s->h - 1 + (type == ST_GARAGE) <= base->y1;
}

int map_editor_owner(const Map *m, const Base bases[2], int type, int x, int y)
{
    int team;
    for (team = 0; team < 2; team++)
        if (map_custom_contains(&bases[team], type, x, y)) return team;
    if (m->custom_bases && (type == ST_GARAGE || type == ST_FLAGHOUSE)) return -1;
    return (struct_info[type].flags & SF_TURRET) ? TEAM_INDEPENDENT : TEAM_ENEMY;
}

void map_editor_update_turrets(Map *m, const Base bases[2])
{
    int i;
    for (i = 0; i < m->num_st; i++) {
        Structure *st = &m->st[i];
        if (live_structure(m, i) && (struct_info[st->type].flags & SF_TURRET))
            st->team = (uint8_t)map_editor_owner(m, bases, st->type, st->x, st->y);
    }
}

static int clamp_tile(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

void map_area_start(MapAreaEdit *edit, const Base bases[2], int team, int x, int y, int handle_radius)
{
    const Base *base = &bases[team];
    int mx = clamp_tile(x, base->x0, base->x1), my = clamp_tile(y, base->y0, base->y1);
    edit->team = team;
    edit->original = edit->bounds = *base;
    edit->start_x = x;
    edit->start_y = y;
    edit->edge_x = edit->edge_y = 0;
    if (x - mx < -handle_radius || x - mx > handle_radius ||
        y - my < -handle_radius || y - my > handle_radius) {
        edit->active = 1;                               /* draw a replacement rectangle */
        return;
    }
    if (x - base->x0 <= handle_radius) edit->edge_x = -1;
    else if (base->x1 - x <= handle_radius) edit->edge_x = 1;
    if (y - base->y0 <= handle_radius) edit->edge_y = -1;
    else if (base->y1 - y <= handle_radius) edit->edge_y = 1;
    edit->active = edit->edge_x || edit->edge_y ? 2 : 3;  /* resize or move */
}

void map_area_move(MapAreaEdit *edit, const Map *m, int x, int y)
{
    Base *b = &edit->bounds;
    const Base *start = &edit->original;
    int dx = x - edit->start_x, dy = y - edit->start_y;
    if (edit->active == 1) {
        int sx = clamp_tile(edit->start_x, 1, m->w - 2), sy = clamp_tile(edit->start_y, 1, m->h - 2);
        x = clamp_tile(x, 1, m->w - 2); y = clamp_tile(y, 1, m->h - 2);
        b->x0 = sx < x ? sx : x; b->x1 = sx > x ? sx : x;
        b->y0 = sy < y ? sy : y; b->y1 = sy > y ? sy : y;
    } else if (edit->active == 2) {
        if (edit->edge_x < 0) b->x0 = clamp_tile(start->x0 + dx, 1, start->x1 - 2);
        if (edit->edge_x > 0) b->x1 = clamp_tile(start->x1 + dx, start->x0 + 2, m->w - 2);
        if (edit->edge_y < 0) b->y0 = clamp_tile(start->y0 + dy, 1, start->y1 - 3);
        if (edit->edge_y > 0) b->y1 = clamp_tile(start->y1 + dy, start->y0 + 3, m->h - 2);
    } else if (edit->active == 3) {
        dx = clamp_tile(dx, 1 - start->x0, m->w - 2 - start->x1);
        dy = clamp_tile(dy, 1 - start->y0, m->h - 2 - start->y1);
        b->x0 = start->x0 + dx; b->x1 = start->x1 + dx;
        b->y0 = start->y0 + dy; b->y1 = start->y1 + dy;
    }
}

int map_area_finish(MapAreaEdit *edit, const Map *m, Base bases[2])
{
    const Base *b = &edit->bounds, *start = &edit->original;
    edit->active = 0;
    if (b->x0 == start->x0 && b->y0 == start->y0 && b->x1 == start->x1 && b->y1 == start->y1) return 0;
    return map_custom_zone(m, bases, edit->team, b->x0, b->y0, b->x1, b->y1) ? 1 : -1;
}

static int zone_valid(const Map *m, const Base *base, const Base *other)
{
    return base->x0 >= 1 && base->y0 >= 1 && base->x1 < m->w - 1 && base->y1 < m->h - 1 &&
           base->x1 - base->x0 >= 2 && base->y1 - base->y0 >= 3 &&
           (base->x1 < other->x0 || base->x0 > other->x1 || base->y1 < other->y0 || base->y0 > other->y1);
}

int map_custom_zone(const Map *m, Base bases[2], int team, int x0, int y0, int x1, int y1)
{
    Base next = bases[team];
    int i;
    next.x0 = x0 < x1 ? x0 : x1; next.x1 = x0 < x1 ? x1 : x0;
    next.y0 = y0 < y1 ? y0 : y1; next.y1 = y0 < y1 ? y1 : y0;
    if (!zone_valid(m, &next, &bases[team ^ 1])) return 0;
    for (i = 0; i < m->num_st; i++) {
        const Structure *st = &m->st[i];
        if (st->team == team && (st->type == ST_GARAGE || st->type == ST_FLAGHOUSE) &&
            live_structure(m, i) && !map_custom_contains(&next, st->type, st->x, st->y)) return 0;
    }
    bases[team] = next;
    return 1;
}

void map_custom_begin(Map *m, Base bases[2], int players, int mode)
{
    map_no_camps(bases);
    m->arena = m->num_spawns = 0;
    m->custom_bases = 1;
    m->players = (uint8_t)map_custom_players(m->size, players, 0);
    m->mode = (uint8_t)mode;
    map_custom_zone(m, bases, TEAM_PLAYER, 2, 2, m->w / 2 - 2, m->h / 2 - 2);
    map_custom_zone(m, bases, TEAM_ENEMY, m->w / 2 + 1, m->h / 2 + 1, m->w - 3, m->h - 3);
}

void map_custom_clear_generated_bases(Map *m, Base bases[2], int players, int mode)
{
    int team, x, y;
    for (team = 0; team < 2; team++) {
        const Base *b = &bases[team];
        for (y = b->y0 - 1; y <= b->y1 + 5 && y < m->h - 1; y++)
            for (x = b->x0 - 1; x <= b->x1 + 1 && x < m->w - 1; x++)
                if (x > 0 && y > 0 && (m->sid[y][x] || m->type[y][x] == TILE_WALL)) map_set(m, x, y, TILE_SAND);
    }
    map_prune(m);
    for (x = 0; x < m->num_st; x++) {
        Structure *st = &m->st[x];
        if (st->type == ST_FLAGHOUSE && live_structure(m, x))
            for (y = st->y; y < st->y + struct_info[ST_FLAGHOUSE].h; y++) {
                int i;
                for (i = st->x; i < st->x + struct_info[ST_FLAGHOUSE].w; i++)
                    if (m->sid[y][i] == x + 1) map_set(m, i, y, TILE_SAND);
            }
    }
    map_prune(m);
    map_custom_begin(m, bases, players, mode);
}

const char *map_custom_error(const Map *m, const Base bases[2])
{
    int i, team;
    if (!m->custom_bases) return NULL;
    if (m->players != map_custom_players(m->size, m->players, 0) || m->mode >= MAP_MODE_COUNT)
        return "CHOOSE A GAME MODE AND PLAYER COUNT";
    for (team = 0; team < 2; team++) {
        int count = map_custom_count(m, team, ST_GARAGE);
        if (!zone_valid(m, &bases[team], &bases[team ^ 1])) return "CHECK THE TEAM AREAS";
        if (count != m->players / 2) return team == TEAM_PLAYER ? "CHECK CAMP GARAGE COUNT" : "CHECK FORTRESS GARAGE COUNT";
        if (map_custom_count(m, team, ST_FLAGHOUSE) > 1) return "ONE FLAG BASE PER TEAM";
    }
    for (i = 0; i < m->num_st; i++) {
        const Structure *st = &m->st[i];
        if ((st->type != ST_GARAGE && st->type != ST_FLAGHOUSE) || !live_structure(m, i)) continue;
        if (!map_custom_contains(&bases[st->team], st->type, st->x, st->y)) return "BUILD INSIDE YOUR TEAM AREA";
        if (st->type == ST_GARAGE && (st->y + 3 >= m->h - 1 ||
            (tile_info[m->type[st->y + 3][st->x + 1]].flags & TF_SOLID))) return "CLEAR THE GARAGE EXIT";
        if (st->type == ST_FLAGHOUSE && !map_mode_has_flags(m->mode)) return "REMOVE FLAG BUILDINGS FIRST";
    }
    return NULL;
}

static void assign_custom_garage(Base *base, int index, int x, int y)
{
    int n = base->num_garages++;
    base->garages[n] = index;
    base->class_tx[n] = x + 1;
    base->class_ty[n] = y + 1;
    if (!n) {
        base->garage = index;
        base->spawn_tx = x + 1;
        base->spawn_ty = y + 1;
    }
}

static int mapfile_load_impl(Map *m, Base bases[2], Biome *biome, char *name, int name_len, const char *text, uint32_t seed, int draft)
{
    const char *p = text, *tiles = NULL;
    int px = 4, py = MAP_H - BASE_H - 8, ex = MAP_W - BASE_W - 4, ey = 4, row = 0, i, m_time = TIME_DAY;
    int m_weather = -1, m_extras = -1, garage_bases = 0;                   /* (-1: the file doesn't say) */
    int compounds[2][4] = {{0}}, custom_mode = -1, file_size = SIZE_LARGE;
    if (!starts(p, "OFPMAP")) return -1;
    map_fill(m, TILE_SAND);
    m->num_points = 0;
    if (biome) *biome = mapfile_biome(text);
    if (name && name_len > 0) name[0] = 0;
    /* tiles and settings first */
    for (p = next_line(p); *p; p = next_line(p)) {
        if (!tiles && starts(p, "name=") && name) {
            const char *e = line_end(p + 5);
            int n = (int)(e - (p + 5));
            if (n > name_len - 1) n = name_len - 1;
            memcpy(name, p + 5, (size_t)n);
            name[n] = 0;
        } else if (!tiles && starts(p, "description=")) {
            read_detail(m->details.description, sizeof m->details.description, p + 12);
        } else if (!tiles && starts(p, "author=")) {
            read_detail(m->details.author, sizeof m->details.author, p + 7);
        } else if (!tiles && starts(p, "contact=")) {
            read_detail(m->details.contact, sizeof m->details.contact, p + 8);
        } else if (starts(p, "time=")) {
            m_time = time_from_name(p + 5);
        } else if (starts(p, "size=")) {                 /* how big (before the tiles; none: LARGE, the original's) */
            int k;
            for (k = 0; k < MAP_SIZES; k++)
                if (!strncmp(p + 5, map_size_name[k], strlen(map_size_name[k]))) {
                    file_size = k;
                    map_set_size(m, k);
                }
        } else if (starts(p, "players=")) {              /* made for so many (2: 1 vs 1) */
            const char *q = p + 8;
            int v = 0;
            while (*q >= '0' && *q <= '9') { if (v < 1000000) v = v * 10 + (*q - '0'); q++; }
            m->players = (uint8_t)(v >= 2 && v <= 8 ? v : 0);
        } else if (starts(p, "ambient=")) {
            const char *value = p + 8;
            m->quiet = line_end(value) - value == 3 && starts(value, "off");
        } else if (starts(p, "arena=")) {                /* an arena: no camps */
            m->arena = starts(p + 6, "yes");
        } else if (starts(p, "spawns=")) {               /* an arena's spawn points: "x,y x,y ..." (tiles, up to 8) */
            const char *q = p + 7;
            int n2 = 0;
            while (n2 < 8 && *q >= '0' && *q <= '9') {
                int px2 = 0, py2 = 0;
                while (*q >= '0' && *q <= '9') { if (px2 < 1000000) px2 = px2 * 10 + (*q - '0'); q++; }
                if (*q++ != ',') break;
                while (*q >= '0' && *q <= '9') { if (py2 < 1000000) py2 = py2 * 10 + (*q - '0'); q++; }
                if (px2 >= 2 && py2 >= 2 && px2 < MAP_W - 2 && py2 < MAP_H - 2) { m->spawn_tx[n2] = (int16_t)px2; m->spawn_ty[n2] = (int16_t)py2; n2++; }
                while (*q == ' ') q++;
            }
            m->num_spawns = (uint8_t)n2;
        } else if (starts(p, "points=")) {               /* conquest: "x,y x,y x,y" (tiles) */
            const char *q = p + 7;                       /* (read by hand: no stdio, for the web's core) */
            int n2 = 0;
            while (n2 < 3 && *q >= '0' && *q <= '9') {
                int px2 = 0, py2 = 0;
                while (*q >= '0' && *q <= '9') { if (px2 < 1000000) px2 = px2 * 10 + (*q - '0'); q++; }
                if (*q++ != ',') break;
                while (*q >= '0' && *q <= '9') { if (py2 < 1000000) py2 = py2 * 10 + (*q - '0'); q++; }
                if (px2 >= 2 && py2 >= 2 && px2 < MAP_W - 2 && py2 < MAP_H - 2) { m->point_tx[n2] = (int16_t)px2; m->point_ty[n2] = (int16_t)py2; n2++; }
                while (*q == ' ') q++;
            }
            m->num_points = (uint8_t)n2;
        } else if (starts(p, "weather=")) {
            m_weather = weather_from_name(p + 8);
        } else if (starts(p, "extras=")) {               /* none, pickups, hill, or pickups,hill */
            const char *q = p + 7;
            m_extras = 0;
            while (*q && *q != '\n' && *q != '\r') {
                if (starts(q, "pickups")) m_extras |= 1;
                else if (starts(q, "hill")) m_extras |= 2;
                while (*q && *q != ',' && *q != '\n' && *q != '\r') q++;
                if (*q == ',') q++;
                while (*q == ' ') q++;
            }
        } else if (starts(p, "roads=preserve")) {
            m->preserve_roads = line_end(p) - p == (int)sizeof("roads=preserve") - 1;
        } else if (starts(p, "mode=")) {
            custom_mode = map_mode_from_name(p + 5);
        } else if (starts(p, "bases=custom") && line_end(p) - p == 12) {
            m->custom_bases = 1;
        } else if (starts(p, "bases=garages")) {
            garage_bases = line_end(p) == p + sizeof("bases=garages") - 1;
        } else if (starts(p, "compound=player ") || starts(p, "compound=enemy ")) {
            int side = starts(p, "compound=player ") ? TEAM_PLAYER : TEAM_ENEMY;
            const char *q = p + (side == TEAM_PLAYER ? 16 : 15);
            for (i = 0; i < 4; i++) compounds[side][i] = read_int(&q);
        } else if (starts(p, "base=player")) {
            const char *q = p + 11;
            px = read_int(&q); py = read_int(&q);
        } else if (starts(p, "base=enemy")) {
            const char *q = p + 10;
            ex = read_int(&q); ey = read_int(&q);
        } else if (starts(p, "tiles")) {
            tiles = next_line(p);
            for (row = 0; row < m->h; row++) {         /* (its own size: rows of its width) */
                const char *e;
                p = next_line(p);
                if (!*p) break;
                e = line_end(p);
                for (i = 0; i < m->w && p + i < e; i++)
                    map_set(m, i, row, tile_of_char(p[i]));
            }
        } else if (starts(p, "end")) {
            break;
        }
    }
    /* the bases go in, then the buildings (any that still fit); an arena has none */
    if (m->custom_bases) {
        if (custom_mode < 0 || m->arena || garage_bases || !map_size_fits(file_size)) return -1;
        m->mode = (uint8_t)custom_mode;
        map_no_camps(bases);
        for (i = 0; i < 2; i++) {
            Base *b = &bases[i];
            b->x0 = compounds[i][0]; b->y0 = compounds[i][1];
            b->x1 = compounds[i][2]; b->y1 = compounds[i][3];
        }
        if (!zone_valid(m, &bases[0], &bases[1]) || !zone_valid(m, &bases[1], &bases[0])) return -1;
    } else if (m->arena && m->num_spawns) {
        map_no_camps(bases);
        garage_bases = 0;
    } else {
        m->arena = 0;
        if (garage_bases) {
            if (!garage_base_valid(m, px, py) || !garage_base_valid(m, ex, ey) ||
                (px < ex + 3 && ex < px + 3 && py < ey + 4 && ey < py + 4))
                return -1;
            map_garage_base(m, &bases[TEAM_PLAYER], px, py, TEAM_PLAYER);
            map_garage_base(m, &bases[TEAM_ENEMY], ex, ey, TEAM_ENEMY);
            for (i = 0; i < 2; i++) {
                Base *b = &bases[i];
                const int *bounds = compounds[i];
                if (!bounds[0]) continue;
                if (bounds[0] < 1 || bounds[1] < 1 || bounds[2] >= m->w - 1 || bounds[3] >= m->h - 1 ||
                    bounds[0] > b->x0 || bounds[1] > b->y0 || bounds[2] < b->x1 || bounds[3] < b->y1)
                    return -1;
                b->x0 = bounds[0]; b->y0 = bounds[1];
                b->x1 = bounds[2]; b->y1 = bounds[3];
            }
            if (bases[0].x0 <= bases[1].x1 && bases[1].x0 <= bases[0].x1 &&
                bases[0].y0 <= bases[1].y1 && bases[1].y0 <= bases[0].y1) return -1;
            for (i = 0; i < m->w; i++) {
                map_set(m, i, 0, TILE_ROCK);
                map_set(m, i, m->h - 1, TILE_ROCK);
            }
            for (i = 0; i < m->h; i++) {
                map_set(m, 0, i, TILE_ROCK);
                map_set(m, m->w - 1, i, TILE_ROCK);
            }
        } else {
            if (px < 0 || py < 0 || ex < 0 || ey < 0 || px >= m->w || py >= m->h || ex >= m->w || ey >= m->h)
                return -1;                               /* (a base off the map: a broken file) */
            worldgen_bases(m, seed, px, py, ex, ey, bases);
            if (bases[0].x0 <= bases[1].x1 && bases[1].x0 <= bases[0].x1 &&
                bases[0].y0 <= bases[1].y1 && bases[1].y0 <= bases[0].y1) return -1;   /* (one on the other) */
        }
    }
    if (m->preserve_roads && tiles) {
        /* A generated base can clear the original asphalt. Restore painted roads
         * only where the finished base left open sand, including its gate tiles. */
        for (row = 0, p = tiles; row < m->h && *p; row++, p = next_line(p)) {
            const char *end = line_end(p);
            for (i = 0; i < m->w && i < end - p; i++)
                if (p[i] == '=' && m->type[row][i] == TILE_SAND && !m->sid[row][i])
                    map_set(m, i, row, TILE_ROAD);
        }
    }
    for (p = text; *p; p = next_line(p)) {
        if (starts(p, "struct=")) {
            const char *q = p + 7, *w = q;
            int type, team, x, y;
            while (*q && *q != ' ' && *q != '\n') q++;
            type = struct_by_name(w, (int)(q - w));
            while (*q == ' ') q++;
            team = starts(q, "player") ? TEAM_PLAYER : starts(q, "independent ") ? TEAM_INDEPENDENT : TEAM_ENEMY;
            if (team == TEAM_INDEPENDENT && (type <= 0 || !(struct_info[type].flags & SF_TURRET))) return -1;
            while (*q && *q != ' ' && *q != '\n') q++;
            x = read_int(&q); y = read_int(&q);
            if (m->custom_bases) {
                Base *base = &bases[team & 1];
                int placed;
                if (type <= 0 || !fits(m, type, x, y)) return -1;
                if ((type == ST_GARAGE || type == ST_FLAGHOUSE) && !map_custom_contains(base, type, x, y)) return -1;
                if (type == ST_GARAGE && base->num_garages >= MAX_GARAGES) return -1;
                placed = map_place_structure(m, (StructType)type, x, y, team);
                if (placed < 0) return -1;
                if (type == ST_GARAGE) assign_custom_garage(base, placed, x, y);
                else if (type == ST_FLAGHOUSE) base->flag_house = placed;
            } else if (type > 0 && type != ST_GARAGE && fits(m, type, x, y)) {
                if (garage_bases) {
                    int k;
                    for (k = 0; k < 2; k++)
                        if (x <= bases[k].spawn_tx && x + struct_info[type].w > bases[k].spawn_tx &&
                            y <= bases[k].spawn_ty + 2 && y + struct_info[type].h > bases[k].spawn_ty + 2) break;
                    if (k < 2) continue;
                } else if (near_base(bases, type, x, y)) continue;
                int placed = map_place_structure(m, (StructType)type, x, y, team);
                Base *base = &bases[team & 1];
                if (garage_bases && placed >= 0 && type == ST_FLAGHOUSE && base->flag_house < 0 &&
                    x >= base->x0 && y >= base->y0 &&
                    x + struct_info[type].w - 1 <= base->x1 && y + struct_info[type].h - 1 <= base->y1)
                    base->flag_house = placed;
            }
        } else if (starts(p, "end")) {
            break;
        }
    }
    if (!draft && map_custom_error(m, bases)) return -1;
    if (m->custom_bases)
        for (i = 0; i < 2; i++) {
            int k;
            Base *base = &bases[i];
            for (k = base->num_garages; k < MAX_GARAGES; k++) {
                base->garages[k] = -1;
                base->class_tx[k] = base->spawn_tx;
                base->class_ty[k] = base->spawn_ty;
            }
        }
    if (!m->custom_bases && !m->arena && !garage_bases) worldgen_protect_camp(m, &bases[TEAM_PLAYER]);   /* a fair start: no enemy guns at our gate */
    m->time = (uint8_t)m_time;
    m->has_weather = m_weather >= 0; m->weather = (uint8_t)(m_weather > 0 ? m_weather : 0);
    m->has_extras = m_extras >= 0;   m->extras = (uint8_t)(m_extras > 0 ? m_extras : 0);
    return 0;
}

static int inside_base(const Base *b, int x, int y)
{
    return x >= b->x0 && x <= b->x1 && y >= b->y0 && y <= b->y1 + 5;   /* and the road out */
}

static unsigned mapfile_save_impl(const Map *m, const Base bases[2], Biome biome, const char *name, char *out, unsigned cap, int draft)
{
    unsigned n = 0;
    int x, y, i;
    int garage_bases = !m->custom_bases && bases[0].num_garages == 1 && bases[1].num_garages == 1;
    if (!draft && map_custom_error(m, bases)) return 0;
#define PUT(c) do { if (n + 1 >= cap) return 0; out[n++] = (char)(c); } while (0)
#define PUTS(s) do { const char *s_ = (s); while (*s_) PUT(*s_++); } while (0)
#define PUT_FIELD(key, value) do { const unsigned char *ch_ = (const unsigned char *)(value); \
                                   PUTS(key); while (*ch_) { if (*ch_ >= ' ' && *ch_ <= '~') PUT(*ch_); ch_++; } } while (0)
#define PUTI(v) do { char b_[12]; int k_ = 0, v_ = (v); if (v_ < 0) { PUT('-'); v_ = -v_; } \
                     do b_[k_++] = (char)('0' + v_ % 10); while ((v_ /= 10) > 0); \
                     while (k_) PUT(b_[--k_]); } while (0)
    PUTS("OFPMAP 1");
    PUT_FIELD("\nname=", name && name[0] ? name : "Untitled");
    if (m->details.description[0]) PUT_FIELD("\ndescription=", m->details.description);
    if (m->details.author[0]) PUT_FIELD("\nauthor=", m->details.author);
    if (m->details.contact[0]) PUT_FIELD("\ncontact=", m->details.contact);
    PUTS("\nbiome=");
    PUTS(biome_name(biome));
    PUTS("\ntime="); PUTS(time_name(m->time));
    if (biome == BIOME_CLASSIC && m->custom_bases && m->mode != MAP_MODE_CLASSIC) {
        /* Keep a Classic-world map clear even when it uses enhanced rules and the host sets weather. */
        PUTS("\nweather=clear");
    } else if (m->has_weather && map_editor_allows_weather(m->custom_bases ? m->mode : -1, biome) &&
               weather_allowed(biome, m->weather)) {
        PUTS("\nweather="); PUTS(weather_name(m->weather));
    }
    if (m->quiet) PUTS("\nambient=off");
    if (m->players) { PUTS("\nplayers="); PUTI(m->players); }   /* (made for so many) */
    if (m->size != SIZE_LARGE && m->size < MAP_SIZES) { PUTS("\nsize="); PUTS(map_size_name[m->size]); }   /* (LARGE: none, as before) */
    if (m->arena && m->num_spawns) {                     /* an arena, and its spawn points */
        int k3;
        PUTS("\narena=yes\nspawns=");
        for (k3 = 0; k3 < (int)m->num_spawns && k3 < 8; k3++) {
            if (k3) PUT(' ');
            PUTI(m->spawn_tx[k3]); PUT(','); PUTI(m->spawn_ty[k3]);
        }
    }
    if (m->num_points) {                                 /* conquest's capture points (no stdio: the web's core too) */
        int k2;
        PUTS("\npoints=");
        for (k2 = 0; k2 < (int)m->num_points && k2 < 3; k2++) {
            char pb[16];
            int v[2] = { m->point_tx[k2], m->point_ty[k2] }, q, pn = 0;
            if (k2) pb[pn++] = ' ';
            for (q = 0; q < 2; q++) {
                int val = v[q] < 0 ? 0 : v[q];
                if (q) pb[pn++] = ',';
                if (val >= 100) pb[pn++] = (char)('0' + val / 100);
                if (val >= 10) pb[pn++] = (char)('0' + val / 10 % 10);
                pb[pn++] = (char)('0' + val % 10);
            }
            pb[pn] = 0;
            PUTS(pb);
        }
    }
    if (m->has_extras) {
        PUTS("\nextras=");
        PUTS(m->extras == 0 ? "none" : m->extras == 1 ? "pickups" : m->extras == 2 ? "hill" : "pickups,hill");
    }
    if (m->preserve_roads) PUTS("\nroads=preserve");
    if (m->custom_bases) {
        PUTS("\nbases=custom\nmode="); PUTS(mode_ids[m->mode]);
        PUTS("\nmodes="); PUTS(m->mode == MAP_MODE_CLASSIC ? "flag" : mode_ids[m->mode]);
    } else if (garage_bases) PUTS("\nbases=garages");
    for (i = 0; i < 2; i++) {
        const Base *base = &bases[i];
        int gx = garage_bases ? base->spawn_tx - 1 : base->x0;
        int gy = garage_bases ? base->spawn_ty - 1 : base->y0;
        if (!m->custom_bases) {
            PUTS(i == TEAM_PLAYER ? "\nbase=player " : "\nbase=enemy ");
            PUTI(gx); PUT(' '); PUTI(gy);
        }
        if (m->custom_bases || (garage_bases && (base->x0 != gx || base->y0 != gy || base->x1 != gx + 2 || base->y1 != gy + 3))) {
            PUTS(i == TEAM_PLAYER ? "\ncompound=player " : "\ncompound=enemy ");
            PUTI(base->x0); PUT(' '); PUTI(base->y0); PUT(' '); PUTI(base->x1); PUT(' '); PUTI(base->y1);
        }
    }
    PUTS("\ntiles\n");
    for (y = 0; y < m->h; y++) {
        for (x = 0; x < m->w; x++) PUT(TILE_CH[m->type[y][x] < TILE_COUNT ? m->type[y][x] : 0]);
        PUT('\n');
    }
    for (i = 0; i < m->num_st; i++) {
        const Structure *st = &m->st[i];
        if (st->type == ST_NONE || (!m->custom_bases && st->type == ST_GARAGE) || (st->hp == 0 && struct_info[st->type].hp)) continue;
        if (m->sid[st->y][st->x] != i + 1) continue;          /* built over */
        if (!m->custom_bases && !garage_bases && (inside_base(&bases[0], st->x, st->y) || inside_base(&bases[1], st->x, st->y))) continue;
        PUTS("struct="); PUTS(struct_info[st->type].name);
        PUTS(st->team == TEAM_INDEPENDENT ? " independent " : st->team == TEAM_PLAYER ? " player " : " enemy ");
        PUTI(st->x); PUT(' '); PUTI(st->y); PUT('\n');
    }
    PUTS("end\n");
    out[n] = 0;
    return n;
#undef PUT
#undef PUTS
#undef PUTI
#undef PUT_FIELD
}

int mapfile_load(Map *m, Base bases[2], Biome *biome, char *name, int name_len, const char *text, uint32_t seed)
{ return mapfile_load_impl(m, bases, biome, name, name_len, text, seed, 0); }
int mapfile_load_draft(Map *m, Base bases[2], Biome *biome, char *name, int name_len, const char *text, uint32_t seed)
{ return mapfile_load_impl(m, bases, biome, name, name_len, text, seed, 1); }
unsigned mapfile_save(const Map *m, const Base bases[2], Biome biome, const char *name, char *out, unsigned cap)
{ return mapfile_save_impl(m, bases, biome, name, out, cap, 0); }
unsigned mapfile_save_draft(const Map *m, const Base bases[2], Biome biome, const char *name, char *out, unsigned cap)
{ return mapfile_save_impl(m, bases, biome, name, out, cap, 1); }
