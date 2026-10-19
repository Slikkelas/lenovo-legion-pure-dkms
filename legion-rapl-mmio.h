// SPDX-License-Identifier: GPL-2.0
#ifndef LEGION_RAPL_MMIO_H_
#define LEGION_RAPL_MMIO_H_

#include <linux/io.h>
#include <linux/pci.h>
#include <linux/mutex.h>

#define POWER_LIMIT_MAX_VALUE_IN_MW 999000

struct legion_rapl_mmio_private {
	void __iomem *rapl_mmio_base;
	struct pci_dev *thermal_dev;
	resource_size_t bar_start;
	resource_size_t bar_len;
	struct mutex lock;
};

int  legion_rapl_mmio_init(const struct device *parent);
void legion_rapl_mmio_exit(const struct device *parent);

int  legion_pl1_power_read(struct legion_rapl_mmio_private *rapl_mmio_private, unsigned int *pl1);
int  legion_pl2_power_read(struct legion_rapl_mmio_private *rapl_mmio_private, unsigned int *pl2);
int  legion_pl4_power_read(struct legion_rapl_mmio_private *rapl_mmio_private, unsigned int *pl4);

int  legion_pl1_time_read(struct legion_rapl_mmio_private *rapl_mmio_private, unsigned int *time_us);
int  legion_pl2_time_read(struct legion_rapl_mmio_private *rapl_mmio_private, unsigned int *time_us);

int  legion_is_locked_power_read(struct legion_rapl_mmio_private *rapl_mmio_private, bool *isLocked);
int  legion_pl4_lock_read(struct legion_rapl_mmio_private *rapl_mmio_private, bool *isLocked);

int  legion_lock_current_mmio(struct legion_rapl_mmio_private *rapl_mmio_private);
int  legion_set_and_lock(struct legion_rapl_mmio_private *rapl_mmio_private, unsigned int pl1_mw, unsigned int pl2_mw);
int  legion_pl4_set_and_lock(struct legion_rapl_mmio_private *rapl_mmio_private, unsigned int pl4_mw);
int  legion_set_power_and_time_lock(struct legion_rapl_mmio_private *rapl_mmio_private,
				    unsigned int pl1_mw, unsigned int pl1_time_us,
				    unsigned int pl2_mw, unsigned int pl2_time_us);

int  legion_set_power(struct legion_rapl_mmio_private *rapl_mmio_private,
		      unsigned int pl1_mw, unsigned int pl2_mw);
int  legion_set_power_and_time(struct legion_rapl_mmio_private *rapl_mmio_private,
			       unsigned int pl1_mw, unsigned int pl1_time_us,
			       unsigned int pl2_mw);
int  legion_pl4_set(struct legion_rapl_mmio_private *rapl_mmio_private, unsigned int pl4_mw);

int  legion_pl1_power_set(struct legion_rapl_mmio_private *rapl_mmio_private, unsigned int pl1_mw);
int  legion_pl2_power_set(struct legion_rapl_mmio_private *rapl_mmio_private, unsigned int pl2_mw);
int  legion_pl1_time_set(struct legion_rapl_mmio_private *rapl_mmio_private, unsigned int time_us);
int  legion_pl2_time_set(struct legion_rapl_mmio_private *rapl_mmio_private, unsigned int time_us);

#endif /* LEGION_RAPL_MMIO_H_ */
