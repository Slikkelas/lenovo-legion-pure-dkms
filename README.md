The Lenovo EC (Embedded Controller) clamps the CPU to 30W on Linux because it expects a Windows DTT handshake.
This is a stripped-down, bloat-free extraction of the lenovo_legion DKMS module that translates the ACPI WMI calls, unblocking the CPU limit natively.

Active Features of Module:

    Hardware Power Limit Unlocking (RAPL & MMIO Synchronization)

        The Lenovo EC natively expects a Windows Dynamic Tuning Technology (DTT) handshake. Without it, the EC aggressively clamps the CPU to ~30W via MMIO limits.
    
        The module hooks into the intel-rapl subsystem and syncs the standard MSR limits (PL1/PL2) into the thermal BAR MMIO registers (0x59A0, 0x59B0), effectively tricking the EC and restoring full wattage.
    (Optional)
        The module has a custom boot parameter (lock_mmio=Y) to permanently lock the MMIO registers to the MSR limits on boot.
            Sets bit 63 to 1. This means if you lock the power limits at maximum wattage during boot, and later decide you want to switch to a low-power "Quiet Mode" to save battery, you won't be able to lower the MMIO limits until you restart the system.
                Method: Modprobe Config
                This method tells the kernel's module manager to always append the parameter whenever lenovo_legion is loaded.
                Open your terminal and locate the configuration file in /etc/modprobe.d/lenovo-legion-lock_mmio.conf
                Add the following single line to the file:
                options lenovo_legion lock_mmio=1

    Intel MSR Voltage Control (Undervolting/Overvolting)

        Exposes deep sysfs controls to apply voltage offsets (undervolting) across 5 distinct planes: CPU Core, GPU, Cache, Uncore, and AnalogIO.

    Hardware Monitoring (hwmon)

        Exposes native sensors to Linux hwmon for CPU, GPU, and System temperatures, as well as current and maximum Fan RPMs.

    Lenovo Custom Fan Curves (WMI FM & FTable)

        Directly translates Lenovo's proprietary WMI Fan Methods (FM) to allow reading and writing of custom 10-point fan curve tables to the EC.

    WMI Gamezone & Thermal Profiles

        Event listening (like pressing Fn+Q) is handled by the standard Linux ACPI driver "ideapad_laptop", but this module still actively manages the backend states. It reads and applies Lenovo's Quiet, Balanced, Performance, and Custom thermal modes directly to the hardware.

    Extended EC Capabilities (WMI Other / CapData)

        Exposes toggles for advanced hardware features via sysfs, including iGPU mode switching (Auto/iGPU only), OverDrive (display overdrive), USB charging features, GPU overclocks, and touchpad disabling.
