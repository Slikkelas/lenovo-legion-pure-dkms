// SPDX-License-Identifier: GPL-2.0
#include "legion-common.h"
#include "legion-rapl-mmio.h"
#include "legion-intel-msr-sysfs.h"

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/version.h>
#include <asm/msr.h>

static bool lock_mmio = false;
module_param(lock_mmio, bool, 0644);
MODULE_PARM_DESC(lock_mmio, "Lock MMIO power limits to CPU MSR 0x610 values on boot and resume (default: N)");

int legion_sync_msr_to_mmio(struct legion_rapl_mmio_private *mmio_priv, bool lock)
{
	u32 msr_low = 0, msr_high = 0;
	u64 msr_val = 0;
	unsigned int pl1_mw, pl2_mw;
	unsigned int pl1_time_enc, pl2_time_enc, f1, y1, f2, y2;
	unsigned int pl1_time_us, pl2_time_us;

	if (!mmio_priv || !mmio_priv->rapl_mmio_base)
		return -ENODEV;

	/* Read MSR 0x610 (MSR_PKG_POWER_LIMIT) directly from CPU 0 */
	if (rdmsr_safe_on_cpu(0, 0x610, &msr_low, &msr_high) != 0)
		return -EIO;

	msr_val = ((u64)msr_high << 32) | msr_low;

	/* Extract Power Limits (Bits 0-14 and Bits 32-46) in mW */
	pl1_mw = ((msr_val & 0x7FFF) * 1000) / 8;
	pl2_mw = (((msr_val >> 32) & 0x7FFF) * 1000) / 8;

	if (pl1_mw == 0 || pl2_mw == 0)
		return -EINVAL;

	/* Extract and decode Time Windows (Bits 17-23 and Bits 49-55) in us */
	pl1_time_enc = (msr_val >> 17) & 0x7F;
	pl2_time_enc = (msr_val >> 49) & 0x7F;

	f1 = (pl1_time_enc & 0x60) >> 5;
	y1 = pl1_time_enc & 0x1F;
	pl1_time_us = (unsigned int)((((1ULL << y1) * (4 + f1) * 1000000ULL / 4) >> 10));

	f2 = (pl2_time_enc & 0x60) >> 5;
	y2 = pl2_time_enc & 0x1F;
	pl2_time_us = (unsigned int)((((1ULL << y2) * (4 + f2) * 1000000ULL / 4) >> 10));

	if (lock) {
		return legion_set_power_and_time_lock(mmio_priv,
						      pl1_mw, pl1_time_us,
						      pl2_mw, pl2_time_us);
	}

	return legion_set_power_and_time(mmio_priv, pl1_mw, pl1_time_us, pl2_mw);
}

static int legion_pm_resume(struct device *dev)
{
	struct legion_data *data = dev_get_drvdata(dev);

	if (data && data->rapl_mmio_private.rapl_mmio_base) {
		if (legion_sync_msr_to_mmio(&data->rapl_mmio_private, lock_mmio) == 0)
			dev_info(dev, "Resumed: synced MSR 0x610 power limits to MMIO (locked=%d)\n", lock_mmio);
	}

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(legion_pm, NULL, legion_pm_resume);

static int legion_probe(struct platform_device *pdev)
{
	int err = 0;
	struct legion_data *data;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->mutex);
	platform_set_drvdata(pdev, data);

	/* Initialize RAPL MMIO and sync with CPU MSR 0x610 (unlocked by default) */
	err = legion_rapl_mmio_init(&pdev->dev);
	if (err) {
		dev_warn(&pdev->dev, "RAPL MMIO device not found (%d), continuing with MSR only\n", err);
	} else {
		if (legion_sync_msr_to_mmio(&data->rapl_mmio_private, lock_mmio) == 0)
			dev_info(&pdev->dev, "Synced CPU MSR 0x610 limits to RAPL MMIO (locked=%d)\n", lock_mmio);
	}

	/* Initialize Intel MSR SysFS (/sys/class/legion-intel-msr/intel-msr-0) */
	err = legion_intel_msr_sysfs_init(&pdev->dev);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize Intel MSR sysfs: %d\n", err);
		legion_rapl_mmio_exit(&pdev->dev);
		mutex_destroy(&data->mutex);
		return err;
	}

	dev_info(&pdev->dev, "Universal Intel MSR & RAPL MMIO driver loaded\n");
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
static void legion_remove(struct platform_device *pdev)
#else
static int legion_remove(struct platform_device *pdev)
#endif
{
	struct legion_data *data = platform_get_drvdata(pdev);

	legion_intel_msr_sysfs_exit(&pdev->dev);
	legion_rapl_mmio_exit(&pdev->dev);

	if (data)
		mutex_destroy(&data->mutex);

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
	return 0;
#endif
}

static struct platform_driver legion_driver = {
	.probe  = legion_probe,
	.remove = legion_remove,
	.driver = {
		.name = "lenovo-legion",
		.pm   = pm_sleep_ptr(&legion_pm),
	},
};

static struct platform_device *legion_pdev;

static int __init legion_init(void)
{
	int err;

	err = platform_driver_register(&legion_driver);
	if (err)
		return err;

	legion_pdev = platform_device_alloc("lenovo-legion", PLATFORM_DEVID_NONE);
	if (!legion_pdev) {
		platform_driver_unregister(&legion_driver);
		return -ENOMEM;
	}

	err = platform_device_add(legion_pdev);
	if (err) {
		platform_device_put(legion_pdev);
		platform_driver_unregister(&legion_driver);
		return err;
	}

	return 0;
}

static void __exit legion_exit(void)
{
	platform_device_unregister(legion_pdev);
	platform_driver_unregister(&legion_driver);
}

module_init(legion_init);
module_exit(legion_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jaroslav Bolek (Modified by Slikkelas)");
MODULE_DESCRIPTION("Universal Intel MSR Undervolt, Ratio & RAPL MMIO Sync Driver");
MODULE_VERSION("2.0.0");
