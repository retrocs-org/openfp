/* SPDX-License-Identifier: MIT */
#ifndef OFP_AMIGA_PUBLIC_SERVERS_H
#define OFP_AMIGA_PUBLIC_SERVERS_H

#include "platform/amiga/menu_settings.h"

enum { AMIGA_PUBLIC_SERVERS = 24 };

typedef struct {
    char address[AMIGA_ADDRESS];
    char name[35];
    unsigned players, capacity;
    uint32_t build;
    int running, locked;
} AmigaPublicServer;

typedef struct {
    AmigaPublicServer server[AMIGA_PUBLIC_SERVERS];
    char reply[40], script[40], done[40];
    uint32_t started;
    int count, pending;
} AmigaPublicList;

struct WBStartup;
void amiga_worker_workbench(const struct WBStartup *startup);
int amiga_public_worker(void);
int amiga_public_start(AmigaPublicList *list);
/* Returns 0 while pending, 1 on success, and -1 if the fetch failed. */
int amiga_public_poll(AmigaPublicList *list);
/* the map editor's F8: MY MAP (path) to the web site's gallery, by author (a job like the list's:
 * started in the background, polled: 0 on its way, 1 shared, -2 ten a day already, -1 failed) */
int amiga_share_worker(const char *path, const char *author);
int amiga_share_start(AmigaPublicList *job, const char *path, const char *author);
int amiga_share_poll(AmigaPublicList *job);
#endif
