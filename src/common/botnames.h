/* SPDX-License-Identifier: MIT
 * The AI bots' names: comical ones, made up for the game. A match's are a shuffle of the list
 * by its seed, so every bot in it has a name of its own, and the same match names them the
 * same everywhere (the team screen, the report, the server's records). */
#ifndef OFP_BOTNAMES_H
#define OFP_BOTNAMES_H
#include <stdint.h>

const char *bot_name(uint32_t seed, int k);   /* the k-th bot's (0, 1, ...; no two alike up to the list's length) */

#endif
