/* SPDX-License-Identifier: MIT */
#ifndef OFP_FX_BUDGET_H
#define OFP_FX_BUDGET_H
#include <stdint.h>
extern unsigned fx_budget_quality;
void fx_budget_frame(unsigned cost_ms, unsigned target_ms, int enabled);
typedef struct { unsigned quality, slow_ms, fast_ms; } FxBudget;
/* Render cost only, excluding frame limiter waits. No game state or RNG. */
static inline unsigned fx_budget_step(FxBudget *b, unsigned cost_ms, unsigned target_ms, int enabled) {
    if (!enabled) { b->quality = 0; b->slow_ms = b->fast_ms = 0; return 0; }
    if (!target_ms || cost_ms > 500) return b->quality;
    if (cost_ms > target_ms) {
        b->fast_ms = 0; b->slow_ms += cost_ms;
        if (b->slow_ms >= 2000) { if (b->quality < 2) b->quality++; b->slow_ms = 0; }
    } else if (cost_ms * 4 < target_ms * 3) {
        b->slow_ms = 0; b->fast_ms += target_ms;
        if (b->fast_ms >= 10000) { if (b->quality) b->quality--; b->fast_ms = 0; }
    } else b->slow_ms = b->fast_ms = 0;
    return b->quality; /* 0 full; 1 fewer particles; 2 minimal decoration */
}
#endif
