# alg-rgb

Native Linux RGB keyboard control for Acer ALG laptops using the undocumented CLV0001 ACPI interface.

`alg-rgb` is a lightweight Linux kernel driver and command-line utility that allows controlling the keyboard backlight directly from Linux without Windows, vendor software, background services, or third-party RGB suites.

This project was created by reverse engineering Acer's Windows RGB implementation and reproducing the same ACPI communication path on Linux.

---

## Features

- Native Linux support
- Direct ACPI communication
- No Windows dependency
- No vendor utilities required
- Lightweight kernel module
- Simple CLI interface
- Built-in color validation
- Brightness control
- Open source
- Reverse engineered protocol documentation

Supported colors:

- off
- red
- orange
- yellow
- lime
- light-green
- green
- green-cyan
- cyan
- light-blue
- blue
- violet
- magenta
- pink
- flesh
- bluish-white
- white

Supported brightness levels:

- 0 (off)
- 1
- 2
- 3
- 4 (maximum)

---

## Why This Exists

The Acer ALG series ships with RGB keyboard support, but the official control software is only available on Windows.

On Linux, the keyboard typically remains stuck with firmware defaults and there is no official way to control it.

This project communicates directly with the firmware using the same ACPI interface used by Acer's own Windows implementation.

---

## Reverse Engineering Overview

During analysis of Acer's Windows RGB stack, keyboard control was traced to the proprietary ACPI device:

```text
CLV0001
```

Linux exposes this device as:

```text
\_SB.DCHU
```

The Windows implementation communicates with the device through:

```text
AcpiBridge.sys
```

The RGB protocol was reverse engineered by analyzing firmware traffic and reproducing the same ACPI communication path on Linux.

Communication flow:

```text
Userspace
    │
    ▼
alg-rgb CLI
    │
    ▼
/dev/alg_rgb
    │
    ▼
alg-rgb kernel module
    │
    ▼
ACPI _DSM()
    │
    ▼
CLV0001 / DCHU
    │
    ▼
Keyboard RGB Controller
```

---

## ACPI Protocol

The firmware expects an ACPI `_DSM()` call using:

```text
UUID:
93f224e4-fbdc-4bbf-add6-db71bdc0afad
```

Function:

```text
0x67
```

Payload format:

```text
Byte 0 = Green
Byte 1 = Red
Byte 2 = Blue
Byte 3 = Command / Zone
```

Important:

```text
GRB
```

is used internally instead of:

```text
RGB
```

Example packets:

```text
Red:
00 FF 00 F0

Green:
FF 00 00 F0

Blue:
00 00 FF F0

Yellow:
FF FF 00 F0

White:
FF FF FF F0
```

---

## Safety

Only the known-safe firmware command is exposed:

```text
Function 0x67
```

Additional firmware functions were discovered during reverse engineering:

```text
0x68
0x69
0x6A
```

These remain undocumented and are intentionally disabled.

Testing of one experimental function resulted in a complete system lockup, so unknown firmware calls are not exposed through the public interface.

---

## Supported Hardware

Verified on:

```text
Acer ALG AL15G-53
```

with:

```text
CLV0001
```

present in ACPI.

Other Acer systems exposing the same firmware interface may work but have not yet been validated.

Check for device presence:

```bash
ls /sys/bus/acpi/devices | grep CLV
```

Expected:

```text
CLV0001:00
```

---

## Building

### Kernel Module

```bash
cd kernel
make
```

Load:

```bash
sudo insmod alg_rgb.ko
```

Verify:

```bash
dmesg | tail
```

Expected:

```text
alg-rgb v0.1.1 loaded
```

---

### CLI

```bash
cd cli
make
```

Install:

```bash
sudo cp alg-rgb /usr/local/bin/
```

---

## Installation

The repository includes an installation script that builds and installs both the kernel module and CLI.

Make the installer executable:

```bash
chmod +x install.sh
```

Run the installer:

```bash
sudo ./install.sh
```

The installer will:

* Build the kernel module
* Install the kernel module
* Build the CLI
* Install the `alg-rgb` command

After installation, load the module:

```bash
sudo modprobe alg_rgb
```

Verify:

```bash
lsmod | grep alg_rgb
```

Expected output:

```text
alg_rgb
```

Check the CLI:

```bash
alg-rgb --version
```

Example:

```bash
alg-rgb red
alg-rgb cyan 2
alg-rgb white 4
```
---

## Usage

Set color:

```bash
alg-rgb red
alg-rgb blue
alg-rgb cyan
```

Set color with brightness:

```bash
alg-rgb red 4
alg-rgb red 3
alg-rgb red 2
alg-rgb red 1
alg-rgb red 0
```

Examples:

```bash
alg-rgb orange 4
alg-rgb cyan 2
alg-rgb white 1
```

Show help:

```bash
alg-rgb help
```

Show version:

```bash
alg-rgb --version
```

---

## Direct Device Access

The kernel module creates:

```text
/dev/alg_rgb
```

The CLI writes validated commands to this device.

Example:

```bash
echo "red 4" > /dev/alg_rgb
```

Using the CLI is recommended.

---

## Project Structure

```text
alg-rgb/
├── kernel/
│   ├── alg_rgb.c
│   └── Makefile
│
├── cli/
│   ├── alg-rgb.c
│   └── Makefile
│
├── include/
│   └── version.h
│
├── LICENSE
├── README.md
├── CHANGELOG.md
├── install.sh
└── .gitignore
```

---

## Roadmap

Planned features:

- Suspend/resume restoration
- Boot-time color restoration
- DKMS packaging
- Fedora RPM package
- Additional firmware capabilities
- More verified color presets

Research targets:

- Function 0x68
- Function 0x69
- Function 0x6A
- Additional CLV0001 functionality

---

## Contributing

Testing, reverse engineering, and firmware research are welcome.

If your system exposes CLV0001 and behaves differently, please open an issue and include:

```bash
uname -a
```

and:

```bash
sudo acpidump > acpi.out
```

along with your laptop model.

---

## License

GPL-2.0.

See LICENSE for details.

---

## Acknowledgements

- ACPICA
- Linux ACPI developers
- Ghidra
- The open source reverse engineering community

---

## Disclaimer

This software communicates directly with undocumented firmware interfaces.

The currently implemented RGB functionality has been tested on an Acer ALG AL15G-53 and appears safe, but use at your own risk.

Exercise caution when experimenting with undocumented ACPI functionality.
