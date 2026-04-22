/*
 * Copyright (c) 2026, Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include <arch_helpers.h>
#include <bl31/pmu_dvfs.h>
#include <common/debug.h>
#include <lib/utils.h>
#include <platform_def.h>
#include <plat/common/platform.h>

#define PMU_DVFS_EVENT_COUNTER_IDX	U(0)
#define PMU_DVFS_DEFAULT_EVENT_ID	U(0x19) /* BUS_ACCESS */

#define PMU_DVFS_DEFAULT_LOW_PERMILLE	U(250)
#define PMU_DVFS_DEFAULT_HIGH_PERMILLE	U(700)

#define PMU_DVFS_OPP_LOW		U(0)
#define PMU_DVFS_OPP_NOMINAL		U(1)
#define PMU_DVFS_OPP_HIGH		U(2)

#define PMU_PMCR_E			BIT_32(0)
#define PMU_PMCR_P			BIT_32(1)
#define PMU_PMCR_C			BIT_32(2)

#define PMU_PMCNTEN_C			BIT_32(31)
#define PMU_PMCNTEN_EVT(_n)		BIT_32(_n)

#define PMU_PMI_BIT_CYCLE		BIT_64(31)
#define PMU_PMI_BIT_EVT(_n)		BIT_64(_n)

struct pmu_dvfs_core_state {
	uint64_t last_cycle;
	uint64_t last_event;
	uint16_t load_permille;
	uint8_t current_opp;
	bool initialized;
};

static struct pmu_dvfs_core_state pmu_dvfs_state[PLATFORM_CORE_COUNT];

static uint16_t pmu_dvfs_low_threshold = PMU_DVFS_DEFAULT_LOW_PERMILLE;
static uint16_t pmu_dvfs_high_threshold = PMU_DVFS_DEFAULT_HIGH_PERMILLE;
static uint16_t pmu_dvfs_event_id = PMU_DVFS_DEFAULT_EVENT_ID;

static inline uint64_t read_pmxevcntr_el0(void)
{
	uint64_t v;

	__asm__ volatile ("mrs %0, pmxevcntr_el0" : "=r" (v));
	return v;
}

static inline void write_pmxevcntr_el0(uint64_t v)
{
	__asm__ volatile ("msr pmxevcntr_el0, %0" : : "r" (v));
}

static inline void write_pmxevtyper_el0(uint64_t v)
{
	__asm__ volatile ("msr pmxevtyper_el0, %0" : : "r" (v));
}

static inline uint64_t read_pmccntr_el0_local(void)
{
	uint64_t v;

	__asm__ volatile ("mrs %0, pmccntr_el0" : "=r" (v));
	return v;
}

static inline void write_pmccntr_el0_local(uint64_t v)
{
	__asm__ volatile ("msr pmccntr_el0, %0" : : "r" (v));
}

static inline void write_pmcntenset_el0_local(uint64_t v)
{
	__asm__ volatile ("msr pmcntenset_el0, %0" : : "r" (v));
}

static inline void write_pmintenclr_el1(uint64_t v)
{
	__asm__ volatile ("msr pmintenclr_el1, %0" : : "r" (v));
}

static inline void write_pmintenset_el1(uint64_t v)
{
	__asm__ volatile ("msr pmintenset_el1, %0" : : "r" (v));
}

static inline uint64_t read_pmovsclr_el0(void)
{
	uint64_t v;

	__asm__ volatile ("mrs %0, pmovsclr_el0" : "=r" (v));
	return v;
}

static inline void write_pmovsclr_el0(uint64_t v)
{
	__asm__ volatile ("msr pmovsclr_el0, %0" : : "r" (v));
}

static inline void write_pmselr_el0(uint64_t v)
{
	__asm__ volatile ("msr pmselr_el0, %0" : : "r" (v));
	isb();
}

static inline uint8_t pmu_dvfs_pick_opp(uint16_t load, uint8_t current)
{
	if (load >= pmu_dvfs_high_threshold) {
		return PMU_DVFS_OPP_HIGH;
	}

	if (load <= pmu_dvfs_low_threshold) {
		return PMU_DVFS_OPP_LOW;
	}

	if (current == PMU_DVFS_OPP_HIGH && load > pmu_dvfs_low_threshold) {
		return PMU_DVFS_OPP_NOMINAL;
	}

	if (current == PMU_DVFS_OPP_LOW && load < pmu_dvfs_high_threshold) {
		return PMU_DVFS_OPP_NOMINAL;
	}

	return current;
}

int pmu_dvfs_global_init(void)
{
	if (pmu_dvfs_low_threshold >= pmu_dvfs_high_threshold ||
	    pmu_dvfs_high_threshold > U(1000)) {
		ERROR("PMU DVFS: invalid thresholds (%u/%u)\n",
		      pmu_dvfs_low_threshold, pmu_dvfs_high_threshold);
		return -1;
	}

	NOTICE("PMU DVFS: enabled (event=0x%x, low=%u, high=%u)\n",
	       pmu_dvfs_event_id, pmu_dvfs_low_threshold,
	       pmu_dvfs_high_threshold);
	return 0;
}

void pmu_dvfs_core_init(void)
{
	unsigned int core_pos = plat_my_core_pos();
	struct pmu_dvfs_core_state *st;
	uint32_t pmcr;
	uint64_t mask;

	if (core_pos >= PLATFORM_CORE_COUNT) {
		return;
	}

	st = &pmu_dvfs_state[core_pos];

	pmcr = read_pmcr_el0();
	pmcr |= PMU_PMCR_E | PMU_PMCR_C | PMU_PMCR_P;
	write_pmcr_el0(pmcr);
	isb();

	write_pmselr_el0(PMU_DVFS_EVENT_COUNTER_IDX);
	write_pmxevtyper_el0((uint64_t)pmu_dvfs_event_id);
	write_pmxevcntr_el0(U(0));
	write_pmccntr_el0_local(U(0));

	mask = PMU_PMI_BIT_CYCLE | PMU_PMI_BIT_EVT(PMU_DVFS_EVENT_COUNTER_IDX);
	write_pmovsclr_el0(mask);

	write_pmintenclr_el1(~ULL(0));
	write_pmintenset_el1(mask);
	write_pmcntenset_el0_local((uint64_t)(PMU_PMCNTEN_C |
				      PMU_PMCNTEN_EVT(PMU_DVFS_EVENT_COUNTER_IDX)));
	isb();

	st->last_cycle = read_pmccntr_el0_local();
	write_pmselr_el0(PMU_DVFS_EVENT_COUNTER_IDX);
	st->last_event = read_pmxevcntr_el0();
	st->load_permille = 0U;
	st->current_opp = PMU_DVFS_OPP_NOMINAL;
	st->initialized = true;
}

void pmu_dvfs_handle_ppi(void)
{
	unsigned int core_pos = plat_my_core_pos();
	struct pmu_dvfs_core_state *st;
	uint64_t now_cycle, now_event;
	uint64_t d_cycle, d_event;
	uint8_t next_opp;
	uint64_t ovf;

	if (core_pos >= PLATFORM_CORE_COUNT) {
		return;
	}

	st = &pmu_dvfs_state[core_pos];
	if (!st->initialized) {
		pmu_dvfs_core_init();
	}

	ovf = read_pmovsclr_el0();
	write_pmovsclr_el0(ovf);

	now_cycle = read_pmccntr_el0_local();
	write_pmselr_el0(PMU_DVFS_EVENT_COUNTER_IDX);
	now_event = read_pmxevcntr_el0();

	d_cycle = now_cycle - st->last_cycle;
	d_event = now_event - st->last_event;
	st->last_cycle = now_cycle;
	st->last_event = now_event;

	if (d_cycle == U(0)) {
		return;
	}

	if (d_event > d_cycle) {
		d_event = d_cycle;
	}

	st->load_permille = (uint16_t)((d_event * U(1000)) / d_cycle);
	next_opp = pmu_dvfs_pick_opp(st->load_permille, st->current_opp);

	if (next_opp != st->current_opp) {
		st->current_opp = next_opp;
		plat_pmu_dvfs_apply_opp(core_pos, next_opp, st->load_permille);
	}
}

void pmu_dvfs_set_thresholds(uint16_t low_permille, uint16_t high_permille)
{
	assert(low_permille < high_permille);
	assert(high_permille <= U(1000));

	pmu_dvfs_low_threshold = low_permille;
	pmu_dvfs_high_threshold = high_permille;
}

void pmu_dvfs_set_event(uint16_t event_id)
{
	pmu_dvfs_event_id = event_id;
}

uint16_t pmu_dvfs_get_last_load(unsigned int core_pos)
{
	if (core_pos >= PLATFORM_CORE_COUNT) {
		return 0U;
	}

	return pmu_dvfs_state[core_pos].load_permille;
}

uint8_t pmu_dvfs_get_last_opp(unsigned int core_pos)
{
	if (core_pos >= PLATFORM_CORE_COUNT) {
		return PMU_DVFS_OPP_NOMINAL;
	}

	return pmu_dvfs_state[core_pos].current_opp;
}

#pragma weak plat_pmu_dvfs_apply_opp
void plat_pmu_dvfs_apply_opp(unsigned int core_pos, uint8_t target_opp,
			    uint16_t load_permille)
{
	INFO("PMU DVFS: core=%u opp=%u load=%u/1000\n",
	     core_pos, target_opp, load_permille);
}
