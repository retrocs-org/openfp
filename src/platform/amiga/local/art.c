/* SPDX-License-Identifier: MIT
 * src/platform/amiga/local/art.c: the art the client draws (from tools/amiga/gen_*.py), its one home. The
 * generated headers declare their arrays static (each meant for one file): here they are made the
 * program's, and internal.h's declarations of them (included last) are checked against them. */
#include <stdint.h>
#include "core/match.h"
#define static                                       /* (the generated arrays: external, defined here once) */
#include "platform/amiga/classic_art.h"
#ifdef OFP_AMIGA_ONLINE
#include "platform/amiga/vehicle_art.h"
#endif
#include "platform/amiga/world_art.h"
#include "platform/amiga/faces_art.h"
#include "platform/amiga/classic_maps.h"
#ifdef OFP_AMIGA_ONLINE
#include "platform/amiga/map_catalog.h"
#ifndef OFP_AMIGA_AGA
#include "platform/amiga/original_terrain.h"
#endif
#include "../../../../third_party/font8x8_basic.h"   /* (from src/platform/amiga/local/) */
#endif
#undef static
#include "platform/amiga/local/internal.h"

#ifdef OFP_AMIGA_ONLINE
const int catalog_map_count = sizeof catalog_maps / sizeof catalog_maps[0];
#endif
