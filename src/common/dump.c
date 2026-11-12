/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <string.h>
#include "common/dump.h"
#include "core/match.h"

static uint32_t fnv(const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t h = 2166136261u;
    while (n--) { h ^= *b++; h *= 16777619u; }
    return h;
}

void game_dump(const Game *g, FILE *f)
{
    int i;
    fprintf(f, "tick %u\n", (unsigned)g->tick);
    fprintf(f, "checksum %08x\n", (unsigned)game_checksum(g));
    fprintf(f, "state rng=%08x winner=%d time_up=%d humans_out=%d weather=%d extras=%d adapt=%d adapt_skill=%d versus=%d biome=%d\n",
            (unsigned)g->rng, g->winner, g->time_up, g->humans_out, g->weather, g->extras, g->adapt, g->adapt_skill,
            g->versus, g->biome);
    for (i = 0; i < g->num_tanks; i++) {
        const Tank *t = &g->tanks[i];
        fprintf(f, "tank %d x=%d y=%d angle=%u speed=%d cls=%d team=%d active=%d armor=%d lives=%d mines=%d pows=%d "
                   "score=%u respawn=%u out=%d flag=%d fuel=%u ai=%d still=%u moving=%u saved=%u lost=%u kills=%u "
                   "tank_kills=%d reload=%d unload=%d mine_cd=%d\n",
                i, (int)t->x, (int)t->y, (unsigned)t->angle, (int)t->speed, t->cls, t->team, t->active, t->armor, t->lives,
                t->mines, t->pows, (unsigned)t->score, (unsigned)t->respawn, t->out, t->has_flag, (unsigned)t->fuel, t->ai,
                (unsigned)t->still, (unsigned)t->moving, (unsigned)t->saved, (unsigned)t->lost, (unsigned)t->kills,
                t->tank_kills, t->reload, t->unload, t->mine_cd);
        fprintf(f, "ai %d mind=%08x\n", i, (unsigned)fnv(&g->ai[i], sizeof g->ai[i]));
    }
    for (i = 0; i < 2; i++)
        fprintf(f, "flag %d x=%d y=%d state=%d carrier=%d\n", i, (int)g->flags[i].x, (int)g->flags[i].y,
                g->flags[i].state, g->flags[i].carrier);
    for (i = 0; i < MAX_SHOTS; i++) {
        const Shot *s = &g->shots[i];
        if (s->active) fprintf(f, "shot %d x=%d y=%d vx=%d vy=%d ttl=%u owner=%d team=%d kind=%d\n", i, (int)s->x, (int)s->y,
                               (int)s->vx, (int)s->vy, (unsigned)s->ttl, s->owner, s->team, s->kind);
    }
    for (i = 0; i < MAX_MEN; i++) {
        const Man *m = &g->men[i];
        if (m->kind) fprintf(f, "man %d kind=%d state=%d x=%d y=%d team=%d timer=%u\n", i, m->kind, m->state, (int)m->x,
                             (int)m->y, m->team, (unsigned)m->timer);
    }
    for (i = 0; i < MAX_HELIS; i++) {
        const Heli *h = &g->helis[i];
        if (h->active) fprintf(f, "heli %d x=%d y=%d angle=%u hp=%d target=%d leaving=%d age=%u\n", i, (int)h->x, (int)h->y,
                               (unsigned)h->angle, h->hp, h->target, h->leaving, (unsigned)h->age);
    }
    for (i = 0; i < MAX_MINES; i++) {
        const Mine *m = &g->mines[i];
        if (m->active) fprintf(f, "mine %d x=%d y=%d team=%d arm=%d\n", i, (int)m->x, (int)m->y, m->team, m->arm);
    }
    for (i = 0; i < g->num_props; i++) {
        const Prop *p = &g->props[i];
        fprintf(f, "prop %d kind=%d x=%d y=%d hp=%d alive=%d\n", i, p->kind, (int)p->x, (int)p->y, p->hp, p->alive);
    }
    for (i = 0; i < g->map.num_st; i++) {
        const Structure *st = &g->map.st[i];
        fprintf(f, "structure %d type=%d x=%d y=%d team=%d hp=%d cooldown=%d burst=%d aim=%u\n", i, st->type, st->x, st->y,
                st->team, st->hp, st->cooldown, st->burst, (unsigned)st->aim);
    }
    for (i = 0; i < MAP_H; i++)                          /* the ground, a row at a time */
        fprintf(f, "row %d tiles=%08x hp=%08x\n", i, (unsigned)fnv(g->map.type[i], MAP_W), (unsigned)fnv(g->map.hp[i], MAP_W));
    if (g->extras) {
        for (i = 0; i < MAX_PICKUPS; i++)
            fprintf(f, "pickup %d tx=%d ty=%d kind=%d active=%d back=%u\n", i, g->pickups[i].tx, g->pickups[i].ty,
                    g->pickups[i].kind, g->pickups[i].active, (unsigned)g->pickups[i].back);
        fprintf(f, "hill tx=%d ty=%d held=%u,%u holder=%d\n", g->hill_tx, g->hill_ty, (unsigned)g->hill[0], (unsigned)g->hill[1],
                g->hill_holder);
    }
}

int replay_dump_at(const Replay *r, uint32_t tick, Game *scratch, FILE *f)
{
    InputBits in[MAX_TANKS];
    uint32_t t;
    int k;
    if (match_start_recorded(scratch, &r->setup, r->map, r->tanks) != 0) return -1;
    for (t = 0; t < tick && t < r->ticks; t++) {
        memset(in, 0, sizeof in);
        for (k = 0; k < r->tanks && k < MAX_TANKS; k++) in[k] = r->inputs[(size_t)t * (size_t)r->tanks + (size_t)k];
        game_tick(scratch, in);
    }
    game_dump(scratch, f);
    return 0;
}

int dump_diff(const char *path_a, const char *path_b, FILE *out, int max)
{
    FILE *a = fopen(path_a, "r"), *b = fopen(path_b, "r");
    char la[1024], lb[1024];
    int line = 0, diffs = 0;
    if (!a || !b) { if (a) fclose(a); if (b) fclose(b); return -1; }
    for (;;) {
        char *ra = fgets(la, sizeof la, a), *rb = fgets(lb, sizeof lb, b);
        if (!ra && !rb) break;
        line++;
        if (!ra) la[0] = 0;
        if (!rb) lb[0] = 0;
        if (strcmp(la, lb)) {
            if (diffs < max) {
                la[strcspn(la, "\n")] = 0; lb[strcspn(lb, "\n")] = 0;
                fprintf(out, "line %d:\n  < %s\n  > %s\n", line, ra ? la : "(ends)", rb ? lb : "(ends)");
            }
            diffs++;
        }
    }
    fclose(a); fclose(b);
    if (diffs > max) fprintf(out, "... and %d lines more\n", diffs - max);
    return diffs;
}
