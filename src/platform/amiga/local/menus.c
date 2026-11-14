/* SPDX-License-Identifier: MIT
 * src/platform/amiga/local/menus.c: the online client's menus: profiles and signing in, options, local and network game, teams, the server lists, the lobby.
 * A part of the Amiga client, its own translation unit: what the parts share is in internal.h. */
#include "platform/amiga/local/internal.h"
#include "common/credits.h"
#include "common/cloud.h"
static uint32_t credits_start;

const char *const world_names[] = { "CLASSIC", "DESERT", "JUNGLE", "WINTER", "SCI-FI", "VOLCANO", "WASTELAND", "MOON BASE", "SWAMP" };

AmigaMenuSettings settings;
AmigaPublicList public_list;
int public_tab = 1, public_checked, public_failed;
static SavedServer browser[AMIGA_SERVERS];
char online_status[96], edit_text[256];
static char connected_address[AMIGA_ADDRESS], last_direct[AMIGA_ADDRESS];
static int order_seat;
int orders_available(void) {
    int slot = online_playing ? online.slot : order_seat;
    if (!game_orders_allowed(&game) || (unsigned)slot >= (unsigned)game.num_tanks) return 0;
    return online_playing ? online.can_order : game.team_leader[game.tanks[slot].team & 1] == slot && !game.tanks[slot].ai;
}
void orders_seat_step(void) { if (!online_playing && signed_profile[1] != SIGNED_OUT) order_seat ^= 1; }
void orders_accept(void) {
    static const int kinds[5] = { ORDER_ATTACK, ORDER_DEFEND, ORDER_ESCORT, ORDER_REGROUP, ORDER_NONE };
    if (!orders_available() || pause_row < 2 || pause_row > 6) return;
    if (online_playing) netc_team_order(&online, kinds[pause_row - 2]);
    else game_team_order(&game, order_seat, kinds[pause_row - 2]);
    unpause();
}
int pause_row;                        /* ESC in a match: RESUME, QUIT MATCH (local: it waits; online: on) */
int chat_edit;
static int lobby_page, lobby_action, lobby_keyboard, lobby_key;
static char lobby_name[NET_TEAM_NAME];
static const char lobby_letters[] = "ABCDEFGHIJKLM" "NOPQRSTUVWXYZ" "0123456789 -." "@/:?!,'()+_#&";
enum { LA_NONE, LA_CAMP, LA_FORTRESS, LA_READY, LA_WATCH, LA_PAGE, LA_STATUS, LA_LEAVE, LA_CHAT, LA_KEYBOARD, LA_COUNT };
enum { LK_CHAT = SIDE_FORTRESS + 1 };
enum { LK_COLS = 13, LK_ROWS = 4, LK_SAVE = LK_COLS * LK_ROWS, LK_DELETE, LK_CANCEL };
enum { LK_X = 30, LK_Y = 102, LK_W = 20, LK_H = 17, LK_FOOT = 174, LK_BUTTON = 87 };
enum { LOBBY_CHAT_X = 10, LOBBY_CHAT_Y = 237, LOBBY_KEYS_X = 234, LOBBY_CHAT_END = 309 };
char chat_line[NET_CHAT];
int recorded;                          /* (this match's result: in the profile's record once) */
int menu_page, menu_row, menu_edit;
int replay_loading;
int preview_map = -1;
int team_ready[2], host_side, team_tank[2];
static char team_names[2][12];
static int editing_team = -1;
int signed_profile[2] = { SIGNED_OUT, SIGNED_OUT };
int join_player;
static int join_return, join_return_row, join_new_slot;
static int join_rows[AMIGA_PROFILES + 1], join_count;
static InputBits menu_joy, signin_joy;
static int remap_capture, remap_player;
static uint8_t remap_keys[6];
static int profile_action, profile_slot;
static AmigaProfile profile_draft;
static int cloud_form, cloud_create;
static char cloud_user[16], cloud_email[255], cloud_password[73];
static char *cloud_text_field(void) {return menu_row==1?cloud_user:menu_row==2?cloud_email:cloud_password;}
static size_t cloud_text_cap(void) {return menu_row==1?sizeof cloud_user:menu_row==2?sizeof cloud_email:sizeof cloud_password;}
int online_dirty = 1, online_enter, online_escape, online_refresh;
uint32_t local_tick, local_draw;

static const char *const main_items[MAIN_P1] = {
    "PLAY  >", "TOOLS  >", "SETTINGS  >", "ABOUT  >", "QUIT"
};
static const char *const play_items[] = { "LOCAL GAME  >", "NETWORK GAME  >", "TRAINING", "CHALLENGES  >", "BACK" };
static const char *const tool_items[TOOL_ROWS] = { "REPLAYS", "MAP EDITOR", "BENCHMARK", "INPUT TEST", "BACK" };
static const char *const setting_items[SET_ROWS] = { "GAME OPTIONS", "GRAPHICS", "AUDIO", "CONTROLS", "BACK" };
static const char *const remap_names[6] = { "FORWARD", "BACKWARD", "LEFT", "RIGHT", "FIRE", "LAY MINE" };
enum { PROFILE_USE, PROFILE_EDIT, PROFILE_DELETE, PROFILE_ACTIONS };
enum { SERVER_JOIN, SERVER_EDIT, SERVER_DELETE };
enum { SERVER_FORM_NONE, SERVER_FORM_ADD, SERVER_FORM_EDIT, SERVER_FORM_DIRECT, SERVER_FORM_DELETE };
static int server_action, server_form, server_slot, server_confirm;
enum { EDIT_ACCOUNT, EDIT_NAME, EDIT_FACE, EDIT_TANK, EDIT_STEER, EDIT_SAVE, EDIT_BACK, EDIT_ROWS };
enum { P1_INPUT, P1_JOY, P1_KEYS, P2_INPUT, P2_JOY, P2_KEYS, CONTROLS_BACK, CONTROLS_RESET, CONTROL_ROWS };
enum { KEYS_FIRST, KEYS_LAST = KEYS_FIRST + 5, KEYS_SAVE, KEYS_BACK, KEYS_ROWS };

static int keymaps_overlap(const uint8_t *first, const uint8_t *second)
{
    int i;
    for (i = 0; i < 6; i++)
        if (keymap_action(first, second[i]) || keymap_action(second, first[i])) return 1;
    return 0;
}

/* Older versions let both seats bind the same key. Preserve P1 and keep as many
 * of P2's existing bindings as possible. */
static void separate_saved_keymaps(void)
{
    static const uint8_t spare[6] = { 0x3e, 0x1e, 0x2d, 0x2f, 0x2e, 0x0f };  /* numpad 8, 2, 4, 6, 5, 0 */
    const uint8_t *first = player_keys(0), *second = player_keys(1);
    AmigaSeatControls *controls = &settings.controls[1];
    int action, candidate = 0, arrows = !memcmp(second, default_keys[0], sizeof remap_keys);
    if (!keymaps_overlap(first, second)) return;
    if (!controls->has_keys) memcpy(controls->keys, second, sizeof controls->keys);
    controls->has_keys = 1;
    for (action = 0; action < 6; action++) {
        uint8_t code;
        int alias = arrows && ((action == 4 && keymap_action(first, AMIGA_KEY_RAMIGA)) ||
                               (action == 5 && keymap_action(first, AMIGA_KEY_RALT)));
        if (!alias && !keymap_action(first, controls->keys[action])) continue;
        do {
            /* The default P2 keys and the numpad give twelve candidates.
             * Each conflicting binding leaves a candidate unused. */
            code = candidate < 6 ? default_keys[1][candidate] : spare[candidate - 6];
            candidate++;
        } while (keymap_action(first, code) || memchr(controls->keys, code, sizeof controls->keys));
        controls->keys[action] = code;
    }
}

void save_menu_settings(void)
{
    if (amiga_settings_save(&settings))
        strcpy(online_status, "Could not save settings.");
}

static const char *signed_name(int player)
{
    int slot = signed_profile[player];
    return slot >= 0 ? settings.profile[slot].name : "";
}

int local_classic(void)
{
    return settings.world == BIOME_CLASSIC || (settings.map == MY_MAP && my_map_mode == MAP_MODE_CLASSIC);
}

int profile_tank(int player)
{
    int slot = signed_profile[player];
    int tank = slot >= 0 ? settings.profile[slot].tank : -1;
    if (tank < 0) tank = player ? TANK_MEDIUM : settings.cls;
    return local_classic() ? tank_chassis(tank) : tank;
}

int profile_steering(int player)
{
    int slot = signed_profile[player];
    int steer = slot >= 0 ? settings.profile[slot].steer : player == 0 ? settings.profile[settings.current].steer : 0;
    return steer > 0 ? STEER_DIRECT : STEER_TANK;
}

void apply_menu_settings(void)
{
    int player;
    separate_saved_keymaps();
    for (player = 0; player < 2; player++)
        input_mode[player] = settings.controls[player].input >= 0 ? settings.controls[player].input :
            player == 0 && settings.profile[settings.current].input > 0 ? AMIGA_INPUT_JOY : AMIGA_INPUT_BOTH;
    menu_steer = profile_steering(0);
    detailed_radar = !settings.radar_off;
    music_on = settings.music != VOLUME_DISABLED;
    music_volume(settings.music);
    sound_volume(settings.effects);
    if (settings.map >= local_map_count) settings.map = 0;
    if (settings.map_size < 0) {
        settings.map_size = selected_map_size(settings.map);
        if (settings.map_size < 0 || settings.map_size > SIZE_LARGE) settings.map_size = SIZE_LARGE;
        if (settings.map >= LOCAL_MAPS) settings.world = catalog_maps[settings.map - LOCAL_MAPS].biome;
        if (settings.map == MY_MAP && my_map_size >= 0) settings.world = my_map_biome;
    }
    {
        unsigned modes = local_world_modes(settings.world);
        if (!(modes & (1u << settings.mode))) {
            settings.mode = 0;
            while (!(modes & (1u << settings.mode))) settings.mode++;
        }
    }
    if (settings.world == BIOME_CLASSIC && settings.map_size != SIZE_ANY)
        settings.map_size = SIZE_LARGE;
    if (!map_fits_filters(settings.map, settings.world, settings.mode, settings.map_size))
        settings.map = random_for_size(settings.map_size);
    if (settings.world == BIOME_CLASSIC && random_map(settings.map)) {
        int named = first_named_map(settings.world, settings.mode, settings.map_size);
        if (named >= 0) settings.map = named;
    }
    map_choice = settings.map;
    colour_blind = settings.sides;
    marks_no_blood = settings.marks;
    mode_choice = settings.mode;
    level_choice = settings.level;
    cls_choice = profile_tank(0);
#ifndef OFP_LOCAL_DUEL
    world_choice = settings.world;
#endif
}

void menu_change(int page)
{
    if (page == MENU_ABOUT) credits_start = net_ms();
    if (page == MENU_LOCAL && menu_page != MENU_TEAMS) preview_map = -1;
    if (page == MENU_SIGNIN) profile_action = PROFILE_USE;
    menu_row = page == MENU_LOCAL && menu_page == MENU_TEAMS ? LG_NEXT : 0;
    if (menu_edit) ModifyIDCMP(window, RAW_EVENTS);
    menu_page = page;
    if (page == MENU_NETWORK) {
        server_form = SERVER_FORM_NONE;
        server_action = SERVER_JOIN;
        if (!online.socket_open && public_tab && !public_checked && !public_list.pending) online_refresh = 1;
    }
    if (page == MENU_TEAMS) {
        team_ready[0] = team_ready[1] = 0;
        settings.players = signed_profile[1] == SIGNED_OUT ? 1 : 2;
        host_side = TEAM_PLAYER;
        strcpy(team_names[TEAM_PLAYER], "CAMP");
        strcpy(team_names[TEAM_ENEMY], "FORTRESS");
        team_tank[0] = profile_tank(0);
        team_tank[1] = profile_tank(1);
    }
    menu_edit = 0;
    editing_team = -1;
    online_status[0] = 0;
    online_dirty = 1;
}

static void show_signin(int preferred)
{
    int slot, first_available = -1, selected = -1;
    menu_change(MENU_SIGNIN);
    menu_joy = player_joystick(0);
    if (join_player == 1) signin_joy = player_joystick(1);
    join_count = 0;
    join_new_slot = -1;
    for (slot = 0; slot < AMIGA_PROFILES; slot++) {
        if (!settings.profile[slot].name[0]) {
            if (join_new_slot < 0) join_new_slot = slot;
            continue;
        }
        if (first_available < 0 && slot != signed_profile[join_player ^ 1])
            first_available = join_count;
        if (slot == preferred) selected = join_count;
        join_rows[join_count++] = slot;
    }
    if (join_new_slot >= 0) {
        if (first_available < 0) first_available = join_count;
        join_rows[join_count++] = JOIN_NEW;
    }
    if (signed_profile[join_player] >= 0) join_rows[join_count++] = JOIN_LEAVE;
    menu_row = selected >= 0 ? selected : first_available >= 0 ? first_available : 0;
}

void open_signin(int player, int return_page)
{
    int preferred = signed_profile[player] >= 0 ? signed_profile[player] :
                    player == 0 ? settings.current : SIGNED_OUT;
    join_player = player;
    join_return = return_page;
    join_return_row = return_page == menu_page ? menu_row : 0;
    show_signin(preferred == signed_profile[player ^ 1] ? SIGNED_OUT : preferred);
}

static void resume_team_setup(void)
{
    if (signed_profile[0] == SIGNED_OUT && signed_profile[1] >= 0) {
        signed_profile[0] = settings.current = signed_profile[1];
        signed_profile[1] = SIGNED_OUT;
        host_side ^= 1;
        team_tank[0] = team_tank[1];
        team_ready[0] = team_ready[1] = 0;
        join_return_row = 0;
        apply_menu_settings();
        save_menu_settings();
    }
    /* Keep the other player's side, tank and ready state. */
    menu_page = MENU_TEAMS;
    menu_row = join_return_row;
    settings.players = signed_profile[1] == SIGNED_OUT ? 1 : 2;
    ModifyIDCMP(window, RAW_EVENTS);
    menu_edit = 0;
    online_status[0] = 0;
    online_dirty = 1;
}

static int both_signed_out(void)
{
    return signed_profile[0] == SIGNED_OUT && signed_profile[1] == SIGNED_OUT;
}

static void sign_out(int player)
{
    signed_profile[player] = SIGNED_OUT;
    team_ready[player] = 0;
    apply_menu_settings();
    team_tank[player] = profile_tank(player);
    if (player == 1 && local_playing && !training_on && !operation.phase) {
        solo = 1;
        reset_match_view();
    }
    online_dirty = 1;
}

static void return_to_title(void)
{
    if (local_playing) rec_end();
    local_playing = training_on = paused = 0;
    if (online.socket_open) online_disconnect("Signed out.");
    menu_change(MENU_MAIN);
    online_enter = 0;
    title_start();
}

void close_signin(void)
{
    if (both_signed_out()) return_to_title();
    else if (join_return == MENU_TEAMS) resume_team_setup();
    else {
        menu_change(join_return);
        if (join_return_row < menu_rows()) menu_row = join_return_row;
    }
}

int profile_menu_active(void)
{
    return menu_page == MENU_SIGNIN || menu_page == MENU_PROFILE_EDIT || menu_page == MENU_PROFILE_DELETE;
}

void profile_back(void)
{
    if (cloud_form) {
        menu_edit=cloud_form=0;menu_row=EDIT_ACCOUNT;memset(cloud_password,0,sizeof cloud_password);memset(edit_text,0,sizeof edit_text);
        ModifyIDCMP(window, RAW_EVENTS);online_dirty=1;return;
    }
    if (menu_edit) {
        menu_edit = 0;
        online_status[0] = 0;
        ModifyIDCMP(window, RAW_EVENTS);
    } else if (menu_page == MENU_SIGNIN) close_signin();
    else submenu_back();
    online_dirty = 1;
}

void main_profile_action(int player)
{
    if (signed_profile[player] == SIGNED_OUT) open_signin(player, MENU_MAIN);
    else {
        sign_out(player);
        if (both_signed_out()) return_to_title();
    }
}

static void finish_signin(int slot)
{
    int joining_second = join_player == 1 && signed_profile[1] == SIGNED_OUT && slot >= 0;
    if (slot == SIGNED_OUT) sign_out(join_player);
    else {
        signed_profile[join_player] = slot;
        if (join_player == 0 && settings.current != slot) {
            settings.current = slot;
            apply_menu_settings();
            save_menu_settings();
        } else apply_menu_settings();
        team_tank[join_player] = profile_tank(join_player);
        team_ready[join_player] = joining_second && join_return == MENU_TEAMS;
    }
    close_signin();
    if (joining_second) local_join_second();
}

/* The list omits empty slots, but saved addresses retain their config keys. */
static int private_server_slot(int row)
{
    int i;
    for (i = 0; i < AMIGA_SERVERS; i++)
        if (settings.server[i][0] && row-- == 0) return i;
    return -1;
}

static int private_server_count(void)
{
    int i, count = 0;
    for (i = 0; i < AMIGA_SERVERS; i++) count += settings.server[i][0] != 0;
    return count;
}

static void server_tab(void)
{
    public_tab = !public_tab;
    menu_row = 0;
    server_action = SERVER_JOIN;
    online_status[0] = 0;
    online_refresh = public_tab && !public_checked && !public_list.pending;
    online_dirty = 1;
}

void settings_back(void)
{
    int page = menu_page;
    menu_change(MENU_SETTINGS);
    menu_row = page == MENU_GRAPHICS ? SET_GRAPHICS : page == MENU_AUDIO ? SET_AUDIO :
               page == MENU_CONTROLS ? SET_CONTROLS : SET_GAME;
}

int menu_rows(void)
{
    switch (menu_page) {
    case MENU_MAIN: return MAIN_ROWS;
    case MENU_SIGNIN: return join_count;
    case MENU_PLAY: return sizeof play_items / sizeof play_items[0];
    case MENU_CHALLENGES: return 4;
    case MENU_TOOLS: return sizeof tool_items / sizeof tool_items[0];
    case MENU_SETTINGS: return sizeof setting_items / sizeof setting_items[0];
    case MENU_PROFILE_EDIT: return cloud_form ? 6 : EDIT_ROWS;
    case MENU_PROFILE_DELETE: return 2;
    case MENU_DIAGNOSTICS: return 2;
    case MENU_CONTROLS: return CONTROL_ROWS;
    case MENU_KEYMAP: return KEYS_ROWS;
    case MENU_OPTIONS: return 6;
    case MENU_GRAPHICS: return GRAPH_ROWS;
    case MENU_AUDIO: return 3;
    case MENU_LOCAL: return LG_ROWS;
    case MENU_TEAMS: return TM_ROWS;
    case MENU_NETWORK: return public_tab ? public_list.count + 1 : private_server_count() + 3;
    case MENU_REPLAYS: return nreplays + 1;
    case MENU_ABOUT: return 1;
    default: return 0;
    }
}

static void menu_move(int delta)
{
    int lobby = online.socket_open && !profile_menu_active();
    int before = menu_row;
    int rows = lobby ? (online.state == NC_ENDED ? 2 : 1) : menu_rows();
    if (menu_page == MENU_NETWORK && server_form == SERVER_FORM_DELETE) {
        server_confirm ^= 1;
        sound_play(SND_HIT, 9);
        online_dirty = 1;
        return;
    }
    if (!lobby && (menu_page == MENU_CONTROLS || menu_page == MENU_KEYMAP ||
                                (menu_page == MENU_PROFILE_EDIT && !cloud_form))) {
        /* The two buttons share one row. Enter that row on the left. */
        rows--;
        if (menu_row == rows) menu_row--;
    }
    if (!rows) return;
    menu_row = (menu_row + rows + delta) % rows;
    if (menu_row != before) sound_play(SND_HIT, 9);
    if (menu_page == MENU_SIGNIN) {
        profile_action = PROFILE_USE;
        online_status[0] = 0;
    }
    if (menu_page == MENU_NETWORK) server_action = SERVER_JOIN;
    online_dirty = 1;
}

/* left/right (or a joystick's): a row's value, as the desktop's menus */
void menu_side_step(int dir)
{
    if (online.socket_open && !profile_menu_active()) {   /* lobby and rematch controls */
        if (online.state == NC_LOBBY) netc_lobby_team_step(&online, dir);
        else if (online.state == NC_ENDED) menu_row = !menu_row;
    } else if (menu_page == MENU_MAIN && menu_row >= MAIN_P1) {
        menu_row = menu_row == MAIN_P1 ? MAIN_P2 : MAIN_P1;
    } else if (menu_page == MENU_LOCAL) {
        switch (menu_row) {
        case LG_MODE: {
            unsigned modes = local_world_modes(settings.world);
            do settings.mode = (settings.mode + LOCAL_MODES + dir) % LOCAL_MODES;
            while (!(modes & (1u << settings.mode)));
            break;
        }
        case LG_WORLD:
            settings.world = biome_step((Biome)settings.world, dir);
            break;
        case LG_SIZE: {
            int index = settings.map_size == SIZE_ANY ? 0 : settings.map_size + 1;
            if (settings.world == BIOME_CLASSIC)
                settings.map_size = settings.map_size == SIZE_ANY ? SIZE_LARGE : SIZE_ANY;
            else {
                index = (index + 4 + dir) % 4;
                settings.map_size = index ? index - 1 : SIZE_ANY;
            }
            break;
        }
        case LG_MAP: {
            int named_only = settings.world == BIOME_CLASSIC &&
                             first_named_map(settings.world, settings.mode, settings.map_size) >= 0;
            do settings.map = (settings.map + local_map_count + dir) % local_map_count;
            while ((named_only && random_map(settings.map)) ||
                   !map_fits_filters(settings.map, settings.world, settings.mode, settings.map_size));
            break;
        }
        default: return;
        }
        apply_menu_settings();
        save_menu_settings();
    } else if (menu_page == MENU_TEAMS) {
        switch (menu_row) {
        case TM_PLAYERS:
            open_signin(1, MENU_TEAMS);
            break;
        case TM_ENEMY:
            if (settings.players == 2) return;
            settings.level = (settings.level + 3 + dir) % 3;
            break;
        default: return;
        }
        apply_menu_settings();
        save_menu_settings();
    } else if (menu_page == MENU_SIGNIN) {
        int action = profile_action + dir;
        if (join_count && join_rows[menu_row] < AMIGA_PROFILES && action >= PROFILE_USE && action < PROFILE_ACTIONS) {
            profile_action = action;
            online_status[0] = 0;
        }
    } else if (menu_page == MENU_PROFILE_EDIT) {
        if (cloud_form) {if(menu_row==0) cloud_create=!cloud_create;online_dirty=1;return;}
        if (menu_row == EDIT_ACCOUNT) { profile_accept(); return; }
        if (menu_row == EDIT_FACE) profile_draft.face = (profile_draft.face + FACES + dir) % FACES;
        if (menu_row == EDIT_TANK) {
            int classic = local_classic(), count = TANK_CLASS_COUNT + !classic + 1;
            int tank = classic ? tank_chassis(profile_draft.tank) : profile_draft.tank;
            profile_draft.tank = (tank + 1 + count + dir) % count - 1;
        }
        if (menu_row == EDIT_STEER) profile_draft.steer = profile_draft.steer > 0 ? 0 : 1;
        if (menu_row >= EDIT_SAVE) menu_row = dir > 0 ? EDIT_BACK : EDIT_SAVE;
    } else if (menu_page == MENU_PROFILE_DELETE) {
        menu_move(dir);
    } else if (menu_page == MENU_CONTROLS) {
        if (menu_row >= CONTROLS_BACK) {
            menu_row = dir > 0 ? CONTROLS_RESET : CONTROLS_BACK;
            online_dirty = 1;
            return;
        } else if (menu_row == P1_INPUT || menu_row == P2_INPUT) {
            int player = menu_row == P2_INPUT;
            settings.controls[player].input = input_mode[player] = (input_mode[player] + AMIGA_INPUT_MODES + dir) % AMIGA_INPUT_MODES;
        } else if (menu_row == P1_JOY || menu_row == P2_JOY) {
            settings.swap_joysticks ^= 1;
            menu_joy = player_joystick(0);
            signin_joy = player_joystick(1);
        } else return;
        save_menu_settings();
    } else if (menu_page == MENU_KEYMAP && menu_row >= KEYS_SAVE) {
        menu_row = dir > 0 ? KEYS_BACK : KEYS_SAVE;
    } else if (menu_page >= MENU_OPTIONS && menu_page <= MENU_AUDIO) {
        if (menu_row == menu_rows() - 1) return;
        if (menu_page == MENU_OPTIONS) {
            if (menu_row == 0) settings.radar_off ^= 1;
            if (menu_row == 1) { settings.sides ^= 1; palette_shown = PALETTE_DIRTY; }
            if (menu_row == 4) settings.powerups ^= 1;
            if (menu_row == 2) settings.match_lives = settings.match_lives == 255 ? (dir > 0 ? 1 : 99) :
                settings.match_lives + dir < 1 || settings.match_lives + dir > 99 ? 255 : settings.match_lives + dir;
            if (menu_row == 3) settings.match_minutes = (settings.match_minutes - 1 + dir + 30) % 30 + 1;
        } else if (menu_page == MENU_GRAPHICS) {
            switch (menu_row) {
            case GRAPH_MARKS: settings.marks ^= 1; break;
            case GRAPH_AUTO: settings.auto_effects ^= 1; break;
            case GRAPH_WEATHER: settings.weather_off ^= 1; break;
            case GRAPH_EXPLOSIONS:
                settings.explosions = (settings.explosions + EXPLOSION_STYLES + dir) % EXPLOSION_STYLES;
                break;
            case GRAPH_FIRES: settings.ground_fires_off ^= 1; break;
            case GRAPH_SURFACE: settings.surface_fx_off ^= 1; break;
            }
        } else {
            int *level = menu_row == 0 ? &settings.music : &settings.effects;
            int next = volume_step(*level, dir);
            if (*level == next) return;
            *level = next;
        }
        apply_menu_settings();
        save_menu_settings();
    } else if (menu_page == MENU_NETWORK && !public_tab) {
        if (server_form == SERVER_FORM_DELETE) server_confirm = dir > 0;
        else if (private_server_slot(menu_row) >= 0) {
            int action = server_action + dir;
            if (action >= SERVER_JOIN && action <= SERVER_DELETE) server_action = action;
        }
    }
    online_dirty = 1;
}

/* back to the match after the pause page: the whole view again (the page covered it) */
void unpause(void)
{
    paused = 0;
    palette_shown = PALETTE_DIRTY;
    reset_match_view();
    online_dirty = 1;
}

/* a match's result, in the current profile's record (once) */
void record_result(int won)
{
    if (recorded) return;
    recorded = 1;
    if (signed_profile[0] >= 0) {
        AmigaProfile *profile = &settings.profile[signed_profile[0]];
        profile->matches++;
        if (won) profile->wins++;
    }
    if (local_playing && !solo && signed_profile[1] >= 0) {
        AmigaProfile *profile = &settings.profile[signed_profile[1]];
        profile->matches++;
        if (game.winner == (game.tanks[1].team & 1)) profile->wins++;
    }
    if (signed_profile[0] >= 0 || (local_playing && !solo && signed_profile[1] >= 0)) save_menu_settings();
}

/* a profile's face (faces_art.h), 24 x 24, into the frame at x (a multiple of 8), y */
static void draw_face(int x, int y, int face)
{
    int plane, row, b, stride = frame->BytesPerRow;
    WaitBlit();
    face = (face % FACES + FACES) % FACES;
    for (plane = 0; plane < DEPTH; plane++)
        for (row = 0; row < 24; row++) {
            UBYTE *px = frame->Planes[plane] + (y + row) * stride + x / 8;
            for (b = 0; b < 3; b++) px[b] = plane < 4 ? faces_art[face][plane][row][b] : 0;   /* (faces: the first 16 pens) */
        }
}

void submenu_back(void)
{
    if (menu_page == MENU_CONTROLS) settings_back();
    else if (menu_page == MENU_KEYMAP) {
        menu_change(MENU_CONTROLS);
        menu_row = remap_player ? P2_KEYS : P1_KEYS;
    } else show_signin(profile_slot);
}

static void save_keymap(void)
{
    AmigaSeatControls *controls = &settings.controls[remap_player];
    AmigaSeatControls previous = *controls;
    memcpy(controls->keys, remap_keys, sizeof remap_keys);
    controls->has_keys = 1;
    if (amiga_settings_save(&settings)) {
        *controls = previous;
        strcpy(online_status, "COULD NOT SAVE KEYMAP");
    } else submenu_back();
}

void controls_accept(void)
{
    if (remap_capture) return;
    if (menu_page == MENU_KEYMAP) {
        if (menu_row == KEYS_SAVE) save_keymap();
        else if (menu_row == KEYS_BACK) submenu_back();
        else {
            remap_capture = 1;
            online_status[0] = 0;
        }
    } else if (menu_row == CONTROLS_BACK) submenu_back();
    else if (menu_row == CONTROLS_RESET) {
        int player;
        for (player = 0; player < 2; player++) {
            AmigaSeatControls *controls = &settings.controls[player];
            controls->input = input_mode[player] = AMIGA_INPUT_BOTH;
            controls->has_keys = 1;
            memcpy(controls->keys, default_keys[player], sizeof controls->keys);
        }
        settings.swap_joysticks = 0;
        menu_joy = player_joystick(0);
        signin_joy = player_joystick(1);
        save_menu_settings();
    } else if (menu_row == P1_KEYS || menu_row == P2_KEYS) {
        remap_player = menu_row == P2_KEYS;
        memcpy(remap_keys, player_keys(remap_player), sizeof remap_keys);
        menu_change(MENU_KEYMAP);
    } else menu_side_step(1);
    online_dirty = 1;
}

static int valid_profile_name(const char *name, int slot)
{
    int i;
    if (!name[0] || strspn(name, " ") == strlen(name)) {
        strcpy(online_status, "ENTER A NAME");
        return 0;
    }
    for (i = 0; i < AMIGA_PROFILES; i++)
        if (i != slot && !strcmp(name, settings.profile[i].name)) {
            strcpy(online_status, "THAT NAME IS ALREADY IN USE");
            return 0;
        }
    return 1;
}

static int commit_profile(const AmigaProfile *next, int current)
{
    AmigaProfile previous = settings.profile[profile_slot];
    int previous_current = settings.current;
    settings.profile[profile_slot] = *next;
    settings.current = current;
    if (!amiga_settings_save(&settings)) {
        if (previous.session[0] && memcmp(previous.key,next->key,32)) cloud_forget(previous.key);
        if (next->session[0]) cloud_register(next->key,next->session);
        return 1;
    }
    settings.profile[profile_slot] = previous;
    settings.current = previous_current;
    strcpy(online_status, "COULD NOT SAVE PROFILE");
    return 0;
}

void profile_accept(void)
{
    if (cloud_form) {
        if(menu_edit) {snprintf(cloud_text_field(),cloud_text_cap(),"%.*s",(int)cloud_text_cap()-1,edit_text);memset(edit_text,0,sizeof edit_text);menu_edit=0;ModifyIDCMP(window,RAW_EVENTS);}
        else if(menu_row==0) cloud_create=!cloud_create;
        else if(menu_row==5) {profile_back();return;}
        else if(menu_row==4) {
            CloudAccount account;char error[96];
            strcpy(online_status,"CONNECTING TO CLOUD ACCOUNT...");online_dirty=1;
            if(!cloud_login(&account,cloud_user,cloud_email,cloud_password,cloud_create,error)) {
                memcpy(profile_draft.key,account.key,32);profile_draft.has_key=1;
                strcpy(profile_draft.session,account.session);strcpy(profile_draft.name,account.name);
                cloud_form=0;menu_row=EDIT_FACE;strcpy(online_status,"CLOUD ACCOUNT CONNECTED");
            } else snprintf(online_status,sizeof online_status,"%s",error);
            memset(cloud_password,0,sizeof cloud_password);
        } else if(menu_row!=2||cloud_create) {
            snprintf(edit_text,sizeof edit_text,"%s",cloud_text_field());menu_edit=1;
            ModifyIDCMP(window,RAW_EVENTS|IDCMP_VANILLAKEY);
        }
        online_dirty=1;return;
    }
    if(menu_page==MENU_PROFILE_EDIT&&!menu_edit&&menu_row==EDIT_ACCOUNT) {
        cloud_form=1;cloud_create=0;menu_row=0;online_status[0]=0;
        snprintf(cloud_user,sizeof cloud_user,"%s",profile_draft.session[0]?profile_draft.name:"");
        cloud_email[0]=cloud_password[0]=0;online_dirty=1;return;
    }
    if (menu_page == MENU_SIGNIN) {
        int slot;
        if (!join_count) return;
        slot = join_rows[menu_row];
        if (slot == JOIN_LEAVE) finish_signin(SIGNED_OUT);
        else if (slot == JOIN_NEW || profile_action == PROFILE_EDIT) {
            profile_slot = slot == JOIN_NEW ? join_new_slot : slot;
            profile_draft = settings.profile[profile_slot];
            menu_change(MENU_PROFILE_EDIT);
        } else if (profile_action == PROFILE_DELETE) {
            profile_slot = slot;
            menu_change(MENU_PROFILE_DELETE);
        } else if (slot == signed_profile[join_player ^ 1])
            strcpy(online_status, "PROFILE ALREADY SIGNED IN");
        else finish_signin(slot);
    } else if (menu_page == MENU_PROFILE_DELETE) {
        if (menu_row == 1) {
            AmigaProfile empty;
            int player, slot, current = settings.current;
            amiga_profile_reset(&empty, profile_slot);
            if (current == profile_slot)
                for (slot = 0; slot < AMIGA_PROFILES; slot++)
                    if (slot != profile_slot && settings.profile[slot].name[0] && slot != signed_profile[1]) {
                        current = slot;
                        break;
                    }
            if (!commit_profile(&empty, current)) return;
            for (player = 0; player < 2; player++) {
                if (signed_profile[player] == profile_slot) {
                    sign_out(player);
                    if (both_signed_out()) {
                        return_to_title();
                        return;
                    }
                }
            }
            apply_menu_settings();
        }
        submenu_back();
    } else if (menu_edit) {
        if (!valid_profile_name(edit_text, profile_slot)) return;
        strcpy(profile_draft.name, edit_text);
        menu_edit = 0;
        online_status[0] = 0;
        ModifyIDCMP(window, RAW_EVENTS);
    } else if (menu_row == EDIT_NAME && !profile_draft.session[0]) {
        strcpy(edit_text, profile_draft.name);
        menu_edit = 1;
        online_status[0] = 0;
        ModifyIDCMP(window, RAW_EVENTS | IDCMP_VANILLAKEY);
    } else if (menu_row == EDIT_SAVE) {
        int creating = !settings.profile[profile_slot].name[0];
        int current = creating && join_player == 0 ? profile_slot : settings.current;
        if (!valid_profile_name(profile_draft.name, profile_slot)) return;
        if (!commit_profile(&profile_draft, current)) return;
        if (creating) finish_signin(profile_slot);
        else {
            apply_menu_settings();
            submenu_back();
        }
    } else if (menu_row == EDIT_BACK) submenu_back();
    else menu_side_step(1);
    online_dirty = 1;
}

static void remap_key(UWORD code)
{
    int action = menu_row - KEYS_FIRST;
    uint8_t previous = remap_keys[action];
    online_status[0] = 0;
    if (code == 0x45) remap_capture = 0;
    else if (code == 0x51) strcpy(online_status, "F2 IS RESERVED");
    else if (code < AMIGA_KEY_BIND_LIMIT) {
        uint8_t *occupied = memchr(remap_keys, code, sizeof remap_keys);
        remap_keys[action] = (uint8_t)code;
        if (occupied && occupied != remap_keys + action) *occupied = previous;
        if (keymaps_overlap(remap_keys, player_keys(remap_player ^ 1))) {
            if (occupied) *occupied = (uint8_t)code;
            remap_keys[action] = previous;
            strcpy(online_status, "KEY IN USE BY OTHER PLAYER");
        } else {
            remap_capture = 0;
            menu_row++;
        }
    }
    online_dirty = 1;
}

static void menu_text_key(UWORD code)
{
    size_t n = strlen(edit_text);
    size_t limit = cloud_form ? cloud_text_cap() : editing_team >= 0 ? sizeof team_names[0] : menu_page == MENU_PROFILE_EDIT ? NET_NAME : AMIGA_ADDRESS;
    if ((code == 8 || code == 127) && n) edit_text[n - 1] = 0;
    else if (code >= 32 && code <= 126 && n + 1 < limit) {
        edit_text[n] = (char)code;
        edit_text[n + 1] = 0;
    }
}

static int address_editing(void)
{
    return menu_edit && menu_page == MENU_NETWORK;
}

static void address_key(UWORD code, UWORD qualifier)
{
    char ch[8];
    struct InputEvent event;
    if (code == 0x44 || code == 0x43) { online_enter = 1; return; }
    if (code == 0x45) { online_escape = 1; return; }
    if (code == 0x41 || code == 0x46) menu_text_key(8);
    else if (code >= 1 && code <= 10) menu_text_key("1234567890"[code - 1]);
    else if (KeymapBase) {
        memset(&event, 0, sizeof event);
        event.ie_Class = IECLASS_RAWKEY;
        event.ie_Code = code;
        event.ie_Qualifier = qualifier;
        if (MapRawKey(&event, ch, sizeof ch, NULL) == 1 &&
            ((ch[0] >= 'a' && ch[0] <= 'z') || (ch[0] >= 'A' && ch[0] <= 'Z') ||
             (ch[0] >= '0' && ch[0] <= '9') || ch[0] == '.' || ch[0] == '-' || ch[0] == ':'))
            menu_text_key(ch[0]);
    }
    online_dirty = 1;
}

static int lobby_action_on(int action)
{
    LobbyRoster roster;
    switch (action) {
    case LA_CAMP: return netc_lobby_can_rename_side(&online, SIDE_CAMP);
    case LA_FORTRESS: return netc_lobby_can_rename_side(&online, SIDE_FORTRESS);
    case LA_READY: return !online.eliminated && online.server_state != SRV_LOADING && online.server_state != SRV_COUNTDOWN;
    case LA_WATCH: return online.server_state == SRV_RUNNING;
    case LA_PAGE:
        netc_lobby_roster(&online, &roster);
        return roster.pages > 1;
    case LA_STATUS: case LA_LEAVE: case LA_CHAT: case LA_KEYBOARD: return 1;
    }
    return 0;
}

static void lobby_step_action(int step)
{
    do lobby_action = (lobby_action + LA_COUNT + step) % LA_COUNT;
    while (lobby_action && !lobby_action_on(lobby_action));
    online_dirty = 1;
}


static void show_menu_palette(void);
static void console_panel(int left, int top, int right, int bottom, int face);
static void network_diagnostics_window(void)
{
    InputBits previous = player_input(0);
    int done = 0, saved = 0;
    while (!done) {
        struct IntuiMessage *msg;
        InputBits input, edge;
        char report[1600], *line, *next;
        int y = 42;
        netc_step(&online);
        if (online.state == NC_RUNNING) break;
        while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort))) {
            ULONG kind = msg->Class; UWORD code = msg->Code;
            if (kind == IDCMP_RAWKEY) {
                if (code & 0x80) keys[code & 0x7f] = 0;
                else { keys[code] = 1; if (code == 0x45) done = 1; }
            } else if (kind == IDCMP_CLOSEWINDOW) done = 1;
            else if (kind == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) {
                if (msg->MouseY > 210 && msg->MouseX > WIDTH / 2) done = 1;
                else if (msg->MouseY > 210) saved = netc_save_diagnostics(&online, "PROGDIR:network-report.txt") == 0 ? 1 : -1;
            }
            ReplyMsg((struct Message *)msg);
        }
        input = player_input(0); edge = input & ~previous; previous = input;
        if (edge & IN_MINE) done = 1;
        if (edge & IN_FIRE) saved = netc_save_diagnostics(&online, "PROGDIR:network-report.txt") == 0 ? 1 : -1;
        show_menu_palette(); fill(INK, 0, 0, WIDTH - 1, HEIGHT - 1);
        console_panel(8, 14, 311, 242, PANEL); label(18, 30, GOLD, "NETWORK STATUS");
        netc_diagnostics(&online, report, sizeof report);
        for (line = report; *line && y < 210; line = next, y += 11) {
            next = strchr(line, '\n'); if (next) *next++ = 0; else next = line + strlen(line);
            line[strlen(line) > 35 ? 35 : strlen(line)] = 0;
            label(18, y, WHITE, line);
        }
        label(18, 225, GOLD, saved > 0 ? "REPORT SAVED" : saved < 0 ? "SAVE FAILED" : "FIRE SAVE REPORT");
        label(206, 225, WHITE, "MINE BACK");
        wait_for_display(); presentation.BitMap = display_buffers[back_buffer]->sb_BitMap;
        BltBitMapRastPort(frame, 0, 0, &presentation, 0, 0, WIDTH, HEIGHT, 0xc0); present_frame();
        music_update(); WaitTOF();
    }
    online_dirty = 1;
}

static void lobby_accept_action(int action)
{
    LobbyRoster roster;
    if (!lobby_action_on(action)) return;
    switch (action) {
    case LA_CAMP: case LA_FORTRESS:
        lobby_keyboard = action == LA_CAMP ? SIDE_CAMP : SIDE_FORTRESS;
        lobby_key = 0;
        lobby_name[0] = 0;
        break;
    case LA_READY:
        if (netc_lobby_can_start(&online)) netc_chat(&online, "/start");
        else if (online.server_state == SRV_RUNNING && online.places_free > 0) netc_join(&online, 1);
        else netc_ready(&online, !online.ready, online.want_cls);
        break;
    case LA_WATCH: netc_watch(&online, 1); break;
    case LA_PAGE:
        netc_lobby_roster(&online, &roster);
        lobby_page = (lobby_page + 1) % roster.pages;
        break;
    case LA_STATUS: network_diagnostics_window(); break;
    case LA_LEAVE: online_disconnect("Disconnected."); break;
    case LA_CHAT: online_chat_begin(); break;
    case LA_KEYBOARD:
        lobby_action = LA_KEYBOARD;
        lobby_keyboard = LK_CHAT;
        lobby_key = 0;
        break;
    }
    online_dirty = 1;
}

void online_clear_lobby_focus(void)
{
    lobby_keyboard = lobby_action = 0;
}

static void lobby_type(char *line, size_t limit, char ch)
{
    size_t n = strlen(line);
    if (ch == 8) { if (n) line[n - 1] = 0; }
    else if (ch >= 32 && ch < 127 && n + 1 < limit) {
        line[n] = ch;
        line[n + 1] = 0;
    }
    online_dirty = 1;
}

static void chat_finish(int send)
{
    if (send && chat_line[0]) netc_chat(&online, chat_line);
    if (send || online.state != NC_LOBBY) chat_line[0] = 0;
    chat_edit = 0;
    online_dirty = 1;
}

static void lobby_keyboard_type(char ch)
{
    if (lobby_keyboard == LK_CHAT) lobby_type(chat_line, sizeof chat_line, ch);
    else lobby_type(lobby_name, sizeof lobby_name, ch);
}

static void lobby_keyboard_finish(int save)
{
    if (lobby_keyboard == LK_CHAT) chat_finish(save);
    else if (save) netc_lobby_rename(&online, lobby_keyboard, lobby_name);
    lobby_keyboard = 0;
    online_dirty = 1;
}

static void lobby_keyboard_pick(void)
{
    if (lobby_key == LK_SAVE || lobby_key == LK_CANCEL) lobby_keyboard_finish(lobby_key == LK_SAVE);
    else lobby_keyboard_type(lobby_key == LK_DELETE ? 8 : lobby_letters[lobby_key]);
}

static void lobby_keyboard_move(InputBits input)
{
    int footer = lobby_key >= LK_SAVE;
    int col = footer ? (lobby_key - LK_SAVE) * 4 + 2 : lobby_key % LK_COLS;
    int row = footer ? LK_ROWS : lobby_key / LK_COLS;
    if (input & (IN_UP | IN_DOWN)) {
        row = (row + (input & IN_UP ? LK_ROWS : 1)) % (LK_ROWS + 1);
        lobby_key = row < LK_ROWS ? row * LK_COLS + col : LK_SAVE + (col * LK_W + LK_W / 2) / LK_BUTTON;
    } else if (input & (IN_LEFT | IN_RIGHT)) {
        int dir = input & IN_LEFT ? -1 : 1;
        if (footer) lobby_key = LK_SAVE + (lobby_key - LK_SAVE + 3 + dir) % 3;
        else lobby_key = row * LK_COLS + (col + LK_COLS + dir) % LK_COLS;
    }
    if (input & IN_MINE) lobby_keyboard_type(8);
    if (input & IN_FIRE) lobby_keyboard_pick();
    online_dirty = 1;
}

void online_chat_begin(void)
{
    chat_edit = 1;
    if (online.state == NC_LOBBY) lobby_action = LA_CHAT;
    else chat_line[0] = 0;
    memset(keys, 0, sizeof keys);
    online_dirty = 1;
}

static void chat_key(UWORD code, UWORD qualifier, int repeat)
{
    if (code == 0x44 || code == 0x43 || code == 0x45) {
        if (!repeat) chat_finish(code != 0x45);
    } else if (code == 0x41 || code == 0x46) {
        lobby_type(chat_line, sizeof chat_line, 8);
    } else if (code == 0x42) {
        if (!repeat) netc_chat_complete(&online, chat_line, sizeof chat_line);
    } else if (KeymapBase) {
        struct InputEvent event = {0};
        char ch[8];
        event.ie_Class = IECLASS_RAWKEY;
        event.ie_Code = code;
        event.ie_Qualifier = qualifier;
        if (MapRawKey(&event, ch, sizeof ch, NULL) == 1) lobby_type(chat_line, sizeof chat_line, ch[0]);
    }
    online_dirty = 1;
}

/* A ready host owns the centre controls. The other player still owns their card. */
static void team_command(int player, InputBits input)
{
    if (!input) return;
    if (player == 0 && (input & IN_MINE)) {
        online_escape = 1;
        return;
    }
    if (player == 0 && team_ready[0]) {
        if (input & IN_UP) menu_move(-1);
        else if (input & IN_DOWN) menu_move(1);
        else if (input & IN_LEFT) menu_side_step(-1);
        else if (input & IN_RIGHT) menu_side_step(1);
        else if (input & IN_FIRE) online_enter = 1;
    } else if (input & IN_FIRE) {
        sound_play(SND_PICKUP, 20);
        team_ready[player] = !team_ready[player];
        if (player == 0) menu_row = TM_START;
    } else if (!team_ready[player]) {
        if (input & (IN_LEFT | IN_RIGHT)) {
            host_side ^= 1;
            team_ready[0] = team_ready[1] = 0;
            online_enter = 0;
        } else if (input & (IN_UP | IN_DOWN)) {
            int dir = input & IN_UP ? -1 : 1;
            int choices = TANK_CLASS_COUNT + !local_classic();
            team_tank[player] = (team_tank[player] + choices + dir) % choices;
        }
    }
    online_dirty = 1;
}

static InputBits menu_key(UWORD code)
{
    InputBits mapped = player_key_action(profile_menu_active() ? join_player : 0, code);
    if (!mapped && profile_menu_active() && join_player == 1) mapped = player_key_action(0, code);
    if (code == 0x45) return IN_MINE;
    if (mapped) return mapped;
    switch (code) {
    case 0x44: case 0x43: return IN_FIRE;    /* Return and keypad Enter */
    case 0x4c: return IN_UP;
    case 0x4d: return IN_DOWN;
    case 0x4f: return IN_LEFT;
    case 0x4e: return IN_RIGHT;
    default: return 0;
    }
}

static void lobby_keyboard_raw_key(UWORD code, UWORD qualifier, int repeat)
{
    InputBits input = menu_key(code);
    struct InputEvent event = {0};
    char ch[8];
    if (code == 0x44 || code == 0x43 || code == 0x45) {
        if (!repeat) lobby_keyboard_finish(code != 0x45);
    } else if (code == 0x41 || code == 0x46) lobby_keyboard_type(8);
    else if (code == 0x42 && lobby_keyboard == LK_CHAT) {
        if (!repeat) netc_chat_complete(&online, chat_line, sizeof chat_line);
        online_dirty = 1;
    } else if (input && (!(qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ||
                         (code >= 0x4c && code <= 0x4f))) {
        if (!repeat || !(input & IN_FIRE)) lobby_keyboard_move(input);
    } else if (KeymapBase) {
        event.ie_Class = IECLASS_RAWKEY;
        event.ie_Code = code;
        event.ie_Qualifier = qualifier;
        if (MapRawKey(&event, ch, sizeof ch, NULL) == 1) lobby_keyboard_type(ch[0]);
    }
}

static void menu_command(InputBits input, int repeat, int joystick)
{
    if (!input) return;
    if (input & IN_MINE) {
        if (!repeat) online_escape = 1;
        return;
    }
    if (title_on) {
        if ((input & IN_FIRE) && !repeat) online_enter = 1;
    } else if (online.socket_open && online.state == NC_LOBBY && !paused && !profile_menu_active()) {
        if (repeat) return;
        if (lobby_action >= LA_CHAT && (input & IN_UP)) lobby_action = LA_NONE;
        else if (lobby_action > LA_NONE && lobby_action < LA_CHAT && (input & IN_DOWN))
            lobby_action = LA_CHAT;
        else if (!lobby_action && (input & (IN_UP | IN_DOWN)))
            netc_lobby_tank_step(&online, input & IN_UP ? -1 : 1);
        else if (input & (IN_LEFT | IN_RIGHT)) {
            int step = input & IN_LEFT ? -1 : 1;
            if (lobby_action) lobby_step_action(step);
            else netc_lobby_team_step(&online, step);
        }
        if (input & IN_FIRE) {
            if (lobby_action) lobby_accept_action(joystick && lobby_action == LA_CHAT ? LA_KEYBOARD : lobby_action);
            else online_enter = 1;
        }
        online_dirty = 1;
    } else if (replay_on) {
        int n = game.num_tanks ? game.num_tanks : 1;
        if (input & IN_LEFT) replay_follow = (replay_follow + n - 1) % n;
        if (input & IN_RIGHT) replay_follow = (replay_follow + 1) % n;
        if ((input & IN_UP) && watch_speed < 8) watch_speed *= 2;
        if ((input & IN_DOWN) && watch_speed > 1) watch_speed /= 2;
        if ((input & IN_FIRE) && !repeat) watch_paused = !watch_paused;
    } else {
        if (paused && !profile_menu_active()) {
            if (input & (IN_LEFT | IN_RIGHT)) { orders_seat_step(); pause_row = 0; online_dirty = 1; }
            if (input & (IN_UP | IN_DOWN)) {
                pause_row = (pause_row + (orders_available() ? 7 : 2) + ((input & IN_UP) ? -1 : 1)) % (orders_available() ? 7 : 2);
                online_dirty = 1;
            }
        } else {
            if (input & IN_UP) menu_move(-1);
            if (input & IN_DOWN) menu_move(1);
            if (input & IN_LEFT) menu_side_step(-1);
            if (input & IN_RIGHT) menu_side_step(1);
        }
        if ((input & IN_FIRE) && !repeat) online_enter = 1;
    }
}

static int local_team_roles(int player)
{
    if (player < 0 || signed_profile[player] == SIGNED_OUT) return 0;
    return (player == 0 ? ROLE_HOST : 0) |
           ((local_modes[mode_choice].extras & EXTRA_ANNIHILATION) ? 0 : ROLE_LEADER);
}

static void team_rename(int player)
{
    if (!(local_team_roles(player) & ROLE_LEADER)) return;
    editing_team = player == 0 ? host_side : host_side ^ 1;
    strcpy(edit_text, team_names[editing_team]);
    menu_edit = 1;
    online_dirty = 1;
    ModifyIDCMP(window, RAW_EVENTS | IDCMP_VANILLAKEY);
}

int team_finish_name(int save)
{
    size_t n = strlen(edit_text);
    if (editing_team < 0) return 0;
    if (save) {
        while (n && edit_text[n - 1] == ' ') edit_text[--n] = 0;
        snprintf(team_names[editing_team], sizeof team_names[0], "%.11s",
                 n ? edit_text : editing_team == TEAM_PLAYER ? "CAMP" : "FORTRESS");
    }
    editing_team = -1;
    menu_edit = 0;
    online_dirty = 1;
    ModifyIDCMP(window, RAW_EVENTS);
    return 1;
}

static void team_key(UWORD code)
{
    int player;
    if (code == 0x52 || code == 0x53) {
        team_rename(code == 0x53);
        return;
    }
    /* Return accepts for the host, as it does on the other menu pages. */
    if (code == 0x44 || code == 0x43) {
        if (signed_profile[0] == SIGNED_OUT) open_signin(0, MENU_TEAMS);
        else team_command(0, IN_FIRE);
        return;
    }
    if (code == 0x51) {
        if (signed_profile[1] == SIGNED_OUT) {
            open_signin(1, MENU_TEAMS);
            online_enter = 0;
        } else team_command(1, IN_FIRE);
        return;
    }
    for (player = settings.players == 1 ? 0 : 1; player >= 0 && player < 2;
         player += settings.players == 1 ? 1 : -1) {
        InputBits input = player_key_action(player, code);
        if (!input) continue;
        if (signed_profile[player] == SIGNED_OUT) {
            if (!(input & IN_FIRE)) continue;
            open_signin(player, MENU_TEAMS);
            online_enter = 0;
        } else team_command(player, input);
        return;
    }
    team_command(0, menu_key(code));
}

static int fire_to_signin(int player, InputBits input)
{
    if (!(input & IN_FIRE) || signed_profile[player] != SIGNED_OUT ||
        profile_menu_active() || replay_on || online.socket_open) return 0;
    open_signin(player, title_on ? MENU_MAIN : menu_page);
    title_on = 0;
    online_enter = 0;
    return 1;
}

void online_events(void)
{
    struct IntuiMessage *message;
    int playing = (online_playing || local_playing || editor_on) && !paused;   /* (the editor: keys held, as a match's) */
    while ((message = (struct IntuiMessage *)GetMsg(window->UserPort))) {
        ULONG type = message->Class;
        UWORD code = message->Code;
        UWORD qualifier = message->Qualifier;
        int mouse_x = message->MouseX, mouse_y = message->MouseY;
#ifndef OFP_ART_PREVIEW
        if (editor_on && type == IDCMP_EXTENDEDMOUSE && code == IMSGCODE_INTUIWHEELDATA && message->IAddress) {
            const struct IntuiWheelData *wheel = (const struct IntuiWheelData *)message->IAddress;
            editor_wheel(mouse_x, mouse_y, wheel->WheelY);
        }
#endif
        ReplyMsg((struct Message *)message);
        if (type == IDCMP_ACTIVEWINDOW || type == IDCMP_INACTIVEWINDOW) {
            active = type == IDCMP_ACTIVEWINDOW;
            memset(keys, 0, sizeof keys);
            if (!active && editor_on) editor_cancel_mouse();
        } else if (editor_on && (type == IDCMP_MOUSEBUTTONS || type == IDCMP_MOUSEMOVE)) {
            editor_mouse(type, code, mouse_x, mouse_y);
        } else if (type == IDCMP_MOUSEBUTTONS && code == SELECTDOWN && online.socket_open &&
                   online.state == NC_LOBBY && !paused) {
            if (lobby_keyboard) {
                int x = mouse_x - LK_X, y = mouse_y - LK_Y;
                if (x >= 0 && x < LK_COLS * LK_W && y >= 0 && y < LK_ROWS * LK_H) {
                    lobby_key = y / LK_H * LK_COLS + x / LK_W;
                    lobby_keyboard_pick();
                } else if (x >= 0 && x < 3 * LK_BUTTON && mouse_y >= LK_FOOT && mouse_y < LK_FOOT + LK_H) {
                    lobby_key = LK_SAVE + x / LK_BUTTON;
                    lobby_keyboard_pick();
                }
            } else {
                int action = LA_NONE;
                if (mouse_y >= 44 && mouse_y < 58) {
                    if (mouse_x >= 4 && mouse_x < 110) action = LA_CAMP;
                    else if (mouse_x >= 210 && mouse_x < 316) action = LA_FORTRESS;
                } else if (mouse_y >= 195 && mouse_y < 210 && mouse_x >= 6 && mouse_x < 310)
                    action = LA_READY + (mouse_x - 6) / 61;
                else if (mouse_y >= LOBBY_CHAT_Y && mouse_y < 250 &&
                         mouse_x >= LOBBY_CHAT_X && mouse_x <= LOBBY_CHAT_END)
                    action = mouse_x < LOBBY_KEYS_X ? LA_CHAT : LA_KEYBOARD;
                if (chat_edit && action != LA_CHAT) chat_finish(0);
                if (lobby_action_on(action)) {
                    lobby_action = action;
                    lobby_accept_action(action);
                }
            }
        } else if (type == IDCMP_RAWKEY) {
            int repeat = (qualifier & IEQUALIFIER_REPEAT) != 0;
            InputBits input;
            if (code & 0x80) {
                keys[code & 0x7f] = 0;
                continue;
            }
            if (lobby_keyboard) {
                lobby_keyboard_raw_key(code, qualifier, repeat);
                continue;
            }
            if (chat_edit) {
                chat_key(code, qualifier, repeat);
                continue;
            }
            if (!paused && online.socket_open && online.state == NC_LOBBY && !repeat) {
                if (code == 0x52) {
                    lobby_accept_action(online.side == SIDE_FORTRESS ? LA_FORTRESS : LA_CAMP);
                    continue;
                }
                if (code == 0x42) { lobby_step_action(1); continue; }
                if (code == 0x45 && lobby_action) { online_clear_lobby_focus(); online_dirty = 1; continue; }
                if (code == 0x51 && online.server_state == SRV_RUNNING) {
                    netc_watch(&online, 1);
                    continue;
                }
                if (code == 0x54 && online.server_state == SRV_RUNNING && online.places_free > 0) {
                    netc_join(&online, 1);
                    continue;
                }
                if (code == 0x57) {
                    LobbyRoster roster;
                    netc_lobby_roster(&online, &roster);
                    lobby_page = (lobby_page + 1) % roster.pages;
                    online_dirty = 1;
                    continue;
                }
                if (code == 0x56 && netc_lobby_can_start(&online)) {
                    netc_chat(&online, "/start");
                    continue;
                }
            }
            if (code == 0x14 && online.socket_open && !paused &&
                (online.state == NC_LOBBY || online_playing) &&
                (!player_key_action(0, code) || (qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)))) {
                if (!repeat) online_chat_begin();        /* T, or Shift+T when T is bound to Player 1 */
                continue;
            }
            if (playing) {
                if (online_playing && netc_spectating(&online) && code == 0x42 && !repeat) {
                    online_follow = (online_follow + 1) % game.num_tanks;
                    online_dirty = 1;
                    continue;
                }
                if (editor_on && editor_text_input(code, qualifier)) continue;
                if (editor_on && (code == 0x7a || code == 0x7b)) {
                    editor_wheel(mouse_x, mouse_y, code == 0x7a ? -1 : 1);
                    continue;
                }
                if (editor_on && repeat && !keys[code & 0x7f]) continue;
                if (editor_on && !repeat && code != 0x45) editor_raw_key(code);
                if (!keys[code & 0x7f]) {
                    keys[code & 0x7f] = 1;
                    if (code == 0x45 && !repeat) online_escape = 1;
                    if (code == 0x51 && LowLevelBase && !editor_on) {
                        input_mode[0] = input_mode[0] == AMIGA_INPUT_JOY ?
                            (settings.controls[0].input == AMIGA_INPUT_KEYS ? AMIGA_INPUT_KEYS : AMIGA_INPUT_BOTH) : AMIGA_INPUT_JOY;
                        online_dirty = 1;
                    }
                }
                continue;
            }
            if (remap_capture) {
                if (!repeat) remap_key(code);
                continue;
            }
            if (address_editing()) {
                if (!repeat || (code != 0x44 && code != 0x43 && code != 0x45)) address_key(code, qualifier);
                continue;
            }
            if (menu_edit) {
                if (!repeat && code == 0x45) online_escape = 1;
                if (!repeat && (code == 0x44 || code == 0x43)) online_enter = 1;
                continue;
            }
            if (!repeat && (fire_to_signin(0, player_key_action(0, code)) ||
                            fire_to_signin(1, player_key_action(1, code)))) continue;
            if (!title_on && !paused && !replay_on && !online.socket_open && menu_page == MENU_TEAMS) {
                if (!repeat) {
                    if (code == 0x45) online_escape = 1;
                    else if ((qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) && (code == 0x50 || code == 0x51))
                        open_signin(code - 0x50, MENU_TEAMS);
                    else team_key(code);
                }
                continue;
            }
            if (!title_on && !paused && !online.socket_open && menu_page == MENU_NETWORK &&
                server_form == SERVER_FORM_NONE && (code == 0x42 || code == 0x54)) {
                if (!repeat && code == 0x42) server_tab();
                else if (!repeat) online_refresh = 1;
                continue;
            }
            input = menu_key(code);
            if (!input && code == 0x40 && (title_on || replay_on)) input = IN_FIRE;
            if (input) menu_command(input, repeat, 0);
        } else if (type == IDCMP_VANILLAKEY && menu_edit && !address_editing()) {
            if (code == 13 && !(qualifier & IEQUALIFIER_REPEAT)) online_enter = 1;
            else if (code == 27 && !(qualifier & IEQUALIFIER_REPEAT)) online_escape = 1;
            else menu_text_key(code);
            online_dirty = 1;
        }
    }
    if (LowLevelBase) {
        ULONG joy = ReadJoyPort(player_joystick_port(0));
        ULONG type = joy & JP_TYPE_MASK;
        InputBits buttons = joystick_actions(joy);
        InputBits second = player_joystick(1);
        InputBits pressed = buttons & ~menu_joy;
        InputBits second_pressed = second & ~signin_joy;
        menu_joy = buttons;
        signin_joy = second;
        if (active && lobby_keyboard) {
            if (pressed) lobby_keyboard_move(pressed);
            return;
        }
        if (active && !playing && !menu_edit && !chat_edit && !remap_capture) {
            if (fire_to_signin(0, pressed) || fire_to_signin(1, second_pressed)) return;
            if (menu_page == MENU_TEAMS && !paused && !online.socket_open) {
                if (signed_profile[0] >= 0) team_command(0, pressed);
                if (signed_profile[1] >= 0) team_command(1, second_pressed);
                return;
            }
            if (profile_menu_active() && join_player == 1 && second_pressed) pressed = second_pressed;
        }
        if (type == JP_TYPE_GAMECTLR && active && !menu_edit && !chat_edit && !remap_capture) {   /* a CD32 pad's own buttons (lowlevel.library) */
            static ULONG pad_held;
            ULONG pad = joy & (JPF_BUTTON_PLAY | JPF_BUTTON_YELLOW | JPF_BUTTON_FORWARD | JPF_BUTTON_REVERSE);
            ULONG down = pad & ~pad_held;
            pad_held = pad;
            if (down & JPF_BUTTON_PLAY) online_escape = 1;  /* PLAY: Escape's (a match: pause; paused: back to it; menus: back) */
            if ((down & JPF_BUTTON_YELLOW) && playing && !editor_on) {   /* YELLOW: the radar, the map or the tank dots */
                settings.radar_off ^= 1;
                detailed_radar = !settings.radar_off;
                hud_ready = 0;
            }
            if (down & (JPF_BUTTON_FORWARD | JPF_BUTTON_REVERSE)) {   /* the shoulders: left and right */
                int step = (down & JPF_BUTTON_FORWARD) ? 1 : -1, n = game.num_tanks ? game.num_tanks : 1;
                if (replay_on) replay_follow = (replay_follow + n + step) % n;
                else if (!playing && online.socket_open && online.state == NC_LOBBY) lobby_step_action(step);
                else if (!playing && !paused && !title_on) menu_side_step(step);
            }
        }
        if (active && !playing && !menu_edit && !chat_edit && !remap_capture) menu_command(pressed, 0, 1);
    }

}

static void menu_row_label(int row, int y, const char *text)
{
    if (row == menu_row) {
        fill(INK, 17, y - 9, WIDTH - 18, y + 3);
        fill(TEAM_ONE, 17, y - 9, 18, y + 3);
    }
    label(22, y, row == menu_row ? GOLD : WHITE, text);
}

static const char *input_label(int player)
{
    static const char *const joystick[] = { "JOYSTICK 1", "JOYSTICK 2" };
    static const char *const both[] = { "KEYS+JOY 1", "KEYS+JOY 2" };
    int port = player ^ settings.swap_joysticks;
    if (!LowLevelBase || input_mode[player] == AMIGA_INPUT_KEYS) return "KEYBOARD";
    return input_mode[player] == AMIGA_INPUT_JOY ? joystick[port] : both[port];
}

static void draw_profile_actions(int x, int y, int action)
{
    fill(action == PROFILE_EDIT ? GOLD : TRACK, x, y - 9, x + 38, y + 3);
    label(x + 4, y, action == PROFILE_EDIT ? INK : WHITE, "EDIT");
    fill(action == PROFILE_DELETE ? GOLD : TRACK, x + 45, y - 9, x + 101, y + 3);
    label(x + 49, y, action == PROFILE_DELETE ? INK : WHITE, "DELETE");
}

/* Menu portraits only. The combat sprite bank stays the same size. */
static void draw_class_icon(int x, int y, int team, int cls)
{
    static const char *const jackal[] = {
        "KKK", "KSK", "KSK", "KSK", "KKK",
        "KKK..KKKKK..KKK", "KTKKKHBBBDKKKTK", "KSKKBBBBBBDKKSK",
        "KTKDBKKKKKDBKTK", "KTKBKHBBBKDBKTK", "KSKBKBDBBKDBKSK",
        "KTKBKBHBBKDBKTK", "KTKDBKKKKKDBKTK", "KSKHBBBBBBDKKSK",
        "KTKDBBBBBBDKKTK", "KTKKDBDSDBKKKTK", "KSK.KKKKKK.KSK",
        "KKK..KKKKK..KKK"
    };
    static const char *const mammoth[] = {
        "KKKKK", "KSSSK", "KSKSK", "KSSSK", "KSTSK", "KSTSK",
        "KKKKKK....KKKKKKK....KKKKKK", "KSTTTKKKKKHBBBBBDKKKKKTTTSK",
        "KTTTTKHBBBBBBBBBBBBBKTTTTK", "KSSSTKHBBBBBBBBBBBBBKTTSSK",
        "KTTTTKHBBKKKKKKKKBBDKTTTTK", "KSSSTKHBBKHHBBBBKBBDKTTSSK",
        "KTTTTKHBBKBBDSBBKBBDKTTTTK", "KSSSTKHBBKBBBDDBKBBDKTTSSK",
        "KTTTTKHBBKBHHBBBKBBDKTTTTK", "KSSSTKHBBKBBBBBBKBBDKTTSSK",
        "KTTTTKHBBKKKKKKKKBBDKTTTTK", "KSSSTKHBBBBBBBBBBBBDKTTSSK",
        "KTTTTKHBBBBBBBBBBBBDKTTTTK", "KSSSTKHBSDDSDDSDDBBDKTTSSK",
        "KTTTTKHBKKDKDDKDKBBDKTTTTK", "KSSSTKHBKDDKDDKDKBBDKTTSSK",
        "KTTTTKKDDDDDDDDDDDDKKTTTTK", "KSTTTK.KKKKKKKKKKKK.KTTTSK",
        "KKKKKK..............KKKKKK"
    };
    static const char shades[] = "KTSBDH";
    const int pens[] = { INK, TRACK, 5, team ? TEAM_TWO : TEAM_ONE,
                         team ? SHADE : 3, team ? REPAIR : GOLD };
    const char *const *art = cls == TANK_LIGHT ? jackal : mammoth;
    int height = cls == TANK_LIGHT ? sizeof jackal / sizeof jackal[0] : sizeof mammoth / sizeof mammoth[0];
    int row;
    y += (TANK_SIZE - height) / 2;
    for (row = 0; row < height; row++) {
        int col = 0, left = x + (TANK_SIZE - (int)strlen(art[row])) / 2;
        while (art[row][col]) {
            int end = col + 1;
            const char *shade = strchr(shades, art[row][col]);
            while (art[row][end] == art[row][col]) end++;
            if (shade) fill(pens[shade - shades], left + col, y + row, left + end - 1, y + row);
            col = end;
        }
    }
}

static void draw_menu_sprite(int x, int y, int sprite)
{
    struct BitMap icon;
    int p;
    UBYTE *mask = sprite_chip + sprite * TANK_FRAME_BYTES;
    InitBitMap(&icon, DEPTH, TANK_SIZE, TANK_SIZE);
    for (p = 0; p < DEPTH; p++) icon.Planes[p] = mask + (p + 1) * TANK_PLANE_BYTES;
    BltMaskBitMapRastPort(&icon, 0, 0, &canvas, x, y, TANK_SIZE, TANK_SIZE, 0xe0, mask);
}

static void draw_tank_icon(int x, int y, int team, int cls)
{
#ifndef OFP_AMIGA_AGA
    /* Menu icons read the matching world's source bank; they do not replace the
       active match's Chip RAM sprites while browsing profiles or team setup. */
    int world = local_classic() ? BIOME_CLASSIC : settings.world;
    if (cls != TANK_RHINO && (world == BIOME_VOLCANIC || world == BIOME_MOON)) {
        extern const uint32_t classic_volcanic_vehicles[6][DIRECTIONS][TANK_SIZE][5];
        extern const uint32_t classic_moon_vehicles[6][DIRECTIONS][TANK_SIZE][5];
        const uint32_t (*art)[5] = world == BIOME_MOON ? classic_moon_vehicles[cls * 2 + team][0] : classic_volcanic_vehicles[cls * 2 + team][0];
        int row, col, plane;
        for (row = 0; row < TANK_SIZE; row++) {
            for (col = 0; col < TANK_SIZE; col++) {
                uint32_t bit = 0x80000000u >> col;
                int pen = 0;
                if (!(art[row][0] & bit)) continue;
                for (plane = 0; plane < 4; plane++) if (art[row][plane + 1] & bit) pen |= 1 << plane;
                fill(pen, x + col, y + row, x + col, y + row);
            }
        }
        return;
    }
#endif
    if (cls == TANK_LIGHT || cls == TANK_HEAVY) draw_class_icon(x, y, team, cls);
    else draw_menu_sprite(x, y, cls == TANK_RHINO ? ROCKET_FIRST + team * ROCKET_DIRECTIONS : team * DIRECTIONS);
}

enum { PREVIEW_SIZE = 96 };

static const char *local_mode_name(void)
{
    if (local_classic()) return "CLASSIC";
    if (map_choice == MY_MAP && my_map_mode >= 0) return map_mode_names[my_map_mode];
    return local_modes[mode_choice].name;
}

static void draw_local_preview(int left, int top, int show_size)
{
    static const UBYTE terrain[TILE_COUNT] = { 2, 4, 7, 5, 14, 3, 5, 3, 3, 12, 4, 14 };
    /* Ground, water and foliage vary by biome even on the shared 16-colour screen (never pens 1, 2: the
     * world's own, whichever world was played last) */
    static const UBYTE biome_pens[BIOME_COUNT][3] = {
        { GOLD, 7, 14 }, { 8, 7, 14 }, { 9, 7, 14 }, { WHITE, 7, 14 },
        { 12, 15, 7 }, { 12, 8, 3 }, { 3, 13, 12 }, { 5, 7, WHITE }, { 14, 7, 12 }
    };
    static UBYTE pixels[DEPTH][PREVIEW_SIZE][PREVIEW_SIZE / 8];
    static int preview_world, preview_mode, w, h, start_x[2], start_y[2];
    static uint32_t preview_seed;
    int x, y, plane;

    if (preview_map != map_choice || preview_world != world_choice ||
        preview_mode != mode_choice || preview_seed != seed) {
        MatchSetup setup;
        const char *map = local_match_setup(&setup, seed);
        preview_map = map_choice;
        preview_world = world_choice;
        preview_mode = mode_choice;
        preview_seed = seed;
        w = h = 0;
        /* The menu can reuse the match workspace. Keep only the packed preview. */
        if ((map || random_map(map_choice)) && match_start(&game, &setup, map) == 0) {
            const UBYTE *pens = biome_pens[game.biome];
            w = game.map.w;
            h = game.map.h;
            memset(pixels, 0, sizeof pixels);
            for (y = 0; y < PREVIEW_SIZE; y++) {
                const uint8_t *row = game.map.type[y * h / PREVIEW_SIZE];
                for (x = 0; x < PREVIEW_SIZE; x++) {
                    int tile = row[x * w / PREVIEW_SIZE];
                    int pen = tile == TILE_SAND ? pens[0] : tile == TILE_WATER ? pens[1] :
                              tile == TILE_TREE || tile == TILE_CANOPY ? pens[2] : terrain[tile];
                    for (plane = 0; plane < DEPTH; plane++)
                        if (pen & (1 << plane)) pixels[plane][y][x / 8] |= 0x80 >> (x & 7);
                }
            }
            for (x = 0; x < 2; x++) {
                int tx = game.bases[x].spawn_tx, ty = game.bases[x].spawn_ty;
                if (game.map.arena && x < game.num_tanks) {
                    tx = fx_to_int(game.tanks[x].x) / TILE_SIZE;
                    ty = fx_to_int(game.tanks[x].y) / TILE_SIZE;
                }
                start_x[x] = tx >= 0 && tx < w ? tx * PREVIEW_SIZE / w : -1;
                start_y[x] = ty >= 0 && ty < h ? ty * PREVIEW_SIZE / h : -1;
            }
        }
    }

    if (!w) {
        label(left + 8, top + 50, WHITE, "NO PREVIEW");
        return;
    }
    WaitBlit();
    for (plane = 0; plane < DEPTH; plane++)
        for (y = 0; y < PREVIEW_SIZE; y++)
            memcpy(frame->Planes[plane] + (top + y) * frame->BytesPerRow + left / 8,
                   pixels[plane][y], sizeof pixels[plane][y]);
    for (plane = 0; plane < 2; plane++) {
        if (start_x[plane] < 0 || start_y[plane] < 0) continue;
        x = left + start_x[plane];
        y = top + start_y[plane];
        if (x < left + 2) x = left + 2;
        if (y < top + 2) y = top + 2;
        if (x > left + PREVIEW_SIZE - 3) x = left + PREVIEW_SIZE - 3;
        if (y > top + PREVIEW_SIZE - 3) y = top + PREVIEW_SIZE - 3;
        fill(INK, x - 2, y - 2, x + 2, y + 2);
        fill(plane ? TEAM_TWO : TEAM_ONE, x - 1, y - 1, x + 1, y + 1);
    }
    if (show_size) {
        char dimensions[14];
        snprintf(dimensions, sizeof dimensions, "%d X %d", w, h);
        label(left + 4, top + PREVIEW_SIZE + 12, WHITE, dimensions);
    }
}

static void console_panel(int left, int top, int right, int bottom, int face)
{
    fill(TRACK, left, top, right, bottom);
    fill(5, left, top, right - 1, top);
    fill(4, left, top + 1, left, bottom - 1);
    fill(face, left + 2, top + 2, right - 2, bottom - 2);
#ifdef OFP_AMIGA_AGA256
    if (face == PANEL) {
        int band, height = bottom - top - 3;
        for (band = 0; band < 6; band++)
            fill(AGA_CONSOLE_FIRST + 5 - band, left + 2, top + 2 + band * height / 6,
                 right - 2, top + 1 + (band + 1) * height / 6);
    }
#endif
    fill(TEAM_ONE, left + 2, top + 2, left + 4, top + 2);
    fill(TEAM_ONE, right - 4, top + 2, right - 2, top + 2);
    fill(4, left + 2, bottom - 2, left + 4, bottom - 2);
    fill(4, right - 4, bottom - 2, right - 2, bottom - 2);
}

static void local_panel(int left, int top, int right, int bottom)
{
    console_panel(left, top, right, bottom, INK);
}

static void console_backdrop(void)
{
    int y;
    fill(INK, 0, 0, WIDTH - 1, HEIGHT - 1);
    fill(TRACK, 3, 5, 3, HEIGHT - 6);
    fill(TRACK, WIDTH - 4, 5, WIDTH - 4, HEIGHT - 6);
    for (y = 12; y < HEIGHT - 9; y += 16) {
        fill(y % 32 == 12 ? 4 : TRACK, 2, y, 4, y + 1);
        fill(y % 32 == 12 ? 4 : TRACK, WIDTH - 5, y, WIDTH - 3, y + 1);
    }
}

void local_center(int center, int y, int pen, const char *text)
{
    label(center - (int)strlen(text) * 4, y, pen, text);
}

static void local_logo(void)
{
    const char *title = "OPENFP";
    int pass, i, x, y;
    local_panel(46, 12, 273, 61);
    for (pass = 0; pass < 2; pass++)
        for (i = 0; title[i]; i++)
            for (y = 0; y < 8; y++) {
                UBYTE bits = font8x8_basic[(unsigned char)title[i]][y];
                for (x = 0; x < 8; x++) {
                    int start = x, sx, sy;
                    if (!(bits & (1u << x))) continue;
                    while (x + 1 < 8 && (bits & (1u << (x + 1)))) x++;
                    sx = 160 - (int)strlen(title) * 8 + i * 16 + start * 2 + (pass ? 0 : 2);
                    sy = 21 + y * 3 + (pass ? 0 : 2);
                    fill(pass ? GOLD : TRACK, sx, sy, sx + (x - start + 1) * 2 - 1, sy + 2);
                }
            }
    local_center(160, 54, WHITE, "AN OPEN REMAKE");
}

static void draw_option_band(int selected, int left, int right, int top, int bottom, int y,
                        const char *name, const char *value)
{
    fill(selected ? GOLD : INK, left, top, right, bottom);
    fill(selected ? TEAM_ONE : TRACK, left, bottom, right, bottom);
    if (selected) fill(INK, left + 3, y - 5, left + 4, y - 1);
    label(left + 8, y, selected ? INK : WHITE, name);
    if (value) label(right - 4 - (int)strlen(value) * 8, y, selected ? INK : GOLD, value);
}

void menu_option_row(int selected, int left, int right, int y, const char *name, const char *value)
{
    draw_option_band(selected, left, right, y - 9, y + 4, y, name, value);
}

static void option_band(int row, int left, int right, int top, int bottom, int y,
                        const char *name, const char *value)
{
    draw_option_band(row == menu_row, left, right, top, bottom, y, name, value);
}

static void show_menu_palette(void)
{
    uint32_t base[COLOURS];
    if (palette_shown == PALETTE_MENU) return;
    base_palette(base);
    show_palette(base);
    palette_shown = PALETTE_MENU;
}

void editor_menu_begin(void)
{
    show_menu_palette();
    console_backdrop();
    console_panel(9, 17, WIDTH - 10, HEIGHT - 11, PANEL);
    fill(TRACK, 20, 37, WIDTH - 21, 37);
}

void editor_menu_present(void)
{
    wait_for_display();
    presentation.BitMap = display_buffers[back_buffer]->sb_BitMap;
    BltBitMapRastPort(frame, 0, 0, &presentation, 0, 0, WIDTH, HEIGHT, 0xc0);
    present_frame();
}

static void option_row(int row, int left, int right, int y, const char *name, const char *value)
{
    menu_option_row(row == menu_row, left, right, y, name, value);
}

static void option_volume(int row, int right, int y, const char *name, int level)
{
    enum { STEP = 12 };
    int i, x = right - VOLUME_MAX * STEP - 15;
    option_row(row, 15, right, y, name, NULL);
    fill(menu_row == row ? TEAM_ONE : 5, x, y - 7, right - 10, y + 2);
    fill(INK, x + 1, y - 6, right - 11, y + 1);
    for (i = 0; i < VOLUME_MAX; i++) {
        int left = x + 3 + i * STEP;
        fill(i < level ? GOLD : TRACK, left, y - 4, left + STEP - 3, y - 1);
    }
}

static void draw_profile_edit(void)
{
    int creating = !settings.profile[profile_slot].name[0];
    if(cloud_form) {
        char value[36];int row;
        local_logo();console_panel(8,68,311,240,PANEL);label(17,83,GOLD,"CLOUD PROFILE");
        for(row=0;row<6;row++) {
            const char *title=row==0?"ACCOUNT":row==1?"USERNAME":row==2?"EMAIL":row==3?"PASSWORD":row==4?(cloud_create?"CREATE ACCOUNT":"SIGN IN"):"CANCEL";
            const char *v=row==0?(cloud_create?"CREATE NEW":"SIGN IN"):row==1?cloud_user:row==2?cloud_email:row==3?(cloud_password[0]?"********":""):"";
            if(row==2&&!cloud_create) continue;
            if(menu_edit&&menu_row==row) v=row==3?"********":edit_text;
            snprintf(value,sizeof value,"%.18s",v);
            option_row(row,15,304,103+row*20,title,value);
        }
        if(online_status[0]) {snprintf(value,sizeof value,"%.35s",online_status);label(17,231,GOLD,value);}
        return;
    }
    int pick = local_classic() ? tank_chassis(profile_draft.tank) : profile_draft.tank;
    const char *tank = pick < 0 ? "DEFAULT" : pick == TANK_RHINO ? "RHINO" : tank_stats[pick].name;
    int icon_tank = pick < 0 ? profile_tank(join_player) : pick;
    local_logo();
    console_panel(8, 68, 311, 204, PANEL);
    label(17, 83, GOLD, creating ? "NEW PROFILE" : "EDIT PROFILE");
    option_row(EDIT_ACCOUNT, 15, 304, 98, "PROFILE TYPE", profile_draft.session[0] ? "CLOUD >" : "LOCAL / CLOUD >");
    option_row(EDIT_NAME, 15, 304, 116, "NAME", profile_draft.name[0] ? profile_draft.name : "ENTER NAME");
    option_band(EDIT_FACE, 15, 304, 123, 149, 139, "AVATAR", NULL);
    draw_face(272, 124, profile_draft.face);
    option_band(EDIT_TANK, 15, 304, 153, 184, 172, "FAVORITE TANK", NULL);
    label(259 - (int)strlen(tank) * 8, 172, menu_row == EDIT_TANK ? INK : GOLD, tank);
    draw_tank_icon(265, 154, join_player, icon_tank);
    option_row(EDIT_STEER, 15, 304, 200, "STEERING", profile_draft.steer > 0 ? "ORIGINAL" : "TANK");
    console_panel(8, 210, 311, 240, PANEL);
    option_row(EDIT_SAVE, 15, 191, 228, creating ? "CREATE PROFILE" : "SAVE", NULL);
    option_row(EDIT_BACK, 199, 304, 228, "CANCEL", NULL);
    if (online_status[0]) local_center(160, 252, GOLD, online_status);
}

static void draw_controls(void)
{
    int player;
    local_logo();
    for (player = 0; player < 2; player++) {
        int y = 68 + 74 * player, row = player * 3;
        console_panel(8, y, 311, y + 68, PANEL);
        label(17, y + 13, GOLD, player ? "PLAYER 2" : "PLAYER 1");
        fill(TRACK, 17, y + 17, 302, y + 17);
        option_row(row, 15, 304, y + 28, "INPUT", input_label(player));
        option_row(row + 1, 15, 304, y + 43, "JOYSTICK",
                   (player ^ settings.swap_joysticks) ? "PORT 2" : "PORT 1");
        option_row(row + 2, 15, 304, y + 58, "CHANGE KEYMAP  >",
                   memcmp(player_keys(player), default_keys[player], sizeof remap_keys) ? "CUSTOM" : "DEFAULT");
    }
    console_panel(8, 216, 311, 245, PANEL);
    option_row(CONTROLS_BACK, 15, 130, 233, "BACK", NULL);
    option_row(CONTROLS_RESET, 138, 304, 233, "RESET TO DEFAULT", NULL);
    if (online_status[0]) local_center(160, 254, GOLD, online_status);
}

const char *raw_key_name(uint8_t code, char text[16])
{
    static const char *const names[AMIGA_KEY_BIND_LIMIT] = {
        [0x0f] = "NUMPAD 0", [0x1d] = "NUMPAD 1", [0x1e] = "NUMPAD 2", [0x1f] = "NUMPAD 3",
        [0x2d] = "NUMPAD 4", [0x2e] = "NUMPAD 5", [0x2f] = "NUMPAD 6", [0x3d] = "NUMPAD 7",
        [0x3e] = "NUMPAD 8", [0x3f] = "NUMPAD 9", [0x3c] = "NUMPAD .", [0x4a] = "NUMPAD -",
        [0x5a] = "NUMPAD (", [0x5b] = "NUMPAD )", [0x5c] = "NUMPAD /",
        [0x5d] = "NUMPAD *", [0x5e] = "NUMPAD +",
        [0x40] = "SPACE", [0x41] = "BACKSPACE", [0x42] = "TAB", [0x43] = "NUMPAD ENTER",
        [0x44] = "RETURN", [0x45] = "ESCAPE", [0x46] = "DELETE",
        [0x4c] = "^ UP ARROW", [0x4d] = "v DOWN ARROW", [0x4e] = "> RIGHT ARROW", [0x4f] = "< LEFT ARROW",
        [0x5f] = "HELP", [0x60] = "LEFT SHIFT", [0x61] = "RIGHT SHIFT", [0x62] = "CAPS LOCK",
        [0x63] = "CTRL", [AMIGA_KEY_LALT] = "LEFT ALT", [AMIGA_KEY_RALT] = "RIGHT ALT",
        [AMIGA_KEY_LAMIGA] = "LEFT AMIGA", [AMIGA_KEY_RAMIGA] = "RIGHT AMIGA"
    };
    if (code < AMIGA_KEY_BIND_LIMIT && names[code]) return names[code];
    if (code >= 0x50 && code <= 0x59) snprintf(text, 16, "F%d", code - 0x4f);
    else if (KeymapBase) {
        struct InputEvent event = {0};
        char raw[8];
        event.ie_Class = IECLASS_RAWKEY;
        event.ie_Code = code;
        if (MapRawKey(&event, raw, sizeof raw, NULL) == 1 && raw[0] > ' ' && raw[0] <= '~') {
            text[0] = raw[0] >= 'a' && raw[0] <= 'z' ? raw[0] - ('a' - 'A') : raw[0];
            text[1] = 0;
        } else snprintf(text, 16, "RAW $%02X", code);
    } else snprintf(text, 16, "RAW $%02X", code);
    return text;
}

static void draw_keymap(void)
{
    int i;
    local_logo();
    console_panel(8, 68, 311, 245, PANEL);
    label(17, 83, GOLD, remap_player ? "PLAYER 2 KEYMAP" : "PLAYER 1 KEYMAP");
    for (i = 0; i < 6; i++) {
        char text[16];
        int row = i + KEYS_FIRST;
        const char *value = remap_capture && row == menu_row ? "PRESS A KEY" : raw_key_name(remap_keys[i], text);
        option_row(row, 15, 304, 102 + i * 17, remap_names[i], value);
    }
    option_row(KEYS_SAVE, 15, 156, 208, "SAVE", NULL);
    option_row(KEYS_BACK, 164, 304, 208, "BACK", NULL);
    if (online_status[0]) local_center(160, 234, GOLD, online_status);
}

static void draw_signin(void)
{
    const char *prompt = "FIRE / ENTER SIGN IN";
    int row;
    local_logo();
    local_panel(30, 69, 289, 226);
    local_center(160, 85, GOLD, signed_profile[join_player] >= 0 ?
                 (join_player ? "PLAYER 2 PROFILE" : "PLAYER 1 PROFILE") :
                 (join_player ? "PLAYER 2 SIGN IN" : "PLAYER 1 SIGN IN"));
    for (row = 0; row < join_count; row++) {
        int y = 103 + row * 21;
        int slot = join_rows[row];
        int action = row == menu_row ? profile_action : -1;
        const char *entry = slot < AMIGA_PROFILES ? settings.profile[slot].name :
                            slot == JOIN_NEW ? "NEW PROFILE" : "SIGN OUT";
        if (action == PROFILE_USE) fill(GOLD, 38, y - 9, slot < AMIGA_PROFILES ? 173 : 281, y + 3);
        label(46, y, action == PROFILE_USE ? INK : slot == signed_profile[join_player ^ 1] ? 5 : WHITE, entry);
        if (slot < AMIGA_PROFILES) draw_profile_actions(180, y, action);
        if (row != menu_row) continue;
        if (slot == JOIN_NEW) prompt = "FIRE / ENTER CREATE";
        else if (slot == JOIN_LEAVE) prompt = "FIRE / ENTER SIGN OUT";
        else if (action == PROFILE_EDIT) prompt = "FIRE / ENTER EDIT";
        else if (action == PROFILE_DELETE) prompt = "FIRE / ENTER DELETE";
    }
    local_center(160, 206, WHITE, online_status[0] ? online_status : prompt);
    local_center(160, 218, 5, "MINE / ESC BACK");
}

static void local_menu_row(int row, int y, const char *name, const char *value)
{
    char text[22];
    int selected = row == menu_row;
    if (selected) fill(GOLD, 15, y - 9, 197, y + (value ? 12 : 3));
    label(21, y, selected ? INK : WHITE, name);
    if (value) {
        snprintf(text, sizeof text, "%.21s", value);
        label(25, y + 10, selected ? INK : GOLD, text);
    }
}

static void draw_role_badges(int x, int y, int roles)
{
    static const int pens[ROLE_BADGE_COUNT] = { GOLD, WHITE, REPAIR };
    for (int i = 0; i < ROLE_BADGE_COUNT; i++) {
        if (!(roles & (1 << i))) continue;
        fill(INK, x, y, x + ROLE_BADGE_SIZE - 1, y + ROLE_BADGE_SIZE - 1);
        for (int row = 0; row < 5; row++)
            for (int col = 0; col < 5; col++)
                if (role_badge_row(i, row) & (16u >> col))
                    fill(pens[i], x + 1 + col, y + 1 + row, x + 1 + col, y + 1 + row);
        x += ROLE_BADGE_STEP;
    }
}

static void team_card(int left, int team, const char *name, int cls, int face, int player)
{
    int color = team ? TEAM_TWO : TEAM_ONE;
    int rocket = cls == TANK_RHINO;
    int roles = local_team_roles(player);
    local_panel(left, 57, left + 94, 224);
    fill(color, left + 2, 59, left + 92, 77);
    local_center(left + 47, 72, INK, team_names[team]);
    local_center(left + 47, 94, WHITE, roles & ROLE_LEADER ? player == 0 ? "F3 RENAME" : "F4 RENAME" : "1 / 1");
    if (player >= 0 && !team_ready[player])
        fill(GOLD, left + 6, 103, left + 88, 193);
    fill(TRACK, left + 7, 104, left + 87, 192);
    if (face >= 0) draw_face((left + 16) & ~7, 111, face);
    else label(left + 13, 128, color, settings.players == 2 ? "P2" : "AI");
    draw_tank_icon(left + 52, 106, team, cls);
    draw_role_badges(left + 10, 135, roles);
    label(left + 8, 151, WHITE, name);
    local_center(left + 47, 167, color, rocket ? "RHINO" : world_tank_stats(local_classic() ? BIOME_CLASSIC : settings.world, (TankClass)cls)->name);
    if (player >= 0) {
        local_center(left + 47, 185, team_ready[player] ? TEAM_TWO : GOLD,
                     team_ready[player] ? "READY" : "PICK TANK");
        if (LowLevelBase && input_mode[player] != AMIGA_INPUT_KEYS)
            local_center(left + 47, 209, WHITE, input_label(player));
        else if (!memcmp(player_keys(player), default_keys[0], sizeof remap_keys)) {
            local_center(left + 47, 205, WHITE, "ARROWS");
            local_center(left + 47, 216, WHITE, "AMIGA ALT");
        } else if (!memcmp(player_keys(player), default_keys[1], sizeof remap_keys))
            local_center(left + 47, 209, WHITE, "WASD Q E");
        else local_center(left + 47, 209, WHITE, "CUSTOM KEYS");
    } else {
        local_center(left + 47, 205, WHITE, "F2 TO JOIN");
        if (LowLevelBase)
            local_center(left + 47, 216, WHITE, settings.swap_joysticks ? "JOY 1 FIRE" : "JOY 2 FIRE");
    }
}

static void team_button(int row, int y, const char *text)
{
    int selected = team_ready[0] && row == menu_row;
    fill(selected ? GOLD : SHADE, 107, y - 8, 212, y + 3);
    local_center(160, y, selected ? INK : WHITE, text);
}

static void draw_local_setup(int teams)
{
    const AmigaProfile *profile = &settings.profile[settings.current];
    char name[10], difficulty[14];
    if (!teams) {
        local_logo();
        local_panel(8, 69, 203, 229);
        local_panel(208, 69, 315, 229);
        label(21, 86, TEAM_ONE, "LOCAL GAME");
        local_menu_row(LG_WORLD, 104, "WORLD", world_names[world_choice]);
        local_menu_row(LG_MODE, 128, "MODE", local_mode_name());
        local_menu_row(LG_SIZE, 152, "SIZE", settings.map_size == SIZE_ANY ? "ANY" : map_size_name[settings.map_size]);
        local_menu_row(LG_MAP, 176, "MAP", random_map(map_choice) ? "RANDOM" : map_label(map_choice));
        local_menu_row(LG_NEXT, 206, "START GAME  >", NULL);
        local_menu_row(LG_BACK, 222, "BACK", NULL);
        local_center(261, 86, GOLD, "BATTLE MAP");
        draw_local_preview(216, 94, 1);
        local_center(261, 218, GOLD, random_map(map_choice) ? "RANDOM" : "FIXED MAP");
    } else {
        local_panel(8, 18, 311, 49);
        local_center(160, 31, GOLD, !strcmp(local_mode_name(), "CLASSIC") ? "CLASSIC (1987 RULES)" : local_mode_name());
        local_center(160, 43, WHITE, "TEAM SETUP");
        snprintf(name, sizeof name, "%.9s", signed_name(0));
        team_card(host_side ? 215 : 7, host_side, name, team_tank[0],
                  signed_profile[0] >= 0 ? profile->face : -1, 0);
        team_card(host_side ? 7 : 215, host_side ^ 1,
                  settings.players == 2 ? signed_name(1) : "COMPUTER",
                  settings.players == 2 ? team_tank[1] : TANK_MEDIUM,
                  signed_profile[1] >= 0 ? settings.profile[signed_profile[1]].face : -1,
                  settings.players == 2 ? 1 : -1);
        local_panel(108, 57, 211, 159);
        draw_local_preview(112, 60, 0);
        team_button(TM_PLAYERS, 169, settings.players == 2 ? "P2 PROFILE" : "JOIN P2");
        snprintf(difficulty, sizeof difficulty, "AI: %s", level_names[level_choice]);
        team_button(TM_ENEMY, 183, settings.players == 2 ? "SPLIT SCREEN" : difficulty);
        team_button(TM_START, 197, settings.players == 2 && !team_ready[1] ? "WAIT FOR P2" : "START MATCH");
        team_button(TM_BACK, 211, "BACK");
    }
    if (!online_status[0] && teams) {
        const char *action = "FIRE / ENTER READY   ESC BACK";
        if (team_ready[0]) {
            if (menu_row != TM_START) action = "FIRE SELECT   ESC EDIT P1 TANK";
            else if (settings.players == 2 && !team_ready[1]) action = "P2 FIRE OR F2 TO READY";
            else action = "FIRE / ENTER START MATCH";
        }
        local_center(160, 237, WHITE, team_ready[0] ? "P1 UP/DOWN CHOOSE" : "UP/DOWN TANK   LEFT/RIGHT SIDE");
        local_center(160, 249, GOLD, action);
    } else if (!online_status[0]) local_center(160, 245, WHITE, "ARROWS CHANGE  ENTER SELECT");
    else local_center(160, 245, WHITE, online_status);
}

static void draw_radar_preview(void)
{
    console_panel(212, 68, 311, 178, PANEL);
    local_center(262, 82, GOLD, "FIELD RADAR");
    inset(223, 90, 78, 65, TRACK);
    if (!settings.radar_off) {
        fill(PANEL, 226, 93, 297, 151);
        fill(4, 226, 126, 297, 129);
        fill(4, 260, 101, 263, 151);
        fill(5, 236, 97, 249, 104);
        fill(5, 273, 133, 291, 142);
        fill(5, 268, 98, 268, 115);
        fill(5, 268, 115, 290, 115);
    }
    fill(TEAM_ONE, 260, 119, 263, 123);
    fill(TEAM_ONE, 259, 120, 264, 122);
    fill(TEAM_ONE, 236, 134, 239, 136);
    fill(TEAM_TWO, 273, 109, 276, 111);
    fill(TEAM_TWO, 282, 146, 285, 148);
    local_center(262, 168, WHITE, settings.radar_off ? "TANK DOTS" : "MAP");
}

static void draw_graphics_preview(void)
{
    const char *caption;
    int i;
    console_panel(212, 68, 311, 178, PANEL);
    local_center(262, 82, GOLD, "PREVIEW");
    inset(222, 90, 80, 62, INK);
    fill(1, 225, 93, 298, 148);
    fill(5, 225, 113, 298, 132);
    fill(4, 225, 114, 298, 131);
    draw_tank_icon(243, 103, 0, TANK_MEDIUM);
    switch (menu_row) {
    case GRAPH_WEATHER:
        if (!settings.weather_off) {
            for (i = 0; i < 19; i++) {
                int x = 226 + i * 29 % 69, y = 94 + i * 37 % 49;
                fill(WHITE, x, y, x, y + 2);
                fill(WHITE, x + 1, y + 3, x + 1, y + 4);
            }
        }
        caption = settings.weather_off ? "WEATHER OFF" : "RAIN SAMPLE";
        break;
    case GRAPH_EXPLOSIONS:
        if (settings.explosions == EXPLOSIONS_FULL)
            draw_menu_sprite(265, 105, BATTLE_FIRST + BATTLE_BLAST * BATTLE_FRAMES + 1);
        else if (settings.explosions == EXPLOSIONS_SIMPLE)
            draw_simple_blast(NULL, 281, 121, 6, 1);
        caption = settings.explosions == EXPLOSIONS_FULL ? "FIRE/DEBRIS" :
                  settings.explosions == EXPLOSIONS_SIMPLE ? "SMALL BURST" : "BLASTS OFF";
        break;
    case GRAPH_SURFACE:
        if (!settings.surface_fx_off) for (i = 0; i < 7; i++) {
            fill(WHITE, 236+i*4, 137-i, 237+i*4, 138-i);
            fill(WHITE, 246+i*4, 141-i, 247+i*4, 142-i);
        }
        caption = settings.surface_fx_off ? "TRAILS OFF" : "ICE SKID TRAIL";
        break;
    case GRAPH_FIRES:
        fill(INK, 273, 125, 290, 129);
        fill(TRACK, 276, 122, 287, 131);
        if (!settings.ground_fires_off)
            draw_menu_sprite(265, 109, BATTLE_FIRST + BATTLE_EMBER * BATTLE_FRAMES);
        caption = settings.ground_fires_off ? "FIRES OFF" : "EMBER SAMPLE";
        break;
    default:
        fill(INK, 277, 122, 286, 128);
        fill(TRACK, 280, 120, 284, 131);
        if (!settings.marks) draw_blood_stain(NULL, 239, 139);
        caption = settings.marks ? "SCORCH ONLY" : "BLOOD MARKS";
        break;
    }
    local_center(262, 166, WHITE, caption);
}

static void draw_options(void)
{
    int game_options = menu_page == MENU_OPTIONS;
    int graphics = menu_page == MENU_GRAPHICS;
    int right = game_options || graphics ? 200 : 304;
    int back = menu_rows() - 1;
    local_logo();
    console_panel(8, 68, game_options || graphics ? 207 : 311, 178, PANEL);
    label(17, 82, GOLD, game_options ? "GAME OPTIONS" : graphics ? "GRAPHICS" : "AUDIO");
    fill(TRACK, 17, 87, right - 2, 87);
    if (game_options) {
        option_row(0, 15, right, 100, "RADAR", settings.radar_off ? "TANK DOTS" : "MAP");
        char value[16];
        option_row(1, 15, right, 115, "SIDES", settings.sides ? "ORANGE/BLUE" : "ORANGE/GREEN");
        snprintf(value, sizeof value, "%d", settings.match_lives);
        option_row(2, 15, right, 130, "TANKS/PLAYER", settings.match_lives == 255 ? "UNLIMITED" : value);
        snprintf(value, sizeof value, "%d MIN", settings.match_minutes);
        option_row(3, 15, right, 145, "MATCH TIME", value);
        option_row(4, 15, right, 160, "POWER-UPS", settings.powerups ? "ENHANCED" : "OFF");
        draw_radar_preview();
    } else if (graphics) {
        static const char *const explosions[EXPLOSION_STYLES] = { "FULL", "SIMPLE", "OFF" };
        char colours[16];
        option_row(GRAPH_MARKS, 15, right, 98, "MARKS", settings.marks ? "NO BLOOD" : "ALL");
        option_row(GRAPH_AUTO, 15, right, 168, "AUTO EFFECTS", settings.auto_effects ? "ON" : "OFF");
        option_row(GRAPH_WEATHER, 15, right, 112, "WEATHER EFFECTS", settings.weather_off ? "OFF" : "ON");
        option_row(GRAPH_EXPLOSIONS, 15, right, 126, "EXPLOSIONS", explosions[settings.explosions]);
        option_row(GRAPH_FIRES, 15, right, 140, "GROUND FIRES", settings.ground_fires_off ? "OFF" : "ON");
        option_row(GRAPH_SURFACE, 15, right, 154, "SKID TRAILS", settings.surface_fx_off ? "OFF" : "ON");
        snprintf(colours, sizeof colours, "%u COLOURS", (unsigned)COLOURS);
        (void)colours;
        draw_graphics_preview();
    } else {
        option_volume(0, right, 103, "MUSIC VOLUME", settings.music);
        option_volume(1, right, 127, "EFFECT VOLUME", settings.effects);
    }
    console_panel(8, 187, 311, 230, PANEL);
    option_row(back, 15, 304, 211, "BACK", NULL);
    if (graphics) {
        static const char *const help[GRAPH_ROWS] = {
            "HIDE BLOOD, KEEP SCORCH MARKS", "RAIN, SNOW AND OTHER MAP WEATHER",
            "CHOOSE HOW MUCH OF THE BLAST TO SHOW", "SHOW FIRES AFTER BUILDINGS FALL",
            "ICE AND OIL TRAILS / VISUAL ONLY",
            "REDUCE DECORATION ON SLOW FRAMES",
            "SAVED AS YOU GO"
        };
        local_center(160, 245, WHITE, help[menu_row]);
    }
}

static void draw_hub(int tools)
{
    static const char *const tool_hints[] = { "WATCH MATCHES", "BUILD A MAP", "RUN TEST", "JOYSTICKS" };
    static const char *const setting_hints[] = { "RADAR AND RULES", "DISPLAY", "VOLUME", "KEYS AND JOYSTICKS" };
    const char *const *items = tools ? tool_items : setting_items;
    const char *const *hints = tools ? tool_hints : setting_hints;
    int i, back = menu_rows() - 1;
    local_logo();
    console_panel(8, 68, 311, 178, PANEL);
    label(18, 83, GOLD, tools ? "TOOLS" : "SETTINGS");
    fill(TRACK, 18, 90, 301, 90);
    for (i = 0; i < back; i++) option_row(i, 15, 304, 105 + i * 19, items[i], hints[i]);
    console_panel(8, 187, 311, 242, PANEL);
    option_row(back, 15, 304, 205, items[back], NULL);
    if (tools && menu_row == TOOL_BENCHMARK)
        local_center(160, 226, GOLD, online_status[0] ? online_status : "2-TANK PERFORMANCE TEST");
}

static void draw_play(void)
{
    static const char *const hints[] = { "SET UP A MATCH", "SERVER BROWSER", "LEARN TO PLAY", "SHORT MISSIONS" };
    int i, back = menu_rows() - 1;
    local_logo();
    console_panel(8, 68, 311, 184, PANEL);
    label(18, 83, GOLD, "PLAY");
    fill(TRACK, 18, 90, 301, 90);
    for (i = 0; i < back; i++) option_row(i, 15, 304, 103 + 23 * i, play_items[i], hints[i]);
    console_panel(8, 193, 311, 236, PANEL);
    option_row(back, 15, 304, 217, play_items[back], NULL);
}

static const char *listed_version_note(uint32_t build)
{
    int protocol = (int)(build >> 16);
    if (build && protocol < NET_PROTOCOL_OLDEST) return "ASK THE HOST TO UPDATE";
    if (protocol > NET_PROTOCOL) return "UPDATE: OPENFP.RETROCS.ORG/PLAY/";
    return NULL;
}

static void draw_network(void)
{
    static const char *const form_titles[] = { "", "ADD PRIVATE SERVER", "EDIT PRIVATE SERVER",
                                               "DIRECT CONNECT", "DELETE PRIVATE SERVER?" };
    char text[37], value[17];
    int count, i;
    local_logo();
    console_panel(8, 68, 311, 181, PANEL);
    console_panel(8, 188, 311, 251, PANEL);
    if (server_form != SERVER_FORM_NONE) {
        label(18, 83, GOLD, form_titles[server_form]);
        fill(TRACK, 18, 90, 301, 90);
        if (server_form == SERVER_FORM_DELETE) {
            label(18, 114, WHITE, edit_text);
            menu_option_row(!server_confirm, 15, 142, 150, "CANCEL", NULL);
            menu_option_row(server_confirm, 151, 304, 150, "DELETE", NULL);
        } else {
            label(18, 112, WHITE, "HOST OR HOST:PORT");
            inset(15, 123, 290, 19, TRACK);
            label(20, 136, GOLD, edit_text);
        }
        label(18, 209, WHITE, server_form == SERVER_FORM_DIRECT ? "ENTER JOIN   ESC CANCEL" :
              server_form == SERVER_FORM_DELETE ? "ENTER CHOOSE   ESC CANCEL" : "ENTER SAVE   ESC CANCEL");
    } else {
        label(18, 83, GOLD, "NETWORK GAME");
        menu_option_row(public_tab, 15, 135, 102, "PUBLIC", NULL);
        menu_option_row(!public_tab, 143, 263, 102, "PRIVATE", NULL);
        label(276, 102, WHITE, "TAB");
        if (public_tab) {
            int last = public_list.count - 1;
            int first = last < 0 ? 0 : ((menu_row < last ? menu_row : last) / 4) * 4;
            for (i = first; i < public_list.count && i < first + 4; i++) {
                const AmigaPublicServer *server = &public_list.server[i];
                snprintf(text, sizeof text, "%.20s", server->name);
                snprintf(value, sizeof value, "%u/%u %s", (unsigned)server->players, (unsigned)server->capacity,
                         listed_version_note(server->build) ? (int)(server->build >> 16) > NET_PROTOCOL ? "NEWER" : "OLD" :
                         server->locked ? "LOCKED" : server->running ? "PLAYING" : "LOBBY");
                option_row(i, 15, 304, 121 + 17 * (i - first), text, value);
            }
            if (!public_list.count)
                label(18, 139, WHITE, public_list.pending ? "FETCHING OFFICIAL LIST..." :
                      public_failed ? "PUBLIC LIST UNAVAILABLE" : "NO PUBLIC SERVERS FOUND");
            option_row(public_list.count, 15, 103, 211, "BACK", NULL);
            label(112, 211, WHITE, "ENTER JOIN   F5 REFRESH");
            if (menu_row < public_list.count && listed_version_note(public_list.server[menu_row].build))
                label(18, 242, GOLD, listed_version_note(public_list.server[menu_row].build));
        } else {
            count = private_server_count();
            for (i = 0; i < count; i++) {
                int slot = private_server_slot(i), y = 121 + 17 * i;
                const SavedServer *server = &browser[slot];
                snprintf(text, sizeof text, "%.20s", server->state == SERVER_REACHABLE && server->info.name[0] ?
                         server->info.name : settings.server[slot]);
                menu_option_row(i == menu_row && server_action == SERVER_JOIN, 15, 184, y, text, NULL);
                menu_option_row(i == menu_row && server_action == SERVER_EDIT, 191, 232, y, "EDIT", NULL);
                menu_option_row(i == menu_row && server_action == SERVER_DELETE, 239, 304, y, "DELETE", NULL);
            }
            if (!count) label(18, 139, WHITE, "NO PRIVATE SERVERS SAVED");
            option_row(count, 15, 140, 204, "ADD SERVER", NULL);
            option_row(count + 1, 148, 304, 204, "DIRECT CONNECT", NULL);
            option_row(count + 2, 15, 103, 224, "BACK", NULL);
            if (menu_row < count && !online_status[0]) {
                const SavedServer *server = &browser[private_server_slot(menu_row)];
                if (server->state == SERVER_REACHABLE) {
                    snprintf(text, sizeof text, "%u/%u %s %dMS", (unsigned)server->info.players,
                             (unsigned)server->info.max_players, server->info.running ? "PLAYING" : "LOBBY", server->ping);
                    label(113, 224, GOLD, text);
                } else label(113, 224, WHITE, server->state == SERVER_OFFLINE ? "NO REPLY   F5 CHECK" : "F5 CHECK SERVER");
            }
        }
    }
    if (online_status[0] && !(server_form == SERVER_FORM_NONE && public_tab && menu_row < public_list.count &&
                             listed_version_note(public_list.server[menu_row].build))) {
        snprintf(text, sizeof text, "%.35s", online_status);
        label(18, 242, GOLD, text);
    }
}

static void draw_replays(void)
{
    enum { VISIBLE_REPLAYS = 7 };
    int i, first = menu_row >= VISIBLE_REPLAYS ? menu_row - VISIBLE_REPLAYS + 1 : 0;
    if (first > nreplays - VISIBLE_REPLAYS) first = nreplays - VISIBLE_REPLAYS;
    if (first < 0) first = 0;
    local_logo();
    console_panel(8, 68, 311, 204, PANEL);
    label(18, 82, GOLD, "REPLAYS");
    fill(TRACK, 18, 88, 301, 88);
    if (replay_loading) {
        label(20, 107, WHITE, replay_names[menu_row]);
        label(20, 130, GOLD, replay_loading == 1 ? "READING REPLAY..." :
              replay_loading == 2 ? "PREPARING MAP..." : "PREPARING DISPLAY...");
        inset(20, 145, 280, 12, SHADE);
        fill(GOLD, 22, 147, 21 + 92 * replay_loading, 154);
    } else {
        if (!nreplays) label(20, 113, WHITE, "NO REPLAYS FOUND");
        for (i = 0; i < VISIBLE_REPLAYS && first + i < nreplays; i++)
            option_row(first + i, 15, 304, 102 + i * 15, replay_names[first + i], NULL);
    }
    console_panel(8, 211, 311, 244, PANEL);
    option_row(nreplays, 15, 100, 232, "BACK", NULL);
    if (online_status[0]) {
        char note[25];
        snprintf(note, sizeof note, "%.24s", online_status);
        label(108, 232, GOLD, note);
    } else if (nreplays && !replay_loading) label(108, 232, WHITE, "FIRE TO WATCH");
}

static void draw_about(void)
{
    int k, top = 32, bottom = 221;
    int span = bottom - top + OFP_CREDIT_LINES * 13 + 60;
    int scroll = (int)((net_ms() - credits_start) / OFP_CREDITS_STEP_MS % (unsigned)span);
    console_panel(8, 12, 311, 228, PANEL);
    for (k = 0; k < OFP_CREDIT_LINES; k++) {
        int y = bottom + k * 13 - scroll;
        if (y < top || y > bottom - 8) continue;
        local_center(160, y, ofp_credits[k].heading ? GOLD : WHITE, ofp_credits[k].text);
    }
    option_row(0, 15, 304, 243, "BACK", NULL);
}

static int lobby_side_pen(int side)
{
    return side == SIDE_CAMP ? TEAM_ONE : side == SIDE_FORTRESS ? TEAM_TWO : GOLD;
}

static void draw_online_lobby(void)
{
    static const int order[] = { SIDE_CAMP, SIDE_ANY, SIDE_FORTRESS };
    int teams = netc_lobby_teams(&online), bots = netc_lobby_bots(&online);
    char text[44], capacity[22];
    LobbyRoster roster;
    int col, i, page;
    netc_lobby_roster(&online, &roster);
    page = lobby_page < roster.pages ? lobby_page : roster.pages - 1;

    console_panel(6, 5, 313, 40, PANEL);
    snprintf(text, sizeof text, "%.26s", online.server_name);
    label(12, 15, GOLD, text);
    label(248, 15, WHITE, online.versus ? "VERSUS" : "CO-OP");
    snprintf(text, sizeof text, "%.36s", online.motd);
    label(12, 25, WHITE, text);
    snprintf(capacity, sizeof capacity, "MAX %d TANKS", online.match_capacity ? online.match_capacity : MATCH_PLAYERS);
    if (bots) snprintf(capacity + strlen(capacity), sizeof capacity - strlen(capacity), "/%d AI", bots);
    snprintf(text, sizeof text, "NEXT %.*s", 29 - (int)strlen(capacity), online.next_map[0] ? online.next_map : "RANDOM");
    label(12, 36, WHITE, text);
    label(306 - 8 * (int)strlen(capacity), 36, GOLD, capacity);

    for (col = 0; col < roster.columns; col++) {
        int x = teams ? (col == 0 ? 4 : col == 1 ? 114 : 210) : 4 + 157 * col;
        int width = teams ? (col == 1 ? 92 : 106) : 154;
        int side = teams ? order[col] : SIDE_ANY;
        int selected = teams && online.side == side;
        int color = lobby_side_pen(side);
        const char *heading = teams ? netc_lobby_team_name(&online, side) : online.versus ? "PILOTS" : "CO-OP";
        local_panel(x, 44, x + width - 1, 193);
        int action = col == 0 ? LA_CAMP : LA_FORTRESS;
        int can_rename = teams && col != 1 && lobby_action_on(action);
        int focused = can_rename && lobby_action == action;
        if (selected || focused) fill(focused ? GOLD : color, x + 2, 46, x + width - 3, 56);
        local_center(x + width / 2 - (can_rename ? 5 : 0), 54, selected || focused ? INK : color, heading);
        if (can_rename) label(x + width - 11, 54, selected || focused ? INK : GOLD, ">");
        for (i = page * LOBBY_ROWS; i < (page + 1) * LOBBY_ROWS && i < roster.count[col]; i++) {
            int index = roster.entry[col][i], y = 58 + 33 * (i % LOBBY_ROWS);
            const LobbyPlayer *p = index < online.nlobby ? &online.lobby[index] : NULL;
            int ours = p && p->id == online.id;
            int roles = netc_lobby_roles(&online, p);
            int cls = ours ? online.want_cls : p ? p->cls : TANK_MEDIUM;
            int portrait = ours ? online.portrait : p ? p->portrait : 0;
            int ping = ours ? online.rtt : p ? p->rtt : 0;
            int ready = ours ? online.ready : p && p->ready;
            int px = (x + 11) & ~7;
            fill(ours ? PANEL : TRACK, x + 2, y, x + width - 3, y + 31);
            if (ours) fill(color, x + 2, y, x + 3, y + 31);
            draw_tank_icon(x + width - 31, y + 1, side == SIDE_FORTRESS, cls);
            if (p) snprintf(text, sizeof text, "%.*s", (width - 10) / 8, p->name);
            else snprintf(text, sizeof text, "BOT %d", index - online.nlobby + 1);
            label(x + 5, y + 8, color, text);
            if (p && portrait) draw_face(px, y + 9, portrait - 1);
            else label(px, y + 24, WHITE, p ? "?" : "AI");
            draw_role_badges(px, y + 24, roles);
            if (p) {
                const char *status = p->playing == 1 ? "PLAY" : p->playing == 2 ? "VIEW" : ready ? "RDY" : "WAIT";
                label(px + 24, y + 19, ready ? GOLD : WHITE, status);
                snprintf(text, sizeof text, "%d", ping > 999 ? 999 : ping);
                label(px + 24, y + 29, ping > 150 ? TEAM_ONE : WHITE, text);
            }
        }
    }

    for (i = LA_READY; i <= LA_LEAVE; i++) {
        int x = 6 + 61 * (i - LA_READY), on = lobby_action_on(i);
        int focus = on && lobby_action == i;
        const char *name;
        local_panel(x, 195, x + 58, 209);
        if (focus) fill(GOLD, x + 2, 197, x + 56, 207);
        if (i == LA_READY) {
            if (online.server_state == SRV_COUNTDOWN && online.countdown > 0) {
                snprintf(text, sizeof text, "IN %d", online.countdown);
                name = text;
            } else if (online.eliminated) name = "OUT";
            else if (online.server_state == SRV_RUNNING && online.joining) name = "JOINING";
            else if (online.server_state == SRV_RUNNING && online.places_free > 0) name = "JOIN";
            else if (netc_lobby_can_start(&online)) name = "START";
            else name = online.ready ? "UNREADY" : "READY";
        } else if (i == LA_WATCH) name = "WATCH";
        else if (i == LA_PAGE && on) {
            snprintf(text, sizeof text, "PG %d/%d", page + 1, roster.pages);
            name = text;
        } else name = i == LA_PAGE ? "PAGE" : i == LA_STATUS ? "STATUS" : "LEAVE";
        local_center(x + 29, 205, focus ? INK : on ? GOLD : 5, name);
    }
    console_panel(6, 211, 313, 251, PANEL);
    {
        const ClientChat *recent[2];
        uint32_t q;
        int count = 0, y = 222;
        for (q = online.chat_seq; q && online.chat_seq - q < 40 && count < 2; q--) {
            const ClientChat *line = &online.chat[q % 40];
            if (line->seq == q && line->text[0]) recent[count++] = line;
        }
        while (count) {
            const ClientChat *line = recent[--count];
            int mention = netc_chat_mentions(&online, line, online.id);
            if (line->from < 0) snprintf(text, sizeof text, "* %s", line->text);
            else snprintf(text, sizeof text, "%s: %s", line->name, line->text);
            text[36] = 0;
            if (mention) fill(TRACK, 10, y - 7, 309, y + 1);
            label(12, y, mention ? GOLD : line->from == online.id ? TEAM_ONE : WHITE, text);
            y += 9;
        }
    }
    {
        int chat_focus = chat_edit || lobby_action == LA_CHAT;
        int key_focus = lobby_action == LA_KEYBOARD;
        fill(chat_focus ? GOLD : TRACK, LOBBY_CHAT_X, LOBBY_CHAT_Y, LOBBY_KEYS_X - 3, 248);
        fill(key_focus ? GOLD : TRACK, LOBBY_KEYS_X, LOBBY_CHAT_Y, LOBBY_CHAT_END, 248);
        size_t n = strlen(chat_line);
        snprintf(text, sizeof text, "> %.24s%s", n ? chat_line + (n > 24 ? n - 24 : 0) :
                 chat_edit ? "" : "CHAT", chat_edit ? "_" : "");
        label(12, 246, chat_focus ? INK : GOLD, text);
        label(LOBBY_KEYS_X + 6, 246, key_focus ? INK : GOLD, "KEYBOARD");
    }

    if (lobby_keyboard) {
        int key, chat = lobby_keyboard == LK_CHAT;
        const char *line = chat ? chat_line : lobby_name;
        size_t n = strlen(line);
        console_panel(18, 62, 301, 198, PANEL);
        label(30, 76, GOLD, chat ? "CHAT" : lobby_keyboard == SIDE_CAMP ? "RENAME CAMP" : "RENAME FORTRESS");
        inset(30, 82, 260, 15, TRACK);
        label(34, 93, WHITE, line + (n > 31 ? n - 31 : 0));
        for (key = 0; key < LK_SAVE; key++) {
            int x = LK_X + LK_W * (key % LK_COLS), y = LK_Y + LK_H * (key / LK_COLS);
            char glyph[] = { lobby_letters[key] == ' ' ? '_' : lobby_letters[key], 0 };
            fill(lobby_key == key ? GOLD : TRACK, x, y, x + LK_W - 3, y + LK_H - 3);
            label(x + 5, y + 11, lobby_key == key ? INK : WHITE, glyph);
        }
        for (key = LK_SAVE; key <= LK_CANCEL; key++) {
            int x = LK_X + LK_BUTTON * (key - LK_SAVE);
            const char *name = key == LK_DELETE ? "DEL" : key == LK_CANCEL ? "CANCEL" : chat ? "SEND" : "SAVE";
            fill(lobby_key == key ? GOLD : TRACK, x, LK_FOOT, x + LK_BUTTON - 4, LK_FOOT + LK_H - 3);
            local_center(x + LK_BUTTON / 2, LK_FOOT + 11, lobby_key == key ? INK : WHITE, name);
        }
    }

    if (online.got_leaders) {
        AmigaProfile *pr = &settings.profile[settings.current];
        for (i = 0; i < online.nleaders; i++)
            if (!strcmp(online.leaders[i].name, pr->name) && online.leaders[i].rating != pr->rating) {
                pr->rating = online.leaders[i].rating;
                save_menu_settings();
            }
    }
}

void online_menu(void)
{
    char text[37];
    int y, i, profiles = profile_menu_active();
    sound_engine(0);                                     /* (the menu: no engine running) */
    show_menu_palette();
    console_backdrop();
    if (menu_page == MENU_PROFILE_DELETE || (!profiles && (paused || (online.socket_open && online.state != NC_LOBBY))))
        console_panel(9, 24, WIDTH - 10, HEIGHT - 25, PANEL);
    if (paused && !profiles) {
        label(20, 42, GOLD, "PAUSED");
        label(20, 62, WHITE, local_playing ? "THE MATCH WAITS" : "THE MATCH GOES ON: YOUR TANK WAITS");
        if (pause_row == 0) fill(INK, 17, 87 - 9, WIDTH - 18, 87 + 3);
        label(22, 87, pause_row == 0 ? GOLD : WHITE, "RESUME");
        if (pause_row == 1) fill(INK, 17, 106 - 9, WIDTH - 18, 106 + 3);
        label(22, 106, pause_row == 1 ? GOLD : WHITE,
              editor_test_running() ? "BACK TO EDITOR" : local_playing ? "QUIT MATCH" : "BACK TO SERVER LOBBY");
        if (orders_available()) {
            int k;
            static const int kinds[5] = { ORDER_ATTACK, ORDER_DEFEND, ORDER_ESCORT, ORDER_REGROUP, ORDER_NONE };
            for (k = 0; k < 5; k++) {
                int y = 125 + k * 17;
                if (pause_row == k + 2) fill(INK, 17, y - 9, WIDTH - 18, y + 3);
                label(22, y, pause_row == k + 2 ? GOLD : WHITE, game_order_name(kinds[k]));
            }
        }
        if (game_orders_allowed(&game) && !online_playing && signed_profile[1] != SIGNED_OUT) {
            snprintf(text, sizeof text, "P%d ORDERS: LEFT/RIGHT PLAYER", order_seat + 1);
            label(20, 202, GOLD, text);
        }
        label(20, 218, WHITE, "FIRE CHOOSE   MINE RESUME");

    } else if (online.socket_open && !profiles && online.state == NC_LOBBY) {
        draw_online_lobby();
    } else if (online.socket_open && !profiles) {
        label(20, 42, GOLD, "OPENFP ONLINE");
        snprintf(text, sizeof text, "%.35s", online.server_name[0] ? online.server_name : connected_address);
        label(20, 69, WHITE, text);
        if (online.state == NC_ENDED) {            /* the result; REMATCH (the tally), LOBBY */
            if (online.slot >= 0 && online.slot < game.num_tanks) record_result(online.winner == (game.tanks[online.slot].team & 1));
            label(20, 98, GOLD, online.winner < 0 ? "MATCH ENDED" :
                  online.slot >= 0 && online.slot < game.num_tanks && online.winner == (game.tanks[online.slot].team & 1) ? "YOU WIN" : "YOU LOSE");
            snprintf(text, sizeof text, "REMATCH  %d OF %d%s", online.rematch_yes, online.rematch_of, online.my_rematch ? "  *" : "");
            menu_row_label(0, 120, text);
            menu_row_label(1, 140, "LOBBY");
        }
        label(20, 184, WHITE, online.state == NC_ENDED ? "ESC LOBBY" : "ESC DISCONNECT");
    } else if (menu_page == MENU_NETWORK) {
        draw_network();
    } else if (menu_edit) {
        local_logo();
        console_panel(8, 68, 311, 168, PANEL);
        label(18, 83, GOLD, editing_team >= 0 ? "RENAME TEAM" : "EDIT PROFILE NAME");
        fill(TRACK, 18, 90, 301, 90);
        inset(15, 105, 290, 19, TRACK);
        label(20, 118, GOLD, edit_text);
        console_panel(8, 177, 311, 235, PANEL);
        label(18, 200, WHITE, "ENTER SAVE   ESC CANCEL");
        if (online_status[0]) label(18, 218, GOLD, online_status);
    } else if (menu_page == MENU_LOCAL || menu_page == MENU_TEAMS) {
        draw_local_setup(menu_page == MENU_TEAMS);
    } else if (menu_page == MENU_REPLAYS) {
        draw_replays();
    } else if (menu_page == MENU_ABOUT) {
        draw_about();
    } else if (menu_page == MENU_MAIN) {
        local_logo();
        local_panel(64, 77, 255, 173);
        for (i = 0; i < MAIN_P1; i++) {
            int y = 94 + i * 17;
            if (i == menu_row) fill(GOLD, 72, y - 9, 247, y + 4);
            label(81, y, i == menu_row ? INK : WHITE, main_items[i]);
        }
        for (i = 0; i < 2; i++) {
            int x = 6 + i * 157, slot = signed_profile[i], selected = menu_row == MAIN_P1 + i;
            console_panel(x, 182, x + 150, 227, PANEL);
            if (slot >= 0) draw_face(x + 2, 192, settings.profile[slot].face);
            snprintf(text, sizeof text, "P%d %.11s", i + 1, slot == SIGNED_OUT ? "" : signed_name(i));
            label(x + 28, 201, GOLD, text);
            fill(selected ? GOLD : INK, x + 27, 209, x + 145, 222);
            label(x + 31, 219, selected ? INK : WHITE, slot == SIGNED_OUT ? "SIGN IN" : "SIGN OUT");
        }
    } else if (menu_page == MENU_SIGNIN) {
        draw_signin();
    } else if (menu_page == MENU_TOOLS || menu_page == MENU_SETTINGS) {
        draw_hub(menu_page == MENU_TOOLS);
    } else if (menu_page == MENU_CHALLENGES) {
        static const char *const names[] = { "TIMED RESCUE", "BRIDGE CAPTURE", "ONE TANK RAID", "BACK" };
        static const char *const hints[] = { "JUNGLE / 3 MIN", "DESERT / 2 MIN", "VOLCANO / 1 LIFE" };
        local_logo(); console_panel(8, 68, 311, 184, PANEL);
        label(18, 83, GOLD, "FIELD CHALLENGES");
        for (i = 0; i < 3; i++) option_row(i, 15, 304, 105 + 25*i, names[i], hints[i]);
        console_panel(8, 193, 311, 236, PANEL);
        option_row(3, 15, 304, 217, names[3], NULL);
    } else if (menu_page == MENU_PLAY) {
        draw_play();
    } else if (menu_page == MENU_PROFILE_EDIT) {
        draw_profile_edit();
    } else if (menu_page == MENU_PROFILE_DELETE) {
        snprintf(text, sizeof text, "DELETE %s?", settings.profile[profile_slot].name);
        label(20, 62, GOLD, text);
        label(20, 88, WHITE, "THE LOCAL PROFILE WILL BE REMOVED.");
        menu_row_label(0, 127, "CANCEL");
        menu_row_label(1, 151, "DELETE PROFILE");
        if (!online_status[0]) local_center(160, 209, WHITE, "ENTER CHOOSE   ESC CANCEL");
    } else if (menu_page == MENU_DIAGNOSTICS) {
        int seat, action;
        local_logo(); console_panel(8, 68, 311, 230, PANEL);
        label(18, 83, GOLD, "KEYS AND JOYSTICK PORTS");
        for (seat = 0; seat < 2; seat++) {
            InputBits bits = player_input(seat);
            snprintf(text, sizeof text, "P%d / PORT %d", seat + 1, (settings.swap_joysticks ? 1 - seat : seat) + 1);
            label(18, 107 + seat * 48, WHITE, text);
            for (action = 0; action < 6; action++) {
                fill(bits & (1u << action) ? GOLD : INK, 20 + action * 45, 115 + seat * 48, 59 + action * 45, 130 + seat * 48);
            }
        }
        label(18, 193, WHITE, "FWD BACK LEFT RIGHT FIRE MINE");
        label(18, 216, GOLD, "BACK");
    } else if (menu_page == MENU_CONTROLS) {
        draw_controls();
    } else if (menu_page == MENU_KEYMAP) {
        draw_keymap();
    } else if (menu_page >= MENU_OPTIONS && menu_page <= MENU_AUDIO) {
        draw_options();
    }
    {
        const char *message = online.socket_open && !profiles ?
            (online.state == NC_LOBBY && !paused ? "" : online.status) :
            (menu_page == MENU_NETWORK || menu_page == MENU_REPLAYS || menu_page == MENU_LOCAL || menu_page == MENU_TEAMS ||
             (menu_page >= MENU_OPTIONS && menu_page <= MENU_AUDIO) || menu_page == MENU_TOOLS || menu_page == MENU_SIGNIN ||
             menu_page == MENU_CONTROLS || menu_page == MENU_KEYMAP ||
             menu_page == MENU_PROFILE_EDIT) ? "" : online_status;
        for (y = online.socket_open && !profiles ? 136 : 213;
             *message && y <= (online.socket_open && !profiles ? 148 : 225); y += 12) {
            size_t n = strlen(message);
            if (n > 35) n = 35;
            memcpy(text, message, n);
            text[n] = 0;
            label(20, y, WHITE, text);
            message += n;
        }
    }
    wait_for_display();
    presentation.BitMap = display_buffers[back_buffer]->sb_BitMap;
    BltBitMapRastPort(frame, 0, 0, &presentation, 0, 0, WIDTH, HEIGHT, 0xc0);
    present_frame();
    online_dirty = 0;
}

static void connect_server(const char *address, int locked)
{
    char host[AMIGA_ADDRESS];
    uint16_t port;
    AmigaProfile *profile;
    if (signed_profile[0] == SIGNED_OUT) {
        open_signin(0, MENU_NETWORK);
        strcpy(online_status, "SIGN IN TO JOIN A SERVER");
        return;
    }
    if (amiga_server_address(address, host, &port)) {
        strcpy(online_status, "Use a host or host:port.");
        return;
    }
    if (locked) {
        strcpy(online_status, "This server requires a password.");
        return;
    }
    strcpy(connected_address, address);
    strcpy(online_status, "Checking game rules and connecting...");
    online_menu();
    profile = &settings.profile[signed_profile[0]];
    if (!profile->has_key) {
        net_random(profile->key, sizeof profile->key);
        profile->has_key = 1;
        save_menu_settings();
    }
    if(profile->session[0]) cloud_register(profile->key,profile->session);
    if (netc_open(&online, host, port, profile->name, "", profile->key,
                  profile->tank < 0 ? TANK_MEDIUM : profile->tank == TANK_RHINO ? TANK_HEAVY : profile->tank,
                  menu_steer) != 0)
        snprintf(online_status, sizeof online_status, "%s", online.status);
    else {
        online.wait_for_place = 1;
        online.portrait = profile->face + 1;
        lobby_page = menu_row = 0;
        online_clear_lobby_focus();
        ModifyIDCMP(window, RAW_EVENTS | IDCMP_MOUSEBUTTONS);
    }
    online_dirty = 1;
}

static void open_server_form(int form, int slot, const char *address)
{
    server_form = form;
    server_slot = slot;
    server_confirm = 0;
    online_status[0] = 0;
    menu_edit = form != SERVER_FORM_DELETE;
    strcpy(edit_text, address);
}

void network_back(void)
{
    if (server_form == SERVER_FORM_NONE) menu_change(MENU_PLAY);
    else {
        server_form = SERVER_FORM_NONE;
        menu_edit = 0;
        online_status[0] = 0;
        online_dirty = 1;
    }
}

static int save_server(int slot, const char *address)
{
    char previous[AMIGA_ADDRESS];
    strcpy(previous, settings.server[slot]);
    strcpy(settings.server[slot], address);
    if (amiga_settings_save(&settings)) {
        strcpy(settings.server[slot], previous);
        strcpy(online_status, "Could not save private servers.");
        return 0;
    }
    browser[slot].state = SERVER_UNCHECKED;
    network_back();
    server_action = SERVER_JOIN;
    return 1;
}

void network_accept(void)
{
    int slot, count;
    if (menu_edit) {
        char host[AMIGA_ADDRESS];
        uint16_t port;
        if (amiga_server_address(edit_text, host, &port)) {
            strcpy(online_status, "Use a host or host:port.");
        } else if (server_form == SERVER_FORM_DIRECT) {
            strcpy(last_direct, edit_text);
            network_back();
            connect_server(last_direct, 0);
        } else if (save_server(server_slot, edit_text)) {
            menu_row = 0;
            while (private_server_slot(menu_row) != server_slot) menu_row++;
        }
    } else if (server_form == SERVER_FORM_DELETE) {
        if (!server_confirm) network_back();
        else save_server(server_slot, "");
    } else if (public_tab) {
        if (menu_row < public_list.count) {
            const AmigaPublicServer *server = &public_list.server[menu_row];
            connect_server(server->address, server->locked);
        } else menu_change(MENU_PLAY);
    } else if ((slot = private_server_slot(menu_row)) >= 0) {
        if (server_action == SERVER_EDIT) open_server_form(SERVER_FORM_EDIT, slot, settings.server[slot]);
        else if (server_action == SERVER_DELETE) open_server_form(SERVER_FORM_DELETE, slot, settings.server[slot]);
        else connect_server(settings.server[slot], 0);
    } else {
        count = private_server_count();
        if (menu_row == count) {
            for (slot = 0; slot < AMIGA_SERVERS && settings.server[slot][0]; slot++) {}
            if (slot < AMIGA_SERVERS) open_server_form(SERVER_FORM_ADD, slot, "");
            else strcpy(online_status, "Delete a saved server to add one.");
        } else if (menu_row == count + 1) open_server_form(SERVER_FORM_DIRECT, -1, last_direct);
        else menu_change(MENU_PLAY);
    }
    online_dirty = 1;
}

void check_server(void)
{
    char host[AMIGA_ADDRESS];
    uint16_t port;
    int slot = private_server_slot(menu_row);
    if (slot < 0 || amiga_server_address(settings.server[slot], host, &port)) {
        strcpy(online_status, "Select a private server to check.");
        return;
    }
    strcpy(online_status, "Checking server...");
    online_menu();
    browser[slot].state = net_query(host, port, &browser[slot].info, 1800,
                                   &browser[slot].ping) == 0 ? SERVER_REACHABLE : SERVER_OFFLINE;
    online_status[0] = 0;
}
