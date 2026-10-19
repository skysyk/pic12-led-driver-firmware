# PIC12F683 QY-K4 LED controller

Implementation of `PIC12F683_LED_Controller_Firmware_Implementation_Plan.md`.
The controller firmware and indicator-only bench firmware build with MPLAB XC8.
The portable C tests exercise the same button, dimmer, and journal code as the PIC.
Physical board tests have not been performed.

## Behavior

- Start off at 50% with erased/invalid EEPROM, without writing a default record.
- Restore the most recent valid saved power state and brightness on startup.
- Power toggles on release of a debounced short press. A long power hold does nothing.
- Tap up/down for a 10 percentage point change; clamp between 10% and 100%.
- Hold a brightness button for about 400 ms to start changing by 1 percentage point
  about every 20 ms. Release after a hold does not add a coarse step.
- Brightness adjustment in standby turns the main output on, including at a limit.
- Opposing brightness buttons cancel adjustment until both have been released.
  Power cancels overlapping brightness gestures until all brightness buttons
  have been released, including staggered releases and ongoing hold ramps.
- Standby indicator is lit while off and extinguished while on.
- Brightness saves wait 900 ms after the last actual change; power changes request
  a save immediately. Unchanged state causes no write.

Logical brightness is an integer percentage. PWM is linear, rounded to a 10-bit
duty value. Intermediate levels use CCP1/Timer2 at **1,953.125 Hz** (8 MHz,
`PR2=255`, prescaler 1:4). Off and maximum use static inactive/active GP2 levels
for true 0% and 100%. Timer0 independently free-runs at **1.024 ms/tick**.
Intervals are rounded up: debounce/ramp 20.48 ms, hold 400.384 ms, save 900.096 ms.

## Hardware assumptions

| Physical pin | Signal | Connection |
| --- | --- | --- |
| 1 | VDD | R4, measured controller supply |
| 2 | GP5 | R3, down, weak pull-up |
| 3 | GP4 | R2, up, weak pull-up |
| 4 | GP3 | R1, power, **external 10 kΩ pull-up to VDD** |
| 5 | GP2/CCP1 | L1, MOSFET gate circuit |
| 6 | GP1 | L2, input, no pull-up, never driven |
| 7 | GP0 | L3, standby indicator |
| 8 | VSS | L4, ground |

`firmware/board.h` assumes active-low buttons, active-high MOSFET drive, and an
active-high standby indicator. Verify these assumptions against the board.
Change the polarity constants and rebuild if needed. An inverted MOSFET circuit
also needs an appropriate external bias to hold the load off while reset leaves
the PIC pins as inputs; firmware cannot control the reset interval.

Configuration selects internal oscillator GPIO mode, MCLR off, power-up timer
and brown-out reset on, watchdog off, code/data protection off, IESO/FCMEN off.
Analog inputs, ADC, comparator, voltage reference, and interrupt-on-change are
disabled. GPIO latches are loaded with inactive levels before output enable.

## Build and test

From PowerShell in this directory:

```powershell
.\scripts\test.ps1
.\scripts\build.ps1 -IndicatorOnly
.\scripts\build.ps1
```

The scripts automatically find the local portable tools in `.tools` when present.
For another installation:

```powershell
.\scripts\test.ps1 -Compiler 'C:\path\to\clang.exe'
.\scripts\build.ps1 -Xc8 'C:\Program Files\Microchip\xc8\v4.00\bin\xc8-cc.exe' -DevicePack 'C:\path\to\PIC10-12Fxxx_DFP\1.10.193\xc8'
```

Host tests support GCC, Clang, or Zig (`zig cc`). Assertions are explicitly enabled.
XC8 needs the PIC10-12Fxxx device pack; `-DevicePack` points to its **xc8** folder.
MPLAB X can also build these sources in a standalone PIC12F683 project: add all
`firmware/*.c`, include `firmware`, select XC8, C99, and `-O2`. Configuration pragmas
are in `hw_init.c`. For the bench build define `BOARD_INDICATOR_ONLY=1` and omit
`storage.c`.

Outputs:

- `build/indicator-only.hex`: GP2 always held inactive, indicator follows the
  simulated standby state when buttons are used. Does not read or write EEPROM.
- `build/led-controller.hex`: full controller with persistence.
- Matching `.map`, `.elf`, and compiler reports for memory/debug inspection.

The build does not program a chip or access the TL866CS.

## EEPROM journal

All 256 bytes hold 32 rotating, eight-byte records:

| Offset | Meaning |
| --- | --- |
| 0 | Commit marker `0xA5`, written last |
| 1 | Version `1` |
| 2–3 | 16-bit sequence, little-endian |
| 4–5 | 10-bit duty, little-endian |
| 6 | Power flag, `0` or `1` |
| 7 | CRC-8/SMBUS over bytes 1–6: poly `0x07`, init `0`, no reflection/xorout |

Boot rejects bad markers, versions, CRCs, flags, and out-of-range duties, and
compares sequences modulo 65536. This ordering assumes valid journal entries
span fewer than 32768 updates, as normal operation of the 32-slot ring ensures.
Stored duties are restored to the nearest integer percentage; values generated
by this firmware round-trip exactly.

A save snapshots the requested state, invalidates the next slot, writes and
verifies each payload byte, then writes and verifies the final marker. It never
overwrites the latest valid slot. Writes are asynchronous; only the EEPROM unlock
sequence briefly disables interrupts. New requests arriving during a write remain
pending and are compared against the completed snapshot. Failed verification
preserves the prior committed state and retries after one second, avoiding a
tight write loop. A later power change can request an earlier retry.

A record normally uses nine byte writes, including two writes to its marker.
EEPROM is never initialized by the HEX. Check the programmer's erase/preserve
settings if retaining existing settings across firmware updates matters.
Without power-fail detection/hold-up, abrupt USB loss can discard changes still
in the quiet window or an incomplete transaction; the previous record is retained.

## Validation

`tests/test_firmware.c` covers bounce, exact tap/hold timing, no extra step after
a hold, limits, long holds, chords, power priority, standby adjustment, restoration,
write avoidance, deferred/prompt saves, power/brightness overlaps with either
release order and held buttons, state changes during an asynchronous write,
CRC corruption, invalid fields, three complete slot rotations, sequence rollover,
and write-verification failures with retry backoff.

The power-loss matrix tests every possible torn-byte value at each of nine write
positions, both for the first record and reuse of a full ring: **4,608 cases**.
It also tests loss after the marker completes but before RAM bookkeeping.
The mock models a single in-progress EEPROM byte; bench power cycling remains
necessary to validate the actual power supply and EEPROM behavior.

See `docs/bench-checklist.md` for the remaining physical acceptance steps and
`docs/validation.md` for the recorded build results.

References: [PIC12F683 datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/41211D.pdf),
[MPLAB XC8 downloads](https://www.microchip.com/en-us/tools-resources/develop/mplab-xc-compilers/xc8),
[Microchip device packs](https://packs.download.microchip.com/).
