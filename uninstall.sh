#!/usr/bin/env bash

set -euo pipefail

VERSION=$(
    grep 'ALG_RGB_VERSION_STRING' include/version.h |
    sed 's/.*"\(.*\)".*/\1/'
)

if [[ -x /usr/local/bin/alg-rgb ]]; then
    echo "[*] Stopping any background RGB animation..."
    sudo /usr/local/bin/alg-rgb stop >/dev/null 2>&1 || true
fi

echo "[*] Unloading kernel module..."
if [[ -d /sys/module/alg_rgb ]]; then
    sudo modprobe -r alg_rgb
fi

echo "[*] Removing DKMS module..."
sudo dkms remove "alg-rgb/${VERSION}" --all 2>/dev/null || true

echo "[*] Removing CLI..."
sudo rm -f /usr/local/bin/alg-rgb

echo "[*] Removing udev rule..."
sudo rm -f /etc/udev/rules.d/99-alg-rgb.rules

echo "[*] Removing module autoload configuration..."
sudo rm -f /etc/modules-load.d/alg_rgb.conf

echo "[*] Removing runtime state configuration..."
sudo rm -f /usr/lib/tmpfiles.d/alg-rgb.conf
sudo rm -f /run/alg-rgb/animation.lock
sudo rmdir /run/alg-rgb 2>/dev/null || true
sudo rm -f /run/alg-rgb-animation.pid

echo "[*] Reloading udev..."
sudo udevadm control --reload-rules
sudo udevadm trigger

echo "[*] Removing module source..."
sudo rm -rf "/usr/src/alg-rgb-${VERSION}"

echo "[*] Removing group..."
if getent group alg-rgb >/dev/null; then
    sudo groupdel alg-rgb 2>/dev/null || true
fi

echo
echo "Uninstallation complete."
