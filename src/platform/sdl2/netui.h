/* SPDX-License-Identifier: MIT
 * The network screens: the server browser (from the listing service) and the
 * lobby of the server you're on (players, chat, leaderboard, getting ready). */
#ifndef OFP_SDL2_NETUI_H
#define OFP_SDL2_NETUI_H

#include <SDL.h>
#include "common/client.h"
#include "platform/sdl2/theme.h"

typedef struct {
    char     host[64], name[NET_SERVER_NAME], mode[12], world[16], map[32];
    int      port, players, max_players, password, running, ping;
    unsigned build;
    char     people[400];            /* who's on it (names, by commas) */
    char     goal[16];               /* its mode: flag, hill, conquest, deathmatch, annihilation, warlord */
    int      fav, recent;            /* one of my favourites; joined lately */
    char     friends[96];            /* friends of mine on it */
    int      rating;                 /* the people on it, as its leaderboard rates them (0: not known) */
} BrowserEntry;

#define BR_FAVS 16
#define BR_RECENT 10
#define BR_FRIENDS 24

typedef struct {
    BrowserEntry e[64];
    int  n, sel, top, typing_pw, fetched;
    char status[128], pw[64];
    /* mine (from the settings): favourite servers, the ones I joined lately, people I played with */
    char favs[BR_FAVS][72], recent[BR_RECENT][72], friends[BR_FRIENDS][16];
    int  nfavs, nrecent, nfriends, friends_online;
    int  need;                       /* players here (signed in): a server must have room for them all */
    char want_goal[16];              /* QUICK MATCH: a game in this mode first, if there is one ("": any) */
    int  my_rating;                  /* QUICK MATCH: mine, from the listing (0: not rated yet): close games first */
    int  tab, mine_sel, nmine;       /* tab 1: MY SERVERS (the settings', not on the public list) */
    char mine[8][64], mine_pw[8][64];
} Browser;
/* QUICK MATCH's modes: the server's goal words, and their names (0: any) */
#define QUICK_GOALS 8
extern const char *const quick_goal_key[QUICK_GOALS], *const quick_goal_name[QUICK_GOALS];

enum { BR_NOTHING, BR_JOIN, BR_BACK, BR_REFRESH, BR_FAVOURITE, BR_JOIN_MINE, BR_DROP_MINE,   /* (MINE: b->mine_sel) */
       BR_WATCH };                    /* L: the server's last match, its replay from the web site */
/* the newest replay of a server's matches in the listing's answer to action=matches (JSON): its id, 0 = none */
int  browser_replay_id(const char *matches_json);

/* mark favourites, recent servers and friends, and put favourites first, then recent */
void browser_mark(Browser *b);
/* the listing's text answer into the list (also for fuzz testing); 0 = ok. Changes body. */
int  browser_parse(Browser *b, char *body);
/* QUICK MATCH: the best server to join right now (-1 = none): the right version, no
 * password, room; people waiting in the lobby first, then a match on with room, then the
 * fullest open one; a friend on it counts extra, a long way away (ping) counts against, and
 * (rated) players far from my rating count against too */
/* QUICK MATCH: my rating, from the listing (action=rating); 0 if not rated or it doesn't answer */
int  browser_my_rating(const char *listing_url, const char *name);
int  browser_pick(const Browser *b);

int  browser_fetch(Browser *b, const char *listing_url);       /* blocking, a few seconds at most */
/* nav: up/down/fire edges; key: scancode pressed; text: typed (password) */
int  browser_input(Browser *b, InputBits pressed, int key, const char *text);
void browser_draw(SDL_Renderer *r, const Browser *b, const char *listing_url, int sw, int sh);

typedef struct {
    char line[NET_CHAT];             /* what I'm typing */
    int seat, show_scores, page, action;     /* local pilot, roster page or leaderboard */
    int diagnostics, diagnostic_button;
    char diagnostic_saved[64];
    int typing;                              /* the chat field has keyboard focus */
    int slap_click_id;              /* first click must be on the same human as the second */
    char join_code[8];               /* hosting: the listing's code for this game (friends type it in JOIN) */
} LobbyUI;

void lobby_draw(SDL_Renderer *r, const NetClient *c, const NetClient *extra, int extras,
                const LobbyUI *ui, SDL_Texture *tanks, int hosting, int sw, int sh);
const LobbyPlayer *lobby_player_at(const NetClient *c, const LobbyUI *ui, int sw, int x, int y);
enum { LB_NONE, LB_CAMP, LB_FORTRESS, LB_READY, LB_WATCH, LB_SCORES, LB_PAGE, LB_STATUS, LB_EXIT, LB_CHAT, LB_KEYBOARD, LB_COUNT };
int lobby_action_at(const NetClient *c, const NetClient *player, const LobbyUI *ui, int sw, int x, int y);
int lobby_action_step(const NetClient *c, const NetClient *player, const LobbyUI *ui, int dir);

#endif
