#!/bin/bash
# Lenovo Legion Pure DKMS Installer

# Ensure the script is run as root
if [ "$EUID" -ne 0 ]; then
  echo "Please run this installer as root (sudo ./install.sh)"
  exit 1
fi

echo "Extracting module version..."
MOD_NAME=$(awk -F'=' '/^PACKAGE_NAME/ {gsub(/"/, "", $2); print $2}' dkms.conf)
MOD_VERS=$(awk -F'=' '/^PACKAGE_VERSION/ {gsub(/"/, "", $2); print $2}' dkms.conf)

echo "Installing $MOD_NAME version $MOD_VERS to DKMS..."

# 1. Copy source to the secure kernel tree
cp -R . /usr/src/${MOD_NAME}-${MOD_VERS}/

# 2. Register, build, and install via DKMS
dkms add -m ${MOD_NAME} -v ${MOD_VERS}
dkms build -m ${MOD_NAME} -v ${MOD_VERS}
dkms install -m ${MOD_NAME} -v ${MOD_VERS}

# 3. Set up auto-load on boot
echo "Setting up auto-load on boot..."
cp lenovo-legion.conf /etc/modules-load.d/

# 4. Load the module immediately
modprobe lenovo_legion

echo ""
echo "===================================================="
echo "SUCCESS! The Lenovo Legion Pure DKMS driver is loaded."
echo "Your CPU limits are unlocked and follows the Fn+Q performance profiles."
echo "===================================================="
