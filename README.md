The Lenovo EC (Embedded Controller) clamps the CPU to 30W on Linux because it expects a Windows DTT handshake.
This is a stripped-down, bloat-free extraction of the lenovo_legion DKMS module that translates the ACPI WMI calls, unblocking the CPU limit natively.
