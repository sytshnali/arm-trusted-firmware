/*
 * Copyright (c) 2026, Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PMU_DVFS_H
#define PMU_DVFS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * PMU DVFS framework:
 * - Uses one PMU event counter + cycle counter to estimate per-core load.
 * - Intended to be called from platform EL3 PPI interrupt handlers.
 */

int pmu_dvfs_global_init(void);
void pmu_dvfs_core_init(void);
void pmu_dvfs_handle_ppi(void);

void pmu_dvfs_set_thresholds(uint16_t low_permille, uint16_t high_permille);
void pmu_dvfs_set_event(uint16_t event_id);

uint16_t pmu_dvfs_get_last_load(unsigned int core_pos);
uint8_t pmu_dvfs_get_last_opp(unsigned int core_pos);

/*
 * Platform hook to apply a target OPP for one core.
 * Default weak implementation only logs decisions.
 */
void plat_pmu_dvfs_apply_opp(unsigned int core_pos, uint8_t target_opp,
			    uint16_t load_permille);

#endif /* PMU_DVFS_H */
