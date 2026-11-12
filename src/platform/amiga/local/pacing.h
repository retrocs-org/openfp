/* SPDX-License-Identifier: MIT */
#ifndef OFP_AMIGA_PACING_H
#define OFP_AMIGA_PACING_H

#include <stdint.h>

static inline uint32_t frame_deadline(uint32_t due, uint32_t start, uint32_t end, uint32_t period)
{
    /* Stay on the original cadence while on time. After an overrun, let the
     * next iteration catch up the simulation and render without an extra wait. */
    uint32_t next = start + period - (start - due) % period;
    return (int32_t)(end - next) >= 0 ? end : next;
}
#endif
