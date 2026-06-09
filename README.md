# alg-rgb

Native Linux RGB keyboard control for Acer ALG laptops using the undocumented CLV0001 ACPI interface.

`alg-rgb` is a lightweight Linux kernel driver and command-line utility that allows controlling the keyboard backlight directly from Linux without Windows, vendor software, background services, or third-party RGB suites.

This project was created by reverse engineering Acer's Windows RGB implementation and reproducing the same ACPI communication path on Linux.

---

## Features

* Native Linux support
* Direct ACPI communication
* No Windows dependency
* No vendor utilities required
* Lightweight kernel module
* Simple CLI interface
* Safe color whitelist
* Open source
* Reverse engineered protocol documentation

Currently supported colors:

* off
* red
* orange
* yellow
* lime
* light-green
* green
* green-cyan
* cyan
* light-blue
* blue
* violet
* magenta
* pink
* flesh
* bluish-white
* white

Additional colors will be added as they are identified and verified from Acer's Windows implementation.

---

## Why This Exists

The Acer ALG series ships with RGB keyboard support, but the official control software is only available on Windows.

On Linux, the keyboard typically remains stuck with the firmware default behavior and there is no official way to control it.

Rather than relying on Windows-only software, this project communicates directly with the laptop firmware using the same ACPI interface used by Acer's own driver.

---

## Reverse Engineering Overview

While investigating Acer's Windows RGB implementation, the keyboard control path was traced to a proprietary ACPI device:

```text
CLV0001
```

Linux exposes this device as:

```text
\_SB.DCHU
```

The Windows driver communicates with this device through:

```text
AcpiBridge.sys
```

After reverse engineering the driver and analyzing the system ACPI tables, the RGB control protocol was identified and reproduced on Linux.

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

and:

```text
Function:
0x67
```

The payload is a 4-byte buffer:

```text
Byte 0 = Green
Byte 1 = Red
Byte 2 = Blue
Byte 3 = Command / Zone
```

Important:

The firmware uses:

```text
GRB
```

instead of:

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
CC FF 00 F0

White:
FF FF FF F0
```

---

## Safety

This driver intentionally uses only the known-safe firmware command:

```text
Function 0x67
```

Several additional firmware functions were discovered during analysis:

```text
0x68
0x69
0x6A
...
```

These are currently undocumented and are intentionally disabled.

Testing of one experimental function resulted in a complete system lockup.

Until their behavior is fully understood, they will not be exposed through the public interface.

---

## Supported Hardware

Currently tested on:

```text
Acer ALG AL15G-53
```

with:

```text
CLV0001
```

present in ACPI.

The driver may work on other Acer systems exposing the same firmware interface, but this has not yet been verified.

Check whether your system exposes the device:

```bash
ls /sys/bus/acpi/devices | grep CLV
```

Expected output:

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

Expected output:

```text
alg-rgb v0.1.0 loaded successfully
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

## Usage

Set keyboard color:

```bash
alg-rgb red
```

Available colors:

```bash
alg-rgb off

alg-rgb red
alg-rgb orange
alg-rgb yellow

alg-rgb lime
alg-rgb light-green
alg-rgb green

alg-rgb green-cyan
alg-rgb cyan

alg-rgb light-blue
alg-rgb blue

alg-rgb violet
alg-rgb magenta
alg-rgb pink

alg-rgb flesh

alg-rgb bluish-white

alg-rgb white
```

---

## Direct Device Access

The kernel module creates:

```text
/dev/alg_rgb
```

The CLI simply writes color names to the device.

Example:

```bash
echo red > /dev/alg_rgb
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
├── .gitignore
└── install.sh
```

---

## Roadmap

Planned features:

* Additional firmware colors
* Brightness control
* Lighting profiles
* Suspend/resume restoration
* Boot-time color restoration
* DKMS package
* Fedora RPM package

Research targets:

* Function 0x68
* Function 0x69
* Function 0x6A
* Additional CLV0001 capabilities

---

## Contributing

Contributions, testing, and firmware research are welcome.

If your laptop exposes the CLV0001 interface and behaves differently, please open an issue and include:

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

Licensed under GPL-2.0.

See the LICENSE file for details.

---

## Acknowledgements

Special thanks to:

* ACPICA
* Linux ACPI developers
* Ghidra
* The open source reverse engineering community
* ChatGPT for assistance with ACPI analysis and documentation

---

## Disclaimer

This software communicates directly with undocumented firmware interfaces.

The currently implemented RGB command has been tested on an Acer ALG AL15G-53 and appears safe, but use at your own risk.

Always exercise caution when experimenting with undocumented ACPI functionality.
