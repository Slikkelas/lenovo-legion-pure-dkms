// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright Jaroslav Bolek 2025
 *
 * Author(s):
 *   Jaroslav Bolek <jaroslav.bolek@gmail.com>
 * Modified by:
 *   Slikkelas
 */

#include "legion-intel-msr.h"

#include <linux/kernel.h>
#include <asm/msr.h>
#include <linux/smp.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <asm/cpu_device_id.h>
// Added by Slikkelas
#include <linux/cpufreq.h>
#include <asm/processor.h> // Required for cpuid_count
// end

#define MSR_VOLTAGE_OFFSET 0x150
#define MSR_OC_MAILBOX 0x150
#define MSR_OC_MAILBOX_CMD_READ_VOLTAGE_LIMIT 0x1A
// Added by Slikkelas
// Intel Hybrid Core Types (CPUID Leaf 0x1A)
#define INTEL_HYBRID_CORE_TYPE_PCORE 0x40
#define INTEL_HYBRID_CORE_TYPE_ECORE 0x20
// Domain definitions (MSR 0x150 Mailbox)
#define OC_DOMAIN_PCORE 0
#define OC_DOMAIN_ECORE 4 // E-cores (Atom L2) typically map to domain 4 on hybrid architectures
// end

/*
 * Read voltage offset from specific plane on current CPU
 */
struct read_msr_data {
    int plane;
    u64 result;
    int error;
};

// Added by Slikkelas
/* 
 * V/F Point Data Structure 
 */
struct vfpoint_data {
    int domain;
    int vf_point;
    int offset_uv; // Note: Handled as mV based on your earlier Slikkelas modifications
    u64 result;
    int error;
};
// end

/*
 * Convert microvolts offset to MSR format
 * Format: 21-bit signed integer in 1/1024 V units (two's complement)
 */
static u32 uv_to_msr(int uv)
{
    // Modified by Slikkelas
    // Conversion to mV instead of µV.
    // Convert mV to 1/1024V units
    // 1mV = 1.024 units of 1/1024V
    const s32 offset_units = DIV_ROUND_CLOSEST(uv * 1024, 1000);
    // Convert µV to 1/1024V units
    //** const s32 offset_units = (uv * 1024) / 1000000;
    // end
    // Handle two's complement for negative values
    // Mask to 21 bits
    return (u32)offset_units & 0x1FFFFF;
}


/*
 * Convert MSR format back to microvolts
 * MSR format: 11-bit signed integer in 1/1024 V units
 */
static int msr_to_uv(const u32 msr_value)
{
    s32 offset_units = 0;

    // Extract 11-bit value from bits [31:21]
    const u32 raw_value = (msr_value >> 21) & 0x7FF;

    // Sign extend from 11 bits to 32 bits
    if (raw_value & 0x400) {  // Check bit 10 (sign bit)
        // Negative value - sign extend
        offset_units = (s32)(raw_value | 0xFFFFF800);
    } else {
        // Positive value
        offset_units = (s32)raw_value;
    }
    // Modified by Slikkelas
    //** Convert from 1/1024V units to µV
    //** return (offset_units * 1000000) / 1024;
    // Convert from 1/1024V units to mV
    return DIV_ROUND_CLOSEST(offset_units * 1000, 1024);
    // end
}


/*
 * Validate voltage offset against per-plane limits
 */
static ssize_t validate_offset(const struct legion_intel_msr_private *intel_msr_private,const int plane,const  int offset_mv)
{
    if (plane < 0 || plane >= NUM_VOLTAGE_PLANES) {
        return -EINVAL;
    }

    if (offset_mv < -intel_msr_private->plane_limits[plane].max_undervolt_uv) {
        return -EINVAL;
    }

    if (offset_mv > intel_msr_private->plane_limits[plane].max_overvolt_uv) {
        return -EINVAL;
    }
    return 0;
}

// Added by Slikkelas
/*
 * Helper to dynamically detect the core type executing this code
 */

/* Helper struct for CPUID SMP calls */
struct cpuid_query {
    unsigned int eax, ebx, ecx, edx;
};

/* Named helper function for smp_call (Replaces the C++ lambda) */
static void get_cpuid_1a_work(void *info)
{
    struct cpuid_query *res = info;
    cpuid_count(0x1A, 0, &res->eax, &res->ebx, &res->ecx, &res->edx);
}

/* Core Type Detection */
static int get_intel_core_type_on_cpu(int cpu)
{
    struct cpuid_query res;
    // Check if Leaf 0x1A is supported
    if (cpuid_eax(0) >= 0x1A) {
        smp_call_function_single(cpu, get_cpuid_1a_work, &res, 1);
        return (res.eax >> 24) & 0xFF;
    }
    return INTEL_HYBRID_CORE_TYPE_PCORE;
}

/*
 * P-Core Active Core Table Manipulation (MSR 0x1AD)
 */
ssize_t legion_intel_msr_apply_pcore_active_ratios(struct legion_intel_msr_private *priv, u64 ratios)
{
    int cpu;
    bool success = false;
    u32 low = (u32)ratios;
    u32 high = (u32)(ratios >> 32);

    guard(mutex)(&priv->lock);

    for_each_online_cpu(cpu) {
        if (get_intel_core_type_on_cpu(cpu) == INTEL_HYBRID_CORE_TYPE_PCORE) {
            if (wrmsr_safe_on_cpu(cpu, MSR_TURBO_RATIO_LIMIT, low, high) == 0) {
                success = true;
            }
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

/*
 * E-Core Active Core Table Manipulation (MSR 0x66C / 0x650)
 */
ssize_t legion_intel_msr_apply_ecore_active_ratios(struct legion_intel_msr_private *priv, u64 ratios)
{
    int cpu;
    bool success = false;
    u32 low = (u32)ratios;
    u32 high = (u32)(ratios >> 32);

    guard(mutex)(&priv->lock);

    for_each_online_cpu(cpu) {
        if (get_intel_core_type_on_cpu(cpu) == INTEL_HYBRID_CORE_TYPE_ECORE) {
            // Write to both potential registers; unsupported ones will be ignored safely
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
            
            // Try standard Atom MSR (0x66C)
            if (rdmsr_safe_on_cpu(cpu, MSR_ATOM_CORE_TURBO_RATIOS, &low, &high) == 0) {
                *ratios = ((u64)high << 32) | low;
                return 0;
            }
            
            // Try Skymont/Arrow Lake specific MSR (0x650)
            if (rdmsr_safe_on_cpu(cpu, 0x650, &low, &high) == 0) {
                *ratios = ((u64)high << 32) | low;
                return 0;
            }
        }
    }

    return -EIO;
}

/* Per-Core Boost Ratio Manipulation via raw HWP (MSR 0x774) */
struct hwp_ratio_data {
    int cpu;
    int target_ratio_human;
};

static void write_per_core_ratio_on_cpu(void *info)
{
    struct hwp_ratio_data *data = info;
    u32 cap_low, cap_high, req_low, req_high;
    unsigned int max_freq_khz = cpufreq_quick_get_max(data->cpu); // e.g. 4700000

    if (max_freq_khz == 0) return;

    if (rdmsr_safe(MSR_HWP_CAPABILITIES, &cap_low, &cap_high) == 0) {
        u32 hwp_max = cap_low & 0xFF; // Abstract hardware max (e.g. 65)
        u32 human_max_ratio = max_freq_khz / 100000; // Translate to human ratio (47)
        
        // Scale the user's ratio (e.g. 40) into the abstract HWP limit (e.g. 55)
        u32 scaled_hwp = DIV_ROUND_CLOSEST(data->target_ratio_human * hwp_max, human_max_ratio);
        
        if (scaled_hwp > hwp_max) scaled_hwp = hwp_max;

        if (rdmsr_safe(MSR_HWP_REQUEST, &req_low, &req_high) == 0) {
            req_low &= ~(0xFF << 8); // Clear bits 15:8 (Maximum Performance)
            req_low |= ((scaled_hwp & 0xFF) << 8); // Inject scaled hardware limit
            wrmsr_safe(MSR_HWP_REQUEST, req_low, req_high);
        }
    }
}

static void read_per_core_ratio_on_cpu(void *info)
{
    struct hwp_ratio_data *data = info;
    u32 cap_low, cap_high, req_low, req_high;
    unsigned int max_freq_khz = cpufreq_quick_get_max(data->cpu); // e.g. 4700000

    data->target_ratio_human = 0; // Default

    if (max_freq_khz > 0 && rdmsr_safe(MSR_HWP_CAPABILITIES, &cap_low, &cap_high) == 0) {
        u32 hwp_max = cap_low & 0xFF; // Abstract hardware max (e.g. 65)
        u32 human_max_ratio = max_freq_khz / 100000; // Translate to human ratio (47)

        if (hwp_max > 0 && rdmsr_safe(MSR_HWP_REQUEST, &req_low, &req_high) == 0) {
            u32 active_hwp = (req_low >> 8) & 0xFF; 
            
            // Translate the abstract HWP back to a human ratio
            data->target_ratio_human = DIV_ROUND_CLOSEST(active_hwp * human_max_ratio, hwp_max);
        }
    }
}

ssize_t legion_intel_msr_set_per_core_ratio(struct legion_intel_msr_private *priv, int cpu, int ratio)
{
    struct hwp_ratio_data data = { .cpu = cpu, .target_ratio_human = ratio };
    if (ratio < 8 || ratio > 120) return -EINVAL; 
    
    guard(mutex)(&priv->lock);
    smp_call_function_single(cpu, write_per_core_ratio_on_cpu, &data, 1);
    return 0;
}

ssize_t legion_intel_msr_get_per_core_ratio(struct legion_intel_msr_private *priv, int cpu, int *ratio)
{
    struct hwp_ratio_data data = { .cpu = cpu, .target_ratio_human = 0 };
    guard(mutex)(&priv->lock);
    smp_call_function_single(cpu, read_per_core_ratio_on_cpu, &data, 1);
    
    if (data.target_ratio_human == 0) return -EIO;
    *ratio = data.target_ratio_human;
    return 0;
}

/*
 * Write V/F Point Offset via OC Mailbox
 */
static void write_vfpoint_offset_on_cpu(void *info)
{
    struct vfpoint_data *data = info;
    const u32 offset_encoded = uv_to_msr(data->offset_uv);
    u32 low = 0, high = 0;

    // MSR 0x150 format for V/F point offset:
    // [63]    = Busy bit (set to 1 to initiate command)
    // [47:40] = Domain/Plane ID
    // [39:32] = Command (0x14 = write V/F point offset)
    // [31:21] = Voltage offset (11-bit signed, two's complement)
    // [7:0]  = V/F point index
    const u64 msr_val = ((u64)1 << 63) |
                            ((u64)(data->domain & 0xFF) << 40) |
                            ((u64)0x14 << 32) |
                            ((u64)(offset_encoded & 0x7FF) << 21) |
                            ((u64)(data->vf_point & 0xFF));

    int err = wrmsr_safe(MSR_VOLTAGE_OFFSET, (const u32)msr_val, (const u32)(msr_val >> 32));
    if (err) {
        data->error = err;
        return;
    }

    // Polling loop to catch errors and prevent MSR contention
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

    data->error = 0;
}

/*
 * Read V/F Point Offset via OC Mailbox
 */
static void read_vfpoint_offset_on_cpu(void *info)
{
    struct vfpoint_data *data = info;
    u32 low = 0, high = 0;

    // Command 0x15 = read V/F point offset (Fixed: was 0x13)
    const u64 msr_val = ((u64)1 << 63) |
                            ((u64)(data->domain & 0xFF) << 40) |
                            ((u64)0x15 << 32) |                       
                            ((u64)(data->vf_point & 0xFF));

    int err = wrmsr_safe(MSR_OC_MAILBOX, (u32)msr_val, (u32)(msr_val >> 32));
    if (err) {
        data->error = err;
        return;
    }

    // REQUIRED: Polling loop to wait for data
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

    data->result = ((u64)high << 32) | low;
    data->error = 0;
}

/*
 * Read V/F Point Ratio via OC Mailbox
 */
static void read_vfpoint_ratio_on_cpu(void *info)
{
    struct vfpoint_data *data = info;
    u32 low = 0, high = 0;

    // Command 0x12 = read V/F point ratio
    const u64 msr_val = ((u64)1 << 63) |
                            ((u64)(data->domain & 0xFF) << 40) |
                            ((u64)0x12 << 32) |
                            ((u64)(data->vf_point & 0xFF));

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

    // The actual frequency ratio is returned in bits [7:0] of the lower 32 bits
    data->result = low & 0xFF;
    data->error = 0;
}

/*
 * P-Core V/F Point Sysfs Show & Store
 */
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
            // NEW: Print the error code so we aren't flying blind!
            len += scnprintf(buf + len, PAGE_SIZE - len, "%d: ERROR %d\n", i, data.error);
        }
    }
    return len;
}

ssize_t legion_intel_msr_pcore_vfpoint_offset_store(struct legion_intel_msr_private *priv, const char *buf, size_t count)
{
    int vf_point, offset_mv;
    
    // Expects input in the format: "<Point> <Offset>" (e.g., "6 -30")
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

    return count;
}

/*
 * E-Core V/F Point Sysfs Show & Store
 */
ssize_t legion_intel_msr_ecore_vfpoint_offset_show(struct legion_intel_msr_private *priv, char *buf)
{
    ssize_t len = 0;
    guard(mutex)(&priv->lock);

    // E-Cores have 7 V/F points. The 7th is mapped to the OC ratio.
    for (int i = 1; i <= 7; i++) {
        struct vfpoint_data data = { .domain = OC_DOMAIN_ECORE, .vf_point = i, .error = -1 };
        smp_call_function_single(0, read_vfpoint_offset_on_cpu, &data, 1);
        
        if (!data.error) {
            int offset_mv = msr_to_uv((u32)data.result);
            len += scnprintf(buf + len, PAGE_SIZE - len, "%d: %d\n", i, offset_mv);
        } else {
                    // NEW: Print the error code so we aren't flying blind!
                    len += scnprintf(buf + len, PAGE_SIZE - len, "%d: ERROR %d\n", i, data.error);
                }
    }
    return len;
}

ssize_t legion_intel_msr_ecore_vfpoint_offset_store(struct legion_intel_msr_private *priv, const char *buf, size_t count)
{
    int vf_point, offset_mv;
    
    // Expects input in the format: "<Point> <Offset>" (e.g., "7 -15")
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

    return count;
}

/*
 * P-Core V/F Point Freq (Ratio) Sysfs Show
 */
ssize_t legion_intel_msr_pcore_vfpoint_freq_show(struct legion_intel_msr_private *priv, char *buf)
{
    ssize_t len = 0;
    guard(mutex)(&priv->lock);

    for (int i = 1; i <= 15; i++) {
        struct vfpoint_data data = { .domain = OC_DOMAIN_PCORE, .vf_point = i, .error = -1 };
        smp_call_function_single(0, read_vfpoint_ratio_on_cpu, &data, 1);
        
        if (!data.error && data.result > 0) {
            len += scnprintf(buf + len, PAGE_SIZE - len, "%d: %llu\n", i, data.result);
        } else if (data.error) {
            // NEW: Print the error code
            len += scnprintf(buf + len, PAGE_SIZE - len, "%d: ERROR %d\n", i, data.error);
        }
    }
    return len;
}

/*
 * E-Core V/F Point Freq (Ratio) Sysfs Show
 */
ssize_t legion_intel_msr_ecore_vfpoint_freq_show(struct legion_intel_msr_private *priv, char *buf)
{
    ssize_t len = 0;
    guard(mutex)(&priv->lock);

    for (int i = 1; i <= 7; i++) {
        struct vfpoint_data data = { .domain = OC_DOMAIN_ECORE, .vf_point = i, .error = -1 };
        smp_call_function_single(0, read_vfpoint_ratio_on_cpu, &data, 1);
        
        if (!data.error && data.result > 0) {
            len += scnprintf(buf + len, PAGE_SIZE - len, "%d: %llu\n", i, data.result);
        } else if (data.error) {
                    // NEW: Print the error code
                    len += scnprintf(buf + len, PAGE_SIZE - len, "%d: ERROR %d\n", i, data.error);
                }
    }
    return len;
}
// end

/*
 * Write voltage offset to specific plane on a CPU
 */
static void write_voltage_offset_on_cpu(void *info)
{
    const struct {
        int plane;
        int offset_uv;
    } *data = info;

    const u32 offset_encoded = uv_to_msr(data->offset_uv);

    // MSR 0x150 format for voltage offset (OC Mailbox):
    // Based on VoltageShift and intel-undervolt implementations:
    // [63]    = Busy bit (set to 1 to initiate command)
    // [47:40] = Domain/Plane ID
    // [39:32] = Command (0x11 = write voltage offset)
    // [31:21] = Voltage offset (11-bit signed, two's complement, in 1/1024V units)
    // [20:0]  = Other fields (ratio, etc - set to 0 for voltage offset)
    const u64 msr_val = ((u64)1 << 63) |                // Busy bit
              ((u64)(data->plane & 0xFF) << 40) |        // Domain at bits [47:40]
              ((u64)0x11 << 32) |                        // Command at bits [39:32]
              ((u64)(offset_encoded & 0x7FF) << 21);     // Voltage offset at bits [31:21]

    wrmsr_safe(MSR_VOLTAGE_OFFSET, (const u32)msr_val, (const u32)(msr_val >> 32));
}

static void read_voltage_offset_on_cpu(void *info)
{
    struct read_msr_data *data = info;
    u32 low = 0, high = 0;

    // Construct read command for MSR 0x150
    // Command 0x10 = read voltage offset
    const u64 msr_val = ((u64)1 << 63) |                      // Busy bit
                        ((u64)(data->plane & 0xFF) << 40) |   // Domain
                        ((u64)0x10 << 32);                    // Read command

    // Write read command
    int err = wrmsr_safe(MSR_OC_MAILBOX, (u32)msr_val, (u32)(msr_val >> 32));
    if (err) {
        data->error = err;
        return;
    }

    // Small delay for mailbox to process
    udelay(10);

    // Read result
    err = rdmsr_safe(MSR_OC_MAILBOX, &low, &high);
    if (err) {
        data->error = err;
        return;
    }

    data->result = ((u64)high << 32) | low;
    data->error = 0;
}

/*
* Read voltage limits for all planes
* Must be called with intel_msr_private->lock held or during initialization
*/
static void read_voltage_limits(struct legion_intel_msr_private *intel_msr_private)
{
    for (int i = 0; i < NUM_VOLTAGE_PLANES; i++)
    {
        // Mark all planes as write-supported by default
        intel_msr_private->plane_limits[i].write_supported = 1;
    }
    
    // Core Ultra CPUs (Arrow Lake: 0xC5, 0xC6, 0xB5 and Meteor Lake: 0xAA, 0xAC) don't support 
    // uncore and analogio voltage offset writes. These planes can be read but writes are 
    // silently ignored by hardware.
    // Raptor Lake (0xB7, 0xBA, 0xBF) still supports all plane writes.
    if (boot_cpu_data.x86_model == 0xAA || boot_cpu_data.x86_model == 0xAC ||  // Meteor Lake
        boot_cpu_data.x86_model == 0xC5 || boot_cpu_data.x86_model == 0xC6 ||  // Arrow Lake
        boot_cpu_data.x86_model == 0xB5) {  // Arrow Lake U
        intel_msr_private->plane_limits[PLANE_UNCORE].write_supported = 0;
        intel_msr_private->plane_limits[PLANE_ANALOGIO].write_supported = 0;
    }
}

/*
 * Check if MSR 0x150 (OC Mailbox) is available on this CPU
 */
static int legion_intel_msr_check_msr_availability(struct legion_intel_msr_private *intel_msr_private)
{
    u32 low     = 0,
        high    = 0;

    // Check if we're on an Intel CPU
    if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL) {
        return -ENODEV;
    }

    // Check minimum CPU family (should be 6 for Core series)
    if (boot_cpu_data.x86 < 6) {
        return -ENODEV;
    }

    // Try to read MSR 0x150 to verify it exists
    const int err = rdmsr_safe_on_cpu(0, MSR_OC_MAILBOX, &low, &high);
    if (err) {
        return -ENODEV;
    }

    read_voltage_limits(intel_msr_private);


    return 0;
}


ssize_t legion_intel_msr_offset_read_show(struct legion_intel_msr_private *intel_msr_private,const int plane,int* offset_uv)
{
    struct read_msr_data data;

    data.plane = plane;
    data.error = -1;

    guard(mutex)(&intel_msr_private->lock);


    smp_call_function_single(0, read_voltage_offset_on_cpu, &data, 1);

    if (data.error) {
        return data.error;
    }

    *offset_uv = msr_to_uv((u32)data.result);

    return 0;
}


/*
 * Apply voltage offset to all CPUs for a given plane
 */
ssize_t legion_intel_msr_apply_voltage_offset(struct legion_intel_msr_private *intel_msr_private,const int plane,const int offset_uv)
{
    struct {
        int plane;
        int offset_uv;
    } data;

    data.plane = plane;
    data.offset_uv = offset_uv;

    guard(mutex)(&intel_msr_private->lock);

    // Check if this plane supports writes
    if (!intel_msr_private->plane_limits[plane].write_supported) {
        return -EOPNOTSUPP;  // Operation not supported
    }

    const ssize_t ret = validate_offset(intel_msr_private, data.plane, data.offset_uv);
    if (ret < 0)
        return ret;

    // Execute on all CPUs (including 0 to allow reset)
    on_each_cpu(write_voltage_offset_on_cpu, &data, 1);

    return 0;
}

int  legion_intel_msr_init(struct legion_intel_msr_private *intel_msr_private) {

	mutex_init(&intel_msr_private->lock);

	/* Initialize with defaults first to ensure safe reads if init fails */
	for (int i = 0; i < NUM_VOLTAGE_PLANES; i++) {
		intel_msr_private->plane_limits[i].max_undervolt_uv = DEFAULT_MAX_UNDERVOLT_UV;
		intel_msr_private->plane_limits[i].max_overvolt_uv = DEFAULT_MAX_OVERVOLT_UV;
	}

	return legion_intel_msr_check_msr_availability(intel_msr_private);
}

void legion_intel_msr_exit(struct legion_intel_msr_private *intel_msr_private) {
	mutex_destroy(&intel_msr_private->lock);
}
