// SPDX-License-Identifier: GPL-2.0
#include "legion-common.h"
#include "legion-rapl-mmio.h"

#include <linux/cleanup.h>
#include <linux/pci_ids.h>

#define RAPL_LIMIT_OFFSET 0x59A0
#define RAPL_PL4_OFFSET   0x59B0

#define RAPL_PL1_MASK       0x7FFF
#define RAPL_PL1_ENABLE     (1ULL << 15)
#define RAPL_PL1_CLAMP      (1ULL << 16)
#define RAPL_PL1_TIME_MASK  0xFE0000
#define RAPL_PL1_TIME_SHIFT 17

#define RAPL_PL2_MASK       0x7FFF00000000ULL
#define RAPL_PL2_SHIFT      32
#define RAPL_PL2_ENABLE     (1ULL << 47)
#define RAPL_PL2_CLAMP      (1ULL << 48)
#define RAPL_PL2_TIME_MASK  0xFE000000000000ULL
#define RAPL_PL2_TIME_SHIFT 49

#define RAPL_PL4_MASK       0x7FFF
#define RAPL_PL4_ENABLE     (1ULL << 15)
#define RAPL_PL4_LOCK       (1ULL << 31)

#define RAPL_LOCK_BIT       (1ULL << 63)

#define POWER_UNIT_DIVISOR  8

#define PCI_DEVICE_ID_INTEL_SKL_THERMAL   0x1903
#define PCI_DEVICE_ID_INTEL_ICL_THERMAL   0x8A03
#define PCI_DEVICE_ID_INTEL_TGL_THERMAL   0x9A03
#define PCI_DEVICE_ID_INTEL_ADL_THERMAL   0x461D
#define PCI_DEVICE_ID_INTEL_RPL_THERMAL   0xA71D
#define PCI_DEVICE_ID_INTEL_MTLP_THERMAL  0x7D03
#define PCI_DEVICE_ID_INTEL_LNLM_THERMAL  0x641D
#define PCI_DEVICE_ID_INTEL_ARL_S_THERMAL 0xAD03

static const u16 supported_thermal_devices[] = {
	PCI_DEVICE_ID_INTEL_SKL_THERMAL,
	PCI_DEVICE_ID_INTEL_ICL_THERMAL,
	PCI_DEVICE_ID_INTEL_TGL_THERMAL,
	PCI_DEVICE_ID_INTEL_ADL_THERMAL,
	PCI_DEVICE_ID_INTEL_RPL_THERMAL,
	PCI_DEVICE_ID_INTEL_MTLP_THERMAL,
	PCI_DEVICE_ID_INTEL_LNLM_THERMAL,
	PCI_DEVICE_ID_INTEL_ARL_S_THERMAL,
	0x0000
};

static bool is_device_supported(const u16 device_id)
{
	for (int i = 0; supported_thermal_devices[i] != 0x0000; i++) {
		if (supported_thermal_devices[i] == device_id)
			return true;
	}
	return false;
}

static struct pci_dev *find_thermal_device_by_id(void)
{
	struct pci_dev *pdev = NULL;

	/* Universal fast path: Intel Processor Thermal Device is always at 00:04.0 */
	pdev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(4, 0));
	if (pdev) {
		if (pdev->vendor == PCI_VENDOR_ID_INTEL &&
		    (is_device_supported(pdev->device) ||
		     (pdev->class >> 8) == PCI_CLASS_SIGNAL_PROCESSING_OTHER)) {
			return pdev;
		}
		pci_dev_put(pdev);
		pdev = NULL;
	}

	while ((pdev = pci_get_device(PCI_VENDOR_ID_INTEL, PCI_ANY_ID, pdev)) != NULL) {
		if (is_device_supported(pdev->device))
			return pdev;
	}

	return NULL;
}

static unsigned int units_to_watts(const unsigned int units)
{
	return (units * 1000) / POWER_UNIT_DIVISOR;
}

static unsigned int watts_to_units(const unsigned int milliwatts)
{
	return (milliwatts * POWER_UNIT_DIVISOR) / 1000;
}

static unsigned int decode_time_window(const unsigned int encoded)
{
	const unsigned int f = (encoded & 0x60) >> 5;
	const unsigned int y = encoded & 0x1F;
	unsigned long long time_us = (1ULL << y) * (4 + f) * 1000000ULL / 4;

	return (unsigned int)(time_us >> 10);
}

static unsigned int encode_time_window(unsigned int time_us)
{
	unsigned int f = 0;
	const unsigned long long time_units = ((unsigned long long)time_us << 10) / 1000000ULL;

	if (time_units == 0)
		return 0;

	unsigned int y = 0;
	if (time_units > 1) {
		for (y = 0; y < 31; y++) {
			if ((1ULL << (y + 1)) > time_units)
				break;
		}
	}

	if (y > 31)
		return 0x7F;

	unsigned long long y_val = 1ULL << y;
	if (time_units >= y_val) {
		f = (unsigned int)((4 * (time_units - y_val)) / y_val);
		if (f > 3)
			f = 3;
	}

	return (f << 5) | (y & 0x1F);
}

int legion_rapl_mmio_init(const struct device *parent)
{
	struct legion_data *data = dev_get_drvdata(parent);
	struct legion_rapl_mmio_private *rapl_mmio_private = &data->rapl_mmio_private;
	const int bar = 0;

	mutex_init(&rapl_mmio_private->lock);

	rapl_mmio_private->thermal_dev = find_thermal_device_by_id();
	if (!rapl_mmio_private->thermal_dev)
		return -ENODEV;

	rapl_mmio_private->bar_start = pci_resource_start(rapl_mmio_private->thermal_dev, bar);
	rapl_mmio_private->bar_len = pci_resource_len(rapl_mmio_private->thermal_dev, bar);

	if (!rapl_mmio_private->bar_start || !rapl_mmio_private->bar_len) {
		pci_dev_put(rapl_mmio_private->thermal_dev);
		rapl_mmio_private->thermal_dev = NULL;
		return -ENODEV;
	}

	rapl_mmio_private->rapl_mmio_base = ioremap(rapl_mmio_private->bar_start, rapl_mmio_private->bar_len);
	if (!rapl_mmio_private->rapl_mmio_base) {
		pci_dev_put(rapl_mmio_private->thermal_dev);
		rapl_mmio_private->thermal_dev = NULL;
		return -ENOMEM;
	}

	return 0;
}

void legion_rapl_mmio_exit(const struct device *parent)
{
	struct legion_data *data = dev_get_drvdata(parent);
	struct legion_rapl_mmio_private *rapl_mmio_private = &data->rapl_mmio_private;

	if (rapl_mmio_private->rapl_mmio_base) {
		iounmap(rapl_mmio_private->rapl_mmio_base);
		rapl_mmio_private->rapl_mmio_base = NULL;
	}

	if (rapl_mmio_private->thermal_dev) {
		pci_dev_put(rapl_mmio_private->thermal_dev);
		rapl_mmio_private->thermal_dev = NULL;
	}

	mutex_destroy(&rapl_mmio_private->lock);
}

int legion_pl1_power_read(struct legion_rapl_mmio_private *rapl_mmio_private, unsigned int *pl1)
{
	if (!rapl_mmio_private->rapl_mmio_base)
		return -ENODEV;

	guard(mutex)(&rapl_mmio_private->lock);
	const uint64_t rapl_limit = readq(rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);
	*pl1 = units_to_watts(rapl_limit & RAPL_PL1_MASK);
	return 0;
}

int legion_pl2_power_read(struct legion_rapl_mmio_private *rapl_mmio_private, unsigned int *pl2)
{
	if (!rapl_mmio_private->rapl_mmio_base)
		return -ENODEV;

	guard(mutex)(&rapl_mmio_private->lock);
	const uint64_t rapl_limit = readq(rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);
	*pl2 = units_to_watts((rapl_limit & RAPL_PL2_MASK) >> RAPL_PL2_SHIFT);
	return 0;
}

int legion_pl1_time_read(struct legion_rapl_mmio_private *rapl_mmio_private, unsigned int *time_us)
{
	if (!rapl_mmio_private->rapl_mmio_base)
		return -ENODEV;

	guard(mutex)(&rapl_mmio_private->lock);
	const uint64_t rapl_limit = readq(rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);
	*time_us = decode_time_window((rapl_limit & RAPL_PL1_TIME_MASK) >> RAPL_PL1_TIME_SHIFT);
	return 0;
}

int legion_pl2_time_read(struct legion_rapl_mmio_private *rapl_mmio_private, unsigned int *time_us)
{
	if (!rapl_mmio_private->rapl_mmio_base)
		return -ENODEV;

	guard(mutex)(&rapl_mmio_private->lock);
	const uint64_t rapl_limit = readq(rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);
	*time_us = decode_time_window((rapl_limit & RAPL_PL2_TIME_MASK) >> RAPL_PL2_TIME_SHIFT);
	return 0;
}

int legion_pl4_lock_read(struct legion_rapl_mmio_private *rapl_mmio_private, bool *isLocked)
{
	if (!rapl_mmio_private->rapl_mmio_base)
		return -ENODEV;

	guard(mutex)(&rapl_mmio_private->lock);
	const uint64_t rapl_pl4 = readq(rapl_mmio_private->rapl_mmio_base + RAPL_PL4_OFFSET);
	*isLocked = (rapl_pl4 & RAPL_PL4_LOCK) ? true : false;
	return 0;
}

int legion_pl4_power_read(struct legion_rapl_mmio_private *rapl_mmio_private, unsigned int *pl4)
{
	if (!rapl_mmio_private->rapl_mmio_base)
		return -ENODEV;

	guard(mutex)(&rapl_mmio_private->lock);
	const uint64_t rapl_pl4 = readq(rapl_mmio_private->rapl_mmio_base + RAPL_PL4_OFFSET);
	*pl4 = units_to_watts(rapl_pl4 & RAPL_PL4_MASK);
	return 0;
}

int legion_is_locked_power_read(struct legion_rapl_mmio_private *rapl_mmio_private, bool *isLocked)
{
	if (!rapl_mmio_private->rapl_mmio_base)
		return -ENODEV;

	guard(mutex)(&rapl_mmio_private->lock);
	const uint64_t rapl_limit = readq(rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);
	*isLocked = (rapl_limit & RAPL_LOCK_BIT) ? true : false;
	return 0;
}

int legion_lock_current_mmio(struct legion_rapl_mmio_private *rapl_mmio_private)
{
	if (!rapl_mmio_private->rapl_mmio_base)
		return -ENODEV;

	guard(mutex)(&rapl_mmio_private->lock);
	uint64_t rapl_limit = readq(rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);
	rapl_limit |= (RAPL_PL1_ENABLE | RAPL_PL2_ENABLE | RAPL_LOCK_BIT);
	writeq(rapl_limit, rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);
	return 0;
}

int legion_pl4_set_and_lock(struct legion_rapl_mmio_private *rapl_mmio_private, const unsigned int pl4_mw)
{
	if (!rapl_mmio_private->rapl_mmio_base)
		return -ENODEV;
	if (pl4_mw > POWER_LIMIT_MAX_VALUE_IN_MW)
		return -EINVAL;

	guard(mutex)(&rapl_mmio_private->lock);
	const unsigned int units = watts_to_units(pl4_mw);
	uint64_t rapl_pl4 = readq(rapl_mmio_private->rapl_mmio_base + RAPL_PL4_OFFSET);

	rapl_pl4 &= ~RAPL_PL4_MASK;
	rapl_pl4 |= (units & RAPL_PL4_MASK) | RAPL_PL4_ENABLE | RAPL_PL4_LOCK;
	writeq(rapl_pl4, rapl_mmio_private->rapl_mmio_base + RAPL_PL4_OFFSET);
	return 0;
}

int legion_set_and_lock(struct legion_rapl_mmio_private *rapl_mmio_private,
			const unsigned int pl1_mw, const unsigned int pl2_mw)
{
	if (!rapl_mmio_private->rapl_mmio_base)
		return -EINVAL;
	if (pl1_mw > POWER_LIMIT_MAX_VALUE_IN_MW || pl2_mw > POWER_LIMIT_MAX_VALUE_IN_MW)
		return -EINVAL;

	guard(mutex)(&rapl_mmio_private->lock);
	const unsigned int pl1_units = watts_to_units(pl1_mw);
	const unsigned int pl2_units = watts_to_units(pl2_mw);
	uint64_t rapl_limit = readq(rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);

	rapl_limit &= ~(RAPL_PL1_MASK | RAPL_PL2_MASK);
	rapl_limit |= (pl1_units & RAPL_PL1_MASK);
	rapl_limit |= ((uint64_t)pl2_units << RAPL_PL2_SHIFT) & RAPL_PL2_MASK;
	rapl_limit |= (RAPL_PL1_ENABLE | RAPL_PL2_ENABLE | RAPL_LOCK_BIT);
	writeq(rapl_limit, rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);
	return 0;
}

int legion_set_power_and_time_lock(struct legion_rapl_mmio_private *rapl_mmio_private,
				   const unsigned int pl1_mw, const unsigned int pl1_time_us,
				   const unsigned int pl2_mw, const unsigned int pl2_time_us)
{
	if (!rapl_mmio_private->rapl_mmio_base)
		return -EINVAL;
	if (pl1_mw > POWER_LIMIT_MAX_VALUE_IN_MW || pl2_mw > POWER_LIMIT_MAX_VALUE_IN_MW)
		return -EINVAL;

	guard(mutex)(&rapl_mmio_private->lock);
	const unsigned int pl1_units = watts_to_units(pl1_mw);
	const unsigned int pl2_units = watts_to_units(pl2_mw);
	const unsigned int pl1_time_enc = encode_time_window(pl1_time_us);
	const unsigned int pl2_time_enc = encode_time_window(pl2_time_us);
	uint64_t rapl_limit = readq(rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);

	rapl_limit &= ~(RAPL_PL1_MASK | RAPL_PL2_MASK | RAPL_PL1_TIME_MASK | RAPL_PL2_TIME_MASK);
	rapl_limit |= (pl1_units & RAPL_PL1_MASK);
	rapl_limit |= ((uint64_t)pl2_units << RAPL_PL2_SHIFT) & RAPL_PL2_MASK;
	rapl_limit |= ((uint64_t)pl1_time_enc << RAPL_PL1_TIME_SHIFT) & RAPL_PL1_TIME_MASK;
	rapl_limit |= ((uint64_t)pl2_time_enc << RAPL_PL2_TIME_SHIFT) & RAPL_PL2_TIME_MASK;
	rapl_limit |= (RAPL_PL1_ENABLE | RAPL_PL2_ENABLE | RAPL_LOCK_BIT);
	writeq(rapl_limit, rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);
	return 0;
}

int legion_set_power(struct legion_rapl_mmio_private *rapl_mmio_private,
		     const unsigned int pl1_mw, const unsigned int pl2_mw)
{
	if (!rapl_mmio_private->rapl_mmio_base)
		return -EINVAL;
	if (pl1_mw > POWER_LIMIT_MAX_VALUE_IN_MW || pl2_mw > POWER_LIMIT_MAX_VALUE_IN_MW)
		return -EINVAL;

	guard(mutex)(&rapl_mmio_private->lock);
	const unsigned int pl1_units = watts_to_units(pl1_mw);
	const unsigned int pl2_units = watts_to_units(pl2_mw);
	uint64_t rapl_limit = readq(rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);

	rapl_limit &= ~(RAPL_PL1_MASK | RAPL_PL2_MASK);
	rapl_limit |= (pl1_units & RAPL_PL1_MASK);
	rapl_limit |= ((uint64_t)pl2_units << RAPL_PL2_SHIFT) & RAPL_PL2_MASK;
	rapl_limit |= (RAPL_PL1_ENABLE | RAPL_PL2_ENABLE);
	writeq(rapl_limit, rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);
	return 0;
}

int legion_set_power_and_time(struct legion_rapl_mmio_private *rapl_mmio_private,
			      const unsigned int pl1_mw, const unsigned int pl1_time_us,
			      const unsigned int pl2_mw)
{
	if (!rapl_mmio_private->rapl_mmio_base)
		return -EINVAL;
	if (pl1_mw > POWER_LIMIT_MAX_VALUE_IN_MW || pl2_mw > POWER_LIMIT_MAX_VALUE_IN_MW)
		return -EINVAL;

	guard(mutex)(&rapl_mmio_private->lock);
	const unsigned int pl1_units = watts_to_units(pl1_mw);
	const unsigned int pl2_units = watts_to_units(pl2_mw);
	const unsigned int pl1_time_enc = encode_time_window(pl1_time_us);
	uint64_t rapl_limit = readq(rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);

	rapl_limit &= ~(RAPL_PL1_MASK | RAPL_PL2_MASK | RAPL_PL1_TIME_MASK);
	rapl_limit |= (pl1_units & RAPL_PL1_MASK);
	rapl_limit |= ((uint64_t)pl2_units << RAPL_PL2_SHIFT) & RAPL_PL2_MASK;
	rapl_limit |= ((uint64_t)pl1_time_enc << RAPL_PL1_TIME_SHIFT) & RAPL_PL1_TIME_MASK;
	rapl_limit |= (RAPL_PL1_ENABLE | RAPL_PL2_ENABLE);
	writeq(rapl_limit, rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);
	return 0;
}

int legion_pl4_set(struct legion_rapl_mmio_private *rapl_mmio_private, const unsigned int pl4_mw)
{
	if (!rapl_mmio_private->rapl_mmio_base)
		return -ENODEV;
	if (pl4_mw > POWER_LIMIT_MAX_VALUE_IN_MW)
		return -EINVAL;

	guard(mutex)(&rapl_mmio_private->lock);
	const unsigned int units = watts_to_units(pl4_mw);
	uint64_t rapl_pl4 = readq(rapl_mmio_private->rapl_mmio_base + RAPL_PL4_OFFSET);

	rapl_pl4 &= ~RAPL_PL4_MASK;
	rapl_pl4 |= (units & RAPL_PL4_MASK) | RAPL_PL4_ENABLE;
	writeq(rapl_pl4, rapl_mmio_private->rapl_mmio_base + RAPL_PL4_OFFSET);
	return 0;
}

int legion_pl1_power_set(struct legion_rapl_mmio_private *rapl_mmio_private, const unsigned int pl1_mw)
{
	if (!rapl_mmio_private->rapl_mmio_base)
		return -EINVAL;
	if (pl1_mw > POWER_LIMIT_MAX_VALUE_IN_MW)
		return -EINVAL;

	guard(mutex)(&rapl_mmio_private->lock);
	const unsigned int pl1_units = watts_to_units(pl1_mw);
	uint64_t rapl_limit = readq(rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);

	rapl_limit &= ~RAPL_PL1_MASK;
	rapl_limit |= (pl1_units & RAPL_PL1_MASK) | RAPL_PL1_ENABLE;
	writeq(rapl_limit, rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);
	return 0;
}

int legion_pl2_power_set(struct legion_rapl_mmio_private *rapl_mmio_private, const unsigned int pl2_mw)
{
	if (!rapl_mmio_private->rapl_mmio_base)
		return -EINVAL;
	if (pl2_mw > POWER_LIMIT_MAX_VALUE_IN_MW)
		return -EINVAL;

	guard(mutex)(&rapl_mmio_private->lock);
	const unsigned int pl2_units = watts_to_units(pl2_mw);
	uint64_t rapl_limit = readq(rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);

	rapl_limit &= ~RAPL_PL2_MASK;
	rapl_limit |= (((uint64_t)pl2_units << RAPL_PL2_SHIFT) & RAPL_PL2_MASK) | RAPL_PL2_ENABLE;
	writeq(rapl_limit, rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);
	return 0;
}

int legion_pl1_time_set(struct legion_rapl_mmio_private *rapl_mmio_private, const unsigned int time_us)
{
	if (!rapl_mmio_private->rapl_mmio_base)
		return -EINVAL;

	guard(mutex)(&rapl_mmio_private->lock);
	const unsigned int time_enc = encode_time_window(time_us);
	uint64_t rapl_limit = readq(rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);

	rapl_limit &= ~RAPL_PL1_TIME_MASK;
	rapl_limit |= ((uint64_t)time_enc << RAPL_PL1_TIME_SHIFT) & RAPL_PL1_TIME_MASK;
	writeq(rapl_limit, rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);
	return 0;
}

int legion_pl2_time_set(struct legion_rapl_mmio_private *rapl_mmio_private, const unsigned int time_us)
{
	if (!rapl_mmio_private->rapl_mmio_base)
		return -EINVAL;

	guard(mutex)(&rapl_mmio_private->lock);
	const unsigned int time_enc = encode_time_window(time_us);
	uint64_t rapl_limit = readq(rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);

	rapl_limit &= ~RAPL_PL2_TIME_MASK;
	rapl_limit |= ((uint64_t)time_enc << RAPL_PL2_TIME_SHIFT) & RAPL_PL2_TIME_MASK;
	writeq(rapl_limit, rapl_mmio_private->rapl_mmio_base + RAPL_LIMIT_OFFSET);
	return 0;
}
