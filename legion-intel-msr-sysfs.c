// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright Jaroslav Bolek 2025
 *
 * Author(s):
 *   Jaroslav Bolek <jaroslav.bolek@gmail.com>
 * Modified:
 *   Slikkelas <https://github.com/Slikkelas>
 */

#include "legion-common.h"
#include "legion-intel-msr.h"

#include <linux/device.h>
#include <linux/gfp_types.h>
#include <linux/idr.h>

#define LEGION_INTEL_MSR_BASE_PATH "intel-msr"

static DEFINE_IDA(legion_intel_msr_sysfs_ida);

static int legion_intel_msr_dev_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	return add_uevent_var(env, "DRIVER=legion-intel-msr");
}

const struct class legion_intel_msr_class = {
	.name = "legion-intel-msr",
	.dev_uevent = legion_intel_msr_dev_uevent,
};



static ssize_t send_event(struct device *dev,const enum legion_events_type type,const int value)
{
	char event_type[64]  = {0};
	char event_value[64]  = {0};

	sprintf(event_type,"EVENT_TYPE=%u",(unsigned int)type);
	sprintf(event_value,"EVENT_VALUE=%d",value);

	char * envp[] = {
		event_type,
		event_value,
		NULL
	};

	if (kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp)) {
		dev_err(dev, "Failed to send uevent for attribute change\n");
	}

	return 0;
}

/*
 * Sysfs attribute show/store functions
 */
static ssize_t cpu_offset_show(struct device *dev,struct device_attribute *attr, char *buf)
{
    int mv_offset = 0;
	struct legion_data *priv = dev_get_drvdata(dev);

	/* Critical: Prevent NULL pointer dereference and system freeze */
	if (!priv) {
		return -ENODEV;
	}

    const ssize_t ret = legion_intel_msr_offset_read_show(&priv->intel_msr_private,PLANE_CPU,&mv_offset);
    if(ret)
    	return ret;

    return sprintf(buf, "%d\n", mv_offset);
}

static ssize_t cpu_offset_store(struct device *dev,struct device_attribute *attr,const char *buf,const size_t count)
{
    int cpu_offset = 0;
	struct legion_data *priv = dev_get_drvdata(dev);

	/* Critical: Prevent NULL pointer dereference and system freeze */
	if (!priv) {
		return -ENODEV;
	}

    if (kstrtoint(buf, 10, &cpu_offset)) {
    	return -EINVAL;
    }

    legion_intel_msr_apply_voltage_offset(&priv->intel_msr_private,PLANE_CPU, cpu_offset);

	send_event(dev,LENOVO_INTEL_MSR_PLANE_CPU,cpu_offset);

    return (ssize_t)count;
}

static ssize_t cache_offset_show(struct device *dev,struct device_attribute *attr, char *buf)
{
    int mv_offset = 0;
	struct legion_data *priv = dev_get_drvdata(dev);

	/* Critical: Prevent NULL pointer dereference and system freeze */
	if (!priv) {
		return -ENODEV;
	}

    const ssize_t ret = legion_intel_msr_offset_read_show(&priv->intel_msr_private,PLANE_CACHE,&mv_offset);
    if(ret)
    	return ret;

    return sprintf(buf, "%d\n", mv_offset);
}

static ssize_t cache_offset_store(struct device *dev,struct device_attribute *attr,const char *buf,size_t count)
{
    int cache_offset = 0;
	struct legion_data *priv = dev_get_drvdata(dev);

	/* Critical: Prevent NULL pointer dereference and system freeze */
	if (!priv) {
		return -ENODEV;
	}

	const ssize_t ret = kstrtoint(buf, 10, &cache_offset);
    if (ret < 0)
        return ret;

    legion_intel_msr_apply_voltage_offset(&priv->intel_msr_private,PLANE_CACHE, cache_offset);

	send_event(dev,LENOVO_INTEL_MSR_PLANE_CACHE,cache_offset);

	return (ssize_t)count;
}

static ssize_t gpu_offset_show(struct device *dev,struct device_attribute *attr, char *buf)
{
    int mv_offset = 0;
	struct legion_data *priv = dev_get_drvdata(dev);

	/* Critical: Prevent NULL pointer dereference and system freeze */
	if (!priv) {
		return -ENODEV;
	}

    const ssize_t ret = legion_intel_msr_offset_read_show(&priv->intel_msr_private,PLANE_GPU,&mv_offset);
    if(ret)
    	return ret;

    return sprintf(buf, "%d\n", mv_offset);
}

static ssize_t gpu_offset_store(struct device *dev,struct device_attribute *attr,const char *buf,size_t count)
{
    int gpu_offset = 0;
	struct legion_data *priv = dev_get_drvdata(dev);

	/* Critical: Prevent NULL pointer dereference and system freeze */
	if (!priv) {
		return -ENODEV;
	}


	const ssize_t ret = kstrtoint(buf, 10, &gpu_offset);
    if (ret < 0)
        return ret;

    legion_intel_msr_apply_voltage_offset(&priv->intel_msr_private,PLANE_GPU, gpu_offset);

	send_event(dev,LENOVO_INTEL_MSR_PLANE_GPU,gpu_offset);

	return (ssize_t)count;
}

static ssize_t uncore_offset_show(struct device *dev,struct device_attribute *attr, char *buf)
{
    int mv_offset = 0;
	struct legion_data *priv = dev_get_drvdata(dev);

	/* Critical: Prevent NULL pointer dereference and system freeze */
	if (!priv) {
		return -ENODEV;
	}

    const ssize_t ret = legion_intel_msr_offset_read_show(&priv->intel_msr_private,PLANE_UNCORE,&mv_offset);
    if(ret)
    	return ret;

    return sprintf(buf, "%d\n", mv_offset);
}

static ssize_t uncore_offset_store(struct device *dev,struct device_attribute *attr,const char *buf,size_t count)
{
    int uncore_offset = 0;
	struct legion_data *priv = dev_get_drvdata(dev);

	/* Critical: Prevent NULL pointer dereference and system freeze */
	if (!priv) {
		return -ENODEV;
	}

    const ssize_t ret = kstrtoint(buf, 10, &uncore_offset);
    if (ret < 0)
        return ret;

    legion_intel_msr_apply_voltage_offset(&priv->intel_msr_private,PLANE_UNCORE, uncore_offset);

	send_event(dev,LENOVO_INTEL_MSR_PLANE_UNCORE,uncore_offset);

    return (ssize_t)count;
}

static ssize_t analogio_offset_show(struct device *dev,struct device_attribute *attr, char *buf)
{
    int mv_offset = 0;
	struct legion_data *priv = dev_get_drvdata(dev);

	/* Critical: Prevent NULL pointer dereference and system freeze */
	if (!priv) {
		return -ENODEV;
	}

    const ssize_t ret = legion_intel_msr_offset_read_show(&priv->intel_msr_private,PLANE_ANALOGIO,&mv_offset);
    if(ret)
    	return ret;

    return sprintf(buf, "%d\n", mv_offset);
}

static ssize_t analogio_offset_store(struct device *dev,struct device_attribute *attr,const char *buf,size_t count)
{
	struct legion_data *priv = dev_get_drvdata(dev);
    int analogio_offset = 0;

	/* Critical: Prevent NULL pointer dereference and system freeze */
	if (!priv) {
		return -ENODEV;
	}

    const ssize_t ret = kstrtoint(buf, 10, &analogio_offset);
    if (ret < 0)
        return ret;

    legion_intel_msr_apply_voltage_offset(&priv->intel_msr_private,PLANE_ANALOGIO, analogio_offset);

	send_event(dev,LENOVO_INTEL_MSR_PLANE_ANALOGIO,analogio_offset);

    return (ssize_t)count;
}

// Added by Slikkelas
/* P-Core Active Ratio Table (1C through 8C limits) */
static ssize_t pcore_active_ratios_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    u64 result = 0;
    struct legion_data *priv = dev_get_drvdata(dev);

    if (!priv) return -ENODEV;
    if (legion_intel_msr_read_pcore_active_ratios(&priv->intel_msr_private, &result) < 0) return -EIO;

    u32 low = (u32)result;
    u32 high = (u32)(result >> 32);

    return sprintf(buf, "%d %d %d %d %d %d %d %d\n",
                   low & 0xFF, (low >> 8) & 0xFF, (low >> 16) & 0xFF, (low >> 24) & 0xFF,
                   high & 0xFF, (high >> 8) & 0xFF, (high >> 16) & 0xFF, (high >> 24) & 0xFF);
}

static ssize_t pcore_active_ratios_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int r[8] = {0};
    u64 msr_val = 0;
    struct legion_data *priv = dev_get_drvdata(dev);

    if (!priv) return -ENODEV;

    if (sscanf(buf, "%d %d %d %d %d %d %d %d", &r[0], &r[1], &r[2], &r[3], &r[4], &r[5], &r[6], &r[7]) != 8) {
        dev_err(dev, "Invalid format. Provide 8 numbers for 1C to 8C active limits.\n");
        return -EINVAL;
    }

    for (int i = 0; i < 8; i++) {
        if ((r[i] < 8 && r[i] != 0) || r[i] > 120) return -EINVAL;
        msr_val |= ((u64)r[i] << (i * 8));
    }

    if (legion_intel_msr_apply_pcore_active_ratios(&priv->intel_msr_private, msr_val) < 0) return -EIO;
    return count;
}

/* Per-Core specific ratio targeting */
static ssize_t core_ratio_limit_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct legion_data *priv = dev_get_drvdata(dev);
    int cpu, ratio, len = 0;

    if (!priv) return -ENODEV;

    // Dumps the current ratio limit for every single online core
    for_each_online_cpu(cpu) {
        if (legion_intel_msr_get_per_core_ratio(&priv->intel_msr_private, cpu, &ratio) == 0) {
            len += sprintf(buf + len, "CPU%d: %d\n", cpu, ratio);
        }
    }
    return len;
}

static ssize_t core_ratio_limit_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int target_cpu = 0, ratio = 0;
    struct legion_data *priv = dev_get_drvdata(dev);

    if (!priv) return -ENODEV;

    // Expects: "<CPU_ID> <RATIO>". If CPU_ID is -1, it applies to ALL cores.
    if (sscanf(buf, "%d %d", &target_cpu, &ratio) != 2) {
        dev_err(dev, "Invalid format. Use: '<cpu_id> <ratio>'. Use -1 for all cores.\n");
        return -EINVAL;
    }

    if (target_cpu == -1) {
        int cpu;
        for_each_online_cpu(cpu) {
            legion_intel_msr_set_per_core_ratio(&priv->intel_msr_private, cpu, ratio);
        }
    } else {
        if (!cpu_online(target_cpu)) return -EINVAL;
        legion_intel_msr_set_per_core_ratio(&priv->intel_msr_private, target_cpu, ratio);
    }
    return count;
}

static DEVICE_ATTR_RW(pcore_active_ratios);
static DEVICE_ATTR_RW(core_ratio_limit);
// end

static ssize_t cpu_max_undervolt_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	struct legion_data *priv = dev_get_drvdata(dev);

	/* Critical: Prevent NULL pointer dereference and system freeze */
	if (!priv) {
		return -ENODEV;
	}

    return sprintf(buf, "%d\n", priv->intel_msr_private.plane_limits[PLANE_CPU].max_undervolt_uv);
}

static ssize_t cpu_max_overvolt_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	struct legion_data *priv = dev_get_drvdata(dev);

	/* Critical: Prevent NULL pointer dereference and system freeze */
	if (!priv) {
		return -ENODEV;
	}

    return sprintf(buf, "%d\n", priv->intel_msr_private.plane_limits[PLANE_CPU].max_overvolt_uv);
}




static ssize_t cache_max_undervolt_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	struct legion_data *priv = dev_get_drvdata(dev);

	/* Critical: Prevent NULL pointer dereference and system freeze */
	if (!priv) {
		return -ENODEV;
	}

    return sprintf(buf, "%d\n", priv->intel_msr_private.plane_limits[PLANE_CACHE].max_undervolt_uv);
}


/*
 * Show voltage offset limits for all planes
 */
static ssize_t cache_max_overvolt_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	struct legion_data *priv = dev_get_drvdata(dev);

	/* Critical: Prevent NULL pointer dereference and system freeze */
	if (!priv) {
		return -ENODEV;
	}
// Corrected by Slikkelas
// Changed .max_undervolt_uv to .max_overvolt_uv
    return sprintf(buf, "%d\n", priv->intel_msr_private.plane_limits[PLANE_CACHE].max_overvolt_uv);
// end
}



static ssize_t gpu_max_undervolt_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	struct legion_data *priv = dev_get_drvdata(dev);

	/* Critical: Prevent NULL pointer dereference and system freeze */
	if (!priv) {
		return -ENODEV;
	}

    return sprintf(buf, "%d\n", priv->intel_msr_private.plane_limits[PLANE_GPU].max_undervolt_uv);
}


/*
 * Show voltage offset limits for all planes
 */
static ssize_t gpu_max_overvolt_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	struct legion_data *priv = dev_get_drvdata(dev);

	/* Critical: Prevent NULL pointer dereference and system freeze */
	if (!priv) {
		return -ENODEV;
	}

    return sprintf(buf, "%d\n", priv->intel_msr_private.plane_limits[PLANE_GPU].max_overvolt_uv);
}




static ssize_t uncore_max_undervolt_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	struct legion_data *priv = dev_get_drvdata(dev);

	/* Critical: Prevent NULL pointer dereference and system freeze */
	if (!priv) {
		return -ENODEV;
	}

    return sprintf(buf, "%d\n", priv->intel_msr_private.plane_limits[PLANE_UNCORE].max_undervolt_uv);
}


/*
 * Show voltage offset limits for all planes
 */
static ssize_t uncore_max_overvolt_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	struct legion_data *priv = dev_get_drvdata(dev);

	/* Critical: Prevent NULL pointer dereference and system freeze */
	if (!priv) {
		return -ENODEV;
	}

    return sprintf(buf, "%d\n", priv->intel_msr_private.plane_limits[PLANE_UNCORE].max_overvolt_uv);
}

/*
 * Show which voltage planes support write operations
 */
static ssize_t cpu_offset_ctrl_supported_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct legion_data *priv = dev_get_drvdata(dev);

	if (!priv) {
		return -ENODEV;
	}

    return sprintf(buf, "%d\n", priv->intel_msr_private.plane_limits[PLANE_CPU].write_supported);
}

static ssize_t cache_offset_ctrl_supported_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct legion_data *priv = dev_get_drvdata(dev);

	if (!priv) {
		return -ENODEV;
	}

    return sprintf(buf, "%d\n", priv->intel_msr_private.plane_limits[PLANE_CACHE].write_supported);
}

static ssize_t gpu_offset_ctrl_supported_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct legion_data *priv = dev_get_drvdata(dev);

	if (!priv) {
		return -ENODEV;
	}

    return sprintf(buf, "%d\n", priv->intel_msr_private.plane_limits[PLANE_GPU].write_supported);
}

static ssize_t uncore_offset_ctrl_supported_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct legion_data *priv = dev_get_drvdata(dev);

	if (!priv) {
		return -ENODEV;
	}

    return sprintf(buf, "%d\n", priv->intel_msr_private.plane_limits[PLANE_UNCORE].write_supported);
}

static ssize_t analogio_offset_ctrl_supported_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct legion_data *priv = dev_get_drvdata(dev);

	if (!priv) {
		return -ENODEV;
	}

    return sprintf(buf, "%d\n", priv->intel_msr_private.plane_limits[PLANE_ANALOGIO].write_supported);
}

static ssize_t analogio_max_undervolt_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	struct legion_data *priv = dev_get_drvdata(dev);

	/* Critical: Prevent NULL pointer dereference and system freeze */
	if (!priv) {
		return -ENODEV;
	}

    return sprintf(buf, "%d\n", priv->intel_msr_private.plane_limits[PLANE_ANALOGIO].max_undervolt_uv);
}


/*
 * Show voltage offset limits for all planes
 */
static ssize_t analogio_max_overvolt_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	struct legion_data *priv = dev_get_drvdata(dev);

	/* Critical: Prevent NULL pointer dereference and system freeze */
	if (!priv) {
		return -ENODEV;
	}

    return sprintf(buf, "%d\n", priv->intel_msr_private.plane_limits[PLANE_ANALOGIO].max_overvolt_uv);
}

static DEVICE_ATTR_RW(cpu_offset);
static DEVICE_ATTR_RW(cache_offset);
static DEVICE_ATTR_RW(gpu_offset);
static DEVICE_ATTR_RW(uncore_offset);
static DEVICE_ATTR_RW(analogio_offset);



static DEVICE_ATTR_RO(cpu_max_undervolt);
static DEVICE_ATTR_RO(cpu_max_overvolt);

static DEVICE_ATTR_RO(cache_max_undervolt);
static DEVICE_ATTR_RO(cache_max_overvolt);

static DEVICE_ATTR_RO(gpu_max_undervolt);
static DEVICE_ATTR_RO(gpu_max_overvolt);

static DEVICE_ATTR_RO(uncore_max_undervolt);
static DEVICE_ATTR_RO(uncore_max_overvolt);

static DEVICE_ATTR_RO(analogio_max_undervolt);
static DEVICE_ATTR_RO(analogio_max_overvolt);

static DEVICE_ATTR_RO(cpu_offset_ctrl_supported);
static DEVICE_ATTR_RO(cache_offset_ctrl_supported);
static DEVICE_ATTR_RO(gpu_offset_ctrl_supported);
static DEVICE_ATTR_RO(uncore_offset_ctrl_supported);
static DEVICE_ATTR_RO(analogio_offset_ctrl_supported);


static struct attribute *legion_intel_msr_sysfs_attributes[]  = {
	    &dev_attr_cpu_offset.attr,
	    &dev_attr_cache_offset.attr,
	    &dev_attr_gpu_offset.attr,
	    &dev_attr_uncore_offset.attr,
	    &dev_attr_analogio_offset.attr,
		// Added by Slikkelas
		&dev_attr_pcore_active_ratios.attr,
		&dev_attr_core_ratio_limit.attr,
		// end

	    &dev_attr_cpu_max_undervolt.attr,
	    &dev_attr_cpu_max_overvolt.attr,
	    &dev_attr_cache_max_undervolt.attr,
	    &dev_attr_cache_max_overvolt.attr,
	    &dev_attr_gpu_max_undervolt.attr,
	    &dev_attr_gpu_max_overvolt.attr,
	    &dev_attr_uncore_max_undervolt.attr,
	    &dev_attr_uncore_max_overvolt.attr,
	    &dev_attr_analogio_max_undervolt.attr,
	    &dev_attr_analogio_max_overvolt.attr,

	    &dev_attr_cpu_offset_ctrl_supported.attr,
	    &dev_attr_cache_offset_ctrl_supported.attr,
	    &dev_attr_gpu_offset_ctrl_supported.attr,
	    &dev_attr_uncore_offset_ctrl_supported.attr,
	    &dev_attr_analogio_offset_ctrl_supported.attr,

		NULL
};

static const struct attribute_group legion_intel_msr_attributes_group = {
	    .attrs = legion_intel_msr_sysfs_attributes
};



int  legion_intel_msr_sysfs_init(struct device *parent) {

	int ret = 0;
	struct legion_data* data = dev_get_drvdata(parent);

	if (!data)
	    return -ENODEV;

	ret = legion_intel_msr_init(&data->intel_msr_private);
    if (ret)
        return ret;

    ret = class_register(&legion_intel_msr_class);
    if (ret) {
    	return ret;
    }

    ret = data->intel_msr_sysfs_private.ida_id = ida_alloc(&legion_intel_msr_sysfs_ida, GFP_KERNEL);
	if (data->intel_msr_sysfs_private.ida_id < 0)
		goto err_unregister_class;

	data->intel_msr_sysfs_private.dev = device_create(&legion_intel_msr_class, parent,
					  MKDEV(0, 0), data, "%s-%u",
					  LEGION_INTEL_MSR_BASE_PATH,
					  data->intel_msr_sysfs_private.ida_id);
	if (IS_ERR(data->intel_msr_sysfs_private.dev)) {
		ret = (int)PTR_ERR(data->intel_msr_sysfs_private.dev);
		goto err_free_ida;
	}


	ret = device_add_group(data->intel_msr_sysfs_private.dev,&legion_intel_msr_attributes_group);
    if (ret) {
    	goto err_unregister_dev;
    }

	return 0;

err_unregister_dev:
	device_unregister(data->intel_msr_sysfs_private.dev);
err_free_ida:
	ida_free(&legion_intel_msr_sysfs_ida, data->intel_msr_sysfs_private.ida_id);
err_unregister_class:
	class_unregister(&legion_intel_msr_class);
	return ret;
}


void legion_intel_msr_sysfs_exit(const struct device *parent) {

	struct legion_data* data = dev_get_drvdata(parent);

	if (!data)
		return;

	/* Remove sysfs group first */
	device_remove_group(data->intel_msr_sysfs_private.dev,&legion_intel_msr_attributes_group);

	/* Device unregister will clean up remaining kobject hierarchy
	 * and wait for all outstanding sysfs operations to complete */
	device_unregister(data->intel_msr_sysfs_private.dev);

	/* Now safe to destroy mutex - all callbacks have completed */
	legion_intel_msr_exit(&data->intel_msr_private);

	ida_free(&legion_intel_msr_sysfs_ida, data->intel_msr_sysfs_private.ida_id);

	class_unregister(&legion_intel_msr_class);
}
