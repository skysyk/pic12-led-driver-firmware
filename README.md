# PIC12F683 QY-K4 LED controller

Implementation of `PIC12F683_LED_Controller_Firmware_Implementation_Plan.md`.
The controller firmware and indicator-only bench firmware build with MPLAB XC8.
The portable C tests exercise the same button, dimmer, and journal code as the PIC.
Physical board tests have not been performed.

## Behavior

- Start off in steady mode at 50%, with a 3-second breathing period, when EEPROM
  is erased/invalid, without writing a default record.
- Restore saved power, steady brightness, breathing mode,
  period, and minimum/maximum brightness on startup.
- A power press released before one second toggles the output on/off as soon as
  release is debounced, with no double-tap waiting window. While on, holding power
  for one second toggles steady/breathing mode once, keeping the output on.
  Continued holding and release do not toggle again or turn the output off.
  A continuous power hold that starts while off arms
  settings reset after three seconds and restores defaults after six seconds.
  Long power holds that start while on do not reset settings.
- In steady mode, tap up/down for a 10 percentage point change between 10% and
  100%, with an extra 0.25% PWM-duty level. A Down tap at 10% or below selects
  this one-count level; further Down taps/holds stay there. An Up tap from the
  one-count level returns to 10%.
- In steady mode, hold a brightness button for about 400 ms to start changing by 1 percentage point
  about every 20 ms, including 9%, 8%, down through 1%, then 0.25%. Holding Up
  from 0.25% starts at 1% and continues upward by 1%. Release after a hold does
  not add a coarse step. All intermediate levels survive power cycles.
- Brightness adjustment in standby turns the main output on, including at a limit.
- Opposing brightness buttons cancel adjustment until both have been released.
  Power cancels overlapping brightness gestures until all brightness buttons
  have been released, including staggered releases and ongoing hold ramps.
- The status LED flashes for half a second when switching either direction
  between steady and breathing modes. Otherwise it is off during normal
  standby/operation, solid while editing minimum, blinking while editing maximum, and blinking during the
  three-second reset confirmation period. Turning the output off cancels a flash.
- Brightness and period saves wait 900 ms after the last change; power changes,
  mode changes, editor confirmation, and settings reset request an immediate save.
  Animation alone never causes EEPROM writes. Unchanged settings cause no write.

## Breathing controls

The output follows a 2,048-phase sine approximation between the saved minimum and
maximum (default 10% and 100%), slowing near each endpoint. Hold power for one second
while the output is on to toggle breathing. Entering starts near the saved steady
brightness, clamped to the breathing limits; exiting returns to steady brightness. After power-on/restoration,
breathing starts at its minimum. The full rise-and-fall cycle defaults to 3 seconds.
Each mode toggle lights the status LED for 489 ticks (500.736 ms nominally).
Each separate hold gives the same flash; ordinary power-on and restoring a saved mode
do not flash. The indication does not delay controls or create EEPROM writes.

While breathing, up makes the cycle 0.5 seconds longer and down makes it 0.5 seconds
shorter. Periods wrap: up from 10 seconds selects 0.5 seconds; down from 0.5 seconds
selects 10 seconds. A held button repeats after 400 ms, then every 20.48 ms.
Both buttons together cancel adjustment as in steady mode. A period adjustment in
standby wakes the saved breathing mode. The mode and period survive power loss;
the instantaneous animation phase is not stored. Steady brightness is retained
separately and is not changed by the animation or period buttons.

The 1.024 ms timebase uses 488 ticks per half-second setting: actual nominal full
cycles are 0.499712–9.994240 seconds, before oscillator tolerance. PWM is
20 kHz; the breathing envelope is independent of the PWM carrier.
The full cycle has 2,048 phase positions and 513 normalized amplitude weights,
enough to reach all 401 hardware duties including off and static full-on. At
periods of 1.5 seconds and longer, every duty in the selected range appears in
each complete cycle: 361 levels for default 10–100% limits, or 401 for 0–100%.
The 1.024 ms update interval limits faster cycles: full-range 0.5- and 1-second
cycles reach 293 and 391 distinct duties respectively. Periods below 2.5 seconds
skip some phase positions to preserve timing; longer cycles visit all 2,048.
The interpolated lookup table keeps sine error within two PWM counts of the ideal
envelope across integer-percentage ranges. With the one-count minimum, only
otherwise-zero frames are clipped to one; error against the shifted ideal sine
is within three PWM counts. Mode entry stays within three PWM counts
of the clamped steady brightness.

### Min/max editor

| Gesture while breathing | Behavior |
| --- | --- |
| Hold up and down together for 1 second | Enter minimum-brightness editing; light the status LED and preview the selected level |
| Tap up/down in the editor | Adjust by 10 percentage points, matching steady mode; hold to ramp by 1% |
| Short power press in the editor | Switch between minimum and maximum editing; preview the selected level |
| Hold up and down together for 1 second again | Save both limits and resume breathing |

The editor keeps minimum strictly below maximum and allows endpoints from 0% to
100%, plus a one-count (0.25% PWM-duty) minimum between off and 1%.
Down taps and holds reach that one-count level before off. Holding Down stays
there indefinitely, including a fresh hold that starts at this level. Release
and give a separate Down tap to select off. Up from off selects the one-count
level first; another tap selects 10%, or subsequent hold repeats reach 1%, 2%, etc.
The upper limit remains 1–100%; with a 1% maximum the minimum can be either off
or the one-count level. Other coarse steps clamp at the other limit minus/plus
1% instead of crossing it.
It pauses the animation and previews the selected limit steadily. The status
indicator is solid for minimum and blinks for maximum (245 ticks on, 245 off:
250.88 ms per half-cycle nominally). Selecting either limit starts its indication
immediately; the main LEDs keep a steady preview and controls remain responsive.
The blink phase is transient and does not cause settings saves. A held
pair triggers once; release both buttons before the next enter/exit hold. Power
only switches the selected limit on a tap in the editor; power holds are ignored.
Its normal on/off and mode-hold gestures resume after leaving. Individual up/down holds ramp by 1% every 20.48 ms
after the 400 ms hold threshold, without an extra step on release.

Both limits save together when you exit and survive power cycles. Unconfirmed
edits are discarded on power loss; opening/closing without changing limits does
not write EEPROM. Steady brightness and breathing period are preserved. The
status LED turns off on exit. The size-optimized build fits with 31 flash
words and three RAM bytes free; physical editor checks remain pending.

### Settings reset

With the output off, press and continuously hold power. After three seconds, the
status LED turns on and starts toggling every 0.5 seconds. Keep holding for three
more seconds to restore output off, 50% steady brightness, steady mode, a 3-second
breathing period, and 10–100% breathing limits. The LED stops blinking and an
immediate asynchronous save is requested. Releasing before six seconds cancels
the reset, stops blinking, and leaves the output off and preferences unchanged.
Short power taps still turn on normally; long holds do not become taps on release.

The gesture must start while logically off: a dark 0% breathing frame is still on.
Continued holding after reset does not retrigger or cause repeated EEPROM saves.
Reset commits a new default-settings record rather than erasing the journal, so
an interrupted transaction retains the last valid settings. The former five-second
standby-indicator disable feature and its preference have been removed.

## PWM and timing

User brightness settings are integer percentages. EEPROM stores percentages;
legacy 10-bit duty records are decoded on startup. Runtime percentages convert
directly to the 400-count PWM period (four counts per percent), with a
sixteen-bit duty interface and cache between the state machine and hardware. The breathing
envelope operates directly in duty counts rather than rounding each frame to 1%.
Intermediate levels use CCP1/Timer2 at **20,000 Hz** nominally with the 8 MHz clock
(`PR2=99`, prescaler 1:1). Off and maximum use
static inactive/active GP2 levels
for true 0% and 100%. Timer0 independently free-runs at **1.024 ms/tick**.
Intervals are rounded up: debounce/ramp 20.48 ms, brightness hold 400.384 ms,
power mode hold 1000.448 ms after press debounce, and save 900.096 ms.
Power taps below that hold threshold act after release debounce (about 20 ms).

## Hardware assumptions

| Physical pin | Signal | Connection |
| --- | --- | --- |
| 1 | VDD | R4, measured controller supply |
| 2 | GP5 | R3, down, weak pull-up |
| 3 | GP4 | R2, up, weak pull-up |
| 4 | GP3 | R1, power, **external 10 kΩ pull-up to VDD** |
| 5 | GP2/CCP1 | L1, MOSFET gate circuit |
| 6 | GP1 | L2, input, no pull-up, never driven |
| 7 | GP0 | L3, edit/reset status LED (original standby indicator) |
| 8 | VSS | L4, ground |

`firmware/board.h` uses active-low buttons, active-high MOSFET drive, and an
active-low status LED, matching the reconstruction notes. Wire the
indicator as VDD → series resistor (~510 Ω) → LED anode → cathode → GP0 (pin 7).
GP0 is high when the indicator is off and low when it is on. Verify the wiring
against the board.
Change the polarity constants and rebuild if needed. An inverted MOSFET circuit
also needs an appropriate external bias to hold the load off while reset leaves
the PIC pins as inputs; firmware cannot control the reset interval.

Configuration selects internal oscillator GPIO mode, MCLR off, power-up timer
and brown-out reset on, software-controlled watchdog, code/data protection off,
and IESO/FCMEN off. Configuration stays `0x33C4`; startup enables the watchdog via
`WDTCON=0x15`, about 1.06 seconds nominally from LFINTOSC. Only completed Timer0
tick processing clears it; the interrupt handler, idle loop, and PWM wait do not.
Loss of ticks or a stalled foreground operation therefore permits a watchdog reset.
The Timer2 boundary wait has a 256-poll budget, about 0.64 ms at 8 MHz plus ISR
latency. Expiration drives the main output inactive, disconnects CCP1, and waits
without clearing the watchdog. Recovery reloads the last valid EEPROM settings.
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

- `build/indicator-only.hex`: GP2 always held inactive; the status LED follows
  editor/reset gestures. To test it, hold power while off for three seconds, then
  release before six seconds. Does not read or write EEPROM.
- `build/led-controller.hex`: full controller with persistence.
- Matching `.map`, `.elf`, and compiler reports for memory/debug inspection.

The build does not program a chip or access the TL866CS.

## Minipro programmer tool

The upstream source is checked out in `minipro/`. A native 64-bit Windows build
is in `build/minipro/minipro.exe`, alongside its chip databases and licenses.
Use the launcher from this directory so the databases are found:

```powershell
.\scripts\minipro.ps1 -V
.\scripts\minipro.ps1 -q tl866a -d PIC12F683
```

Rebuild with `.\scripts\build-minipro.ps1`. See
[minipro build notes](docs/minipro-build.md) for dependencies and verification.

## EEPROM journal

The 256-byte EEPROM holds 16 rotating, 16-byte slots. Each version 5 record uses
ten bytes; the rest is reserved. This layout makes room for both limits and keeps
address calculations small while supporting safe migration from the 32-slot
legacy journal.

| Offset | Meaning |
| --- | --- |
| 0 | Commit marker `0xA5`, written last |
| 1 | Version `5` (version `4` remains readable in aligned slots; `1`–`3` at their original eight-byte locations) |
| 2–3 | 16-bit sequence, little-endian |
| 4 | Bits 0–6: steady brightness 1–100%, or `0` for one PWM count; bit 7: power on |
| 5 | Minimum 0–99%, or byte value `128` for one PWM count |
| 6 | Bits 0–6: maximum 1–100%; bit 7: breathing enabled |
| 7 | Cycle period in half-second steps, 1–20 |
| 8 | CRC-8/SMBUS over bytes 1–7: poly `0x07`, init `0`, no reflection/xorout |
| 9 | Zero migration guard, invalidating an overlapping legacy version byte |

Boot rejects bad markers, versions, CRCs, guards, and out-of-range settings, and
compares sequences modulo 65536. This ordering assumes valid journal entries
span fewer than 32768 updates, as normal operation of the ring ensures. Bounds
must satisfy `0 <= minimum < maximum <= 100` after decoding the one-count
sentinel. Legacy duties are restored to the nearest integer percentage; ordinary
values in the new format still store percentages directly.

Versions 1/2 restore brightness and power in steady mode with the default
breathing period. Version 3 additionally restores breathing mode and
period. Versions 1–3 use default 10–100% breathing limits; version 4 restores
both saved percentage limits. The first version 5 save selects a slot that
cannot overlap the newest valid older record;
its commit marker is written only after all payload and guard bytes verify.
The guard prevents an old odd-slot record from reappearing when a CRC equals
`0xA5` after sequence rollover. Firmware predating version 5 cannot read these
records; the initial v5 firmware also rejects the new steady 1–9% values.
Downgrading may therefore restore an older record or defaults.
Legacy indicator preferences are accepted and ignored, including in older version
4 records; a version 4 off minimum stays off even if its obsolete indicator bit
was set. Version 5 reuses the standalone value `128` for the one-count minimum.
Use the six-second power hold or
`scripts/program.ps1 -ResetSettings` to restore all defaults. The bench image never
accesses EEPROM, so its settings last only until power is disconnected.

A save snapshots the requested state, invalidates the next slot, writes and
verifies each payload byte, then writes and verifies the final marker. It never
overwrites the latest valid slot. Writes are asynchronous; only the EEPROM unlock
sequence briefly disables interrupts. New requests arriving during a write remain
pending and are compared against the completed snapshot. Failed verification
preserves the prior committed state and retries after one second, avoiding a
tight write loop. A later power change can request an earlier retry.

A record normally uses eleven byte writes, including two writes to its marker.
EEPROM is never initialized by the HEX. Check the programmer's erase/preserve
settings if retaining existing settings across firmware updates matters.
Without power-fail detection/hold-up, abrupt USB loss can discard changes still
in the quiet window or an incomplete transaction; the previous record is retained.

## Validation

`tests/test_firmware.c` covers bounce, exact tap/hold timing, no extra step after
a hold, limits, long holds, chords, power priority, standby adjustment, restoration,
write avoidance, deferred/prompt saves, power/brightness overlaps with either
release order and held buttons, six-second reset timing/cancellation, status LED
rendering, one-second power holds and immediate tap timing, all 2,048 sine positions,
all 20 complete breathing periods and their visited phases, default/full/narrow
ranges, all 100 percentage spans at all 20 periods, complete duty coverage from
1.5 seconds onward, sine approximation error across every PWM span, 505,000 mode-entry cases,
period wraparound and delayed saves, animation write avoidance, breathing power
restoration, editor entry/exit timing, preview and save behavior, all 5,050 valid
min/max pairs with 10% taps, 1% hold ramps, conflict prevention,
legacy-record compatibility, state changes during an asynchronous write,
CRC corruption, invalid fields, three complete slot rotations, sequence rollover,
and write-verification failures with retry backoff.

`tests/test_hw.c` compiles the production hardware code against a small register
model. It checks all 401 duties, including transitions differing only in bit 8,
the Timer2 setup, static endpoints, status LED, tick handling, and EEPROM interrupt
restoration. Delayed/stopped Timer2 tests check the polling limit and watchdog
recovery from both full-on and active PWM. `tests/test_main.c` executes the
production main loop and hardware driver with injected ticks/buttons and an LF
watchdog model, checking normal servicing and missing-tick/render-stall recovery.
The model supplies Timer2 boundaries; it does not measure waveform
timing, voltages, emissions, or execution speed on the PIC.

The power-loss matrix tests every possible torn-byte value at each of eleven write
positions, both for the first record and reuse of a full ring: **5,632 cases**.
An additional **11,517 migration cases** cover all legacy versions and slot
positions, including full torn-byte coverage for representative even/odd layouts.
Another **2,816 cases** check interrupted reset saves, for **19,965 cases** total.
It also tests loss after the marker completes but before RAM bookkeeping.
The mock models a single in-progress EEPROM byte; bench power cycling remains
necessary to validate the actual power supply and EEPROM behavior.

See `docs/bench-checklist.md` for the remaining physical acceptance steps and
`docs/validation.md` for the recorded build results.

References: [PIC12F683 datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/41211D.pdf),
[MPLAB XC8 downloads](https://www.microchip.com/en-us/tools-resources/develop/mplab-xc-compilers/xc8),
[Microchip device packs](https://packs.download.microchip.com/).
