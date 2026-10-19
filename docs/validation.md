# Validation record — 2026-10-18

Implemented from the plan supplied in the project directory. No device was
programmed, and no board measurements or physical acceptance tests were performed.

## Target builds

Compiler: Microchip MPLAB XC8 v4.00, Windows x64 portable archive, `-O2`, C99.
Device support: Microchip PIC10-12Fxxx_DFP v1.10.193, target PIC12F683.
Both final builds completed without warnings or errors.

| Image | Program words | RAM bytes | Configuration |
| --- | --- | --- | --- |
| `build/led-controller.hex` | 1721 / 2048 (84.0%) | 101 / 128 (78.9%) | `0x33C4` |
| `build/indicator-only.hex` | 1010 / 2048 (49.3%) | 65 / 128 (50.8%) | `0x33C4` |

The HEX verification script checked record lengths/checksums, flash address bounds,
complete 14-bit words, reset vector presence, the configuration word, and absence
of EEPROM data. The compiler reported zero bytes of EEPROM initialization in each
image; the runtime journal uses the chip's existing 256 EEPROM bytes.

SHA-256 of delivered images:

```text
led-controller.hex
835e52a420edbd50e9b6010153a5825f899f1ffdb7379017d64a3d4d64545edd

indicator-only.hex
fb9d8de35074daf5a304524f8fc01f29a9ebebd8e45bce55fc4f4ac14ed66508
```

## Host tests

Compiler: Zig 0.15.2 (`zig cc`), C99, `-Wall -Wextra -Werror -pedantic`, assertions
enabled. `scripts/test.ps1` passed all checks in `tests/test_firmware.c`, including
4,608 torn-write cases. The mock enforces no overlapping EEPROM operations and
permits writes to stay busy across repeated foreground service calls.

Power-priority regression tests cover up/down, initial on/off state, either
staggered release order and simultaneous release, a continued hold after power
release, cancellation of an existing ramp, persistence of the chosen power state,
and normal operation after a fresh brightness press. The staggered-release
regression failed before the fix and passed after it.

Standby LED tests cover the five-second threshold, early release, no short press
on release, no retrigger after prolonged holds or counter rollover, holds while
on, overlapping brightness buttons, immediate saving, persistence across reboot,
and no extra writes for a repeated disable. Version 1 journal records load with
the indicator enabled; version 2 records preserve the new preference. Tests cover
mixed-version sequence rollover and invalid version/flag rejection. The 4,608
torn-write cases now also check recovery of the standby LED preference.

These tests exercise production button, dimming, and journal sources. They do not
simulate PIC special-function registers, pin voltages, PWM edge timing, electrical
reset behavior, or actual EEPROM endurance. Those checks are listed in
`bench-checklist.md`.

## Tool provenance

Tools were downloaded into the ignored project-local `.tools` directory; no
system-wide compiler installation was required.

- XC8 archive: [official v4.00 Windows archive](https://ww1.microchip.com/downloads/aemDocuments/documents/DEV/ProductDocuments/SoftwareTools/xc8-v4.00-full-install-windows-x64.tar.xz).
  SHA-256: `1ab191fd446e1a53ebb039e95ad695c6f18bef0b2ee3ddd9f08268a46dddb9f5`.
- Device pack: [Microchip PIC10-12Fxxx_DFP 1.10.193](https://packs.download.microchip.com/Microchip.PIC10-12Fxxx_DFP.1.10.193.atpack).
  Register and configuration names were checked against the included PIC12F683
  header and configuration data, then verified by the XC8 builds.
- Host compiler: [Zig 0.15.2 Windows x64](https://ziglang.org/download/0.15.2/zig-x86_64-windows-0.15.2.zip).
  Its SHA-256 was checked against the official download manifest before extraction.
