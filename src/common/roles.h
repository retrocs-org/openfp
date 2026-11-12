/* SPDX-License-Identifier: MIT */
#ifndef OFP_ROLES_H
#define OFP_ROLES_H

enum { ROLE_LEADER = 1, ROLE_HOST = 2, ROLE_ADMIN = 4 };
enum { ROLE_BADGE_SIZE = 7, ROLE_BADGE_STEP = 8, ROLE_BADGE_COUNT = 3 };

/* Five rows of five pixels. The cap, flag and shield share a dark tile. */
static inline unsigned role_badge_row(int badge, int row)
{
    static const unsigned char glyph[ROLE_BADGE_COUNT][5] = {
        { 0x0e, 0x0e, 0x1f, 0x1f, 0x0c },
        { 0x1f, 0x17, 0x1f, 0x10, 0x10 },
        { 0x1f, 0x15, 0x1b, 0x0e, 0x04 }
    };
    return glyph[badge][row];
}

#endif
