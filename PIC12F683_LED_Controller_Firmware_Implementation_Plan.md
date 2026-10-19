# PIC12F683 LED Controller — Firmware Implementation Plan

**Target:** Replace the original eight-pin controller on the QY-K4 LED dimmer board with a PIC12F683, retaining the existing three buttons, MOSFET output, and standby indicator. Restore the saved brightness and power state after USB power is disconnected and reconnected.

**Status:** Implementation plan, not compiled or tested firmware. The net assignments below are taken from prior continuity and voltage measurements, but the button polarity, indicator polarity, and several peripheral connections still require bench verification before a permanent replacement is soldered in.

## 1. Hardware and pin assignments

| DIP/SOIC pin | Board label | PIC12F683 signal | Planned usage |
|---|---|---|---|
| 1 | R4 | VDD | Controller positive supply (~4.55–4.68 V measured) |
| 2 | R3 | GP5 | Brightness down button, digital input, internal weak pull-up |
| 3 | R2 | GP4 | Brightness up button, digital input, internal weak pull-up |
| 4 | R1 | GP3/MCLR | Power toggle button, digital input with **external 10 kΩ pull-up to R4/VDD** |
| 5 | L1 | GP2/CCP1 | Hardware PWM output to MOSFET gate circuit |
| 6 | L2 | GP1 | Unresolved; start as input and do not drive before verifying |
| 7 | L3 | GP0 | Standby indicator control, output; polarity to verify |
| 8 | L4 | VSS | Ground |

Existing parts: the MOSFET gate network reportedly includes a ~470 Ω series resistor and ~10 kΩ gate-to-ground resistor. The standby indicator series resistor measured ~506 Ω. Reuse only after confirming output polarity and that the controller is not directly powering the main LED load.

**Essential input assumption:** The three buttons pull their inputs low when pressed. Verify this on the original board or with continuity before enabling weak pull-ups. The PIC12F683 has individually selectable weak pull-ups on GP4/GP5 but not GP3 as a GPIO input, so GP3 requires the additional 10 kΩ resistor. Select the configuration with MCLR disabled so GP3 is a GPIO input.

## 2. Firmware platform and configuration

- Language/build: MPLAB X + XC8 C; generate a HEX file for programming with the TL866CS and a compatible SOIC-8 adapter. Select the exact PIC12F683 device/package in the programmer.
- Internal oscillator: 8 MHz; use the internal oscillator GPIO mode so GP4 and GP5 remain available for buttons.
- Configuration: disable external MCLR (`MCLRE=OFF`); enable power-up timer and brown-out reset as appropriate; keep code protection off during development. Choose watchdog policy deliberately (initially off for easier debugging, later on if the main loop is structured to service it).
- GPIO initialization: hold MOSFET output off during startup, disable analog inputs (`ANSEL=0`) and comparator (`CMCON0=0x07`), set GP3/GP4/GP5 as inputs and GP0/GP2 as outputs. Do not drive GP1 until its net has been identified.
- Button pull-ups: enable `WPU4` and `WPU5` and the global pull-up enable bit. GP3 receives the external 10 kΩ to VDD.
- Timer strategy: Timer2 drives PWM entirely in hardware; a separate periodic time base handles button scanning and deferred EEPROM saves.

## 3. PWM and brightness

- Route CCP1 hardware PWM to GP2 (physical pin 5). Initially use 8 MHz oscillator, `PR2=255` and Timer2 prescaler 1:4: approximately **1.95 kHz PWM with 10-bit duty-cycle resolution**. Verify with an oscilloscope before powering the main LEDs.
- Keep a logical brightness value separate from `power_on`; switching power off sets the effective PWM duty to zero without erasing the saved brightness.
- User-adjustable brightness range: initial proposal ~10–100%; make minimum, maximum, ramp speed, and step size compile-time constants.
- Short brightness press: change by one coarse step, initially 10 percentage points (ten levels, nine transitions from minimum to maximum); clamp at both limits. Exact step behavior is not required to emulate the old chip.
- Brightness button held for >400 ms: smoothly ramp in the chosen direction until release, e.g. one percentage point every 20 ms. Hold-to-ramp must not also trigger a short-press change on release.
- Apply duty updates to the CCP registers rather than bit-banging GPIO; consider perceptual mapping later only if linear duty adjustments appear visually uneven.

## 4. Button handling and state machine

- Use a roughly 1 ms tick and individually debounced button state, e.g. require 20–30 ms of stable input before accepting a change.
- Recognize `pressed`, `released`, `short_press` and `held` events, suppressing repeated short-press events while held.
- Power button: on debounced short press, toggle `power_on`. On transition to off, save the current brightness unchanged. Do not require pressing brightness buttons to restore it.
- Brightness up/down: use stepped changes for taps and continuous ramping for holds. Proposed behavior if brightness is changed while off: turn on and apply the change (can be altered after testing).
- Simultaneous opposing brightness buttons: ignore both; prioritize the power button if it occurs simultaneously.
- Standby LED on GP0: signal off/standby and turn it off while the main LEDs are on; confirm whether the board expects active-high or active-low drive.

## 5. EEPROM persistence and wear leveling

**Use a wear-leveled, power-fail-tolerant journal instead of overwriting one address.** The PIC12F683 has **256 EEPROM bytes**. Divide these into **32 slots of 8 bytes**:

| Offset | Value |
|---|---|
| 0 | Commit marker, `0xA5` for valid; written **last** |
| 1 | Record format version, initially `1` |
| 2–3 | Monotonically increasing 16-bit sequence number, little-endian |
| 4–5 | Brightness/PWM value, little-endian (10-bit value stored in 16 bits) |
| 6 | Flags: bit 0 = last saved power state |
| 7 | CRC-8 over bytes 1–6 |

**Startup load:** Scan 32 slots, accept only records with the commit marker, supported version, valid brightness/flags, and matching CRC. Find the newest valid 16-bit sequence using wraparound-aware comparison. Restore its state. If all slots are invalid, choose a safe default (suggested: off, brightness 50%) and leave EEPROM unwritten until a user change.

**Write algorithm:** Compare the requested state against the last *committed* state. If unchanged, skip the write. Otherwise select the slot after the last valid one; write an invalid commit marker there first, write bytes 1–7, read back/verify as desired, and write `0xA5` only after the record is complete. Keep the previous valid slot untouched. Move the next write to the following slot, wrapping after slot 31. Sequence wraps modulo 65536. Use the datasheet EEPROM unlock/write sequence, keep write-enable off except during updates, and handle write failures safely.

**Save timing:** For brightness adjustments, restart a debounce-for-storage timer after every change, and save once after ~750–1000 ms of no further adjustments; never write on each smooth-ramp increment. Save promptly when the power state changes, even if the brightness save timer has not expired. An interrupted EEPROM commit must leave at least one older valid record.

**Power loss note:** Because the board is powered over USB and lacks a verified hold-up supply or power-fail detector, the last fractional-second adjustment may not be saved if USB is abruptly unplugged during the pending-save window. Saving at the end of a completed adjustment and immediately upon power toggling minimizes this risk; absolute last-instant preservation would require additional power-fail hardware or more aggressive writes.

The 32-slot ring greatly reduces write concentration. Because the marker byte may be invalidated and then committed on each slot reuse, it can experience **two writes per record at that slot**; do not simply multiply nominal per-cell endurance by 32 without accounting for this.

## 6. Software structure

- `board.h`: pin mappings, polarity flags, system constants.
- `hw_init.c`: oscillator, configuration-dependent setup, GPIO, PWM, timers.
- `buttons.c`: debounce, press duration and events.
- `dimmer.c`: current on/off state, stepped/ramp adjustment, duty translation.
- `storage.c`: CRC-8, record validation, newest-record search, journal write, deferred-save timer.
- `main.c`: initialize hardware with LEDs off, load persistent state, render output, dispatch button events and background storage tasks.

Keep interrupt handlers short, using them only for timing/event flags; perform EEPROM state management in the foreground. Confirm the XC8 compiler/runtime and the correct special-function register names for this specific PIC before writing production code.

## 7. Implementation and acceptance tests

1. Verify each button's connection and pressed voltage, GP0 indicator polarity, GP1 isolation, and GP2-to-gate signal path. Check SOIC body width and pin-1 orientation.
2. Build a minimal firmware that initializes the chip and toggles only the indicator, with the MOSFET held off.
3. Scope the GP2 PWM output at min, middle and max brightness; confirm frequency, polarity, safe boot/off states, and response of the existing MOSFET gate circuitry.
4. Implement button debouncing, short taps, long-hold ramps and power toggling. Check minimum/maximum clamping and no accidental repeat.
5. Test EEPROM first-run defaults, state restoration after brief and long USB disconnections, and no writes when no value changed.
6. Simulate power interruption during each phase of a journal write, confirming that startup picks the previous valid record rather than a corrupt one.
7. Stress-test sequence rollover, slot cycling, rapid presses and extended button holds; inspect EEPROM to verify rotation through all 32 slots.
8. Only then program the production SOIC-8 chip with the TL866CS and solder it in. Keep reprogramming access in mind: in-circuit programming may conflict with attached board components, so off-board socket programming is the initial method.

## References

- [Microchip PIC12F683 datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/41211D.pdf): GPIO pull-ups and GP3, PWM CCP1/Timer2, EEPROM organization and write safeguards.
- Board electrical assignments are based on the measurements previously discussed in this conversation, not on a manufacturer schematic.
