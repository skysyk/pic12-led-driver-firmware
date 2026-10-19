# Board acceptance checks

These checks are pending; compilation and host simulation cannot verify them.

1. Check pin-1 orientation and package fit. Confirm R4/VDD and L4/VSS, board supply
   voltage, all three button traces and pressed-low voltages, and the external
   10 kΩ GP3 pull-up. Confirm GP1 can be left floating as an input.
2. Confirm the GP2 gate path, series resistor, and gate-to-ground bias; verify that
   the PIC drives only the gate circuit. Confirm the indicator path from VDD via
   ~510 Ω and the LED anode/cathode to GP0. The firmware uses active-high PWM
   and an active-low indicator: GP0 high means off, low means on.
3. Build and program `indicator-only.hex` into the PIC12F683 off-board using the
   TL866CS and appropriate adapter. Read back/verify the device and configuration
   word. With this image, GP2 stays inactive and EEPROM is untouched. Tap power to
   toggle the indicator. Scope GP2 through boot/reset and button activity.
4. With the main load disconnected, program `led-controller.hex`. With erased
   EEPROM, confirm GP2 is inactive at boot and standby indication is correct.
   Scope 10%, 50%, and 100% commands; intermediate PWM should be about 37.736 kHz,
   maximum should be a static active level, and off a static inactive level.
5. Reconnect the load with a suitable current-limited supply. Check taps, holds,
   both limits, power priority with staggered releases and a held brightness
   button, opposing-button cancellation, standby restoration,
   and standby brightness adjustment. Check for startup flashes during USB cycling.
   In standby, hold power for five seconds: only the indicator should turn off,
   releasing power must leave the main LEDs off, and unplug/reconnect must retain
   the disabled indicator. A shorter hold and a long hold while on must not disable
   it. Repeated long holds must leave it disabled without extra EEPROM writes.
6. Adjust brightness, wait at least a second, disconnect USB briefly and for longer,
   and verify restoration. Toggle off and repeat. Unplug during a ramp and within
   the quiet window; expect the last fully committed state. A prompt save still
   needs time to finish its EEPROM byte writes.
7. Inspect EEPROM after repeated adjustments and power toggles to confirm slot
   rotation, valid CRCs, no writes during inactivity, and correct newest record.
   Interrupt USB during commits and confirm fallback to a valid older record.
8. Only after these checks, solder the production part into the board. Retain an
   off-board programming route: existing circuit components can interfere with ICSP.

## Breathing mode acceptance

- While output is on, double-press power within 350 ms: output must remain on and
  begin rising/falling between 10% and 100%. Double-press again to restore the
  steady brightness. A single press must turn off after the double-press window;
  long holds while on must not disable the standby indicator.
- Tap up/down to traverse all 20 periods, including wraparound at 0.5/10 seconds.
  Scope complete rise-and-fall cycles, including 0.499712 and 9.994240 seconds
  nominally, and verify the 37.736 kHz carrier is unchanged. Check smoothness at
  both extremes and button response while the animation and EEPROM writes run.
- Confirm both-button cancellation and power priority during period changes.
  A brightness button in standby must wake the saved breathing mode and adjust
  its period, without changing steady brightness.
- Change period, wait at least one second, and cycle USB. Check mode/period,
  steady brightness, power state, and standby LED preference all restore. Check
  startup with existing version 1/2 journals; mode should default to steady and
  period to 3 seconds until changed. Animation must not create journal entries.
- Minimum/maximum editing is a UI proposal only; current bounds remain 10–100%.
