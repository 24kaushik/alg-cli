#!/bin/bash
set -e

echo "[*] Building kernel module..."
cd kernel
make

echo "[*] Loading module..."
sudo insmod alg_rgb.ko || true

cd ../cli

echo "[*] Building CLI..."
make

echo "[*] Installing CLI..."
sudo cp alg-rgb /usr/local/bin/

echo
echo "Done."
echo
echo "Try:"
echo "  alg-rgb red"
echo "  alg-rgb yellow"
echo "  alg-rgb white"