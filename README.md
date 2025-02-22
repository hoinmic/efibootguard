# EFI Boot Guard #

A bootloader based on UEFI.

Provides the following functionality:
* Arm a hardware watchdog prior to loading an OS
* Provides a simple update mechanism with fail-safe algorithm

## Development ##

Mailing list:
[efibootguard-dev@googlegroups.com](efibootguard-dev@googlegroups.com)

Archive:
[https://www.mail-archive.com/efibootguard-dev@googlegroups.com/](https://www.mail-archive.com/efibootguard-dev@googlegroups.com)

For sending patches, please refer to the mailing list and `CONTRIBUTING.md` in
the source tree.

Continuous integration:
* GitHub Actions: [![CI](https://github.com/siemens/efibootguard/actions/workflows/main.yaml/badge.svg?branch=master)](https://github.com/siemens/efibootguard/actions/workflows/main.yaml)
* Coverity: ![[coverity]](https://img.shields.io/coverity/scan/13885.svg)

## Watchdog support ##

The following watchdog drivers are implemented (and are probed in this order):
* WDAT (ACPI) watchdog
* AMD FCH
* Intel i6300esb
* Intel Quark
* Siemens SIMATIC IPC4x7E
* Intel TCO

Note that if no working watchdog is found, the boot process deliberately fails.
That said, setting a watchdog timeout of `0` allows to boot nonetheless without
a working watchdog, e.g., for testing purposes.

## Configuration ##

`efibootguard` reads its configuration from an environment storage. Currently,
the following environment backends are implemented:
* Dual FAT Partition storage

See `Installation And Usage` for further information.

## Further Documentation ##

* [Update Mechanism](docs/UPDATE.md)
* [Environment Tools](docs/TOOLS.md)
* [API Library](docs/API.md)
* [Compilation Instructions](docs/COMPILE.md)
* [Installation And Usage](docs/USAGE.md)
* [System Recovery](docs/RECOVERY.md)
