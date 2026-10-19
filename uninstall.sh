#!/bin/bash
# Universal Intel MSR & RAPL MMIO DKMS Uninstaller

if [ "$EUID" -ne 0 ]; then
  echo "Please run this uninstaller as root (sudo ./uninstall.sh)"
  exit 1
fi

# Extract module name and version from dkms.conf (with safe fallbacks)
if [ -f dkms.conf ]; then
  MOD_NAME=$(awk -F'=' '/^PACKAGE_NAME/ {gsub(/"/, "", $2); print $2}' dkms.conf)
  MOD_VERS=$(awk -F'=' '/^PACKAGE_VERSION/ {gsub(/"/, "", $2); print $2}' dkms.conf)
else
  MOD_NAME="LenovoLegion"
  MOD_VERS="2.0.0"
fi

MSR_PATH="/sys/class/legion-intel-msr/intel-msr-0"

echo "Uninstalling $MOD_NAME version $MOD_VERS..."

# 1. Stop and disable the systemd tuning service
if systemctl list-unit-files | grep -q "^legion-tune.service"; then
  echo "Stopping and disabling legion-tune.service..."
  systemctl stop legion-tune.service 2>/dev/null || true
  systemctl disable legion-tune.service 2>/dev/null || true
fi

# 2. Remove systemd service and tuning script
echo "Removing systemd unit and tuning script..."
rm -f /etc/systemd/system/legion-tune.service
rm -f /usr/local/bin/legion-tune-settings.sh
systemctl daemon-reload
systemctl reset-failed 2>/dev/null || true

# 3. Reset voltage offsets to 0mV before unloading the module
if [ -d "$MSR_PATH" ]; then
  echo "Resetting voltage offsets to 0mV..."
  for plane in cpu_offset cache_offset gpu_offset uncore_offset analogio_offset; do
    if [ -w "$MSR_PATH/$plane" ]; then
      echo "0" > "$MSR_PATH/$plane" 2>/dev/null || true
    fi
  done
fi

# 4. Unload the kernel module
if lsmod | grep -q "^lenovo_legion"; then
  echo "Unloading lenovo_legion kernel module..."
  modprobe -r lenovo_legion 2>/dev/null || true
fi

# 5. Remove auto-load and modprobe config files
echo "Removing module auto-load and modprobe configurations..."
rm -f /etc/modules-load.d/lenovo-legion.conf
rm -f /etc/modprobe.d/lenovo-legion-lock_mmio.conf

# 6. Unregister from DKMS and delete source tree
echo "Removing $MOD_NAME ($MOD_VERS) from DKMS..."
dkms remove -m "${MOD_NAME}" -v "${MOD_VERS}" --all 2>/dev/null || true
rm -rf "/usr/src/${MOD_NAME}-${MOD_VERS}"

# 7. Refresh module dependencies
depmod -a

echo ""
echo "===================================================="
echo "SUCCESS! $MOD_NAME and legion-tune.service have been removed."
echo "Note: If MMIO lock (bit 63) or custom turbo ratios were active,"
echo "reboot your system to restore default hardware registers."
echo "===================================================="
