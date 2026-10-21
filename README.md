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
  about every 102 ms, including 9%, 8%, down through 1%, then 0.25%. Holding Up
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
  Retiring a failed EEPROM slot starts a rapid warning blink until the map saves.
  If no destination remains, the warning continues and saves stop. It overrides other LED patterns.
- Brightness and period saves wait 900 ms after the last change; power changes,
  mode changes, editor confirmation, and settings reset request an immediate save.
  Animation alone never causes EEPROM writes. Unchanged settings cause no write.

## Breathing controls

The output follows the simulation's **Exponential** curve between the saved
minimum and maximum (default 10% and 100%):
`(exp(-cos(2*pi*phase)) - exp(-1)) / (exp(1) - exp(-1))`.
It has symmetric rise/fall, a narrower peak, and a broader trough than sine,
without explicit peak/minimum holds. Hold power for one second
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
The full cycle has 2,048 phase positions. A 33-byte Q8 lookup for the rising half
is interpolated in Q13; the falling half reflects the phase. Internal Q3 targets
are rounded to two fractional bits (Q2). A **10 kHz dithering interrupt** alternates
adjacent hardware duties: four 100 us slots average exactly to a constant target,
giving 0.0625% average-duty increments and a longest pattern of 0.4 ms (2.5 kHz).
The carrier stays 20 kHz. There are 1,601 available average-duty commands over
401 instantaneous hardware levels; a cycle need not visit all commands. Across
all 2,048 phases, the full 0–100% exponential envelope visits 816 Q2 targets.
Only breathing produces fractional targets. Steady levels and editor previews
are exact duties; their update interrupt stops after one write. The rounding
residual is preserved across animation frames and mode changes.

Periods below 2.5 seconds skip some phase positions to preserve timing; longer
cycles visit all 2,048. The interpolated target stays within two hardware counts
of the simulation's analytical exponential curve across percentage ranges.
With the one-count minimum, targets below one count are clipped to one: no
dither slot falls below the selected 0.25% floor. Mode entry stays within three
counts of clamped steady brightness. Actual low-end smoothness, driver pulse
response, camera artifacts, and acoustic effects still require a board test.
The existing breathing mode now uses this envelope; there is no additional mode
or button gesture. All v6 saved settings remain compatible, including mode,
period, brightness bounds, and the EEPROM retirement map.

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
Its normal on/off and mode-hold gestures resume after leaving. Individual up/down holds ramp by 1% every 102.4 ms
after the 400 ms hold threshold, without an extra step on release.

Both limits save together when you exit and survive power cycles. Unconfirmed
edits are discarded on power loss; opening/closing without changing limits does
not write EEPROM. Steady brightness and breathing period are preserved. The
status LED turns off on exit unless a save fault is latched. The size-optimized
build fits with thirty-five flash words and eight RAM bytes free; physical editor checks
remain pending.

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
Reset commits a new default-settings record and preserves the retired-slot map.
An interrupted transaction retains the last valid settings. The former five-second
standby-indicator disable feature and its preference have been removed.

## PWM and timing

User brightness settings are integer percentages. EEPROM stores percentages;
only the current v6 percentage format is read on startup. Runtime percentages convert
directly to the 400-count PWM period (four counts per percent), with a
sixteen-bit Q3 duty interface (0..3200) between the state machine and hardware.
The driver rounds to Q2 (0..1600), packs it into a byte pair, and preserves
two fractional bits in a 10 kHz accumulator. The packed low bit stays zero so
the ISR can position the hardware duty bits with a single shift.
Intermediate levels use CCP1/Timer2 at **20,000 Hz** nominally with the 8 MHz clock
(`PR2=99`, prescaler 1:1). Off and maximum use
static inactive/active GP2 levels
for true 0% and 100%. Timer0 independently free-runs at **1.024 ms/tick**.
Intervals are rounded up: debounce/period repeat 20.48 ms, brightness repeat
102.4 ms, brightness hold 400.384 ms,
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

The 256-byte EEPROM holds sixteen aligned 16-byte slots. The current **v6** format
stores settings and a durable bad-slot map together. Versions 1–5, unknown versions,
and records on eight-byte half-slot boundaries are ignored. If no valid v6 record
exists, startup uses defaults without writing EEPROM; the next changed settings
start a v6 journal. Existing v5 preferences are not migrated.

| Offset | Meaning |
| --- | --- |
| 0 | Commit marker `0xA5`, written last |
| 1 | Version `6` only |
| 2–3 | 16-bit sequence, little-endian |
| 4 | Bits 0–6: steady brightness 1–100%, or `0` for one PWM count; bit 7: power on |
| 5 | Minimum 0–99%, or `128` for one PWM count |
| 6 | Bits 0–6: maximum 1–100%; bit 7: breathing enabled |
| 7 | Period in half-second settings, 1–20 |
| 8–9 | Retired-slot bitmap, little-endian; bit N retires slot N |
| 10 | CRC-8/SMBUS over bytes 1–9: polynomial `0x07`, init `0`, no reflection/xorout |
| 11–15 | Reserved |

Boot validates records and ORs all their maps **before** comparing sequences. It
then excludes retired slots and restores the newest remaining settings. A stuck
commit marker cannot make an old retired record win after sequence wrap. Bounds
still require `minimum < maximum` after decoding the one-count sentinel.

A save snapshots the requested settings, invalidates its destination, writes and
verifies the payload, then writes and verifies the marker: twelve asynchronous
byte writes. The latest committed slot is protected. Only the EEPROM unlock
briefly disables interrupts. New requests remain pending during a transaction.

A verification failure retries the slot once after one second. A second failure
retires it and advances to another destination after another one-second delay.
The map forces a save even if preferences have reverted to their saved values.
New deferred adjustments restart the 900 ms quiet period without shortening a
failure backoff; the next attempt waits for the longer remaining delay.

The status LED blinks 131.072 ms on/off (about 3.8 Hz) while a new retirement awaits
a verified save. The warning overrides other LED patterns and clears after that
save unless saving capacity is exhausted. At least two healthy slots are needed
for ongoing atomic saves: the protected record and another destination. With no
destination remaining, writes stop and the warning continues; controls and main
lighting keep working from RAM. A reboot may rediscover retirements that could
not be committed, while restoring the last readable, unretired settings or defaults.

Retirement survives reboot and the six-second preference reset. Two consecutive
transient failures can retire a healthy slot; there is no automatic unretirement.
Only deliberate full EEPROM erasure, such as `scripts/program.ps1 -ResetSettings`,
clears the maps. It does not repair physically damaged cells. See
[bad-slot tracking](docs/bad-slot-tracking.md) for the design and validation.

EEPROM is never initialized by the HEX. Programmer preservation retains all raw
bytes, but unsupported save versions remain unreadable. Abrupt USB loss can
discard changes in the quiet period or an incomplete transaction; the previous
committed settings/map remain available. The indicator-only bench image never
accesses EEPROM.


## Validation

`tests/test_firmware.c` covers bounce, exact tap/hold timing, no extra step after
a hold, limits, long holds, chords, power priority, standby adjustment, restoration,
write avoidance, deferred/prompt saves, power/brightness overlaps with either
release order and held buttons, six-second reset timing/cancellation, status LED
rendering, one-second power holds and immediate tap timing, all 2,048 exponential-envelope positions,
all 20 complete breathing periods and their visited phases, default/full/narrow
ranges, all 100 percentage spans at all 20 periods, complete duty coverage from
1.5 seconds onward in rounded targets, exponential-curve approximation error across every PWM span, 505,000 mode-entry cases,
period wraparound and delayed saves, animation write avoidance, breathing power
restoration, editor entry/exit timing, preview and save behavior, all 5,050 valid
min/max pairs with 10% taps, 1% hold ramps, conflict prevention,
old-format rejection, current v6 restoration, state changes during an asynchronous write,
CRC corruption, invalid fields, three complete slot rotations, sequence rollover,
and write-verification failures with retry-once/retire, protected-slot wraparound,
capacity exhaustion, fault indication, and recovery.

`tests/test_hw.c` compiles the production hardware code against a small register
model. It checks all 3,201 internal Q3 inputs rounded to Q2, all four starting
residuals, exact four-slot means, 100,000 target changes without residual loss,
integer-IRQ shutoff, and the Timer2 setup,
static endpoints, status LED, tick handling, and EEPROM interrupt
restoration. Delayed/stopped Timer2 tests check the polling limit and watchdog
recovery from both full-on and active PWM. `tests/test_main.c` executes the
production main loop and hardware driver with injected ticks/buttons and an LF
watchdog model, checking normal servicing, missing-tick/render-stall recovery,
and the physical LED register during EEPROM failure and recovery. It also verifies
that breathing dithers while every steady level and both editor previews remain fixed.
The model supplies Timer2 boundaries; it does not measure waveform
timing, voltages, emissions, or execution speed on the PIC.

After a normal target build, `node tests/test_hex.js` checks the linked HEX:
14,950 settings-copy/startup cases and 102,432 interrupt entries across all internal
targets/residuals, both RAM banks, and coincident Timer0 interrupts. The last duty
write takes at most 34 cycles (17 us); the full ISR takes at most 53 cycles
(26.5 us), including four-cycle entry latency. Another 103,200 cycle-driven schedules
check target publication and Timer0 arrival alignments: no slot is lost, no target
is torn, and both fields finish within 91 cycles (45.5 us) of the boundary, before
the next 50 us PWM latch. Another 23,723 linked-envelope cases check the simulator
formula, reflected exhale, peak encoding, and interrupts during calculation.
The isolated envelope takes at most 502 cycles (251 us), excluding interrupts.

The linked main-loop model runs 18 real-time schedules with 19,800 completed ticks
and 144 asynchronous EEPROM writes. No timer tick or PWM latch is lost. The worst
boundary-to-watchdog interval is 3,098 cycles (1.549 ms): occasional settings/save
work spans one extra queued tick, which is recovered without sustained slowdown.
Duty writes within these main-loop schedules finish within 72 cycles (36 us).
These are instruction-model checks; physical waveform and jitter tests remain pending.

The settings matrices interrupt all twelve writes: **6,144** initial/reused journal,
**3,072** reset, **14,448** lowest-level, and **3,072** retirement-save cases.

`tests/test_storage.c` adds **70,000 saves** across sequence wrap and repeated
reboots, healthy-slot reuse, durable map union, capacity exhaustion, and **6,144**
metadata interruptions. Combined, the suites cover **32,880 interrupted writes**.
Old versions 1–5 are rejected and reserved tail bytes are ignored.
It also tests loss after the marker completes but before RAM bookkeeping.
The mock models a single in-progress EEPROM byte; bench power cycling remains
necessary to validate the actual power supply and EEPROM behavior.

See `docs/bench-checklist.md` for the remaining physical acceptance steps and
`docs/validation.md` for the recorded build results.
The current 102.4 ms brightness-ramp build was programmed and verified at
2026-10-21T09:45:27Z with EEPROM reset to erased `0xFF` and user IDs preserved.

References: [PIC12F683 datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/41211D.pdf),
[MPLAB XC8 downloads](https://www.microchip.com/en-us/tools-resources/develop/mplab-xc-compilers/xc8),
[Microchip device packs](https://packs.download.microchip.com/).
