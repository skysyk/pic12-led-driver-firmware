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
   word. With this image, GP2 stays inactive and EEPROM is untouched. Hold power
   while off for three seconds to see the reset warning blink, then release to
   cancel. Scope GP2 through boot/reset and button activity.
4. With the main load disconnected, program `led-controller.hex`. With erased
   EEPROM, confirm GP2 is inactive at boot and the status LED stays off normally.
   Scope 10%, 50%, and 100% commands; intermediate PWM should be about 20 kHz,
   maximum should be a static active level, and off a static inactive level.
5. Reconnect the load with a suitable current-limited supply. Check taps, holds,
   both limits, power priority with staggered releases and a held brightness
   button, opposing-button cancellation, standby restoration,
   and standby brightness adjustment. Check for startup flashes during USB cycling.
   The old standby indication and five-second disable action are removed. Verify
   the status LED stays off in normal steady/breathing operation and while off.
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

- While output is on, hold power for one second: output must remain on and
  begin rising/falling between the saved limits (default 10–100%). Release and
  hold again to restore steady brightness. Continued holding must switch only
  once, without turning off on release or resetting even through six seconds,
  including during a dark 0% breathing frame.
- Power presses released before one second must turn the output off as soon as
  release is debounced, including 400–999 ms presses. Check that two quick taps
  turn off and back on without changing mode. Up/down hold ramps stay at 400 ms.
- Each switch between steady and breathing must light the status LED for about
  half a second, identically in both directions, then turn it off. Every separate
  hold gives a new flash. Brightness/period controls must remain responsive during it.
  Turning off cancels the flash, and waking/restoring a mode must not flash.
- Tap up/down to traverse all 20 periods, including wraparound at 0.5/10 seconds.
  Scope complete rise-and-fall cycles, including 0.499712 and 9.994240 seconds
  nominally, and verify the 20 kHz carrier stays constant. Check smoothness at
  both extremes and button response while the animation and EEPROM writes run.
- Verify the sine envelope slows near both endpoints and starts near the clamped
  steady brightness on mode entry. At slow periods, scope the individual raw PWM
  levels: a 99–100% range should use all five duties 396..400 rather than only the
  two integer-percent endpoints. At periods of 1.5 seconds and longer, check that
  every duty appears (401 over 0–100%, 361 over 10–100%). The 2,048-phase envelope
  skips some positions at periods below 2.5 seconds; 0.5/1-second full-range cycles
  use only 293/391 duties because of the rendering interval. Check that controls
  and EEPROM activity do not stretch the cycle or miss Timer0 ticks, including at
  the shortest period.
- Confirm both-button cancellation and power priority during period changes.
  A brightness button in standby must wake the saved breathing mode and adjust
  its period, without changing steady brightness.
- Change period, wait at least one second, and cycle USB. Check mode/period,
  steady brightness, power state, and limits all restore. Check
  startup with existing version 1/2 journals; mode should default to steady and
  period to 3 seconds until changed. Animation must not create journal entries.
- While breathing, hold both brightness buttons for about one second to enter
  minimum editing. The status LED must light and the output become steady at that
  limit. A power tap selects maximum editing; the status LED must blink with
  approximately 250 ms on/off phases while the main LEDs stay at the selected
  brightness. Selecting minimum returns to solid indication. Up/down taps change by 10 percentage
  points, holds ramp by 1% without crossing the other limit. Test 0% minimum and
  100% maximum, a one-point range, and coarse steps near either boundary.
  Power taps/holds in the editor must not turn off or change breathing mode.
  Test adjustments and selection changes during a dark indicator phase. Blinking
  must remain responsive and must not cause EEPROM writes.
- Hold both again to save and resume; neither gesture may retrigger while still
  held. The status LED must go off on exit. Staggered releases must not change
  period or limits. Release/repress only one brightness button while holding the
  other: the editor must not enter or exit again. Hold power with both brightness
  buttons, then release power: the chord must remain cancelled until both
  brightness buttons are released. USB cycling after a
  completed save must restore both limits; cycling before exit must discard edits.
- Inspect the new 16-slot journal and legacy migration. Preserve the original
  EEPROM dump and test power interruption during migration off-board. Confirm
  older mode/period settings survive and unedited limits default to 10–100%.
  Obsolete indicator preferences must not affect the status LED.

## Settings reset acceptance

- With non-default saved preferences and output off, hold power: the status LED
  must stay off for three seconds, then light and toggle every 0.5 seconds during
  the next three. The main output must remain off throughout.
- Release before six seconds, including just before the threshold: blinking must
  stop, output must remain off, and preferences must survive USB cycling unchanged.
- Keep holding through six seconds: status LED off, output off, 50% brightness,
  steady mode, 3-second period, and 10–100% limits. Allow the asynchronous save to
  finish, then verify these defaults survive USB cycling.
- Keep holding well past completion and verify no retrigger or additional journal
  writes. Confirm an on-state hold switches mode once and cannot reset; a new
  hold after a tap has turned off must still reset normally.

## Watchdog recovery acceptance

- With the load disconnected, use a temporary fault-injection build to stop
  Timer2 during an intermediate PWM update. Verify GP2 is forced inactive within
  roughly 1 ms, CCP1 is disconnected, and reset follows after about one second.
  Repeat starting from static full-on and active PWM. Watchdog timing varies with
  LFINTOSC tolerance; the host model does not measure it.
- Stop Timer0 interrupts while leaving foreground polling active. Verify that
  idle polling cannot keep the watchdog alive and the controller restarts.
- Confirm that the last valid EEPROM settings restore after recovery, including
  power state, and that firmware recovery does not initialize or erase settings.
  Repeat around asynchronous EEPROM commits and verify fallback after interrupted
  writes. Check the external gate bias holds the load off during reset.
- Restore the production image after fault injection and verify long steady and
  breathing operation, all buttons, editing, reset holds, and EEPROM saves do
  not produce unwanted watchdog resets. Confirm normal cycle timing is unchanged.
