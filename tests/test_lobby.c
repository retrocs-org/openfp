/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/client.h"

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)

static Server server;
static NetClient pilots[3];

static void pump(uint32_t ms)
{
    uint32_t start = net_ms();
    do {
        int i;
        server_step(&server);
        for (i = 0; i < 3; i++) if (pilots[i].socket_open) netc_step(&pilots[i]);
        net_sleep(2);
    } while (net_ms() - start < ms);
}

static void connect_pilot(int index, const char *name)
{
    int i;
    NetClient *p = &pilots[index];
    CHECK(netc_open(p, "127.0.0.1", net_port(server.sock), name, "", NULL, TANK_MEDIUM, 0) == 0);
    for (i = 0; i < 30 && !p->lobby_social; i++) pump(100);
    CHECK(p->state == NC_LOBBY && p->lobby_social && p->manual_start);
}

static const ClientChat *heard(const NetClient *p, const char *text)
{
    uint32_t q;
    for (q = p->chat_seq; q && p->chat_seq - q < 40; q--) {
        const ClientChat *line = &p->chat[q % 40];
        if (line->seq == q && !strcmp(line->text, text)) return line;
    }
    return NULL;
}

int main(void)
{
    NetClient *alice = &pilots[0], *bob = &pilots[1], *eve = &pilots[2];
    ServerConfig cfg;
    const ClientChat *slap;
    char text[NET_CHAT];
    ClientChat mention = {0};

    server_default_config(&cfg);
    cfg.manual_start = 1;
    cfg.start_after_ms = 100;
    cfg.countdown_ms = 650;
    cfg.fill_bots = 1;
    strcpy(cfg.admin_password, "local-test-password");
    CHECK(server_open(&server, 0, &cfg, NULL) == 0);
    connect_pilot(0, "Alice");
    connect_pilot(1, "Bob Smith");
    connect_pilot(2, "Eve");
    bob->portrait = 7;
    pump(350);
    CHECK(alice->lobby_host == alice->id && bob->lobby_host == alice->id);
    CHECK(netc_lobby_can_start(alice) && !netc_lobby_can_start(bob));
    CHECK(alice->match_capacity == 8 && netc_lobby_bots(alice) == 5);

    /* Team leadership follows side selection order, independently of lobby hosting. */
    netc_side(bob, SIDE_CAMP);
    pump(80);
    netc_side(alice, SIDE_CAMP);
    netc_side(eve, SIDE_FORTRESS);
    pump(350);
    CHECK(alice->team_leader[0] == bob->id && alice->team_leader[1] == eve->id);
    CHECK(netc_lobby_can_rename(bob) && netc_lobby_can_rename(alice));
    CHECK(netc_lobby_can_rename_side(alice, SIDE_FORTRESS) && !netc_lobby_can_rename_side(eve, SIDE_CAMP));
    netc_chat(eve, "/teamname #1 Impostors");
    netc_lobby_rename(bob, SIDE_CAMP, "Copper Crew");
    netc_lobby_rename(alice, SIDE_FORTRESS, "Iron Watch");
    pump(600);
    CHECK(!strcmp(netc_lobby_team_name(alice, SIDE_CAMP), "Copper Crew"));
    CHECK(!strcmp(netc_lobby_team_name(alice, SIDE_FORTRESS), "Iron Watch"));
    CHECK(heard(eve, "only the team leader, lobby host or an admin can rename this team"));
    CHECK(!heard(alice, "only the team leader, lobby host or an admin can rename this team"));
    {
        LobbyRoster roster;
        netc_lobby_roster(alice, &roster);
        CHECK(roster.columns == 3 && roster.count[0] == 4 && roster.count[2] == 4 && !roster.count[1]);
        CHECK(alice->lobby[roster.entry[2][0]].id == eve->id);
        CHECK(alice->lobby[roster.entry[0][1]].portrait == 7);
    }
    netc_chat(eve, "/login local-test-password");
    for (int i = 0; i < 20 && !(eve->lobby_admin & LOBBY_ADMIN); i++) pump(100);
    CHECK(eve->lobby_admin & LOBBY_ADMIN);
    CHECK(netc_lobby_can_rename_side(eve, SIDE_CAMP));
    pump(350);
    CHECK(netc_lobby_roles(alice, &alice->lobby[0]) == ROLE_HOST);
    CHECK(netc_lobby_roles(alice, &alice->lobby[1]) == ROLE_LEADER);
    CHECK(netc_lobby_roles(alice, &alice->lobby[2]) == (ROLE_LEADER | ROLE_ADMIN));
    CHECK(netc_lobby_roles(alice, NULL) == 0);

    strcpy(text, "bo");
    netc_chat_complete(alice, text, sizeof text);
    CHECK(!strcmp(text, "Bob Smith: "));
    strcpy(text, "hello @bob s");
    netc_chat_complete(alice, text, sizeof text);
    CHECK(!strcmp(text, "hello @Bob Smith "));
    strcpy(text, "/slap bo");
    netc_chat_complete(alice, text, sizeof text);
    CHECK(!strcmp(text, "/slap Bob Smith"));
    netc_chat(alice, text);
    pump(550);
    slap = heard(bob, "Alice slaps @Bob Smith with a rubber tank");
    CHECK(slap && netc_chat_mentions(bob, slap, bob->id) && !netc_chat_mentions(eve, slap, eve->id));
    netc_chat(eve, "/slap Bob Smith");
    pump(550);
    CHECK(heard(eve, "wait a few seconds between slaps"));
    mention.from = bob->id;
    strcpy(mention.text, "ALICE: ready?");
    CHECK(netc_chat_mentions(alice, &mention, alice->id));
    strcpy(mention.text, "@Alice2 and alice@example.com");
    CHECK(!netc_chat_mentions(alice, &mention, alice->id));

    netc_chat(bob, "/start");
    netc_chat(alice, "/start");
    pump(550);
    CHECK(server.state == SRV_LOBBY);
    CHECK(heard(bob, "only the lobby host or an admin can start the match"));
    CHECK(heard(alice, "waiting for the other humans to ready up and for enough players"));
    netc_ready(alice, 1, alice->want_cls);
    netc_ready(bob, 1, bob->want_cls);
    netc_ready(eve, 1, eve->want_cls);
    pump(900);
    CHECK(server.state == SRV_LOBBY);
    netc_chat(alice, "/start");
    pump(200);
    CHECK(server.state == SRV_COUNTDOWN);
    netc_ready(eve, 0, eve->want_cls);
    pump(500);
    CHECK(server.state == SRV_LOBBY);

    netc_close(alice);
    pump(550);
    CHECK(bob->lobby_host == bob->id && eve->lobby_host == bob->id);
    CHECK(netc_lobby_bots(bob) == 6);
    netc_side(bob, SIDE_FORTRESS);
    pump(350);
    CHECK(bob->team_leader[0] == 0 && bob->team_leader[1] == eve->id);
    CHECK(!strcmp(netc_lobby_team_name(bob, SIDE_CAMP), "Copper Crew"));
    netc_ready(bob, 0, bob->want_cls);
    netc_ready(eve, 1, eve->want_cls);
    pump(350);
    netc_chat(bob, "/start");
    for (int i = 0; i < 40 && (bob->state != NC_RUNNING || eve->state != NC_RUNNING); i++) pump(100);
    CHECK(server.state == SRV_RUNNING && server.ms.players == 8);
    CHECK(bob->state == NC_RUNNING && eve->state == NC_RUNNING);
    {
        int tank = bob->slot, id = bob->id;
        uint32_t assignment = bob->assignment;
        CHECK(assignment && tank >= 0 && tank < MATCH_PLAYERS);
        netc_leave_match(bob);
        pump(600);
        CHECK(bob->state == NC_LOBBY && bob->id == id && bob->lobby_host == id && !bob->leaving);
        CHECK(eve->state == NC_RUNNING && server.in_match[tank] == -1 && bob->places_free == 1);
        netc_join(bob, 1);
        for (int i = 0; i < 30 && bob->state != NC_RUNNING; i++) pump(100);
        CHECK(bob->state == NC_RUNNING && bob->slot == tank && bob->assignment != assignment);

        server.game.tanks[tank].lives = 0;
        server.game.tanks[tank].out = 1;
        netc_leave_match(bob);
        pump(600);
        CHECK(bob->state == NC_LOBBY && bob->eliminated && bob->places_free == 0);
        CHECK(eve->state == NC_RUNNING);
        bob->eliminated = 0;   /* the server must refuse even if the client asks anyway */
        netc_join(bob, 1);
        pump(500);
        CHECK(bob->state == NC_LOBBY && bob->eliminated && server.in_match[tank] == -1);
        netc_watch(bob, 1);
        for (int i = 0; i < 30 && bob->state != NC_RUNNING; i++) pump(100);
        CHECK(bob->state == NC_RUNNING && netc_spectating(bob));
        netc_leave_match(bob);
        pump(400);
        CHECK(bob->state == NC_LOBBY && bob->eliminated);
    }
    netc_close(bob);
    netc_close(eve);
    server_close(&server);
    puts("lobby leadership, bots, chat and manual starts OK");
    return 0;
}
