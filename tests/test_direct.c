/* SPDX-License-Identifier: MIT
 * DIRECT CONNECT: the window opens, wants an address, joins, saves to MY SERVERS (the real menu code). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "platform/sdl2/menu.h"
#include "platform/sdl2/ui.h"
#include "platform/sdl2/select.h"
static Style st[1]; static Settings s; static Menu m; static MenuEnv e;
int main(void) {
    int ok = 1, r;
    snprintf(st[0].name, 64, "desert"); e.styles = st; e.num_styles = 1;
    settings_defaults(&s); s.server[0] = 0; s.n_mine = 0;
    menu_reset(&m); m.page = PAGE_NET; m.row = 3; menu_update(&m, &s, &e, 0, 0, 0, 0);   /* DIRECT CONNECT */
    menu_update(&m, &s, &e, 0, 0, 0, 1);
    printf("DIRECT CONNECT: %s\n", m.page == PAGE_DIRECT ? "the window" : "NOT OPEN"); ok &= m.page == PAGE_DIRECT;
    m.row = 3; r = menu_update(&m, &s, &e, 0, 0, 0, 1);              /* CONNECT, no address */
    printf("CONNECT with no address: %s\n", r != MENU_JOIN && m.row == 0 ? "back to the address" : "JOINED (wrong)"); ok &= r != MENU_JOIN && m.row == 0;
    snprintf(s.server, sizeof s.server, "games.example.org:29870"); snprintf(s.server_password, sizeof s.server_password, "sesame");
    m.row = 2; menu_update(&m, &s, &e, 0, 0, 0, 1);                  /* SAVE TO MY SERVERS: yes */
    m.row = 3; r = menu_update(&m, &s, &e, 0, 0, 0, 1);              /* CONNECT */
    printf("CONNECT: %s, MY SERVERS %d (%s, password %s)\n", r == MENU_JOIN ? "joins" : "DOESN'T", s.n_mine, s.n_mine ? s.mine[0] : "-", s.n_mine ? s.mine_pw[0] : "-");
    ok &= r == MENU_JOIN && s.n_mine == 1 && !strcmp(s.mine[0], "games.example.org:29870") && !strcmp(s.mine_pw[0], "sesame");
    m.page = PAGE_DIRECT; m.row = 0; menu_update(&m, &s, &e, 0, 0, 1, 0);   /* back */
    printf("back in the window: %s\n", m.page == PAGE_NET ? "NETWORK GAME" : "STAYS (wrong)"); ok &= m.page == PAGE_NET;
    m.page = PAGE_TOOLS; m.row = 4;
    menu_update(&m, &s, &e, 0, 0, 0, 1);
    ok &= m.page == PAGE_CONTROLLERS;
    m.row = 1; m.prev = 0;
    menu_update(&m, &s, &e, IN_RIGHT, 0, 0, 0);
    ok &= s.stick_deadzone == 38;
    m.row = 4; menu_update(&m, &s, &e, 0, 0, 0, 1);
    ok &= s.stick_deadzone == 37;
    menu_update(&m, &s, &e, 0, 0, 1, 0);
    ok &= m.page == PAGE_TOOLS;
    if (getenv("OFP_MENU_CAPTURE_DIR")) {
        Theme theme = {0}; SDL_Surface *surface; SDL_Renderer *renderer; int page;
        SDL_Init(SDL_INIT_VIDEO);
        surface = SDL_CreateRGBSurfaceWithFormat(0, 384, 216, 32, SDL_PIXELFORMAT_ARGB8888);
        renderer = SDL_CreateSoftwareRenderer(surface);
        if (renderer && !theme_load(renderer, &theme, "default")) {
            ui_init(renderer); ui_set_theme(&theme);
            for (page = 0; page < 2; page++) {
                char path[1200];
                m.page = page ? PAGE_GRAPHICS : PAGE_CONTROLLERS; m.row = page ? 7 : 2;
                menu_draw(renderer, &theme, &m, &s, &e, 384, 216);
                SDL_RenderPresent(renderer);
                snprintf(path, sizeof path, "%s/%s.bmp", getenv("OFP_MENU_CAPTURE_DIR"), page ? "graphics" : "controllers");
                ok &= SDL_SaveBMP(surface, path) == 0;
            }
            ui_shutdown(); ui_set_theme(NULL); theme_free(&theme);
        } else ok = 0;
        SDL_DestroyRenderer(renderer); SDL_FreeSurface(surface); SDL_Quit();
    }
    printf(ok ? "direct: ok\n" : "direct: FAILED\n");
    return !ok;
}
