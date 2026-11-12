/* SPDX-License-Identifier: MIT */
#include "common/fx_budget.h"
unsigned fx_budget_quality;
static FxBudget budget;
void fx_budget_frame(unsigned cost_ms, unsigned target_ms, int enabled) {
    fx_budget_quality = fx_budget_step(&budget, cost_ms, target_ms, enabled);
}
