# Test coverage and validation limits

[Build and flash runbook](../README.md#build-and-flash-runbook) | [Validation record](validation.md) | [Board acceptance checklist](bench-checklist.md)

Paths in this document are relative to the repository root. The recorded counts
and timings describe the validated firmware build; documentation edits do not
constitute a new hardware validation run.

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

See [board acceptance checklist](bench-checklist.md) for the remaining physical acceptance steps and
[validation record](validation.md) for the recorded build results.
The current 102.4 ms brightness-ramp build was programmed and verified at
2026-10-21T09:45:27Z with EEPROM reset to erased `0xFF` and user IDs preserved.
