/* SPDX-License-Identifier: MIT
 * Game simulation. Fixed tick, inputs in, state out. No OS calls, no floats:
 * every backend (SDL2 now, native AmigaOS later) drives the same code. */
#ifndef OFP_GAME_H
#define OFP_GAME_H

#include <stdint.h>
#include "core/fixed.h"
#include "core/world.h"

#define OFP_TITLE   "OpenFP"
#define TICK_RATE   50          /* Hz, PAL-friendly */
#ifdef OFP_LOCAL_DUEL
#define MAX_TANKS   2
#elif defined(OFP_MAX_TANKS)                /* set by the build (CMake OFP_MAX_TANKS) */
#define MAX_TANKS   OFP_MAX_TANKS
#else
#define MAX_TANKS   10              /* up to 8 players and the computer */
#endif
#define MAX_SHOTS   128
#define MAX_EFFECTS 192
#define TANK_HALF   FX(6)       /* collision box half-size, px */
#define RESPAWN_TICKS (3 * TICK_RATE)
#ifdef OFP_LOCAL_DUEL               /* the local arena has no infantry or helipads */
#define MAX_MEN     1
#define MAX_HELIS   1
#else
#define MAX_MEN     128
#define MAX_HELIS   4
#endif
#define MAX_MINES   96
#define MEN_PER_LIFE 15         /* rescued men for an extra tank */
#define CAMP_TICKS  (5 * TICK_RATE)   /* standing still this long calls helicopters */
#define MINE_ARM    (2 * TICK_RATE)   /* time to get clear of your own mine */
#define MAX_MINEFIELD_MINES 36
#define EMBER_TICKS (90 * TICK_RATE)  /* how long the remains of a building glow */        /* enemy mines laid around the map at the start */
#define MEN_PER_MINE 5          /* rescued men for an extra mine */

/* Per-player input for one tick. The only thing that will cross the network. */
enum {
    IN_UP    = 1 << 0,
    IN_DOWN  = 1 << 1,
    IN_LEFT  = 1 << 2,
    IN_RIGHT = 1 << 3,
    IN_FIRE  = 1 << 4,
    IN_MINE  = 1 << 5,
    IN_TAKEOVER = 1 << 7             /* (official ticks only) a person takes this tank over from the computer */
};
typedef uint8_t InputBits;

typedef enum { TANK_LIGHT, TANK_MEDIUM, TANK_HEAVY, TANK_CLASS_COUNT } TankClass;
enum { TANK_RHINO = TANK_CLASS_COUNT };   /* local pick: a Mammoth with the rocket rack */

static inline int tank_chassis(int pick) { return pick == TANK_RHINO ? TANK_HEAVY : pick; }

typedef struct {
    const char *name;
    const char *blurb;          /* one line for the selection screen */
    fx       max_fwd, max_rev;  /* px per tick */
    fx       accel, friction;   /* px per tick per tick */
    angle16  turn_rate;         /* per tick */
    uint8_t  reload;            /* ticks between shots */
    uint8_t  armor;             /* damage it can take */
    uint16_t fuel;              /* seconds of throttle */
    uint8_t  mines, pows, lives;
} TankStats;

#define FUEL_EMPTY_SPEED_DIV 4  /* empty tank crawls at 1/4 speed */

extern const TankStats tank_stats[TANK_CLASS_COUNT];
const TankStats *world_tank_stats(int biome, TankClass cls);

typedef struct {
    fx       x, y, prev_x, prev_y;   /* centre, world px */
    angle16  angle, prev_angle;
    fx       speed;
    fx       drift_x, drift_y;       /* enhanced slippery-surface momentum */
    uint16_t boost_ticks, rapid_ticks;
    uint8_t  cls, team, reload, active;
    uint16_t fuel;                   /* ticks of throttle left */
    uint8_t  armor, lives, mines, pows;
    uint32_t score;
    uint16_t respawn;                /* >0: wrecked, ticks until back in the garage */
    uint8_t  guard;                  /* every tank for itself: just out of the garage, other tanks can't hurt it yet */
    uint8_t  out;                    /* no lives left */
    uint8_t  has_flag;               /* carrying the enemy flag */
    uint8_t  unload, mine_cd;        /* timers */
    uint16_t still;                  /* ticks standing still outside the own base */
    uint16_t moving;                 /* ticks on the move (helicopters give up) */
    uint16_t saved;                  /* men delivered to first aid, ever */
    uint16_t lost, pows_lost;        /* final report: tanks lost, our men who died */
    uint16_t kills, helis_down;      /* final report: soldiers and helicopters destroyed */
    uint8_t  msg, msg_t;             /* last event for the HUD (MSG_*) and its timer */
    uint8_t  steer;                  /* STEER_TANK or STEER_DIRECT (the original's controls) */
    uint8_t  ai;                     /* 0 = a player drives it, else computer at AI_EASY..AI_HARD */
    uint8_t  tank_kills;             /* enemy tanks destroyed */
    uint8_t  garage;                 /* which of its side's garages it uses; 255 = the one for its class */
    uint8_t  rocket_launcher;        /* the heavy chassis's optional rocket rack */
} Tank;

enum { AI_OFF, AI_EASY, AI_NORMAL, AI_HARD };

/* STEER_TANK: left/right turn, up/down drive.
 * STEER_DIRECT: the original's way: push the stick where you want to go and
 * the tank turns and drives that way; pushing the opposite way reverses. */
enum { STEER_TANK, STEER_DIRECT };

enum { MSG_NONE, MSG_FLAG, MSG_EXTRA_LIFE, MSG_NO_FUEL, MSG_FULL, MSG_WIN, MSG_HELI, MSG_SAVED,
       MSG_FLAG_LOST, MSG_FLAG_BACK, MSG_TANK_KILLED, MSG_ENEMY_OUT, MSG_LOST, MSG_DROP };

typedef enum { MAN_NONE, MAN_POW, MAN_SOLDIER } ManKind;
#define MAX_EVACUEES (4 * MAX_TANKS)

typedef struct {
    fx       x, y, prev_x, prev_y;
    uint8_t  kind, state, home;      /* home: structure index for soldiers */
    uint8_t  team;                   /* soldiers: whose side they're on */
    uint8_t  cooldown;
    uint16_t timer;
    angle16  dir;
    fx       tx, ty;                 /* wander target */
} Man;

typedef struct {
    fx       x, y, prev_x, prev_y;
    angle16  angle, prev_angle;
    uint8_t  active, hp, target, leaving, cooldown;
    uint16_t age;
    int16_t  pad;                    /* helipad it took off from, -1 = none */
} Heli;

enum { MAN_IDLE, MAN_ALERT, MAN_FLEE };     /* Man.state */

typedef struct {
    fx       x, y;
    uint8_t  active, team, arm;      /* proximity mine: once armed, anything sets it off */
} Mine;

/* Parked vehicles: trucks, fuel tankers, jeeps, APCs, half-tracks and towed
 * guns standing about the bases and along the roads. They block tanks and
 * shots, can be destroyed, and leave a burnt-out wreck behind. */
typedef enum { PROP_TRUCK, PROP_TANKER, PROP_JEEP, PROP_APC, PROP_HALFTRACK, PROP_GUN, PROP_KINDS } PropKind;

typedef struct {
    const char *name;
    fx       half_w, half_l;         /* half width (across) and half length, px */
    uint8_t  hp, score;              /* hits; points x5 */
    uint8_t  blast;                  /* explosion size when it goes */
    uint8_t  crew;                   /* soldiers who bail out */
} PropInfo;

extern const PropInfo prop_info[PROP_KINDS];

#define MAX_PROPS 96

typedef struct {
    fx       x, y;
    angle16  angle;                  /* the way it faces */
    uint8_t  kind, hp, team, alive;  /* alive 0 + used 1 = wreck */
    uint8_t  used;
    uint16_t died;                   /* tick it was destroyed (low 16 bits) */
    uint8_t  moving, wait;           /* moving: it drives the roads (a patrol); wait: ticks it's been held up */
    int16_t  to_tx, to_ty;           /* (moving) the road tile it's heading for */
} Prop;

enum { FLAG_HIDDEN, FLAG_GROUND, FLAG_CARRIED };

/* The computer opponent. It plays by the same rules as a player: each tick
 * it looks at the game and decides which buttons to press. Deterministic,
 * part of the game state, so it replays and will run the same over a
 * network. See ai.c. */
enum { AIM_RAID, AIM_FETCH, AIM_HOME, AIM_REPAIR, AIM_CHASE, AIM_RECOVER, AIM_FIGHT, AIM_PATROL, AIM_DEFEND,
       AIM_HILL, AIM_PICKUP,                         /* (the extras: hold the hill, fetch a pickup) */
       AIM_POINT, AIM_HUNT, AIM_ESCORT,              /* (warlord's escorts: by their own warlord) */
       AIM_DROP, AIM_TAG };                                   /* (arenas: the supply drop, when hurt) */                        /* (conquest: a capture point; annihilation: their tanks) */
typedef struct {
    uint8_t  mode;                   /* AIM_*: what it is trying to do */
    uint8_t  back;                   /* ticks of backing up to get unstuck */
    uint8_t  dodge, mine_cd;
    uint16_t repath, stuck, seen;    /* ticks to the next route; no progress; time with you in sight */
    int16_t  goal_tx, goal_ty;       /* where the route leads */
    int16_t  pat_tx, pat_ty;         /* patrol point */
    uint16_t pat_t;                  /* ticks left on this patrol leg */
    uint16_t last_d;                 /* route cost at the last progress check */
    fx       last_x, last_y;
    uint16_t dist[MAP_W * MAP_H];    /* route costs to the goal, 0xFFFF = no way */
} AiState;

typedef struct {
    fx       x, y;
    fx       home_x, home_y;         /* where it belongs (its bunker) */
    uint8_t  state, carrier;
} Flag;

typedef enum { SHOT_SHELL, SHOT_BULLET, SHOT_ROCKET, SHOT_HEAVY, SHOT_MORTAR } ShotKind;

typedef struct {
    fx       x, y, prev_x, prev_y, vx, vy;
    uint16_t ttl;
    uint16_t src_sid;                /* structure that fired it (passes its own tiles) */
    uint8_t  owner;                  /* tank index, or 255 for a turret */
    uint8_t  team, kind, damage, target, active;
    uint16_t life;                   /* starting ttl (mortar arcs) */
} Shot;

typedef enum {
    EFF_HIT,        /* shot stops on something hard      */
    EFF_EXPLOSION,  /* a tile or part of a building      */
    EFF_PUFF,       /* shot runs out                      */
    EFF_BLAST,      /* whole building / tank goes up      */
    EFF_MUZZLE,     /* gun fires                          */
    EFF_SPARK,      /* shot hits a tank                   */
    EFF_SPLAT,      /* a soldier under the tracks         */
    EFF_CHEER,      /* men delivered / extra life         */
    EFF_KINDS
} EffectKind;

typedef struct {
    fx       x, y;
    uint8_t  kind, life, ttl, active;
    uint8_t  delay;          /* ticks before it starts (chain explosions) */
    uint8_t  size;           /* 0..255, how big (blast radius hint) */
    uint8_t  sub;            /* muzzle flashes: ShotKind; otherwise 0 */
    angle16  dir;            /* muzzle direction / blast push */
    uint16_t serial;         /* increases per effect: renderers spawn particles once */
} Effect;

/* extras (MatchSetup.extras, bits): pickups on the field; a hill to hold */
#define EXTRA_PICKUPS 1
#define EXTRA_HILL    2
#define EXTRA_CONQUEST 4         /* capture points: hold them for score; no flags */
#define EXTRA_ANNIHILATION 8     /* every tank for itself: no flags; the last one standing wins */
#define EXTRA_TDM     16         /* team deathmatch: no flags, no lives lost; TDM_WIN enemy tanks wins */
#define TDM_WIN       20
#define DROP_EVERY    (40 * TICK_RATE)  /* arenas: a supply drop this long after the last was taken */
#define EXTRA_WARLORD 32      /* a boss tank a side: destroying theirs scores; JUG_WIN wins */
#define JUG_WIN       7
#define FFA_GUARD     (5 * TICK_RATE)   /* every tank for itself: out of the garage, safe this long (or until it fires) */
#define MAX_CP        3          /* capture points */
#define CP_RADIUS     3          /* tiles: on the point */
#define CP_TAKE       (5 * TICK_RATE)   /* alone on it this long: it's yours */
#define CONQUEST_WIN  300        /* score: one a second for every point held */
#define EXTRA_KILLCONF 64        /* kill confirmed: a destroyed tank drops a tag; its foes' touching it scores, its side's denies */
#define KC_WIN        15
#define KC_TAG_TICKS  (30 * TICK_RATE)  /* a tag lies this long */
#define MAX_TAGS      16
#define EXTRA_HARDPOINT 128      /* hardpoint: one point counts, moving every minute; alone on it, a point a second */
#define HP_WIN        120
#define HP_MOVE       (60 * TICK_RATE)
#define EXTRA_ALL     (EXTRA_PICKUPS | EXTRA_HILL | EXTRA_CONQUEST | EXTRA_ANNIHILATION | EXTRA_TDM | EXTRA_WARLORD | EXTRA_KILLCONF | EXTRA_HARDPOINT)
#define EXTRA_NOFLAGS (EXTRA_CONQUEST | EXTRA_ANNIHILATION | EXTRA_TDM | EXTRA_WARLORD)   /* (modes without flags to take) */
#define MAX_PICKUPS   6
#define PICKUP_BACK   (45 * TICK_RATE)  /* a taken pickup comes back (somewhere else) after this */
#define HILL_WIN      (120 * TICK_RATE) /* hold the hill alone this long, all told, and win */
#define HILL_R        3                 /* tiles: how close counts as on the hill */
enum { PICKUP_REPAIR, PICKUP_MINES, PICKUP_FUEL, PICKUP_BOOST, PICKUP_RAPID, PICKUP_KINDS };
#define POWERUP_TICKS (10 * TICK_RATE)
typedef struct { int16_t tx, ty; uint8_t kind, active; uint16_t back; } Pickup;

enum { ORDER_NONE, ORDER_ATTACK, ORDER_DEFEND, ORDER_ESCORT, ORDER_REGROUP, ORDER_COUNT };
typedef struct { uint8_t kind, issuer; int16_t tx, ty; uint32_t until; } TeamOrder;

typedef struct {
    /* AI intentions only; official bot inputs are recorded in replays. */
    TeamOrder team_order[2];
    Map      map;
    Base     bases[2];
    Tank     tanks[MAX_TANKS];
    int      num_tanks;
    int8_t   team_leader[2];         /* starting human leaders by team, -1 if none */
    Shot     shots[MAX_SHOTS];
    Effect   effects[MAX_EFFECTS];
    Man      men[MAX_MEN];
    Man      evacuees[MAX_EVACUEES]; /* visual only: rescued men walking into first aid */
    Heli     helis[MAX_HELIS];
    Mine     mines[MAX_MINES];
    Flag     flags[2];               /* index = team that owns it */
    Prop     props[MAX_PROPS];
    int      num_props;
    AiState  ai[MAX_TANKS];          /* the computer opponent's mind, per tank */
    int      spawn_tx, spawn_ty;     /* player garage (= bases[0] spawn) */
    uint32_t rng;                   /* world simulation, shared by all peers */
    uint32_t bot_rng;               /* server AI only when bot_commands is set */
    uint8_t  bot_commands;          /* consume recorded/official bot inputs */
    int8_t   winner;                 /* -1 while playing */
    uint8_t  versus;                 /* two players against each other: no helicopter hunts */
    uint16_t effect_serial;
    uint8_t  biome;
    uint32_t tick;
    uint32_t time_limit;             /* ticks; 0 = none. When it's up: */
    uint8_t enhanced_rules, no_powerups; /* surface physics and temporary power-ups */
    uint8_t life_limit, vehicle_world; /* 0: legacy lives/standard vehicles; 255: unlimited */
    uint8_t  time_rule;              /* TIME_RULE_*: the side with more points wins, or a draw */
    uint8_t  time_up;                /* the time ran out (winner: by the rule, -1 for a draw) */
    uint8_t  humans_out;             /* every person's tank was out, only computer players left: ended */
    uint8_t  weather;                /* WEATHER_*: a storm slows tanks on open ground and cuts how far anyone sees */
    uint8_t  weather_changing;       /* 1: the weather comes and goes (a storm, then clear, ...) */
    uint8_t  calm;                   /* 1: no helicopters called in (TRAINING: standing still to read is fine) */
    uint8_t  relaxed_pace;
    uint32_t weather_seed;           /* (its storms' times: from the match's seed) */
    uint8_t  adapt;                  /* the computer adapts to how you're doing (ENEMY: ADAPTIVE) */
    int16_t  adapt_skill;            /* 0 easy .. 100 normal .. 200 hard: where it is now */
    int16_t  adapt_mark[2];          /* (the tallies when it last looked: tanks lost on each side) */
    uint8_t  extras;                 /* EXTRA_* */
    uint8_t  raid_bots;              /* tank slots assigned to attack from the opening */
    uint8_t  siege_bots;             /* flag raiders using the siege tactics */
    Pickup   pickups[MAX_PICKUPS];
    int16_t  hill_tx, hill_ty;       /* the hill's middle tile */
    uint32_t hill[2];                /* ticks each side has held it alone */
    int8_t   hill_holder;            /* who's on it alone right now (-1: nobody, or both) */
    uint8_t  num_cp;                 /* conquest: the capture points */
    int16_t  cp_tx[MAX_CP], cp_ty[MAX_CP];
    int8_t   cp_owner[MAX_CP];       /* TEAM_*, -1: nobody's */
    int16_t  cp_take[MAX_CP];        /* -CP_TAKE (the fortress's) .. +CP_TAKE (the camp's) */
    uint16_t cp_score[2];            /* each side's score */
    int8_t   ffa_winner;             /* annihilation: the tank left standing (-1: none yet, or nobody) */
    Pickup   drop;                   /* arenas: a supply drop (a full refill), now and then */
    uint16_t drop_wait;              /* ticks since the last one went */
    int8_t   jug[2];                 /* warlord: each side's Warlord (a tank; -1: none) */
    uint8_t  jug_score[2];           /* the other side's Warlords destroyed */
    struct { fx x, y; uint8_t team, active; uint16_t ttl; } tags[MAX_TAGS];   /* kill confirmed: team = the fallen's side */
    uint16_t kc_score[2];            /* kill confirmed: kills confirmed */
    uint16_t hp_score[2];            /* hardpoint: seconds alone on the active point */
    uint8_t  hp_active;              /* hardpoint: the point that counts (cp_tx, cp_ty) */
} Game;

int game_checkpoint_positions(const Game *g, int16_t xs[MAX_CP], int16_t ys[MAX_CP]);
void game_setup_extras(Game *g, uint8_t extras);
void game_prepare_surfaces(Game *g, uint32_t seed, int decorate);
void game_country_split(Game *g);   /* the country's buildings and guns: the nearer base's side (conquest; a small map) */   /* (match_start calls it) */
void game_make_arena(Game *g);                     /* a random battlefield, camps cleared: an arena, eight spawn points */
void game_extras_tanks(Game *g);                   /* (and this, once the tanks are in) */
int  game_armor_max(const Game *g, int tank);      /* its full armour (a Warlord: five times) */


#define AI_ADAPTIVE 4                /* MatchSetup.ai_level 4, 5, 6: adaptive, starting easy, normal, hard */

enum { TIME_RULE_POINTS, TIME_RULE_DRAW };

void     game_init(Game *g, uint32_t seed, Biome biome);    /* generates the map, no tanks */
int      game_init_map(Game *g, uint32_t seed, const char *map_text);   /* from an .ofpmap; 0 = ok */
int      game_add_tank(Game *g, TankClass cls, int team, fx x, fx y);  /* index or -1 */
int      game_add_tank_at_base(Game *g, TankClass cls, int team);       /* in its garage */
int      game_add_tank_in_garage(Game *g, TankClass cls, int team, int garage);   /* a given one (0..2) */
void     game_set_tank_class(Game *g, int idx, TankClass cls);  /* also refills */
void     game_tick(Game *g, const InputBits inputs[MAX_TANKS]);
/* Advance an online match and write the resolved bot commands into the tick. */
void     game_tick_server(Game *g, InputBits inputs[MAX_TANKS]);
void     game_set_ai(Game *g, int tank, int level);     /* hand a tank to the computer */
int game_orders_allowed(const Game *g);
int game_order_active(const Game *g, int team);
int game_team_order(Game *g, int leader, int kind);
const char *game_order_name(int kind);
InputBits ai_think(Game *g, int tank);                  /* the computer's buttons this tick */
void ai_local_reset(void);
InputBits ai_think_local(Game *g, int tank, unsigned route_nodes);
uint32_t game_checksum(const Game *g);                      /* for desync detection */

/* Should a gate at this world-pixel midpoint raise its barrier? */
int      game_gate_open(const Game *g, int x, int y);

/* Camera top-left for a viewport following a tank, interpolated and clamped. */
void     game_camera(const Game *g, int tank, int view_w, int view_h, int alpha256,
                     int *cam_x, int *cam_y);

#endif
