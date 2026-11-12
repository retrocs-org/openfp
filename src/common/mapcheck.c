/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include "common/mapcheck.h"

int map_issue_has_location(const Map *map, const MapIssue *issue)
{
    return issue->x > 0 && issue->x < map->w - 1 && issue->y > 0 && issue->y < map->h - 1;
}

typedef struct { int x, y; } Tile;
static uint16_t queue[MAP_W * MAP_H];
static uint8_t from[MAP_H][MAP_W];
static const int dx[] = { 1, -1, 0, 0 };
static const int dy[] = { 0, 0, 1, -1 };

static int inside(const Map *m, Tile t)
{
    return t.x > 0 && t.x < m->w - 1 && t.y > 0 && t.y < m->h - 1;
}

static int passable(const Map *m, Tile t, int team, int breach)
{
    int sid;
    if (!inside(m, t)) return 0;
    if (!(tile_info[m->type[t.y][t.x]].flags & TF_SOLID)) return 1;
    sid = m->sid[t.y][t.x];
    if (sid) {
        const Structure *s = &m->st[sid - 1];
        const StructInfo *info = &struct_info[s->type];
        if ((info->flags & SF_GATE) && s->team == team) return 1;
        return breach && s->hp && !(info->flags & SF_INDESTRUCTIBLE);
    }
    return breach && m->hp[t.y][t.x] > 0;
}

static void issue(MapCheck *c, int error, Tile t, const char *text)
{
    int at = error ? c->errors : c->count;
    if (at >= MAP_CHECK_MAX) return;
    if (c->count < MAP_CHECK_MAX) c->count++;
    memmove(&c->issues[at + 1], &c->issues[at], (size_t)(c->count - at - 1) * sizeof c->issues[0]);
    if (error) c->errors++;
    snprintf(c->issues[at].text, sizeof c->issues[at].text, "%s", text);
    c->issues[at].x = (int16_t)t.x;
    c->issues[at].y = (int16_t)t.y;
}

static void flood(const Map *m, Tile start, int team, int breach)
{
    int head = 0, tail = 0;
    memset(from, 0, sizeof from);
    if (!passable(m, start, team, breach)) return;
    from[start.y][start.x] = 5;
    queue[tail++] = (uint16_t)(start.y * MAP_W + start.x);
    while (head < tail) {
        unsigned cell = queue[head++];
        Tile p = { (int)(cell % MAP_W), (int)(cell / MAP_W) };
        int d;
        for (d = 0; d < 4; d++) {
            Tile t = { p.x + dx[d], p.y + dy[d] };
            if (!passable(m, t, team, breach) || from[t.y][t.x]) continue;
            from[t.y][t.x] = (uint8_t)(d + 1);
            queue[tail++] = (uint16_t)(t.y * MAP_W + t.x);
        }
    }
}

static int seen(const Map *m, Tile t)
{
    return inside(m, t) && from[t.y][t.x];
}

static int route(const Map *m, MapCheck *c, Tile t, int radius, int team, int breach)
{
    int x, y, best = radius * radius + 1;
    Tile goal = t;
    for (y = -radius; y <= radius; y++)
        for (x = -radius; x <= radius; x++) {
            Tile p = { t.x + x, t.y + y };
            int d = x*x + y*y;
            if (d < best && seen(m, p)) { best = d; goal = p; }
        }
    if (best > radius * radius) return 0;
    for (;;) {
        int d = from[goal.y][goal.x] - 1;
        c->route[goal.y][goal.x] |= (uint8_t)(1u << (team + (breach ? 2 : 0)));
        if (d == 4) break;
        goal.x -= dx[d];
        goal.y -= dy[d];
    }
    return 1;
}

static int perimeter_exits(const Map *m, const Base *b)
{
    int side, changes = 0, first = -1, previous = 0;
    if (!inside(m, (Tile){ b->x0, b->y0 }) || !inside(m, (Tile){ b->x1, b->y1 })) return 0;
    for (side = 0; side < 4; side++) {
        int i, span = side % 2 ? b->y1 - b->y0 : b->x1 - b->x0;
        for (i = 0; i <= span; i++) {
            Tile in = { side == 0 ? b->x0 + i : side == 1 ? b->x1 : side == 2 ? b->x1 - i : b->x0,
                        side == 0 ? b->y0 : side == 1 ? b->y0 + i : side == 2 ? b->y1 : b->y1 - i };
            Tile out = { in.x + (side == 1) - (side == 3), in.y + (side == 2) - (side == 0) };
            int open = seen(m, in) && seen(m, out);
            if (first < 0) first = open;
            else changes += open && !previous;
            previous = open;
        }
    }
    return changes + (first && !previous);
}

void map_check_playability(const Game *g, MapCheck *c, int legacy_mode)
{
    const Map *m = &g->map;
    Tile garage[2][MAX_GARAGES], flag[2] = {{-1,-1}, {-1,-1}};
    int count[2] = {0}, bunker[2] = {0};
    int mode = m->custom_bases ? m->mode : legacy_mode;
    int i, team;
    memset(c, 0, sizeof *c);
    if (m->arena) {
        if (m->num_spawns < 2) { issue(c, 1, (Tile){-1,-1}, "NEED AT LEAST TWO ARENA SPAWNS"); return; }
        flood(m, (Tile){ m->spawn_tx[0], m->spawn_ty[0] }, TEAM_PLAYER, 0);
        for (i = 0; i < m->num_spawns; i++) {
            Tile t = { m->spawn_tx[i], m->spawn_ty[i] };
            if (!route(m, c, t, 0, TEAM_PLAYER, 0)) issue(c, 1, t, "ARENA SPAWN IS ISOLATED");
        }
        return;
    }
    for (i = 0; i < m->num_st; i++) {
        const Structure *s = &m->st[i];
        team = s->team;
        if (team > TEAM_ENEMY || m->sid[s->y][s->x] != i + 1) continue;
        if (s->type == ST_GARAGE && count[team] < MAX_GARAGES)
            garage[team][count[team]++] = (Tile){ s->x + 1, s->y + 3 };
        if (s->type == ST_FLAGHOUSE && !bunker[team] && (m->custom_bases || g->bases[team].flag_house == i)) {
            flag[team] = (Tile){ s->x + 1, s->y + 1 };
            bunker[team] = 1;
        }
    }
    for (team = 0; team < 2; team++) {
        const Base *b = &g->bases[team];
        if (!bunker[team])
            flag[team] = m->custom_bases && count[team] ? garage[team][0] : (Tile){ (b->x0 + b->x1) / 2, b->y0 + 3 };
        if (!count[team] || (m->custom_bases && count[team] != m->players / 2)) {
            char msg[40];
            snprintf(msg, sizeof msg, "TEAM %d: CHECK GARAGE COUNT", team + 1);
            issue(c, 1, flag[team], msg);
        }
    }
    for (team = 0; team < 2; team++) {
        enum { TARGET_MAX = MAX_GARAGES + MAX_CP + 1 };
        Tile goal[TARGET_MAX];
        const char *kind[TARGET_MAX];
        int start = -1, goals = 0, open[TARGET_MAX] = {0}, flag_goal = -1;
        for (i = 0; i < count[team]; i++) {
            Tile t = garage[team][i];
            if (!passable(m, t, team, 0)) {
                char msg[40];
                snprintf(msg, sizeof msg, "TEAM %d GARAGE %d EXIT BLOCKED", team + 1, i + 1);
                issue(c, 1, t, msg);
            } else if (start < 0) start = i;
            else { goal[goals] = t; kind[goals++] = "GARAGE"; }
        }
        if (start < 0) continue;
        if (mode == MAP_MODE_CONQUEST || mode == MAP_MODE_HARDPOINT) {
            int16_t xs[MAX_CP], ys[MAX_CP];
            int n = game_checkpoint_positions(g, xs, ys);
            for (i = 0; i < n; i++) { goal[goals] = (Tile){xs[i], ys[i]}; kind[goals++] = "CHECKPOINT"; }
        } else if (map_mode_has_flags(mode) && count[team ^ 1]) {
            flag_goal = goals;
            goal[goals] = flag[team ^ 1];
            kind[goals++] = "ENEMY FLAG";
        } else if (count[team ^ 1]) {
            goal[goals] = garage[team ^ 1][0];
            kind[goals++] = "ENEMY BASE";
        }
        flood(m, garage[team][start], team, 0);
        if (perimeter_exits(m, &g->bases[team]) == 1)
            issue(c, 0, garage[team][start], team ? "TEAM 2 HAS ONE OPEN EXIT" : "TEAM 1 HAS ONE OPEN EXIT");
        for (i = 0; i < goals; i++) {
            int radius = !strcmp(kind[i], "CHECKPOINT") ? CP_RADIUS - 1 : 0;
            open[i] = route(m, c, goal[i], radius, team, 0);
        }
        for (i = 0; i < goals && open[i]; i++) {}
        if (i == goals) continue;
        flood(m, garage[team][start], team, 1);
        for (i = 0; i < goals; i++) if (!open[i]) {
            int radius = !strcmp(kind[i], "CHECKPOINT") ? CP_RADIUS - 1 : 0;
            int reached = route(m, c, goal[i], radius, team, 1);
            char msg[40];
            if (reached && i == flag_goal && bunker[team ^ 1]) continue;
            snprintf(msg, sizeof msg, "TEAM %d %s: %s", team + 1, kind[i], reached ? "BREACH" : "NO ROUTE");
            issue(c, !reached, goal[i], msg);
        }
    }
}
