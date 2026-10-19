// SPDX-License-Identifier: GPL-2.0
#ifndef LEGION_INTEL_MSR_SYSFS_H_
#define LEGION_INTEL_MSR_SYSFS_H_

struct device;

struct legion_intel_msr_sysfs_private {
	int ida_id;
	struct device *dev;
};

int  legion_intel_msr_sysfs_init(struct device *parent);
void legion_intel_msr_sysfs_exit(const struct device *parent);

#endif /* LEGION_INTEL_MSR_SYSFS_H_ */
