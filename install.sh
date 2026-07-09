#!/usr/bin/env bash

set -e

VERSION=$(
    grep 'ALG_RGB_VERSION_STRING' include/version.h |
    sed 's/.*"\(.*\)".*/\1/'
)

DKMS_NAME="alg-rgb"

echo "[*] Building CLI..."
make -C cli

echo "[*] Installing CLI..."
sudo install -Dm755 cli/alg-rgb /usr/local/bin/alg-rgb

echo "[*] Installing DKMS source..."
sudo rm -rf "/usr/src/${DKMS_NAME}-${VERSION}"
sudo mkdir -p "/usr/src/${DKMS_NAME}-${VERSION}"

sudo cp -r \
    cli \
    kernel \
    include \
    dkms.conf \
    LICENSE \
    README.md \
    "/usr/src/${DKMS_NAME}-${VERSION}/"

echo "[*] Registering DKMS..."
sudo dkms remove -m "${DKMS_NAME}" -v "${VERSION}" --all 2>/dev/null || true

sudo dkms add \
    -m "${DKMS_NAME}" \
    -v "${VERSION}"

echo "[*] Building DKMS module..."
sudo dkms build \
    -m "${DKMS_NAME}" \
    -v "${VERSION}"

echo "[*] Installing DKMS module..."
sudo dkms install \
    -m "${DKMS_NAME}" \
    -v "${VERSION}"

echo "[*] Enable module autoload..."
echo "alg_rgb" | sudo tee /etc/modules-load.d/alg_rgb.conf >/dev/null

echo "[*] Creating alg-rgb group (if needed)..."
sudo groupadd -f alg-rgb

echo "[*] Adding current user to alg-rgb group..."
sudo usermod -aG alg-rgb "$SUDO_USER"

echo "[*] Installing udev rule..."
sudo install -Dm644 \
    udev/99-alg-rgb.rules \
    /etc/udev/rules.d/99-alg-rgb.rules

echo "[*] Reloading udev..."
sudo udevadm control --reload-rules
sudo udevadm trigger

echo "[*] Reloading kernel module..."
sudo modprobe -r alg_rgb 2>/dev/null || true
sudo modprobe alg_rgb

echo

if id -nG "$(id -un)" | grep -qw alg-rgb; then
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
echo "  alg-rgb off"