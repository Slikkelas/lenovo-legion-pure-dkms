// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright Jaroslav Bolek 2025
 *
 * Author(s):
 *   Jaroslav Bolek <jaroslav.bolek@gmail.com>
 * Modified:
 *   Slikkelas
 */
#ifndef LEGION_INTEL_MSR_H_
#define LEGION_INTEL_MSR_H_

#include <linux/mutex.h>

// Voltage plane offsets within MSR 0x150
#define PLANE_CPU       0
#define PLANE_GPU       1
#define PLANE_CACHE     2
#define PLANE_UNCORE    3
#define PLANE_ANALOGIO  4
#define NUM_VOLTAGE_PLANES 5
// Added by Slikkelas (Only if needed, but most kernels do have this registers already included)
#ifndef MSR_TURBO_RATIO_LIMIT
#define MSR_TURBO_RATIO_LIMIT 0x1AD
#endif
#ifndef MSR_ATOM_CORE_TURBO_RATIOS
#define MSR_ATOM_CORE_TURBO_RATIOS 0x66C
#endif
#ifndef MSR_ATOM_CORE_RATIOS
#define MSR_ATOM_CORE_RATIOS 0x66A
#endif
#ifndef MSR_HWP_CAPABILITIES
#define MSR_HWP_CAPABILITIES 0x771
#endif
#ifndef MSR_HWP_REQUEST
#define MSR_HWP_REQUEST 0x774
#endif
// end

// Modified by Slikkelas
// Default voltage offset limits in uV based on Intel documented specifications
// Conservative limits: ±150mV for safety (Intel allows up to ±300mV in BIOS)
// These safe values work across all Intel generations (12th gen through Core Ultra)
//** #define DEFAULT_MAX_UNDERVOLT_UV 150000  // 150mV undervolt
//** #define DEFAULT_MAX_OVERVOLT_UV 150000   // 150mV overvolt
#define DEFAULT_MAX_UNDERVOLT_UV 150  // 150mV undervolt
#define DEFAULT_MAX_OVERVOLT_UV 150   // 150mV overvolt

struct legion_intel_voltage_limits {
	int max_undervolt_uv;
	int max_overvolt_uv;
	int write_supported;  // Whether this plane supports voltage writes
};


struct legion_intel_msr_private {
	struct legion_intel_voltage_limits plane_limits[NUM_VOLTAGE_PLANES];
	struct mutex lock;
};


ssize_t  legion_intel_msr_apply_voltage_offset(struct legion_intel_msr_private *intel_msr_private,int  plane, int offset_uv);
ssize_t  legion_intel_msr_offset_read_show(struct legion_intel_msr_private *intel_msr_private,int plane,int* offset_uv);
// Added by Slikkelas
ssize_t legion_intel_msr_apply_pcore_active_ratios(struct legion_intel_msr_private *priv, u64 ratios);
ssize_t legion_intel_msr_read_pcore_active_ratios(struct legion_intel_msr_private *priv, u64 *ratios);
ssize_t legion_intel_msr_apply_ecore_active_ratios(struct legion_intel_msr_private *priv, u64 ratios);
ssize_t legion_intel_msr_read_ecore_active_ratios(struct legion_intel_msr_private *priv, u64 *ratios);
ssize_t legion_intel_msr_set_per_core_ratio(struct legion_intel_msr_private *priv, int cpu, int ratio);
ssize_t legion_intel_msr_get_per_core_ratio(struct legion_intel_msr_private *priv, int cpu, int *ratio);
ssize_t legion_intel_msr_pcore_vfpoint_offset_show(struct legion_intel_msr_private *priv, char *buf);
ssize_t legion_intel_msr_pcore_vfpoint_offset_store(struct legion_intel_msr_private *priv, const char *buf, size_t count);
ssize_t legion_intel_msr_ecore_vfpoint_offset_show(struct legion_intel_msr_private *priv, char *buf);
ssize_t legion_intel_msr_ecore_vfpoint_offset_store(struct legion_intel_msr_private *priv, const char *buf, size_t count);
ssize_t legion_intel_msr_pcore_vfpoint_freq_show(struct legion_intel_msr_private *priv, char *buf);
ssize_t legion_intel_msr_ecore_vfpoint_freq_show(struct legion_intel_msr_private *priv, char *buf);
ssize_t legion_intel_msr_bruteforce_store(struct legion_intel_msr_private *priv, const char *buf, size_t count);
ssize_t legion_intel_msr_bruteforce_read_store(struct legion_intel_msr_private *priv, const char *buf, size_t count);

// end

int  legion_intel_msr_init(struct legion_intel_msr_private *intel_msr_private);
void legion_intel_msr_exit(struct legion_intel_msr_private *intel_msr_private);

#endif /* LEGION_INTEL_MSR_H_ */
