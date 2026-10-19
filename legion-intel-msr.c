// SPDX-License-Identifier: GPL-2.0
#include "legion-intel-msr.h"

#include <linux/kernel.h>
#include <linux/cleanup.h>
#include <asm/msr.h>
#include <linux/smp.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <asm/cpu_device_id.h>
#include <linux/cpufreq.h>
#include <asm/processor.h>

#define MSR_VOLTAGE_OFFSET 0x150
#define MSR_OC_MAILBOX     0x150

#define INTEL_HYBRID_CORE_TYPE_PCORE 0x40
#define INTEL_HYBRID_CORE_TYPE_ECORE 0x20

#define OC_DOMAIN_PCORE 0
#define OC_DOMAIN_ECORE 4

struct read_msr_data {
	int plane;
	u64 result;
	int error;
};

struct vfpoint_data {
	int domain;
	int vf_point;
	int offset_uv;
	u64 result;
	int error;
};

static u32 uv_to_msr(int uv)
{
	const s32 offset_units = DIV_ROUND_CLOSEST(uv * 1024, 1000);

	return (u32)offset_units & 0x1FFFFF;
}

static int msr_to_uv(const u32 msr_value)
{
	s32 offset_units = 0;
	const u32 raw_value = (msr_value >> 21) & 0x7FF;

	if (raw_value & 0x400)
		offset_units = (s32)(raw_value | 0xFFFFF800);
	else
		offset_units = (s32)raw_value;

	return DIV_ROUND_CLOSEST(offset_units * 1000, 1024);
}

static ssize_t validate_offset(const struct legion_intel_msr_private *intel_msr_private,
			       const int plane, const int offset_mv)
{
	if (plane < 0 || plane >= NUM_VOLTAGE_PLANES)
		return -EINVAL;
	if (offset_mv < -intel_msr_private->plane_limits[plane].max_undervolt_uv)
		return -EINVAL;
	if (offset_mv > intel_msr_private->plane_limits[plane].max_overvolt_uv)
		return -EINVAL;

	return 0;
}

struct cpuid_query {
	unsigned int eax, ebx, ecx, edx;
};

static void get_cpuid_1a_work(void *info)
{
	struct cpuid_query *res = info;

	cpuid_count(0x1A, 0, &res->eax, &res->ebx, &res->ecx, &res->edx);
}

static int get_intel_core_type_on_cpu(int cpu)
{
	struct cpuid_query res = {0};

	if (cpuid_eax(0) >= 0x1A) {
		smp_call_function_single(cpu, get_cpuid_1a_work, &res, 1);
		return (res.eax >> 24) & 0xFF;
	}
	return INTEL_HYBRID_CORE_TYPE_PCORE;
}

ssize_t legion_intel_msr_apply_pcore_active_ratios(struct legion_intel_msr_private *priv, u64 ratios)
{
	int cpu;
	bool success = false;
	u32 low = (u32)ratios;
	u32 high = (u32)(ratios >> 32);

	guard(mutex)(&priv->lock);

	for_each_online_cpu(cpu) {
		if (get_intel_core_type_on_cpu(cpu) == INTEL_HYBRID_CORE_TYPE_PCORE) {
			if (wrmsr_safe_on_cpu(cpu, MSR_TURBO_RATIO_LIMIT, low, high) == 0)
				success = true;
		}
	}
	return success ? 0 : -EIO;
}

ssize_t legion_intel_msr_read_pcore_active_ratios(struct legion_intel_msr_private *priv, u64 *ratios)
{
	int cpu;
	u32 low, high;

	guard(mutex)(&priv->lock);

	for_each_online_cpu(cpu) {
		if (get_intel_core_type_on_cpu(cpu) == INTEL_HYBRID_CORE_TYPE_PCORE) {
			if (rdmsr_safe_on_cpu(cpu, MSR_TURBO_RATIO_LIMIT, &low, &high) == 0) {
				*ratios = ((u64)high << 32) | low;
				return 0;
			}
		}
	}
	return -EIO;
}

ssize_t legion_intel_msr_apply_ecore_active_ratios(struct legion_intel_msr_private *priv, u64 ratios)
{
	int cpu;
	bool success = false;
	u32 low = (u32)ratios;
	u32 high = (u32)(ratios >> 32);

	guard(mutex)(&priv->lock);

	for_each_online_cpu(cpu) {
		if (get_intel_core_type_on_cpu(cpu) == INTEL_HYBRID_CORE_TYPE_ECORE) {
			wrmsr_safe_on_cpu(cpu, MSR_ATOM_CORE_TURBO_RATIOS, low, high);
			wrmsr_safe_on_cpu(cpu, 0x650, low, high);
			success = true;
		}
	}
	return success ? 0 : -EIO;
}

ssize_t legion_intel_msr_read_ecore_active_ratios(struct legion_intel_msr_private *priv, u64 *ratios)
{
	int cpu;
	u32 low, high;

	guard(mutex)(&priv->lock);

	for_each_online_cpu(cpu) {
		if (get_intel_core_type_on_cpu(cpu) == INTEL_HYBRID_CORE_TYPE_ECORE) {
			if (rdmsr_safe_on_cpu(cpu, MSR_ATOM_CORE_TURBO_RATIOS, &low, &high) == 0) {
				*ratios = ((u64)high << 32) | low;
				return 0;
			}
			if (rdmsr_safe_on_cpu(cpu, 0x650, &low, &high) == 0) {
				*ratios = ((u64)high << 32) | low;
				return 0;
			}
		}
	}
	return -EIO;
}

struct hwp_ratio_data {
	int cpu;
	int target_ratio_human;
};

static void write_per_core_ratio_on_cpu(void *info)
{
	struct hwp_ratio_data *data = info;
	u32 cap_low, cap_high, req_low, req_high;
	unsigned int max_freq_khz = cpufreq_quick_get_max(data->cpu);

	if (max_freq_khz == 0)
		return;

	if (rdmsr_safe(MSR_HWP_CAPABILITIES, &cap_low, &cap_high) == 0) {
		u32 hwp_max = cap_low & 0xFF;
		u32 human_max_ratio = max_freq_khz / 100000;
		u32 scaled_hwp = DIV_ROUND_CLOSEST(data->target_ratio_human * hwp_max, human_max_ratio);

		if (scaled_hwp > hwp_max)
			scaled_hwp = hwp_max;

		if (rdmsr_safe(MSR_HWP_REQUEST, &req_low, &req_high) == 0) {
			req_low &= ~(0xFF << 8);
			req_low |= ((scaled_hwp & 0xFF) << 8);
			wrmsr_safe(MSR_HWP_REQUEST, req_low, req_high);
		}
	}
}

static void read_per_core_ratio_on_cpu(void *info)
{
	struct hwp_ratio_data *data = info;
	u32 cap_low, cap_high, req_low, req_high;
	unsigned int max_freq_khz = cpufreq_quick_get_max(data->cpu);

	data->target_ratio_human = 0;

	if (max_freq_khz > 0 && rdmsr_safe(MSR_HWP_CAPABILITIES, &cap_low, &cap_high) == 0) {
		u32 hwp_max = cap_low & 0xFF;
		u32 human_max_ratio = max_freq_khz / 100000;

		if (hwp_max > 0 && rdmsr_safe(MSR_HWP_REQUEST, &req_low, &req_high) == 0) {
			u32 active_hwp = (req_low >> 8) & 0xFF;

			data->target_ratio_human = DIV_ROUND_CLOSEST(active_hwp * human_max_ratio, hwp_max);
		}
	}
}

ssize_t legion_intel_msr_set_per_core_ratio(struct legion_intel_msr_private *priv, int cpu, int ratio)
{
	struct hwp_ratio_data data = { .cpu = cpu, .target_ratio_human = ratio };

	if (ratio < 8 || ratio > 120)
		return -EINVAL;

	guard(mutex)(&priv->lock);
	smp_call_function_single(cpu, write_per_core_ratio_on_cpu, &data, 1);
	return 0;
}

ssize_t legion_intel_msr_get_per_core_ratio(struct legion_intel_msr_private *priv, int cpu, int *ratio)
{
	struct hwp_ratio_data data = { .cpu = cpu, .target_ratio_human = 0 };

	guard(mutex)(&priv->lock);
	smp_call_function_single(cpu, read_per_core_ratio_on_cpu, &data, 1);

	if (data.target_ratio_human == 0)
		return -EIO;

	*ratio = data.target_ratio_human;
	return 0;
}

static void write_vfpoint_offset_on_cpu(void *info)
{
	struct vfpoint_data *data = info;
	const u32 offset_encoded = uv_to_msr(data->offset_uv);
	u32 low = 0, high = 0;
	const u64 msr_val = ((u64)1 << 63) |
			    ((u64)(data->domain & 0xFF) << 40) |
			    ((u64)0x11 << 32) |
			    ((u64)(offset_encoded & 0x7FF) << 21) |
			    ((u64)(data->vf_point & 0xFF) << 8);

	int err = wrmsr_safe(MSR_VOLTAGE_OFFSET, (u32)msr_val, (u32)(msr_val >> 32));
	if (err) {
		data->error = err;
		return;
	}

	int timeout = 100;
	do {
		udelay(10);
		err = rdmsr_safe(MSR_VOLTAGE_OFFSET, &low, &high);
		if (err) {
			data->error = err;
			return;
		}
	} while ((high & 0x80000000) && --timeout);

	if (timeout == 0) {
		data->error = -ETIMEDOUT;
		return;
	}

	if ((high & 0xFF) != 0) {
		data->error = -(high & 0xFF);
		return;
	}

	data->error = 0;
}

static void read_vfpoint_offset_on_cpu(void *info)
{
	struct vfpoint_data *data = info;
	u32 low = 0, high = 0;
	const u64 msr_val = ((u64)1 << 63) |
			    ((u64)(data->domain & 0xFF) << 40) |
			    ((u64)0x10 << 32) |
			    ((u64)(data->vf_point & 0xFF) << 8);

	int err = wrmsr_safe(MSR_OC_MAILBOX, (u32)msr_val, (u32)(msr_val >> 32));
	if (err) {
		data->error = err;
		return;
	}

	udelay(10);
	err = rdmsr_safe(MSR_OC_MAILBOX, &low, &high);
	if (err) {
		data->error = err;
		return;
	}

	data->result = ((u64)high << 32) | low;
	data->error = 0;
}

ssize_t legion_intel_msr_pcore_vfpoint_offset_show(struct legion_intel_msr_private *priv, char *buf)
{
	ssize_t len = 0;

	guard(mutex)(&priv->lock);

	for (int i = 1; i <= 15; i++) {
		struct vfpoint_data data = { .domain = OC_DOMAIN_PCORE, .vf_point = i, .error = -1 };

		smp_call_function_single(0, read_vfpoint_offset_on_cpu, &data, 1);
		if (!data.error) {
			int offset_mv = msr_to_uv((u32)data.result);

			len += scnprintf(buf + len, PAGE_SIZE - len, "%d: %d\n", i, offset_mv);
		} else {
			len += scnprintf(buf + len, PAGE_SIZE - len, "%d: ERR %d\n", i, data.error);
		}
	}
	return len;
}

ssize_t legion_intel_msr_pcore_vfpoint_offset_store(struct legion_intel_msr_private *priv, const char *buf, size_t count)
{
	int vf_point, offset_mv;

	if (sscanf(buf, "%d %d", &vf_point, &offset_mv) != 2)
		return -EINVAL;
	if (vf_point < 1 || vf_point > 15)
		return -EINVAL;

	struct vfpoint_data data = {
		.domain = OC_DOMAIN_PCORE,
		.vf_point = vf_point,
		.offset_uv = offset_mv
	};

	guard(mutex)(&priv->lock);
	on_each_cpu(write_vfpoint_offset_on_cpu, &data, 1);

	if (data.error)
		return -EIO;

	return count;
}

ssize_t legion_intel_msr_ecore_vfpoint_offset_show(struct legion_intel_msr_private *priv, char *buf)
{
	ssize_t len = 0;

	guard(mutex)(&priv->lock);

	for (int i = 1; i <= 7; i++) {
		struct vfpoint_data data = { .domain = OC_DOMAIN_ECORE, .vf_point = i, .error = -1 };

		smp_call_function_single(0, read_vfpoint_offset_on_cpu, &data, 1);
		if (!data.error) {
			int offset_mv = msr_to_uv((u32)data.result);

			len += scnprintf(buf + len, PAGE_SIZE - len, "%d: %d\n", i, offset_mv);
		} else {
			len += scnprintf(buf + len, PAGE_SIZE - len, "%d: ERR %d\n", i, data.error);
		}
	}
	return len;
}

ssize_t legion_intel_msr_ecore_vfpoint_offset_store(struct legion_intel_msr_private *priv, const char *buf, size_t count)
{
	int vf_point, offset_mv;

	if (sscanf(buf, "%d %d", &vf_point, &offset_mv) != 2)
		return -EINVAL;
	if (vf_point < 1 || vf_point > 7)
		return -EINVAL;

	struct vfpoint_data data = {
		.domain = OC_DOMAIN_ECORE,
		.vf_point = vf_point,
		.offset_uv = offset_mv
	};

	guard(mutex)(&priv->lock);
	on_each_cpu(write_vfpoint_offset_on_cpu, &data, 1);

	if (data.error)
		return -EIO;

	return count;
}

static void read_vfpoint_ratio_on_cpu(void *info)
{
	struct vfpoint_data *data = info;
	u32 low = 0, high = 0;
	const u64 msr_val = ((u64)1 << 63) |
			    ((u64)(data->domain & 0xFF) << 40) |
			    ((u64)0x10 << 32) |
			    ((u64)(data->vf_point & 0xFF) << 8);

	int err = wrmsr_safe(MSR_OC_MAILBOX, (u32)msr_val, (u32)(msr_val >> 32));
	if (err) {
		data->error = err;
		return;
	}

	int timeout = 100;
	do {
		udelay(10);
		err = rdmsr_safe(MSR_OC_MAILBOX, &low, &high);
		if (err) {
			data->error = err;
			return;
		}
	} while ((high & 0x80000000) && --timeout);

	if (timeout == 0) {
		data->error = -ETIMEDOUT;
		return;
	}

	if ((high & 0xFF) != 0) {
		data->error = -(high & 0xFF);
		return;
	}

	data->result = (low >> 8) & 0xFF;
	data->error = 0;
}

ssize_t legion_intel_msr_pcore_vfpoint_freq_show(struct legion_intel_msr_private *priv, char *buf)
{
	ssize_t len = 0;

	guard(mutex)(&priv->lock);

	for (int i = 1; i <= 15; i++) {
		struct vfpoint_data data = { .domain = OC_DOMAIN_PCORE, .vf_point = i, .error = -1 };

		smp_call_function_single(0, read_vfpoint_ratio_on_cpu, &data, 1);
		if (!data.error)
			len += scnprintf(buf + len, PAGE_SIZE - len, "%d: %llu\n", i, data.result);
		else
			len += scnprintf(buf + len, PAGE_SIZE - len, "%d: ERR %d\n", i, data.error);
	}
	return len;
}

ssize_t legion_intel_msr_ecore_vfpoint_freq_show(struct legion_intel_msr_private *priv, char *buf)
{
	ssize_t len = 0;

	guard(mutex)(&priv->lock);

	for (int i = 1; i <= 7; i++) {
		struct vfpoint_data data = { .domain = OC_DOMAIN_ECORE, .vf_point = i, .error = -1 };

		smp_call_function_single(0, read_vfpoint_ratio_on_cpu, &data, 1);
		if (!data.error)
			len += scnprintf(buf + len, PAGE_SIZE - len, "%d: %llu\n", i, data.result);
		else
			len += scnprintf(buf + len, PAGE_SIZE - len, "%d: ERR %d\n", i, data.error);
	}
	return len;
}

static void bruteforce_domains_on_cpu(void *info)
{
	int cpu = smp_processor_id();
	u32 low = 0, high = 0;

	for (u64 domain = 0x0; domain <= 0xFF; domain++) {
		u32 offset_encoded = uv_to_msr(0);
		u64 msr_val = ((u64)1 << 63) |
			      (domain << 40) |
			      ((u64)0x11 << 32) |
			      ((u64)(offset_encoded & 0x7FF) << 21) |
			      ((u64)1 << 8);

		if (wrmsr_safe(MSR_OC_MAILBOX, (u32)msr_val, (u32)(msr_val >> 32)))
			continue;

		int timeout = 100;
		do {
			udelay(10);
			if (rdmsr_safe(MSR_OC_MAILBOX, &low, &high))
				break;
		} while ((high & 0x80000000) && --timeout);

		if (timeout == 0)
			continue;

		u8 pcu_status = high & 0xFF;
		if (pcu_status == 0)
			pr_info("legion-intel-msr: CPU %d accepted Domain 0x%02llX\n", cpu, domain);
	}
}

ssize_t legion_intel_msr_bruteforce_store(struct legion_intel_msr_private *priv, const char *buf, size_t count)
{
	int pcore_target = -1;
	int ecore_target = -1;
	int cpu;

	for_each_online_cpu(cpu) {
		if (pcore_target == -1 && get_intel_core_type_on_cpu(cpu) == INTEL_HYBRID_CORE_TYPE_PCORE)
			pcore_target = cpu;
		if (ecore_target == -1 && get_intel_core_type_on_cpu(cpu) == INTEL_HYBRID_CORE_TYPE_ECORE)
			ecore_target = cpu;
	}

	if (pcore_target != -1)
		smp_call_function_single(pcore_target, bruteforce_domains_on_cpu, NULL, 1);
	if (ecore_target != -1)
		smp_call_function_single(ecore_target, bruteforce_domains_on_cpu, NULL, 1);

	return count;
}

ssize_t legion_intel_msr_bruteforce_read_store(struct legion_intel_msr_private *priv, const char *buf, size_t count)
{
	u32 target_domain;
	u32 low = 0, high = 0;

	if (kstrtouint(buf, 0, &target_domain) || target_domain > 0xFF)
		return -EINVAL;

	for (int index = 0; index <= 0xFF; index++) {
		u64 msr_val = ((u64)1 << 63) |
			      ((u64)(target_domain & 0xFF) << 40) |
			      ((u64)0x10 << 32) |
			      ((u64)(index & 0xFF) << 8);

		preempt_disable();
		if (wrmsr_safe(MSR_OC_MAILBOX, (u32)msr_val, (u32)(msr_val >> 32))) {
			preempt_enable();
			continue;
		}

		int timeout = 100;
		do {
			udelay(10);
			if (rdmsr_safe(MSR_OC_MAILBOX, &low, &high))
				break;
		} while ((high & 0x80000000) && --timeout);
		preempt_enable();

		if (timeout == 0)
			continue;

		if ((high & 0xFF) == 0) {
			int offset_mv = msr_to_uv(low);
			u8 ratio = (low >> 8) & 0xFF;

			pr_info("legion-intel-msr: [Domain 0x%02X] Index 0x%02X -> %dmV, Ratio: %d\n",
				target_domain, index, offset_mv, ratio);
		}
	}

	return count;
}

static void write_voltage_offset_on_cpu(void *info)
{
	const struct {
		int plane;
		int offset_uv;
	} *data = info;

	const u32 offset_encoded = uv_to_msr(data->offset_uv);
	const u64 msr_val = ((u64)1 << 63) |
			    ((u64)(data->plane & 0xFF) << 40) |
			    ((u64)0x11 << 32) |
			    ((u64)(offset_encoded & 0x7FF) << 21);

	wrmsr_safe(MSR_VOLTAGE_OFFSET, (const u32)msr_val, (const u32)(msr_val >> 32));
}

static void read_voltage_offset_on_cpu(void *info)
{
	struct read_msr_data *data = info;
	u32 low = 0, high = 0;
	const u64 msr_val = ((u64)1 << 63) |
			    ((u64)(data->plane & 0xFF) << 40) |
			    ((u64)0x10 << 32);

	int err = wrmsr_safe(MSR_OC_MAILBOX, (u32)msr_val, (u32)(msr_val >> 32));
	if (err) {
		data->error = err;
		return;
	}

	udelay(10);
	err = rdmsr_safe(MSR_OC_MAILBOX, &low, &high);
	if (err) {
		data->error = err;
		return;
	}

	data->result = ((u64)high << 32) | low;
	data->error = 0;
}

static void read_voltage_limits(struct legion_intel_msr_private *intel_msr_private)
{
	for (int i = 0; i < NUM_VOLTAGE_PLANES; i++)
		intel_msr_private->plane_limits[i].write_supported = 1;

	if (boot_cpu_data.x86_model == 0xAA || boot_cpu_data.x86_model == 0xAC ||
	    boot_cpu_data.x86_model == 0xC5 || boot_cpu_data.x86_model == 0xC6 ||
	    boot_cpu_data.x86_model == 0xB5) {
		intel_msr_private->plane_limits[PLANE_UNCORE].write_supported = 0;
		intel_msr_private->plane_limits[PLANE_ANALOGIO].write_supported = 0;
	}
}

static int legion_intel_msr_check_msr_availability(struct legion_intel_msr_private *intel_msr_private)
{
	u32 low = 0, high = 0;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL || boot_cpu_data.x86 < 6)
		return -ENODEV;

	if (rdmsr_safe_on_cpu(0, MSR_OC_MAILBOX, &low, &high))
		return -ENODEV;

	read_voltage_limits(intel_msr_private);
	return 0;
}

ssize_t legion_intel_msr_offset_read_show(struct legion_intel_msr_private *intel_msr_private,
					  const int plane, int *offset_uv)
{
	struct read_msr_data data = { .plane = plane, .error = -1 };

	guard(mutex)(&intel_msr_private->lock);
	smp_call_function_single(0, read_voltage_offset_on_cpu, &data, 1);

	if (data.error)
		return data.error;

	*offset_uv = msr_to_uv((u32)data.result);
	return 0;
}

ssize_t legion_intel_msr_apply_voltage_offset(struct legion_intel_msr_private *intel_msr_private,
					      const int plane, const int offset_uv)
{
	struct {
		int plane;
		int offset_uv;
	} data = { .plane = plane, .offset_uv = offset_uv };

	guard(mutex)(&intel_msr_private->lock);

	if (!intel_msr_private->plane_limits[plane].write_supported)
		return -EOPNOTSUPP;

	const ssize_t ret = validate_offset(intel_msr_private, data.plane, data.offset_uv);
	if (ret < 0)
		return ret;

	on_each_cpu(write_voltage_offset_on_cpu, &data, 1);
	return 0;
}

int legion_intel_msr_init(struct legion_intel_msr_private *intel_msr_private)
{
	mutex_init(&intel_msr_private->lock);

	for (int i = 0; i < NUM_VOLTAGE_PLANES; i++) {
		intel_msr_private->plane_limits[i].max_undervolt_uv = DEFAULT_MAX_UNDERVOLT_UV;
		intel_msr_private->plane_limits[i].max_overvolt_uv = DEFAULT_MAX_OVERVOLT_UV;
	}

	return legion_intel_msr_check_msr_availability(intel_msr_private);
}

void legion_intel_msr_exit(struct legion_intel_msr_private *intel_msr_private)
{
	mutex_destroy(&intel_msr_private->lock);
}
