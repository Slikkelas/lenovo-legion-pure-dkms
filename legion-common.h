// SPDX-License-Identifier: GPL-2.0
#ifndef LEGION_COMMON_H
#define LEGION_COMMON_H

#include "legion-intel-msr-sysfs.h"
#include "legion-intel-msr.h"
#include "legion-rapl-mmio.h"

#include <linux/device.h>
#include <linux/mutex.h>

struct legion_data {
	struct mutex mutex;
	struct legion_rapl_mmio_private rapl_mmio_private;
	struct legion_intel_msr_sysfs_private intel_msr_sysfs_private;
	struct legion_intel_msr_private intel_msr_private;
};

enum legion_events_type {
	LENOVO_INTEL_MSR_PLANE_CPU      = 2001,
	LENOVO_INTEL_MSR_PLANE_CACHE    = 2002,
	LENOVO_INTEL_MSR_PLANE_GPU      = 2003,
	LENOVO_INTEL_MSR_PLANE_UNCORE   = 2004,
	LENOVO_INTEL_MSR_PLANE_ANALOGIO = 2005
};

int legion_sync_msr_to_mmio(struct legion_rapl_mmio_private *mmio_priv, bool lock);

#endif // LEGION_COMMON_H
