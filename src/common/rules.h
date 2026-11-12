/* SPDX-License-Identifier: MIT
 * The rules' fingerprint: what this build's game actually computes, measured. A few short
 * fixed matches are played (computer players, a storm, pickups and the hill, versus, a map
 * file) and their checksums make the fingerprint. Two builds that would get out of step
 * with each other (different rules, or a compiler that computes differently) have
 * different ones; a comment changed, or a build on another machine, doesn't change it.
 * Games and servers compare it when a game joins. */
#ifndef OFP_RULES_H
#define OFP_RULES_H

#include <stdint.h>

uint32_t net_rules(void);              /* computed once per process, never 0 */

#endif
