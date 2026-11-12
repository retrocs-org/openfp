/* SPDX-License-Identifier: MIT
 * Fixed-point math for the deterministic simulation.
 * No floating point is allowed anywhere in src/core: identical inputs must
 * give bit-identical results on every platform (needed for lockstep netplay
 * and cheap on a 68020 without an FPU). */
#ifndef OFP_FIXED_H
#define OFP_FIXED_H

#include <stdint.h>

typedef int32_t fx;                 /* Q16.16 */

#define FX_SHIFT 16
#define FX_ONE   ((fx)1 << FX_SHIFT)
#define FX(i)    ((fx)(i) * FX_ONE)
/* compile-time fraction: FX_FRAC(3, 2) == 1.5 */
#define FX_FRAC(num, den) ((fx)(((int64_t)(num) * FX_ONE) / (den)))

static inline int fx_to_int(fx a) { return (int)(a >> FX_SHIFT); }   /* floor */
static inline fx  fx_mul(fx a, fx b) { return (fx)(((int64_t)a * (int64_t)b) >> FX_SHIFT); }
static inline fx  fx_abs(fx a) { return a < 0 ? -a : a; }

/* Linear blend for render interpolation, alpha in 0..256 */
static inline fx fx_lerp(fx a, fx b, int alpha256)
{
    return a + (fx)(((int64_t)(b - a) * alpha256) >> 8);
}

/* Angles: 16 bits = one full turn, 0 = north, increasing clockwise.
 * The high byte indexes a 256-entry table. */
typedef uint16_t angle16;
#define ANGLE_NORTH ((angle16)0x0000)
#define ANGLE_EAST  ((angle16)0x4000)
#define ANGLE_SOUTH ((angle16)0x8000)
#define ANGLE_WEST  ((angle16)0xC000)

fx fx_sin(angle16 a);
fx fx_cos(angle16 a);
/* direction of the vector (dx east, dy south) as angle16; integer only */
angle16 fx_atan2(fx dx, fx dy);

#endif
