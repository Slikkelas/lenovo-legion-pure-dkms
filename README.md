# Universal Intel MSR & RAPL MMIO DKMS Driver

Many laptops clamp CPU power limits on Linux via the PCI Processor Thermal MMIO bar (`0x59A0`) when a Windows Dynamic Tuning Technology (DTT) handshake is absent. This universal DKMS module synchronizes standard CPU MSR power limits (`0x610`) into the RAPL MMIO registers and exposes Intel MSR voltage offset (undervolting) and turbo ratio controls via `sysfs`.

## Features

1. **Universal MSR-to-MMIO Power Limit Synchronization**
   * Automatically reads native CPU PL1, PL2, and Tau time windows from `MSR_PKG_POWER_LIMIT` (`0x610`) on boot and after suspend/resume, applying them directly to the processor thermal MMIO registers (`0x59A0`).
   * **Optional Hardware MMIO Lock (`lock_mmio=0` by default):** By default, the MMIO register is synchronized **without** setting the hardware lock bit (bit 63), allowing power profiles to adjust limits dynamically.
   * To permanently lock the MMIO register until the next reboot:
     * **Via Modprobe (persistent):** Edit `/etc/modprobe.d/lenovo-legion-lock_mmio.conf` and set:
       ```text
       options lenovo_legion lock_mmio=1
       ```
     * **Via Tuning Script:** Edit `/usr/local/bin/legion-tune-settings.sh` and set `LOCK_MMIO=1`.
     * **Via Sysfs (runtime):**
       ```bash
       echo 1 | sudo tee /sys/class/legion-intel-msr/intel-msr-0/mmio_lock
       ```

2. **Intel MSR Voltage Control (Undervolting / Overvolting)**
   * Exposes sysfs attributes under `/sys/class/legion-intel-msr/intel-msr-0/` for `cpu_offset`, `cache_offset`, `gpu_offset`, `uncore_offset`, and `analogio_offset` (in mV), plus per-point V/F curve offsets.

3. **Active Turbo Ratio & Per-Core HWP Limits**
   * Configure P-Core (`pcore_active_ratios`) and E-Core (`ecore_active_ratios`) turbo tables and per-core HWP limits (`core_ratio_limit`).

## Installation

```bash
sudo ./install.sh
sudo systemctl enable --now legion-tune.service
