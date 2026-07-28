#!/bin/bash
set -e

VERSION=$(
    grep 'ALG_RGB_VERSION_STRING' include/version.h |
    sed 's/.*"\(.*\)".*/\1/'
)

if [[ -x /usr/local/bin/alg-rgb ]]; then
    echo "[*] Stopping any background RGB animation..."
    sudo pkill -TERM -x alg-rgb 2>/dev/null || true
    sleep 0.1
    sudo /usr/local/bin/alg-rgb stop >/dev/null 2>&1 || true
fi

echo "[*] Removing DKMS module..."
sudo dkms remove "alg-rgb/${VERSION}" --all 2>/dev/null || true

echo "[*] Removing CLI..."
sudo rm -f /usr/local/bin/alg-rgb

echo "[*] Removing udev rule..."
sudo rm -f /etc/udev/rules.d/99-alg-rgb.rules

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
echo
echo "A reboot is recommended if the module was loaded."
