/* SPDX-License-Identifier: MIT */
#include "platform/sdl2/pref_path.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include "platform/sdl2/profiles.h"
#include "platform/sdl2/input.h"
#include "platform/sdl2/osk.h"
#include "platform/sdl2/ui.h"
#include "platform/sdl2/theme.h"
#include "platform/sdl2/select.h"
#include "platform/sdl2/render.h"
#include "core/game.h"
#include "common/cloud.h"
#include "common/http.h"
#ifndef _WIN32
#include <sys/stat.h>
#endif

/* stats: matches, won, tanks destroyed and lost; wins by LOCAL GAME's mode; matches by tank */
/* controls: the pad's layout and buttons (-2: the machine's), its own keys (has_keys) */
typedef struct { char name[16]; uint8_t key[32]; int tank, avatar;
                 int matches, won, kills, lost, mode_won[5], cls_played[3];
                 int pad_layout, pad_fire, pad_mine, has_keys, keys[6][2];
                 int steer; char session[65]; int cloud_dirty, cloud_pull; } Profile;                 /* (steer: STEER_TANK, STEER_DIRECT: the original's) */

#define AVATARS 10                                   /* data/avatars.png: ten portraits, 24 x 24 (tools/make_avatars.py) */
static const Theme *theme;                            /* (for the tank cards) */
static SDL_Texture *world_tanks;
static int tank_choices = TANK_CLASS_COUNT;

void signin_set_theme(const void *t) { theme = (const Theme *)t; }
static const char *tank_label[] = { "JACKAL", "BISON", "MAMMOTH", "RHINO" };

void signin_set_tanks(SDL_Texture *tanks, int classic)
{
    world_tanks = tanks;
    tank_choices = TANK_CLASS_COUNT + !classic;
}

static int available_pick(int pick) { return pick < tank_choices ? pick : tank_chassis(pick); }
static int step_pick(int pick, int dir) { return (available_pick(pick) + tank_choices + dir) % tank_choices; }

static Profile prof[MAX_PROFILES];
static int     nprof;
static struct { int dev, profile; Uint32 hold_since; } slot[MAX_SIGNED];
static int     nslot;
/* the picker: one device choosing who they are */
static struct {
    int      open, dev, sel, typing, enter;           /* enter: Enter pressed (keys: FIRE too) */
    int      own;                                     /* 1: a signed-in player's own window */
    int      keep_signed_in;                          /* the team or match still owns this slot */
    int      sub;                                     /* (own) 1: its CONTROLLER page */
    int      renaming;                                /* (own) typing a new name */
    char     note[40];                                /* (own) a word for a moment: why a name wasn't taken */
    Uint32   note_until;
    int      step, tank, avatar;                      /* making one: 0 the name, 1 the tank, 2 the avatar */
    InputBits prev;
    char     name[16];
    CloudAccount cloud_account;
    int cloud_ready;
    Osk      osk;
} pk;

static char *path_of(char *out, size_t cap)
{
    char *pref = ofp_pref_path();
    snprintf(out, cap, "%sprofiles.ini", pref ? pref : "");
    SDL_free(pref);
    return out;
}

static void save(void)
{
    char path[1100];
    FILE *f = fopen(path_of(path, sizeof path), "w");
    int i, k;
    if (!f) return;
#ifndef _WIN32
    fchmod(fileno(f), 0600);
#endif
    fprintf(f, "# OpenFP profiles: name, key (who you are online: keep it to yourself), tank, avatar\n");
    for (i = 0; i < nprof; i++) {
        fprintf(f, "%s\t", prof[i].name);
        for (k = 0; k < 32; k++) fprintf(f, "%02x", prof[i].key[k]);
        fprintf(f, "\t%d\t%d\t%d %d %d %d\t%d %d %d %d %d\t%d %d %d", prof[i].tank, prof[i].avatar,   /* (and the stats) */
                prof[i].matches, prof[i].won, prof[i].kills, prof[i].lost, prof[i].mode_won[0], prof[i].mode_won[1],
                prof[i].mode_won[2], prof[i].mode_won[3], prof[i].mode_won[4], prof[i].cls_played[0], prof[i].cls_played[1],
                prof[i].cls_played[2]);
        {   /* (and its controls, one more field: layout fire mine, then keys or "-") */
            int a2, b2;
            fprintf(f, "\t%d %d %d", prof[i].pad_layout, prof[i].pad_fire, prof[i].pad_mine);
            if (prof[i].has_keys) for (a2 = 0; a2 < 6; a2++) for (b2 = 0; b2 < 2; b2++) fprintf(f, " %d", prof[i].keys[a2][b2]);
            else fprintf(f, " -");
            fprintf(f, "\t%d\n", prof[i].steer);           /* (the steering, a field more) */
        }
    }
    for (i = 0; i < nprof; i++) if (prof[i].session[0]) fprintf(f, "CLOUD\t%d\t%s\t%d\n", i, prof[i].session, prof[i].cloud_dirty);
    fclose(f);
}

static void apply_controls(void);

/* The main thread owns profiles. Workers receive a copy and never touch UI state. */
static struct {
    SDL_Thread *thread; SDL_atomic_t done;
    uint8_t key[32]; char session[65], error[96]; CloudPreferences preferences;
    CloudAccount account; int result, writing; Uint32 retry_at;
} sync_profile;
static CloudPreferences profile_preferences(const Profile *p) {
    CloudPreferences v = {p->tank, p->avatar, p->steer}; return v;
}
static void profile_changed(Profile *p) { if(p->session[0]) p->cloud_dirty=1; save(); }
static void profile_receive(Profile *p, const CloudAccount *a) {
    if(!p->cloud_dirty) {
        if(a->has_preferences) {p->tank=a->preferences.tank; p->avatar=a->preferences.avatar;}
        if(a->has_steering) p->steer=a->preferences.steer;
        else if(p->session[0]) p->cloud_dirty=1; /* First sign-in on this platform keeps its local steering. */
    }
}
static int profile_sync_worker(void *unused) {
    (void)unused;
    sync_profile.result=sync_profile.writing ? cloud_profile_save(sync_profile.session,&sync_profile.preferences,sync_profile.error)
        : cloud_profile_load(sync_profile.session,&sync_profile.account,sync_profile.error);
    SDL_AtomicSet(&sync_profile.done,1); return 0;
}
static void profile_sync_finish(void) {
    if(!sync_profile.thread || !SDL_AtomicGet(&sync_profile.done)) return;
    SDL_WaitThread(sync_profile.thread,NULL);sync_profile.thread=NULL;
    for(int i=0;i<nprof;i++) {
        Profile *p=&prof[i];
        if(memcmp(p->key,sync_profile.key,32)||strcmp(p->session,sync_profile.session)) continue;
        if(!sync_profile.result) {
            if(sync_profile.writing) {
                CloudPreferences v=profile_preferences(p);
                if(v.tank==sync_profile.preferences.tank&&v.avatar==sync_profile.preferences.avatar&&v.steer==sync_profile.preferences.steer) p->cloud_dirty=0;
            } else if(!memcmp(p->key,sync_profile.account.key,32)) profile_receive(p,&sync_profile.account);
            save(); apply_controls();
        } else if(!sync_profile.writing) p->cloud_pull=1;
        break;
    }
    sync_profile.retry_at=SDL_GetTicks()+(sync_profile.result?60000:500);
    memset(sync_profile.session,0,sizeof sync_profile.session);memset(&sync_profile.account,0,sizeof sync_profile.account);
}
static void profile_sync_frame(void) {
    profile_sync_finish();
    if(sync_profile.thread || (Sint32)(SDL_GetTicks()-sync_profile.retry_at)<0) return;
    for(int i=0;i<nprof;i++) if(prof[i].session[0]&&(prof[i].cloud_dirty||prof[i].cloud_pull)) {
        sync_profile.writing=prof[i].cloud_dirty; sync_profile.preferences=profile_preferences(&prof[i]);
        memcpy(sync_profile.key,prof[i].key,32);strcpy(sync_profile.session,prof[i].session);
        SDL_AtomicSet(&sync_profile.done,0);http_init();
        sync_profile.thread=SDL_CreateThread(profile_sync_worker,"cloud-profile",NULL);
        if(sync_profile.thread) prof[i].cloud_pull=0;
        else sync_profile.retry_at=SDL_GetTicks()+60000;
        break;
    }
}

static void new_key(uint8_t key[32])
{
    FILE *r = fopen("/dev/urandom", "rb");
    int k;
    if (r && fread(key, 1, 32, r) == 32) { fclose(r); return; }
    if (r) fclose(r);
    {   /* (no /dev/urandom: Windows; good enough for telling players apart) */
        uint64_t x = (uint64_t)time(NULL) ^ SDL_GetPerformanceCounter() ^ ((uint64_t)(uintptr_t)key << 17);
        for (k = 0; k < 32; k++) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; key[k] = (uint8_t)(x >> 24); }
    }
}

void signin_init(void)
{
    char path[1100], line[256];
    signin_shutdown();
    FILE *f = fopen(path_of(path, sizeof path), "r");
    for (int i = 0; i < nprof; i++) cloud_forget(prof[i].key);
    memset(prof, 0, sizeof prof);
    nprof = 0;
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *t1, *t2;
        Profile *p;
        int k;
        if (!strncmp(line, "CLOUD\t", 6)) {
            int index, dirty = 0; char token[65]; uint8_t scratch[32];
            if (sscanf(line, "CLOUD\t%d\t%64s\t%d", &index, token, &dirty) >= 2 && index >= 0 && index < nprof && !cloud_unhex(token, scratch, 32)) {
                strcpy(prof[index].session, token); prof[index].cloud_dirty = dirty != 0; cloud_register(prof[index].key, token);
            }
            continue;
        }
        if (nprof >= MAX_PROFILES) continue;
        p = &prof[nprof];
        if (line[0] == '#' || !(t1 = strchr(line, '\t')) || !(t2 = strchr(t1 + 1, '\t'))) continue;
        *t1 = 0;
        snprintf(p->name, sizeof p->name, "%.15s", line);
        if (strlen(t1 + 1) < 64 + 1 || !p->name[0]) continue;
        for (k = 0; k < 32; k++) { unsigned v; if (sscanf(t1 + 1 + 2 * k, "%2x", &v) != 1) break; p->key[k] = (uint8_t)v; }
        if (k < 32) continue;
        p->tank = atoi(t2 + 1);
        if (p->tank < TANK_LIGHT || p->tank > TANK_RHINO) p->tank = TANK_MEDIUM;
        p->avatar = strchr(t2 + 1, '\t') ? atoi(strchr(t2 + 1, '\t') + 1) % AVATARS : nprof % AVATARS;
        {   /* the stats, if the line has them (an older file: none yet) */
            const char *st = strchr(t2 + 1, '\t');
            st = st ? strchr(st + 1, '\t') : NULL;
            memset(&p->matches, 0, sizeof *p - offsetof(Profile, matches));
            p->pad_layout = p->pad_fire = p->pad_mine = -2;  /* (the machine's, until it says) */
            if (st) sscanf(st + 1, "%d %d %d %d\t%d %d %d %d %d\t%d %d %d", &p->matches, &p->won, &p->kills, &p->lost,
                           &p->mode_won[0], &p->mode_won[1], &p->mode_won[2], &p->mode_won[3], &p->mode_won[4],
                           &p->cls_played[0], &p->cls_played[1], &p->cls_played[2]);
            if (st) {                                    /* its controls, the field after */
                int tabs = 0;
                const char *ct = st + 1;
                while (*ct && tabs < 3) { if (*ct == '\t') tabs++; ct++; }
                {   /* the steering: the field after the controls (none: tank) */
                    const char *sp = strchr(ct, '\t');
                    p->steer = sp && atoi(sp + 1) == STEER_DIRECT ? STEER_DIRECT : STEER_TANK;
                }
                if (tabs == 3 && sscanf(ct, "%d %d %d", &p->pad_layout, &p->pad_fire, &p->pad_mine) == 3) {
                    int a2, b2, n2 = 0, v;
                    const char *kp = ct;
                    for (n2 = 0; n2 < 3; n2++) { while (*kp && *kp != ' ') kp++; while (*kp == ' ') kp++; }
                    if (*kp && *kp != '-') {
                        p->has_keys = 1;
                        for (a2 = 0; a2 < 6; a2++) for (b2 = 0; b2 < 2; b2++) {
                            v = (int)strtol(kp, (char **)&kp, 10);
                            p->keys[a2][b2] = v;
                        }
                    }
                }
            }
        }
        nprof++;
    }
    fclose(f);
}

static int signed_in(int dev) { int i; for (i = 0; i < nslot; i++) if (slot[i].dev == dev) return i; return -1; }
static void apply_controls(void);                        /* (below: each signed-in player's controls, to their device) */

/* your own window: CONTROLLER (a pad: its page), NAME, FACE, SIGN OUT, BACK; the CONTROLLER page: DRIVE,
 * FIRE, MINE, DEFAULTS, BACK */
enum { OW_DRIVE, OW_FIRE, OW_MINE, OW_DEFAULTS, OW_PADBACK,        /* (the CONTROLLER page) */
       OW_CONTROLLER, OW_STEER, OW_NAME, OW_FACE, OW_TANK, OW_SIGNOUT, OW_BACK };
static int own_rows(void) { return pk.sub ? 5 : (pk.dev >= DEV_PAD1 ? 7 : 6) - pk.keep_signed_in; }
static int own_row(int sel) { int row = (pk.dev >= DEV_PAD1 ? OW_CONTROLLER : OW_STEER) + sel;
    if (pk.sub) return sel;
    return pk.keep_signed_in && row == OW_SIGNOUT ? OW_BACK : row; }
static void finish_rename(void)                          /* the new name: yours (unless someone has it) */
{
    int me = signed_in(pk.dev), i, taken = 0;
    for (i = 0; i < nprof; i++) if (me >= 0 && i != slot[me].profile && !SDL_strcasecmp(prof[i].name, pk.name)) taken = 1;
    if (me >= 0 && pk.name[0] && !taken) { snprintf(prof[slot[me].profile].name, sizeof prof[slot[me].profile].name, "%s", pk.name); save(); }
    else if (me >= 0) {                                  /* (not taken: why, a moment) */
        snprintf(pk.note, sizeof pk.note, "%s", taken ? "SOMEONE HAS THAT NAME" : "A NAME CAN'T BE EMPTY");
        pk.note_until = SDL_GetTicks() + 3000;
    }
    pk.typing = 0; pk.renaming = 0;
    pk.prev = input_device_menu(pk.dev) | IN_FIRE;
}
static const int pad_btn[] = { -2, SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X,
                               SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER };
static const char *pad_btn_name[] = { "AS EVER", "A", "B", "X", "Y", "LB", "RB" };
#define PAD_BTNS 7
static int pad_step(int now, int dir)                    /* the next button for FIRE or MINE (-2, -1: as ever) */
{
    int i;
    for (i = 0; i < PAD_BTNS && pad_btn[i] != now; i++) {}
    if (i == PAD_BTNS) i = 0;
    return pad_btn[(i + dir + PAD_BTNS) % PAD_BTNS];
}
static const char *pad_btn_label(int b, int fire, int layout)
{
    int i;
    if (b < 0) return layout == PAD_CLASSIC ? (fire ? "A, RB, RT" : "B, X, LT") : (fire ? "A, RB" : "X, LB");
    for (i = 1; i < PAD_BTNS; i++) if (pad_btn[i] == b) return pad_btn_name[i];
    return "?";
}
static int profile_in(int p) { int i; for (i = 0; i < nslot; i++) if (slot[i].profile == p) return 1; return 0; }

static void sign_out(int i)
{
    for (; i + 1 < nslot; i++) slot[i] = slot[i + 1];  /* (the rest move up: player 3 becomes 2) */
    nslot--;
    signin_assign_inputs();
}

static void take(int profile)                        /* the picker's choice: in */
{
    if (prof[profile].session[0]) prof[profile].cloud_pull = 1;
    slot[nslot].dev = pk.dev;
    slot[nslot].profile = profile;
    slot[nslot].hold_since = 0;
    nslot++;
    pk.open = 0;
    SDL_StopTextInput();
    signin_assign_inputs();
}

static void create(void)                             /* NEW PROFILE: the typed name */
{
    char *n = pk.name;
    int i;
    while (*n == ' ') n++;
    if (!*n) return;
    for (i = 0; i < nprof; i++) if (!SDL_strcasecmp(prof[i].name, n) &&
        (!pk.cloud_ready || !memcmp(prof[i].key, pk.cloud_account.key, 32))) { if (!profile_in(i)) take(i); return; }   /* (theirs already) */
    if (nprof >= MAX_PROFILES) return;
    memset(&prof[nprof], 0, sizeof prof[nprof]);         /* (a new one: no stats, the machine's controls) */
    prof[nprof].pad_layout = prof[nprof].pad_fire = prof[nprof].pad_mine = -2;
    snprintf(prof[nprof].name, sizeof prof[nprof].name, "%.15s", n);
    if (pk.cloud_ready) {
        memcpy(prof[nprof].key, pk.cloud_account.key, 32);
        strcpy(prof[nprof].session, pk.cloud_account.session);
        prof[nprof].steer = pk.cloud_account.has_steering ? pk.cloud_account.preferences.steer : STEER_TANK;
        prof[nprof].cloud_dirty = 1;
    } else new_key(prof[nprof].key);
    prof[nprof].tank = pk.tank;
    prof[nprof].avatar = pk.avatar;
    nprof++;
    save();
    take(nprof - 1);
}


static struct {
    int open, row, editing, creating, kind, cancelled;
    char username[16], email[255], password[73], error[96];
    CloudDevice device; CloudAccount account;
    SDL_Thread *thread; SDL_atomic_t done;
    int result; Uint32 started, poll_at;
    InputBits prev;
} cu;
#ifndef OFP_TVOS
static char *cloud_ui_field(size_t *cap) {
    if (cu.row == 1) { *cap=sizeof cu.username; return cu.username; }
    if (cu.row == 2) { *cap=sizeof cu.email; return cu.email; }
    *cap=sizeof cu.password; return cu.password;
}
#endif
static int cloud_ui_worker(void *unused) {
    (void)unused;
    if (cu.kind == 1) cu.result=cloud_login(&cu.account,cu.username,cu.email,cu.password,cu.creating,cu.error);
    else if (cu.kind == 2) cu.result=cloud_device_start(&cu.device,cu.error);
    else cu.result=cloud_device_poll(&cu.device,&cu.account,cu.error);
    SDL_AtomicSet(&cu.done,1); return 0;
}
static void cloud_ui_launch(int kind) {
    if (cu.thread) return;
    cu.kind=kind; cu.error[0]=0; SDL_AtomicSet(&cu.done,0); http_init();
    cu.thread=SDL_CreateThread(cloud_ui_worker,"cloud-login",NULL);
    if (!cu.thread) { snprintf(cu.error,sizeof cu.error,"Cannot start account sign-in."); cu.kind=0; }
}
static void cloud_ui_open(void) {
    memset(&cu,0,sizeof cu); cu.open=1; cu.row=1; cu.prev=input_device_menu(pk.dev)|IN_FIRE;
#ifdef OFP_TVOS
    cu.started=SDL_GetTicks(); cloud_ui_launch(2);
#endif
}
static void cloud_ui_close(void) {
    if (cu.thread) { cu.cancelled=1; return; }
    cu.open=cu.editing=0; memset(cu.password,0,sizeof cu.password); memset(&pk.osk,0,sizeof pk.osk);
    SDL_StopTextInput(); pk.prev=input_device_menu(pk.dev)|IN_FIRE|IN_MINE;
}
static void cloud_ui_success(void) {
    int i;
    cloud_register(cu.account.key,cu.account.session);
    for (i=0;i<nprof;i++) if (!memcmp(prof[i].key,cu.account.key,32)) {
        if (profile_in(i)) { snprintf(cu.error,sizeof cu.error,"This account is already signed in."); return; }
        strcpy(prof[i].session,cu.account.session); strcpy(prof[i].name,cu.account.name);
        profile_receive(&prof[i], &cu.account); save();
        cloud_ui_close(); take(i); return;
    }
    if(nprof>=MAX_PROFILES) {snprintf(cu.error,sizeof cu.error,"No space for another profile.");return;}
    pk.cloud_account=cu.account; pk.cloud_ready=1; strcpy(pk.name,cu.account.name);
    pk.step=1;pk.tank=cu.account.has_preferences?cu.account.preferences.tank:1;
    pk.avatar=cu.account.has_preferences?cu.account.preferences.avatar:nprof%AVATARS; cloud_ui_close();
}
static int cloud_ui_event(const SDL_Event *ev,int pad) {
    if(cu.thread) {
        if(ev->type==SDL_KEYDOWN && ev->key.keysym.sym==SDLK_ESCAPE) cu.cancelled=1;
        return 1;
    }
    if(pk.osk.open) {
        if(pad&&input_device_of_event(ev)==pk.dev) {
            int status=osk_pad(&pk.osk,pad);
            if(status!=OSK_TYPING) {cu.editing=0;memset(&pk.osk,0,sizeof pk.osk);}
        }
        return 1;
    }
#ifndef OFP_TVOS
    if(cu.editing) {
        size_t cap,n; char *field=cloud_ui_field(&cap); n=strlen(field);
        if(ev->type==SDL_TEXTINPUT) {
            const unsigned char *p=(const unsigned char *)ev->text.text;
            for(;*p;p++) if(*p>=32&&*p<127&&n+1<cap) {field[n++]=(char)*p;field[n]=0;}
        } else if(ev->type==SDL_KEYDOWN) {
            if(ev->key.keysym.sym==SDLK_BACKSPACE&&n) field[n-1]=0;
            if(ev->key.keysym.sym==SDLK_RETURN||ev->key.keysym.sym==SDLK_ESCAPE) {
                cu.editing=0;SDL_StopTextInput();cu.prev=input_device_menu(pk.dev)|IN_FIRE|IN_MINE;
            }
        }
        return 1;
    }
#endif
    if(ev->type==SDL_KEYDOWN&&!ev->key.repeat) {
        if(ev->key.keysym.sym==SDLK_ESCAPE) cloud_ui_close();
        if(ev->key.keysym.sym==SDLK_RETURN) pk.enter=1;
    }
    return 1;
}
static void cloud_ui_frame(void) {
    InputBits b=input_device_menu(pk.dev),pr=b&~cu.prev;cu.prev=b;
    if(cu.thread) {
        if(pr&IN_MINE) cu.cancelled=1;
        if(!SDL_AtomicGet(&cu.done)) return;
        SDL_WaitThread(cu.thread,NULL);cu.thread=NULL;
        memset(cu.password,0,sizeof cu.password);
        memset(&pk.osk,0,sizeof pk.osk);
        if(cu.cancelled) {cloud_ui_close();return;}
        if((cu.kind==1&&cu.result==0)||(cu.kind==3&&cu.result==1)) {cloud_ui_success();return;}
        if(cu.kind==2&&cu.result==0) {cu.poll_at=SDL_GetTicks()+5000;cu.started=SDL_GetTicks();}
        if(cu.kind==3&&cu.result==0) cu.poll_at=SDL_GetTicks()+5000;
        cu.kind=0;
    }
#ifdef OFP_TVOS
    if(pr&IN_MINE) {cloud_ui_close();return;}
    if(SDL_GetTicks()-cu.started>=600000) {cu.poll_at=0;snprintf(cu.error,sizeof cu.error,"PIN expired. Close and try again.");}
    if(cu.poll_at&&(Sint32)(SDL_GetTicks()-cu.poll_at)>=0) {cu.poll_at=0;cloud_ui_launch(3);}
#else
    if(cu.editing||pk.osk.open) return;
    if(pk.enter) {pr|=IN_FIRE;pk.enter=0;}
    if(pr&IN_UP) cu.row=(cu.row+5)%6;
    if(pr&IN_DOWN) cu.row=(cu.row+1)%6;
    if(!cu.creating&&cu.row==2) cu.row=pr&IN_UP?1:3;
    if(pr&IN_MINE) {cloud_ui_close();return;}
    if(cu.row==0&&(pr&(IN_FIRE|IN_LEFT|IN_RIGHT))) cu.creating=!cu.creating;
    else if(pr&IN_FIRE) {
        if(cu.row==5) cloud_ui_close();
        else if(cu.row==4) cloud_ui_launch(1);
        else if(cu.row>=1&&cu.row<=3) {
            size_t cap;char *field=cloud_ui_field(&cap);cu.editing=1;
            if(pk.dev>=DEV_PAD1) osk_open(&pk.osk,field,cap,cu.row==1?"USERNAME":cu.row==2?"EMAIL":"PASSWORD",cu.row==3);
            else SDL_StartTextInput();
        }
    }
#endif
}
static void cloud_ui_draw(SDL_Renderer *r,int sw,int sh) {
    int x=8,y=8,w=sw-16; const HudColors *c=ui_colors();char line[96];
    ui_fill(r,x,y,w,sh-16,(Rgb){16,16,12});ui_frame(r,x,y,w,sh-16);
    ui_text_bold(r,x+8,y+8,1,c->value,"CLOUD PROFILE");
#ifdef OFP_TVOS
    ui_text(r,x+8,y+35,1,UI_WHITE,"ON YOUR PHONE OR COMPUTER, OPEN");
    ui_text(r,x+8,y+51,1,c->value,CLOUD_PIN_URL);
    snprintf(line,sizeof line,"PIN: %s",cu.thread&&cu.kind==2?"...":cu.device.pin);ui_text_bold(r,x+8,y+78,2,UI_WHITE,line);
    ui_text(r,x+8,y+108,1,UI_WHITE,"ENTER THE PIN AND SIGN IN.");
#else
    char mask[73]; cloud_password_mask(cu.password, mask, sizeof mask);
    for(int i=0;i<6;i++) {
        const char *label=i==0?(cu.creating?"CREATE CLOUD ACCOUNT":"SIGN IN"):i==1?"USERNAME":i==2?"EMAIL":i==3?"PASSWORD":i==4?(cu.creating?"CREATE ACCOUNT":"CONNECT"):"CANCEL";
        const char *value=i==1?cu.username:i==2?cu.email:i==3?mask:"";
        int yy=y+28+i*20,on=cu.row==i;
        if(i==2&&!cu.creating) continue;
        ui_fill(r,x+6,yy-2,w-12,18,on?c->value:(Rgb){30,30,24});
        ui_text_bold(r,x+10,yy,1,on?(Rgb){16,16,12}:UI_WHITE,label);
        snprintf(line,sizeof line,"%.22s%s",value,cu.editing&&on?"_":"");
        ui_text(r,x+w-12-ui_text_width(line,1),yy+8,1,on?(Rgb){16,16,12}:c->value,line);
    }
#endif
    if(cu.thread) ui_text(r,x+8,sh-40,1,c->value,"CONNECTING...");
    else if(cu.error[0]) {snprintf(line,sizeof line,"%.58s",cu.error);ui_text(r,x+8,sh-40,1,c->value,line);}
    ui_text(r,x+8,sh-24,1,UI_GREY,"LOCAL PROFILES: LOCAL AND PRIVATE GAMES ONLY");
    if(pk.osk.open) osk_draw(r,&pk.osk,sw,sh);
}

int signin_event(const SDL_Event *ev, int pad)
{
    int dev;
    if (pk.open && cu.open) return cloud_ui_event(ev,pad);
    if (pk.open && pk.typing) {                          /* typing a new profile's name */
        if (pk.osk.open) {                               /* (a pad: the on-screen keyboard) */
            if (pad && input_device_of_event(ev) == pk.dev) {
                int r = osk_pad(&pk.osk, pad);
                if (r == OSK_DONE && pk.renaming) finish_rename();
                else if (r == OSK_DONE) { pk.typing = 0; if (pk.name[0]) { pk.step = 1; pk.prev = input_device_menu(pk.dev) | IN_FIRE; } }
                if (r == OSK_CANCEL) { pk.typing = 0; pk.renaming = 0; }
            }
            return 1;
        }
        if (ev->type == SDL_TEXTINPUT) {
            const char *c;
            for (c = ev->text.text; *c; c++) {
                size_t l = strlen(pk.name);
                if (l < 15 && (unsigned char)*c >= 32 && (unsigned char)*c < 127 && *c != '\t') { pk.name[l] = *c; pk.name[l + 1] = 0; }
            }
            return 1;
        }
        if (ev->type == SDL_KEYDOWN) {
            SDL_Keycode k = ev->key.keysym.sym;
            if (k == SDLK_BACKSPACE && pk.name[0]) pk.name[strlen(pk.name) - 1] = 0;
            else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && pk.name[0] && !ev->key.repeat) {   /* the name: next, the tank */
                if (pk.renaming) { SDL_StopTextInput(); finish_rename(); }
                else { pk.typing = 0; pk.step = 1; SDL_StopTextInput(); pk.prev = input_device_menu(pk.dev) | IN_FIRE; }
            }
            else if (k == SDLK_ESCAPE) { pk.typing = 0; pk.renaming = 0; SDL_StopTextInput(); }
            return 1;
        }
        return ev->type == SDL_KEYUP;
    }
    if (pk.open) {                                       /* the picker has the input */
        if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_ESCAPE) { pk.open = 0; return 1; }
        if (ev->type == SDL_KEYDOWN && !ev->key.repeat && (ev->key.keysym.sym == SDLK_RETURN || ev->key.keysym.sym == SDLK_KP_ENTER) &&
            pk.dev < DEV_PAD1) pk.enter = 1;                 /* (a held Enter: once) */
        return ev->type == SDL_KEYDOWN || ev->type == SDL_KEYUP || ev->type == SDL_CONTROLLERBUTTONDOWN ||
               ev->type == SDL_CONTROLLERBUTTONUP || ev->type == SDL_CONTROLLERAXISMOTION || ev->type == SDL_TEXTINPUT;
    }
    /* FIRE on keys or a pad that isn't signed in: who are you? */
    dev = input_device_of_event(ev);
    if (dev < 0 || signed_in(dev) >= 0 || nslot >= MAX_SIGNED || !input_device_is_fire(dev, ev)) return 0;
    memset(&pk, 0, sizeof pk);
    pk.open = 1;
    pk.dev = dev;
    pk.prev = input_device_menu(dev) | IN_FIRE;               /* (this press isn't a choice in the picker) */
    pk.sel = 0;
    while (pk.sel < nprof && profile_in(pk.sel)) pk.sel++;   /* (the first that isn't in already) */
    return 1;
}

void signin_frame(void)
{
    Uint32 now = SDL_GetTicks();
    int i;
    profile_sync_frame();
    if (pk.open && cu.open) {cloud_ui_frame();return;}
    if (pk.open && pk.keep_signed_in) {
        if (!input_device_present(pk.dev)) { pk.open = 0; return; }
    } else {
        for (i = 0; i < nslot; i++)                       /* a pad unplugged: out */
            if (!input_device_present(slot[i].dev)) { sign_out(i); i--; }
    }
    if (pk.open && !pk.typing && pk.step) {              /* making one: the tank and the avatar, one page */
        InputBits b = input_device_menu(pk.dev), pr = b & ~pk.prev;   /* (a pad: A OK, B back, whatever it's set to) */
        int on_avatar = pk.step == 2;
        if (pk.enter) { pr |= IN_FIRE; pk.enter = 0; }
        pk.prev = b;
        if (pr & IN_DOWN) pk.step = 2;                   /* down: the avatars; up: the tank */
        if (pr & IN_UP) pk.step = 1;
        if (on_avatar == (pk.step == 2)) {               /* (left/right: the one with the focus) */
            if (pk.step == 1) {
                if (pr & IN_LEFT) pk.tank = step_pick(pk.tank, -1);
                if (pr & IN_RIGHT) pk.tank = step_pick(pk.tank, 1);
            } else {
                if (pr & IN_LEFT) pk.avatar = (pk.avatar + AVATARS - 1) % AVATARS;
                if (pr & IN_RIGHT) pk.avatar = (pk.avatar + 1) % AVATARS;
            }
        }
        if (pr & IN_FIRE) { pk.step = 0; create(); }    /* done: in */
        else if (pr & IN_MINE) {                         /* back: the name or profile picker */
            if (pk.cloud_ready) {
                pk.cloud_ready = 0; pk.step = 0; pk.prev = b;
                memset(&pk.cloud_account, 0, sizeof pk.cloud_account);
                return;
            }
            pk.step = 0; pk.typing = 1;
            if (pk.dev >= DEV_PAD1) osk_open(&pk.osk, pk.name, sizeof pk.name, "YOUR NAME", 0);
            else SDL_StartTextInput();
        }
        return;
    }
    if (pk.open && pk.own && !pk.typing) {               /* your own profile: CONTROLLER, NAME, FACE, SIGN OUT, BACK */
        InputBits b = input_device_menu(pk.dev), pr = b & ~pk.prev;
        int me = signed_in(pk.dev), rows = own_rows(), r;
        Profile *p;
        if (pk.enter) { pr |= IN_FIRE; pk.enter = 0; }
        pk.prev = b;
        if (me < 0) { pk.open = 0; return; }
        p = &prof[slot[me].profile];
        if (pr & IN_UP) pk.sel = (pk.sel + rows - 1) % rows;
        if (pr & IN_DOWN) pk.sel = (pk.sel + 1) % rows;
        if (pr & IN_MINE) {                              /* B: the CONTROLLER page back to the window; the window closed */
            if (pk.sub) { pk.sub = 0; pk.sel = 0; } else pk.open = 0;
            return;
        }
        r = own_row(pk.sel);
        if ((pr & (IN_LEFT | IN_RIGHT)) && (r <= OW_MINE || r == OW_FACE || r == OW_STEER || r == OW_TANK)) {
            int dir = (pr & IN_LEFT) ? -1 : 1;
            if (r == OW_DRIVE) p->pad_layout = ((p->pad_layout < 0 ? PAD_TRIGGERS : p->pad_layout) + dir + PAD_LAYOUTS) % PAD_LAYOUTS;
            if (r == OW_FIRE) p->pad_fire = pad_step(p->pad_fire, dir);
            if (r == OW_MINE) p->pad_mine = pad_step(p->pad_mine, dir);
            if (r == OW_FACE) p->avatar = (p->avatar + dir + AVATARS) % AVATARS;
            if (r == OW_STEER) p->steer = !p->steer;
            if (r == OW_TANK) p->tank = step_pick(p->tank, dir);
            if(r==OW_FACE||r==OW_STEER||r==OW_TANK) profile_changed(p); else save();
            apply_controls();
        }
        if (pr & IN_FIRE) {
            if (r == OW_CONTROLLER) { pk.sub = 1; pk.sel = 0; }
            else if (r == OW_PADBACK) { pk.sub = 0; pk.sel = 0; }
            else if (r == OW_DEFAULTS) { p->pad_layout = p->pad_fire = p->pad_mine = -2; save(); apply_controls(); }
            else if (r == OW_NAME && !p->session[0]) {                     /* a new name: the on-screen keyboard (a pad) or typed */
                pk.typing = 1; pk.renaming = 1;
                snprintf(pk.name, sizeof pk.name, "%s", p->name);
                if (pk.dev >= DEV_PAD1) osk_open(&pk.osk, pk.name, sizeof pk.name, "YOUR NAME", 0);
                else SDL_StartTextInput();
            }
            else if (r == OW_FACE) { p->avatar = (p->avatar + 1) % AVATARS; profile_changed(p); }
            else if (r == OW_TANK) { p->tank = step_pick(p->tank, 1); profile_changed(p); }
            else if (r == OW_STEER) { p->steer = !p->steer; profile_changed(p); }
            else if (r == OW_SIGNOUT) { sign_out(me); pk.open = 0; }   /* (the rest move up: player 2 becomes player 1) */
            else if (r == OW_BACK) pk.open = 0;
        }
        return;
    }
    if (pk.open && !pk.typing) {                         /* the picker, with its own device */
        InputBits b = input_device_menu(pk.dev), pr = b & ~pk.prev;   /* (a pad: A OK, B back, whatever it's set to) */
        int rows = nprof + 2, tries;
        if (pk.enter) { pr |= IN_FIRE; pk.enter = 0; }
        pk.prev = b;
        if (pr & (IN_UP | IN_DOWN)) {
            for (tries = 0; tries < rows; tries++) {
                pk.sel = (pk.sel + ((pr & IN_UP) ? rows - 1 : 1)) % rows;
                if (pk.sel >= nprof || !profile_in(pk.sel)) break;   /* (not someone already in) */
            }
        }
        if (pr & IN_MINE) { pk.open = 0; return; }       /* not now */
        if (pr & IN_FIRE) {
            if (pk.sel < nprof) { if (!profile_in(pk.sel)) take(pk.sel); }
            else if (pk.sel == nprof + 1) cloud_ui_open();
            else {                                       /* NEW PROFILE: a name, then a tank and an avatar */
                pk.cloud_ready = 0;
                memset(&pk.cloud_account, 0, sizeof pk.cloud_account);
                pk.typing = 1;
                pk.tank = 1;
                pk.avatar = nprof % AVATARS;
                snprintf(pk.name, sizeof pk.name, "PLAYER %d", (nslot + 1) % 10);
                if (pk.dev >= DEV_PAD1) osk_open(&pk.osk, pk.name, sizeof pk.name, "YOUR NAME", 0);
                else { pk.name[0] = 0; SDL_StartTextInput(); }
            }
        }
        return;
    }
    for (i = 0; i < nslot; i++) {                        /* MINE held a second: out */
        if (input_device_menu(slot[i].dev) & IN_MINE) {   /* (B held: signing out, whatever its buttons) */
            if (!slot[i].hold_since) slot[i].hold_since = now | 1;
            else if (now - slot[i].hold_since > 1000) { sign_out(i); i--; }
        } else slot[i].hold_since = 0;
    }
}

int         signin_busy(void) { return pk.open; }
int         signin_count(void) { return nslot; }
int         signin_device(int i) { return i >= 0 && i < nslot ? slot[i].dev : -1; }
const char *signin_name(int i) { return i >= 0 && i < nslot ? prof[slot[i].profile].name : ""; }
const uint8_t *signin_key(int i) { return i >= 0 && i < nslot ? prof[slot[i].profile].key : NULL; }
int         signin_avatar(int i) { return i >= 0 && i < nslot ? prof[slot[i].profile].avatar : 0; }
int         signin_tank(int i) { return i >= 0 && i < nslot ? prof[slot[i].profile].tank : 1; }
void        signin_set_tank(int i, int tank)
{
    if (i < 0 || i >= nslot || tank < TANK_LIGHT || tank > TANK_RHINO || prof[slot[i].profile].tank == tank) return;
    prof[slot[i].profile].tank = tank;
    profile_changed(&prof[slot[i].profile]);
}

/* each signed-in profile's own controls on its device (the rest: the machine's) */
static void apply_controls(void)
{
    int i;
    input_reset_controls();
    for (i = 0; i < nslot; i++) {
        const Profile *p;
        if (slot[i].profile < 0 || slot[i].profile >= nprof) continue;
        p = &prof[slot[i].profile];
        if (slot[i].dev >= DEV_PAD1) input_set_dev_pad(slot[i].dev, p->pad_layout, p->pad_fire, p->pad_mine);
        else if (p->has_keys) input_set_keyset(slot[i].dev - DEV_KEYS1, (const int (*)[2])p->keys);
    }
}

/* player n's own controls: changed (OPTIONS -> KEYS), saved with the profile and on its device */
void signin_set_controls(int n, int layout, int fire, int mine, const int keys[6][2])
{
    Profile *p;
    if (n < 0 || n >= nslot || slot[n].profile < 0 || slot[n].profile >= nprof) return;
    p = &prof[slot[n].profile];
    p->pad_layout = layout; p->pad_fire = fire; p->pad_mine = mine;
    if (keys) { memcpy(p->keys, keys, sizeof p->keys); p->has_keys = 1; }
    save();
    apply_controls();
}

/* player n's own controls, into the KEYS page's (what it doesn't have of its own: left as it is) */
void signin_get_controls(int n, int *layout, int *fire, int *mine, int keys[6][2])
{
    const Profile *p;
    if (n < 0 || n >= nslot || slot[n].profile < 0 || slot[n].profile >= nprof) return;
    p = &prof[slot[n].profile];
    if (layout && p->pad_layout != -2) *layout = p->pad_layout;
    if (fire && p->pad_fire != -2) *fire = p->pad_fire;
    if (mine && p->pad_mine != -2) *mine = p->pad_mine;
    if (keys && p->has_keys) memcpy(keys, p->keys, sizeof p->keys);
}

/* just the keys, or just the pad (the rest as it is) */
void signin_set_keys(int n, const int keys[6][2])
{
    if (n < 0 || n >= nslot || slot[n].profile < 0 || slot[n].profile >= nprof) return;
    memcpy(prof[slot[n].profile].keys, keys, sizeof prof[slot[n].profile].keys);
    prof[slot[n].profile].has_keys = 1;
    save();
    apply_controls();
}

void signin_reset_keys(void)
{
    int i, changed = 0;
    for (i = 0; i < nslot; i++) {
        Profile *p = &prof[slot[i].profile];
        if (slot[i].dev < DEV_PAD1 && p->has_keys) {
            p->has_keys = 0;
            changed = 1;
        }
    }
    if (changed) { save(); apply_controls(); }
}

int signin_pad_layout(int n)                         /* player n's pad layout (-2: the machine's) */
{
    return n >= 0 && n < nslot && slot[n].profile >= 0 && slot[n].profile < nprof ? prof[slot[n].profile].pad_layout : -2;
}

void signin_assign_inputs(void)
{
    int devs[MAX_SIGNED], i;
    for (i = 0; i < nslot; i++) devs[i] = slot[i].dev;
    input_assign(devs, nslot);
    apply_controls();                                    /* (and each profile's own controls) */
}

/* text left in a box, shortened (..) if it doesn't fit */
static void fit_text(SDL_Renderer *r, int x, int y, int w, Rgb col, const char *text)
{
    char t[96];
    size_t n;
    snprintf(t, sizeof t, "%s", text);
    while ((n = strlen(t)) > 3 && ui_text_width(t, 1) > w) { t[n - 3] = '.'; t[n - 2] = '.'; t[n - 1] = 0; }   /* (one off, ".." on) */
    ui_text(r, x, y, 1, col, t);
}

static void upcase(char *s) { for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 32); }

#define badge(r, x, y, av, name) ui_avatar(r, x, y, 9, av, name)
void signin_draw_avatar(SDL_Renderer *r, int x, int y, int size, int avatar, const char *name) { ui_avatar(r, x, y, size, avatar, name); }

/* a match over: signed-in player n's profile gets it (won: 1, kills, lost, LOCAL GAME's mode, the tank) */
void signin_record(int n, int won, int kills, int lost, int mode, int cls)
{
    Profile *p;
    if (n < 0 || n >= nslot || slot[n].profile < 0 || slot[n].profile >= nprof) return;
    p = &prof[slot[n].profile];
    p->matches++;
    if (won) { p->won++; p->mode_won[mode % 5]++; }
    p->kills += kills; p->lost += lost;
    p->cls_played[cls % 3]++;
    save();
}

/* a signed-in player's own window: their profile, SIGN OUT and BACK (START on their pad in the menus) */
void signin_open_own(int dev, int keep_signed_in)
{
    if (dev < 0 || signed_in(dev) < 0 || pk.open) return;
    memset(&pk, 0, sizeof pk);
    pk.open = 1; pk.own = 1; pk.dev = dev;
    pk.keep_signed_in = keep_signed_in;
    pk.sel = own_rows() - 1;                             /* (BACK first: signing out takes a choice) */
    pk.prev = input_device_menu(dev) | IN_FIRE;
}

int signin_is_in(int dev) { return signed_in(dev) >= 0; }
int signin_steer(int n) { return n >= 0 && n < nslot && slot[n].profile >= 0 && slot[n].profile < nprof ? prof[slot[n].profile].steer : STEER_TANK; }

void signin_open(int dev)
{
    SDL_Event fake;
    if (dev < 0 || signed_in(dev) >= 0 || nslot >= MAX_SIGNED || pk.open) return;
    memset(&pk, 0, sizeof pk);
    pk.open = 1;
    pk.dev = dev;
    pk.prev = input_device_menu(dev) | IN_FIRE;
    while (pk.sel < nprof && profile_in(pk.sel)) pk.sel++;
    (void)fake;
}

void signin_draw_bar(SDL_Renderer *r, int x, int y, int w)
{
    const HudColors *c = ui_colors();
    int i, cw = w / MAX_SIGNED;
    char buf[24];
    for (i = 0; i < MAX_SIGNED; i++) {
        int cx = x + i * cw;
        ui_fill(r, cx + 1, y, cw - 2, 12, (Rgb){ 18, 18, 14 });
        if (i < nslot) {
            ui_avatar(r, cx + 2, y + 1, 10, prof[slot[i].profile].avatar, prof[slot[i].profile].name);
            snprintf(buf, sizeof buf, "%.9s", prof[slot[i].profile].name);
            upcase(buf);
            ui_text_bold(r, cx + 15, y + 2, 1, c->value, buf);
            if (slot[i].hold_since) {                    /* signing out: how long still */
                Uint32 held = SDL_GetTicks() - slot[i].hold_since;
                ui_fill(r, cx + 1, y + 10, (int)((cw - 2) * (held > 1000 ? 1000 : held) / 1000), 2, (Rgb){ 230, 110, 80 });
            }
        } else ui_text(r, cx + 4, y + 2, 1, UI_GREY, i == nslot ? "FIRE: JOIN" : "");
    }
}

void signin_draw_picker(SDL_Renderer *r, int sw, int sh)
{
    const HudColors *c = ui_colors();
    /* Keep space for match records when there are profiles to select. */
    int rows = nprof + 2, bw = 300, h = pk.step ? 76 : 24 + rows * 11 + (nprof ? 42 : 6) + 12;
    int x = (sw - bw) / 2, y = (sh - h) / 2, i;
    char buf[64];
    if (!pk.open) return;
    if (cu.open) {cloud_ui_draw(r,sw,sh);return;}
    if (pk.typing && pk.osk.open) { osk_draw(r, &pk.osk, sw, sh); return; }
    if (pk.own) {                                        /* your own profile: who, your record; a pad's settings; SIGN OUT, BACK */
        int me = signed_in(pk.dev), k, orows = own_rows(), oh = 62 + orows * 12 + 12, oy = (sh - oh) / 2;
        const Profile *p = me >= 0 ? &prof[slot[me].profile] : NULL;
        if (!p) return;
        ui_fill(r, x, oy, bw, oh, (Rgb){ 16, 16, 12 });
        ui_frame(r, x, oy, bw, oh);
        ui_avatar(r, x + 8, oy + 6, 16, p->avatar, p->name);
        snprintf(buf, sizeof buf, pk.sub ? "PLAYER %d: %s, CONTROLLER" : "PLAYER %d: %s", me + 1, p->name);
        upcase(buf);
        ui_text_bold(r, x + 30, oy + 10, 1, c->value, buf);
        snprintf(buf, sizeof buf, "%d MATCH%s, %d WON", p->matches, p->matches == 1 ? "" : "ES", p->won);
        fit_text(r, x + 8, oy + 28, bw - 16, UI_GREY, buf);
        snprintf(buf, sizeof buf, "%d TANKS DESTROYED, %d LOST", p->kills, p->lost);
        fit_text(r, x + 8, oy + 38, bw - 16, UI_GREY, buf);
        for (k = 0; k < orows; k++) {
            int row = own_row(k), yy = oy + 54 + k * 12, on = pk.sel == k, lay = p->pad_layout < 0 ? PAD_TRIGGERS : p->pad_layout;
            static const char *const lbl[] = { "PAD: DRIVE", "PAD: FIRE", "PAD: MINE", "DEFAULTS", "BACK",
                                               "CONTROLLER", "STEERING", "NAME", "FACE", "FAVORITE TANK", "SIGN OUT", "BACK" };
            char nm[24];
            const char *val;
            snprintf(nm, sizeof nm, "%s%s", pk.renaming ? pk.name : p->name, pk.renaming && (SDL_GetTicks() / 400) % 2 ? "_" : "");
            upcase(nm);
            val = row == OW_DRIVE ? (lay == PAD_CLASSIC ? "THE STICK" : "RT/LT, STICK TURNS")
                : row == OW_FIRE ? pad_btn_label(p->pad_fire, 1, lay) : row == OW_MINE ? pad_btn_label(p->pad_mine, 0, lay)
                : row == OW_NAME ? nm : row == OW_FACE ? " " : row == OW_STEER ? (p->steer == STEER_DIRECT ? "ORIGINAL" : "TANK") : "";
            Rgb fg = on ? (Rgb){ 20, 18, 10 } : UI_WHITE;
            ui_fill(r, x + 8, yy - 2, bw - 16, 11, on ? c->value : (Rgb){ 30, 30, 24 });
            ui_text_bold(r, x + 14, yy, 1, fg, lbl[row]);
            if (row == OW_FACE) {                        /* (your face, with arrows when it's chosen) */
                ui_avatar(r, x + bw - 34, yy - 2, 12, p->avatar, p->name);
                if (on) { ui_text(r, x + bw - 46, yy, 1, fg, "<"); ui_text(r, x + bw - 18, yy, 1, fg, ">"); }
            } else if (row == OW_TANK) {
                int pick = available_pick(p->tank);
                snprintf(buf, sizeof buf, on ? "< %s >" : "%s", tank_label[pick]);
                ui_text(r, x + bw - 40 - ui_text_width(buf, 1), yy, 1, on ? fg : c->value, buf);
                render_tank_icon(r, world_tanks ? world_tanks : theme ? theme->tanks : NULL,
                                 pick, TEAM_PLAYER, x + bw - 24, yy + 4, 16);
            } else if (val[0]) {
                snprintf(buf, sizeof buf, on && row != OW_NAME ? "< %s >" : "%s", val);
                ui_text(r, x + bw - 14 - ui_text_width(buf, 1), yy, 1, on ? fg : c->value, buf);
            }
        }
        if (SDL_GetTicks() < pk.note_until) ui_text(r, x + 8, oy + oh - 12, 1, c->value, pk.note);   /* (why a name wasn't taken) */
        else ui_text(r, x + 8, oy + oh - 12, 1, UI_GREY, pk.dev >= DEV_PAD1 ? "A: OK   LEFT/RIGHT: CHANGE   B: BACK" : "FIRE: OK   MINE: BACK");
        return;
    }
    ui_fill(r, x, y, bw, h, (Rgb){ 16, 16, 12 });
    ui_frame(r, x, y, bw, h);
    snprintf(buf, sizeof buf, "PLAYER %d (%s)", nslot + 1, input_device_name(pk.dev));
    ui_text_bold(r, x + 8, y + 6, 1, c->value, buf);
    ui_text(r, x + bw - 8 - ui_text_width("SIGN IN", 1), y + 6, 1, UI_WHITE, "SIGN IN");
    if (pk.step) {                                       /* making one: the tank and the avatar */
        int wx = 8, wy = 6, ww = sw - 16, wh = sh - 12, k;
        ui_fill(r, wx, wy, ww, wh, (Rgb){ 16, 16, 12 });
        ui_frame(r, wx, wy, ww, wh);
        snprintf(buf, sizeof buf, "PLAYER %d (%s): %.15s", nslot + 1, input_device_name(pk.dev), pk.name);
        upcase(buf);
        ui_text_bold(r, wx + 8, wy + 6, 1, c->value, buf);
        ui_text(r, wx + 8, wy + 18, 1, pk.step == 1 ? c->value : UI_GREY, "YOUR TANK");
        if (theme) select_tank_card(r, theme, world_tanks ? world_tanks : theme->tanks,
                                    available_pick(pk.tank), tank_choices, wx + 8, wy + 28, pk.step == 1);
        else ui_text(r, wx + 8, wy + 40, 1, UI_WHITE, tank_label[pk.tank]);
        ui_text(r, wx + 204, wy + 18, 1, pk.step == 2 ? c->value : UI_GREY, "YOUR AVATAR");
        for (k = 0; k < AVATARS; k++) {                  /* two rows of five */
            int ax = wx + 204 + (k % 5) * 30, ay = wy + 30 + (k / 5) * 32;
            if (k == pk.avatar) ui_fill(r, ax - 2, ay - 2, 28, 28, pk.step == 2 ? c->value : c->frame_dark);
            ui_avatar(r, ax, ay, 24, k, pk.name);
        }
        ui_avatar(r, wx + 204 + 50, wy + 102, 48, pk.avatar, pk.name);   /* the one, big */
        ui_text(r, wx + 8, wy + wh - 12, 1, UI_GREY, "ARROWS: CHOOSE  FIRE: DONE  MINE: BACK");
        return;
    }
    if (pk.typing) {                                     /* a name, from the keyboard */
        snprintf(buf, sizeof buf, "NAME: %.15s%s", pk.name, (SDL_GetTicks() / 400) % 2 ? "_" : " ");
        upcase(buf);
        ui_text(r, x + 8, y + 24, 1, UI_WHITE, buf);
        ui_text(r, x + 8, y + h - 12, 1, UI_GREY, "ENTER: DONE   ESC: NOT NOW");
        return;
    }
    for (i = 0; i < rows; i++) {
        int ry = y + 22 + i * 11, in = i < nprof && profile_in(i);
        if (i == pk.sel) ui_fill(r, x + 4, ry - 1, bw - 8, 10, c->value);
        if (i < nprof) {
            snprintf(buf, sizeof buf, "%.15s%s", prof[i].name, in ? "  (IN)" : "");
            badge(r, x + 8, ry - 1, prof[i].avatar, prof[i].name);
            const char *tank = tank_label[available_pick(prof[i].tank)];
            ui_text(r, x + bw - 8 - ui_text_width(tank, 1), ry, 1, i == pk.sel ? (Rgb){ 20, 18, 10 } : UI_GREY, tank);
        } else snprintf(buf, sizeof buf, "%s", i == nprof ? "NEW LOCAL PROFILE" : "CLOUD PROFILE");
        upcase(buf);
        ui_text(r, x + 22, ry, 1, i == pk.sel ? (Rgb){ 20, 18, 10 } : in ? UI_GREY : UI_WHITE, buf);
    }
    if (pk.sel < nprof) {                                /* the chosen profile's match record */
        int sy = y + 24 + rows * 11 + 2;
        ui_fill(r, x + 6, sy, bw - 12, 1, (Rgb){ 60, 56, 44 });
        sy += 5;
        if (prof[pk.sel].matches) {
            const Profile *p = &prof[pk.sel];
            int fav = p->cls_played[1] >= p->cls_played[0] ? 1 : 0;
            if (p->cls_played[2] > p->cls_played[fav]) fav = 2;
            snprintf(buf, sizeof buf, "%d MATCH%s, %d WON (MOSTLY %s)", p->matches, p->matches == 1 ? "" : "ES", p->won, tank_label[fav]);
            fit_text(r, x + 8, sy, bw - 16, c->value, buf);
            snprintf(buf, sizeof buf, "%d TANKS DESTROYED, %d LOST", p->kills, p->lost);
            fit_text(r, x + 8, sy + 10, bw - 16, UI_WHITE, buf);
            snprintf(buf, sizeof buf, "WON: CTF %d CONQ %d FFA %d TDM %d WL %d", p->mode_won[0], p->mode_won[1], p->mode_won[2],
                     p->mode_won[3], p->mode_won[4]);
            fit_text(r, x + 8, sy + 20, bw - 16, UI_GREY, buf);
        } else ui_text(r, x + 8, sy + 10, 1, UI_GREY, "NO MATCHES YET");
    }
    ui_text(r, x + 8, y + h - 12, 1, UI_GREY, pk.dev >= DEV_PAD1 ?
            (pk.sel == nprof ? "A CREATE PROFILE   B BACK" : "A SIGN IN   B BACK") :
            (pk.sel == nprof ? "ENTER / FIRE CREATE   ESC BACK" : "ENTER / FIRE SIGN IN   ESC BACK"));
}

void signin_test(const char *names)
{
    char buf[128], *t;
    snprintf(buf, sizeof buf, "%s", names);
    for (t = strtok(buf, ","); t && nslot < MAX_SIGNED; t = strtok(NULL, ",")) {
        int i;
        for (i = 0; i < nprof && SDL_strcasecmp(prof[i].name, t); i++) {}
        if (i == nprof) continue;
        slot[nslot].dev = getenv("OFP_TEST_ON_PAD") ? DEV_PAD1 + nslot : nslot < 2 ? DEV_KEYS1 + nslot : DEV_PAD1 + nslot - 2;   /* (pictures: on a pad) */
        slot[nslot].profile = i;
        slot[nslot].hold_since = 0;
        nslot++;
    }
    signin_assign_inputs();
}

void signin_shutdown(void) {
    if(cu.thread) { SDL_WaitThread(cu.thread,NULL);cu.thread=NULL;memset(cu.password,0,sizeof cu.password);memset(&cu.account,0,sizeof cu.account); }
    if(sync_profile.thread) { SDL_WaitThread(sync_profile.thread,NULL); sync_profile.thread=NULL;
        // Preserve pending edits on disk if shutdown precedes the next UI frame.
        for(int i=0;i<nprof;i++) if(sync_profile.writing&&!sync_profile.result &&
            !memcmp(prof[i].key,sync_profile.key,32)&&!strcmp(prof[i].session,sync_profile.session)) {
            CloudPreferences p=profile_preferences(&prof[i]);
            if(p.tank==sync_profile.preferences.tank&&p.avatar==sync_profile.preferences.avatar&&p.steer==sync_profile.preferences.steer) prof[i].cloud_dirty=0;
        }
        save();memset(&sync_profile,0,sizeof sync_profile);
    }
}
