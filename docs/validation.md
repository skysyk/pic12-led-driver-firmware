# Validation record — 2026-10-18

Implemented from the plan supplied in the project directory. An earlier controller
image was programmed and verified on a PIC12F683 with MiniPro on 2026-10-18. The
current nominal 38 kHz images with active-low standby indication and persistent
breathing mode below have been built and tested but not programmed.
No PWM waveform measurements or completed board acceptance tests are recorded.

## Target builds

Compiler: Microchip MPLAB XC8 v4.00, Windows x64 portable archive, `-O2`, C99.
Device support: Microchip PIC10-12Fxxx_DFP v1.10.193, target PIC12F683.
Both final builds completed without warnings or errors.

| Image | Program words | RAM bytes | Configuration |
| --- | --- | --- | --- |
| `build/led-controller.hex` | 2014 / 2048 (98.3%) | 113 / 128 (88.3%) | `0x33C4` |
| `build/indicator-only.hex` | 1259 / 2048 (61.5%) | 78 / 128 (60.9%) | `0x33C4` |

The controller now targets 38 kHz: 8 MHz / (4 * 1 * 53) = 37,735.849 Hz,
using Timer2 prescaler 1:1 and PR2=52. Each period has 212 duty counts (about
7.7 bits of resolution). The hardware conversion scales existing logical duty
values; saved brightness remains compatible with previous firmware. Static output
still supplies exact off and full-on endpoints. Standby LED polarity now matches
the reconstruction notes: GP0 sinks current when the indicator is on and drives
high when off. Initialization loads the high/off latch before enabling GP0 as an
output. Normal standby pulls it low; operation and the saved five-second disable
preference leave it high. Both images use the corrected polarity.
The controller's generated assembly was checked for the high GP0 latch before
output enable and for low/on versus high/off rendering.

Breathing uses a 256-phase linear triangle envelope and keeps the steady brightness
separate. Period settings 1..20 use 488 Timer0 ticks per half-second step, giving
0.499712..9.994240 seconds per complete rise-and-fall cycle. Double-clicking power
while on toggles mode; single-click off waits for a 350.208 ms recognition window.
Periods wrap in both directions, and power priority still cancels overlapping
brightness/period gestures. Mode changes save immediately and period changes
save after the usual quiet window. Animation flags request rendering only.

Version 3 keeps the 32-slot journal and existing 10-bit duty encoding. The former
spare flag bits store period-minus-one (bits 2..6) and breathing mode (bit 7).
Version 1/2 records load in steady mode with a 3-second default period. A bytewise
comparison of the four-byte settings structure and smaller PWM/button arithmetic
keep the feature within flash/RAM. Only 34 flash words remain; the proposed bounds
editor has not been implemented. Physical breathing/timing acceptance is pending.

The HEX verification script checked record lengths/checksums, flash address bounds,
complete 14-bit words, reset vector presence, the configuration word, and absence
of EEPROM data. The compiler reported zero bytes of EEPROM initialization in each
image; the runtime journal uses the chip's existing 256 EEPROM bytes.

SHA-256 of delivered images:

```text
led-controller.hex
bd7dc4d6fbdd2ca6ffe962037d637370ba14e6eb43efcab3698ca033324b3fa6

indicator-only.hex
b7f033d4a4db99ab251b97f8bde1a5ad8ff51d70a16c0a91c4decae7c5096606
```

## Host tests

Compiler: Zig 0.15.2 (`zig cc`), C99, `-Wall -Wextra -Werror -pedantic`, assertions
enabled. `scripts/test.ps1` passed all checks in `tests/test_firmware.c`, including
4,608 torn-write cases. The mock enforces no overlapping EEPROM operations and
permits writes to stay busy across repeated foreground service calls.

PWM tests compare all 1,024 logical duty values against independent wide-arithmetic
scaling, check 10%/50%/100% results, and verify that every 1% brightness adjustment
maps to a distinct hardware duty. Existing EEPROM round-trip and legacy record
tests still pass with the unchanged duty encoding.

Breathing tests check the first-click delay, double-click mode entry/exit without
power cycling, late clicks, standby and long-hold behavior, monotonic rise/fall
and both endpoints for all 20 exact tick periods, period wraparound, retained
steady brightness, delayed period saves, mode/power/preference restoration,
legacy v1/v2 migration through sequence rollover, invalid period rejection, and
no EEPROM writes during repeated animation cycles. The torn-write matrix also
checks restoration of mode and period in addition to brightness, power, and LED
preference.

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

## Physical programming

`scripts/program.ps1 -Firmware .\build\led-controller.hex` completed successfully
with MiniPro 0.7.4 and a TL866CS running firmware 03.2.86. This was the earlier
1.953 kHz image, SHA-256
`835e52a420edbd50e9b6010153a5825f899f1ffdb7379017d64a3d4d64545edd`.
Program memory, all 256 EEPROM bytes, four
user IDs, and configuration `0x33C4` passed explicit verification. EEPROM and user
IDs were preserved from the verified pre-write backup.

The run completed at `2026-10-19T03:41:43Z` (2026-10-18 local time). Backup files,
input binaries, hashes, and the complete log are retained in
`backups/program-pic12f683-20261019T034126841Z-3f2a21/`.

The initial attempt stopped before chip access because database options and paths
were combined into single arguments. The argument construction was corrected,
the mock test now rejects that form, and the script tests passed before the
successful hardware retry.

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
