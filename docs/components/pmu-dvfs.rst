PMU transaction-based DVFS helper (BL31)
=======================================

This component adds a lightweight framework in BL31 to estimate per-core load
from PMU counters and map load into Operating Performance Point (OPP) levels.
It is intended for large CPU-count systems (for example, 128 cores) where the
platform wants to trigger DVFS decisions from PMU overflow PPIs.

Overview
--------

The helper uses:

- ``PMCCNTR_EL0`` as a time base.
- One programmable event counter (default: counter 0).
- A PMU overflow PPI as the sampling trigger.

On each PMU overflow interrupt, BL31 computes:

.. code-block:: text

   load = delta(event_counter) / delta(cycle_counter)

Then ``load`` is converted to a permille value (0..1000) and compared against
thresholds:

- ``low``: load <= low  -> low OPP
- ``high``: load >= high -> high OPP
- between low/high -> nominal OPP (with simple hysteresis)

Build-time enable
-----------------

Enable this feature during build:

.. code-block:: bash

   make ... ENABLE_PMU_DVFS=1

Public API
----------

Header: ``include/bl31/pmu_dvfs.h``.

- ``pmu_dvfs_global_init()``
- ``pmu_dvfs_core_init()``
- ``pmu_dvfs_handle_ppi()``
- ``pmu_dvfs_set_thresholds(low, high)``
- ``pmu_dvfs_set_event(event_id)``
- ``pmu_dvfs_get_last_load(core_pos)``
- ``pmu_dvfs_get_last_opp(core_pos)``

Platform integration steps
--------------------------

1. Build with ``ENABLE_PMU_DVFS=1``.
2. In your platform BL31 setup, keep PMU interrupt routed to EL3.
3. Call ``pmu_dvfs_core_init()`` on each CPU bring-up path.
4. In the EL3 interrupt path, when PMU PPI is received, call
   ``pmu_dvfs_handle_ppi()``.
5. Implement ``plat_pmu_dvfs_apply_opp(core_pos, target_opp, load)`` in your
   platform to program clock/voltage (SCMI, mailbox, or direct clock driver).

Default platform hook behavior
------------------------------

If the platform does not override ``plat_pmu_dvfs_apply_opp()``, BL31 only logs
OPP transitions and does not change clock frequency.

Notes
-----

- The default PMU event is ``0x19`` (architectural BUS_ACCESS). Platforms may
  switch to a more representative transaction event for the target SoC.
- This helper is policy-oriented and intentionally keeps the hardware-specific
  DVFS programming in platform code.
