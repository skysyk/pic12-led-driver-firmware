# Breathing behavior

[User guide](../README.md#user-guide) | [PWM and timing](pwm-and-timing.md) | [Breathing simulator](breathing-demo.html)

The following describes the envelope, editor, and reset implementation. For the
button reference and control flowchart, start with the user guide.

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

## Min/max editor

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

## Settings reset

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
