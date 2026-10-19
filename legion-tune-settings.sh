#!/bin/bash

MSR_PATH="/sys/class/legion-intel-msr/intel-msr-0"
RAPL_MSR_PATH="/sys/class/powercap/intel-rapl:0"
RAPL_MMIO_PATH="/sys/class/powercap/intel-rapl-mmio:0"
MOD_LOCK_PARAM="/sys/module/lenovo_legion/parameters/lock_mmio"

# Optional MMIO Lock: Set to 1 to lock MMIO register, or 0 to leave unlocked (default: 0)
LOCK_MMIO=0

# 1. Wait for the DKMS module to initialize the sysfs path (up to 10 seconds)
for i in {1..10}; do
    if [ -d "$MSR_PATH" ]; then
        break
    fi
    sleep 1
done

# If the path still doesn't exist, exit safely
if [ ! -d "$MSR_PATH" ]; then
    echo "Error: legion-intel-msr sysfs path not found."
    exit 1
fi

# 2. Dynamically sync OS MSR power limits (intel-rapl:0) to MMIO (intel-rapl-mmio:0)
if [ -d "$RAPL_MSR_PATH" ] && [ -d "$RAPL_MMIO_PATH" ]; then
    MSR_PL1_UW=$(cat "$RAPL_MSR_PATH/constraint_0_power_limit_uw" 2>/dev/null)
    MSR_PL2_UW=$(cat "$RAPL_MSR_PATH/constraint_1_power_limit_uw" 2>/dev/null)

    if [ -n "$MSR_PL1_UW" ] && [ -n "$MSR_PL2_UW" ]; then
        echo "$MSR_PL1_UW" > "$RAPL_MMIO_PATH/constraint_0_power_limit_uw" 2>/dev/null || true
        echo "$MSR_PL2_UW" > "$RAPL_MMIO_PATH/constraint_1_power_limit_uw" 2>/dev/null || true
    fi
fi

# Only lock MMIO if enabled via LOCK_MMIO=1 or module parameter lock_mmio=1 (Y)
MOD_LOCK_VAL=$(cat "$MOD_LOCK_PARAM" 2>/dev/null || echo "N")
if [ "$LOCK_MMIO" -eq 1 ] || [ "$MOD_LOCK_VAL" = "Y" ]; then
    if [ -w "$MSR_PATH/mmio_lock" ]; then
        echo 1 > "$MSR_PATH/mmio_lock"
    fi
fi

# 3. Apply -55mV Undervolt to Cache, CPU, and iGPU
echo "-55" > "$MSR_PATH/cache_offset"
echo "-55" > "$MSR_PATH/cpu_offset"
echo "-55" > "$MSR_PATH/gpu_offset"

# 4. P-core & E-core Active Ratio Tables
# P-core Active Table: 1c 2c 3c 4c 5c 6c 7c 8c
# E-core Active Table: 1c 2c 3c 4c 6c 8c 12c 16c 
echo "52 50 48 47 46 46 46 44" > "$MSR_PATH/pcore_active_ratios"
echo "46 46 46 46 44 42 40 40" > "$MSR_PATH/ecore_active_ratios"

sleep 5

## Set P-cores (0-7) to ~4.6 GHz
#    for i in {0..7}; do
#        echo "$i 46" > "$MSR_PATH/core_ratio_limit"
#    done
## Set E-cores (8-23) to ~3.8 GHz
#    for i in {8..23}; do
#        echo "$i 38" > "$MSR_PATH/core_ratio_limit"
#    done

echo "========================================"
echo "   LEGION TUNING APPLIED SUCCESSFULLY   "
echo "========================================"
echo "--- Power Limits ---"
echo -n "MSR PL1 (µW):  "; cat "$RAPL_MSR_PATH/constraint_0_power_limit_uw" 2>/dev/null || echo "N/A"
echo -n "MMIO PL1 (µW): "; cat "$RAPL_MMIO_PATH/constraint_0_power_limit_uw" 2>/dev/null || echo "N/A"
echo -n "MSR PL2 (µW):  "; cat "$RAPL_MSR_PATH/constraint_1_power_limit_uw" 2>/dev/null || echo "N/A"
echo -n "MMIO PL2 (µW): "; cat "$RAPL_MMIO_PATH/constraint_1_power_limit_uw" 2>/dev/null || echo "N/A"
echo -n "MMIO Locked:   "; cat "$MSR_PATH/mmio_lock"
echo ""
echo "--- Undervolt Offsets ---"
echo -n "CPU Offset:    "; cat "$MSR_PATH/cpu_offset"
echo -n "Cache Offset:  "; cat "$MSR_PATH/cache_offset"
echo -n "GPU Offset:    "; cat "$MSR_PATH/gpu_offset"
echo ""
echo "--- P-core Active Table ---"
echo -n "1c 2c 3c 4c 5c 6c 7c 8c:     "; cat "$MSR_PATH/pcore_active_ratios"
echo "--- E-core Active Table ---"
echo -n "1c 2c 3c 4c 6c 8c 12c 16c:   "; cat "$MSR_PATH/ecore_active_ratios"
echo ""
echo "--- Core Ratio Limits ---"
cat "$MSR_PATH/core_ratio_limit"
echo "========================================"
