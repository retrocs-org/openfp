/* SPDX-License-Identifier: MIT */
#ifndef OFP_VOLUME_H
#define OFP_VOLUME_H

enum { VOLUME_DISABLED = 0, VOLUME_MAX = 10 };

static inline int volume_clamp(int level)
{
    if (level < VOLUME_DISABLED) return VOLUME_DISABLED;
    if (level > VOLUME_MAX) return VOLUME_MAX;
    return level;
}

static inline int volume_step(int level, int direction)
{
    return volume_clamp(level + direction);
}

#endif
