/* SPDX-License-Identifier: MIT
 * The daily challenge: one battlefield a day (UTC), the same for everyone, alone against
 * the computer. Everything follows from the date; the web site works it out the same
 * way (public-website/inc/daily.php) to check the replays it's sent. */
#ifndef OFP_DAILY_H
#define OFP_DAILY_H

#include <stdint.h>
#include "core/match.h"

uint32_t daily_today(void);                         /* YYYYMMDD, UTC */
void     daily_setup(uint32_t day, MatchSetup *ms); /* that day's match */

#endif
