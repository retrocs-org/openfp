/* SPDX-License-Identifier: MIT */
#ifndef OFP_AMIGA_PROFILE_H
#define OFP_AMIGA_PROFILE_H
#include <stdint.h>
typedef struct {
    int enabled;
    uint32_t terrain_us, objects_us, hud_us, wait_us, copy_us;
} AmigaFrameProfile;
extern AmigaFrameProfile frame_profile;
/* 0 automatic, 1 graphics.library, 2 blitter, 3 CPU (benchmark comparison). */
extern int frame_copy_method;
uint32_t amiga_us(void);
#endif
