#!/usr/bin/env bash

set -e

VERSION="0.1.1"
DKMS_NAME="alg-rgb"

echo "[*] Building CLI..."
make -C cli

echo "[*] Installing CLI..."
sudo install -Dm755 cli/alg-rgb /usr/local/bin/alg-rgb

echo "[*] Installing DKMS source..."
sudo rm -rf /usr/src/${DKMS_NAME}-${VERSION}
sudo mkdir -p /usr/src/${DKMS_NAME}-${VERSION}

sudo cp -r \
    cli \
    kernel \
    include \
    dkms.conf \
    LICENSE \
    README.md \
    /usr/src/${DKMS_NAME}-${VERSION}/

echo "[*] Registering DKMS..."
sudo dkms remove -m ${DKMS_NAME} -v ${VERSION} --all 2>/dev/null || true

sudo dkms add \
    -m ${DKMS_NAME} \
    -v ${VERSION}

echo "[*] Building DKMS module..."
sudo dkms build \
    -m ${DKMS_NAME} \
    -v ${VERSION}

echo "[*] Installing DKMS module..."
sudo dkms install \
    -m ${DKMS_NAME} \
    -v ${VERSION}

echo "[*] Enable autoload..."
echo "alg_rgb" | sudo tee /etc/modules-load.d/alg_rgb.conf >/dev/null

echo "[*] Loading module..."
sudo modprobe alg_rgb

echo
echo "[+] Installation complete"
