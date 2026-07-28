#!/usr/bin/env bash

set -euo pipefail

VERSION=$(
    grep 'ALG_RGB_VERSION_STRING' include/version.h |
    sed 's/.*"\(.*\)".*/\1/'
)

DKMS_NAME="alg-rgb"
TARGET_USER="${SUDO_USER:-$(id -un)}"
NEEDS_RELOGIN=0

echo "[*] Building CLI..."
make -C cli

if [[ -x /usr/local/bin/alg-rgb ]]; then
    echo "[*] Stopping any background RGB animation..."
    sudo /usr/local/bin/alg-rgb stop >/dev/null 2>&1 || true
fi

# Remove the state file used by development versions before 0.1.3.
sudo rm -f /run/alg-rgb-animation.pid

echo "[*] Installing CLI..."
sudo install -Dm755 cli/alg-rgb /usr/local/bin/alg-rgb

echo "[*] Removing the previous DKMS registration..."
sudo dkms remove -m "${DKMS_NAME}" -v "${VERSION}" --all 2>/dev/null || true

echo "[*] Installing DKMS source..."
sudo rm -rf "/usr/src/${DKMS_NAME}-${VERSION}"
sudo install -Dm644 \
    dkms.conf \
    "/usr/src/${DKMS_NAME}-${VERSION}/dkms.conf"
sudo install -Dm644 \
    kernel/Makefile \
    "/usr/src/${DKMS_NAME}-${VERSION}/kernel/Makefile"
sudo install -Dm644 \
    kernel/alg_rgb.c \
    "/usr/src/${DKMS_NAME}-${VERSION}/kernel/alg_rgb.c"
sudo install -Dm644 \
    include/version.h \
    "/usr/src/${DKMS_NAME}-${VERSION}/include/version.h"

echo "[*] Registering DKMS..."
sudo dkms add \
    -m "${DKMS_NAME}" \
    -v "${VERSION}"

KERNELS=()
for module_dir in /usr/lib/modules/*; do
    if [[ -f "${module_dir}/build/Makefile" ]]; then
        KERNELS+=("${module_dir##*/}")
    fi
done

if (( ${#KERNELS[@]} == 0 )); then
    echo "No installed kernel headers were found under /usr/lib/modules." >&2
    exit 1
fi

for kernel in "${KERNELS[@]}"; do
    echo "[*] Building DKMS module for ${kernel}..."
    sudo dkms build \
        -m "${DKMS_NAME}" \
        -v "${VERSION}" \
        -k "${kernel}"

    echo "[*] Installing DKMS module for ${kernel}..."
    sudo dkms install \
        -m "${DKMS_NAME}" \
        -v "${VERSION}" \
        -k "${kernel}"
done

echo "[*] Enable module autoload..."
echo "alg_rgb" | sudo tee /etc/modules-load.d/alg_rgb.conf >/dev/null

echo "[*] Creating alg-rgb group (if needed)..."
sudo groupadd --system --force alg-rgb

echo "[*] Adding current user to alg-rgb group..."
if [[ "${TARGET_USER}" != "root" ]]; then
    if ! id -nG "${TARGET_USER}" | grep -qw alg-rgb; then
        sudo usermod -aG alg-rgb "${TARGET_USER}"
        NEEDS_RELOGIN=1
    fi
fi

echo "[*] Installing runtime state permissions..."
sudo install -Dm644 \
    tmpfiles.d/alg-rgb.conf \
    /usr/lib/tmpfiles.d/alg-rgb.conf
sudo systemd-tmpfiles --create /usr/lib/tmpfiles.d/alg-rgb.conf

echo "[*] Installing udev rule..."
sudo install -Dm644 \
    udev/99-alg-rgb.rules \
    /etc/udev/rules.d/99-alg-rgb.rules

echo "[*] Reloading udev..."
sudo udevadm control --reload-rules

echo "[*] Reloading kernel module..."
if [[ -d /sys/module/alg_rgb ]]; then
    if ! sudo modprobe -r alg_rgb; then
        echo "Could not unload the old alg_rgb module." >&2
        echo "Close anything using /dev/alg_rgb and run the installer again." >&2
        exit 1
    fi
fi

sudo modprobe alg_rgb
sudo udevadm trigger --action=add --subsystem-match=alg

echo

if (( NEEDS_RELOGIN == 0 )); then
    echo "Installation complete."
else
    echo "Installation complete."
    echo
    echo "IMPORTANT:"
    echo "Please log out and log back in (or reboot)"
    echo "so your new 'alg-rgb' group membership takes effect."
fi

echo
echo "Try:"
echo "  alg-rgb red"
echo "  alg-rgb blue 2"
echo "  alg-rgb animate wave"
echo "  alg-rgb animate pulse pink"
echo "  alg-rgb off"
