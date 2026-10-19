#!/bin/bash
# Universal Intel MSR & RAPL MMIO DKMS Installer

if [ "$EUID" -ne 0 ]; then
  echo "Please run this installer as root (sudo ./install.sh)"
  exit 1
fi

MOD_NAME=$(awk -F'=' '/^PACKAGE_NAME/ {gsub(/"/, "", $2); print $2}' dkms.conf)
MOD_VERS=$(awk -F'=' '/^PACKAGE_VERSION/ {gsub(/"/, "", $2); print $2}' dkms.conf)

echo "Installing $MOD_NAME version $MOD_VERS to DKMS..."

# 1. Remove any stale DKMS registration
dkms remove -m "${MOD_NAME}" -v "${MOD_VERS}" --all 2>/dev/null || true
rm -rf "/usr/src/${MOD_NAME}-${MOD_VERS}"

# 2. Copy clean source to /usr/src
mkdir -p "/usr/src/${MOD_NAME}-${MOD_VERS}"
cp -R . "/usr/src/${MOD_NAME}-${MOD_VERS}/"

# 3. Register, build, and install via DKMS
dkms add -m "${MOD_NAME}" -v "${MOD_VERS}"
dkms build -m "${MOD_NAME}" -v "${MOD_VERS}"
dkms install -m "${MOD_NAME}" -v "${MOD_VERS}"

# 4. Set up auto-load on boot (MMIO lock disabled by default)
echo "Setting up module auto-load and modprobe options (lock_mmio=0)..."
install -m 644 lenovo-legion.conf /etc/modules-load.d/lenovo-legion.conf
install -m 644 lenovo-legion-lock_mmio.conf /etc/modprobe.d/lenovo-legion-lock_mmio.conf

# 5. Reload module immediately
modprobe -r lenovo_legion 2>/dev/null || true
modprobe lenovo_legion

# 6. Install the tuning script and systemd service
echo "Installing legion-tune-settings.sh and legion-tune.service..."
install -m 755 legion-tune-settings.sh /usr/local/bin/legion-tune-settings.sh
install -m 644 legion-tune.service /etc/systemd/system/legion-tune.service
systemctl daemon-reload

echo ""
echo "===================================================="
echo "SUCCESS! Universal MSR & RAPL MMIO driver is loaded."
echo "MMIO lock is disabled by default (lock_mmio=0)."
echo "To enable the tuning service on boot and wake, run:"
echo "  sudo systemctl enable --now legion-tune.service"
echo "===================================================="
