# Validation record — current working tree

Implemented from the plan supplied in the project directory. The current controller
was programmed and verified on a PIC12F683 with MiniPro on 2026-10-19.
It switches modes on a one-second power
hold and turns off on a short release without a recognition window. It retains
the half-second mode-change LED flash, nominal 38 kHz PWM, persistent breathing,
the min/max editor with gesture rearming fix, and six-second settings reset.
The current indicator-only image has been built and tested but not programmed.
No PWM waveform measurements or completed board acceptance tests are recorded.

## Target builds

Compiler: Microchip MPLAB XC8 v4.00, Windows x64 portable archive, `-Os`, C99.
Device support: Microchip PIC10-12Fxxx_DFP v1.10.193, target PIC12F683.
Both final builds completed without warnings or errors.

| Image | Program words | RAM bytes | Configuration |
| --- | --- | --- | --- |
| `build/led-controller.hex` | 2042 / 2048 (99.7%) | 121 / 128 (94.5%) | `0x33C4` |
| `build/indicator-only.hex` | 1248 / 2048 (60.9%) | 89 / 128 (69.5%) | `0x33C4` |

The previous controller used 2014 words/113 RAM bytes with `-O2`. Switching that
same source to the compiler's supported `-Os` option measured 1715 words/105 RAM
bytes, making room for the editor and migration code. Diagnostics-only EEPROM
fault state is now compiled only into host tests. The final controller leaves
Six flash words and seven RAM bytes free; the compiler includes allocated function
temporaries in this RAM total. Pre-editor images/map remain in `build/before-editor/`.
Sequential byte clearing of button events replaces repeated field-offset writes,
making room for the mode-flash timer. Images/maps from before that addition are
retained in `build/before-mode-flash/`.
Images/maps of the last programmed double-tap build are retained in
`build/before-power-hold/`. Removing its recognition timer reclaims two RAM bytes;
the button scanner now has separate brightness and power hold thresholds.

The controller now targets 38 kHz: 8 MHz / (4 * 1 * 53) = 37,735.849 Hz,
using Timer2 prescaler 1:1 and PR2=52. Each period has 212 duty counts (about
7.7 bits of resolution). Percentages now convert directly to hardware counts,
removing the old 10-bit intermediate and its second scaling function/header.
The duty interface is eight bits, with a compile-time period limit. Legacy duties
are still decoded to percentages for persistence compatibility. Static output
still supplies exact off and full-on endpoints. Status LED polarity matches
the reconstruction notes: GP0 sinks current when the indicator is on and drives
high when off. Initialization loads the high/off latch before enabling GP0 as an
output. It flashes low/on for 489 ticks (500.736 ms nominally) on each mode
toggle, then returns high/off. Every separate mode hold starts the timer; power-off
cancels it. Ordinary wake and restoring a saved mode do not flash. The timer
is transient and its expiration requests rendering without an EEPROM save,
including when an unchanged brightness adjustment coincides with expiration.
The LED stays on while editing and toggles during reset confirmation.
The five-second disable feature
and its persistent state have been removed; legacy preference bits are ignored.
The controller's generated assembly was checked for the high GP0 latch before
output enable and for low/on versus high/off rendering.

Breathing uses a 256-phase linear triangle envelope and keeps the steady brightness
separate. Period settings 1..20 use 488 Timer0 ticks per half-second step, giving
0.499712..9.994240 seconds per complete rise-and-fall cycle. Holding power while
on toggles mode at 977 ticks (1000.448 ms after press debounce). Continued holds
switch only once, including past the reset thresholds, and release does not
turn off. Power releases below this threshold toggle output immediately after
release debounce; the old double-tap recognition window has been removed.
Brightness hold ramps retain their 391-tick (400.384 ms) threshold. A power
hold inside the editor is ignored, and an off-state hold still arms/confirms reset.
Periods wrap in both directions, and power priority still cancels overlapping
brightness/period gestures. Mode changes save immediately and period changes
save after the usual quiet window. Animation flags request rendering only.

The editor pauses breathing to preview minimum or maximum. Hold both brightness
buttons for about one second to enter at minimum, use power to select the other
limit, tap up/down by 10 percentage points as in steady mode, and hold to ramp by
1%. Holding both again exits and
requests an immediate save. Both gestures require both brightness buttons to be
released before retriggering. A separate one-byte latch preserves this requirement
across partial releases and power cancellation.
Minimum is clamped to 0..maximum-1 and maximum to minimum+1..100. Edits do not
overwrite steady brightness or period and do not request EEPROM writes until
exit. Unconfirmed edits are lost on power cycling. Physical editor acceptance
is still pending. One clamping path is shared by steady brightness and both editor
limits; coarse steps near a bound stop one percentage point before the other limit.

Reset eligibility is latched only for a power press that starts while logically
off. The button scanner emits separate 3-second arm and 6-second confirmation
events and pauses hold timing while the raw contact is released. Between these,
the status LED toggles every 489 ticks (500.736 ms nominally); release cancels.
Confirmation restores off/50%/steady/3-second/10–100% defaults and requests a
prompt journal save. Continued holds saturate without retriggering. Blink frames
only request rendering, never EEPROM saves. The animation accumulator is reused
for blink timing while off, and the settings state is now five bytes.

Version 4 uses sixteen aligned 16-byte EEPROM slots with ten meaningful bytes,
including CRC and a migration guard. It stores brightness percentages, mode,
period, both limits, and power. The old indicator bit is accepted but ignored and
cleared in new records. Boot also reads every
original eight-byte slot for versions 1..3. Legacy limits default to 10..100%;
v1/v2 mode/period default to steady/3 seconds, while v3 mode/period are retained.
Migration advances beyond the latest legacy half-slot before writing and verifies
the payload before committing. Byte 9 clears the overlapping old odd-slot version
to prevent stale-record revival when a new CRC happens to equal 0xA5 after sequence
rollover. Completed journal saves use eleven asynchronous byte writes.

The HEX verification script checked record lengths/checksums, flash address bounds,
complete 14-bit words, reset vector presence, the configuration word, and absence
of EEPROM data. The compiler reported zero bytes of EEPROM initialization in each
image; the runtime journal uses the chip's existing 256 EEPROM bytes.

SHA-256 of delivered images:

```text
led-controller.hex
1b230d2a5e557e885e7582a43d824e532beb48312bd35bf2024c6ed5b0248a01

indicator-only.hex
418187a37b98c37af384e3ecbb614f8730dd832093211bcd9f5d05f88edac5d5
```

## Host tests

Compiler: Zig 0.15.2 (`zig cc`), C99, `-Wall -Wextra -Werror -pedantic`, assertions
enabled. `scripts/test.ps1` passed all checks in `tests/test_firmware.c`, including
5,632 journal, 11,517 legacy-migration, and 2,816 reset torn-write cases (19,965 total).
The mock enforces no overlapping EEPROM operations and
permits writes to stay busy across repeated foreground service calls.

PWM tests compare all 101 percentage values against independent wide-arithmetic
scaling, check 0%/10%/50%/100% results, and verify that every 1% brightness adjustment
maps to a distinct hardware duty. Existing EEPROM round-trip and legacy record
tests still pass while legacy duty values migrate to percentage storage.

Breathing tests check the exact one-second mode threshold, continued holds without
retriggering, no tap on release after a hold, no mode change during off-state
one-second holds, and immediate off on debounced release for short presses,
400–999 ms presses, and release just before the threshold. Two quick taps must
turn off/on without changing mode. Tests also cover monotonic rise/fall
and both endpoints for all 20 exact tick periods, period wraparound, retained
steady brightness, delayed period saves, mode/power/preference restoration,
legacy v1/v2 migration through sequence rollover, invalid period rejection, and
no EEPROM writes during repeated animation cycles. The torn-write matrix also
checks restoration of mode and period in addition to brightness, power, and bounds.
Mode-flash tests cover both directions, exact duration and rendered expiration,
fresh indications on separate holds, power-off cancellation, no indication on boot/wake, responsive
brightness/period controls, edit indication, and no extra persistence writes.
An unchanged maximum-brightness tap is aligned with the flash's expiration tick
to verify that its render notification is retained.

Editor tests cover the exact entry threshold, both hold gestures firing once,
staggered releases, min/max selection and preview, power clicks/holds inside the
editor, 10% taps and 1% hold ramps, 0%/100% bounds, strict ordering and one-point ranges,
saved versus unconfirmed edits, unchanged-settings write avoidance, and restoration.
Rearming regressions cover both entry and exit, either brightness button remaining
held across release/repress of the other, and power cancellation before or during
the chord. No transition or save is allowed until both brightness buttons are
released; a fresh chord after that release must work normally.
All bits of each meaningful v4 byte are flipped independently; all 256 values in
each settings payload byte are tested with recomputed CRCs to check field validation.
Version 4 records on invalid half-slot boundaries are rejected.
Every one of the 5,050 ordered min/max pairs is also exercised with both coarse
directions on each bound: 20,200 clamping scenarios. Status tests track the
main loop's render flag as well as the requested LED level, so missing render
notifications on editor/reset transitions fail the tests.

The migration matrix covers a full legacy ring with its newest record at each of
the 32 addresses, for each of versions 1..3, across sequence rollover. Each of
eleven writes is interrupted with representative byte values in every layout;
three representative even/odd layouts also exhaust all 256 torn values. Every
write is checked against the protected legacy record's address range. A full v4
rotation is checked for invalidation of all old odd-slot version bytes.

Power-priority regression tests cover up/down, initial on/off state, either
staggered release order and simultaneous release, a continued hold after power
release, cancellation of an existing ramp, persistence of the chosen power state,
and normal operation after a fresh brightness press. The staggered-release
regression failed before the fix and passed after it.

Reset tests cover both exact hold thresholds, every blink transition, early
release immediately before arm/confirmation, cancelled preference preservation,
no short press on release, no retrigger after prolonged holds/counter rollover,
immediate save, restored defaults after reboot, and no writes for an already-default
reset. Holds that start while on toggle mode once without resetting. A new hold
after a completed tap turns off is tested for normal reset eligibility.
Mode holds overlapping either brightness button or both are tested in both modes
and both staggered release orders, without brightness/period leakage or editor
entry. Obsolete indicator preferences are tested in both
states for compatibility. Reset transactions are interrupted at each of eleven
writes with all 256 possible torn-byte values.

These tests exercise production button, dimming, and journal sources. They do not
simulate PIC special-function registers, pin voltages, PWM edge timing, electrical
reset behavior, or actual EEPROM endurance. Those checks are listed in
`bench-checklist.md`.

## Physical programming

The current one-second mode-hold controller was rebuilt, passed host tests, and
was programmed with MiniPro 0.7.4 and TL866CS firmware 03.2.86 on 2026-10-19.
Chip ID `0x0023`, revision `0x03`, matched PIC12F683. The run completed at
`2026-10-19T21:59:58Z`. The programmed HEX SHA-256 is
`1b230d2a5e557e885e7582a43d824e532beb48312bd35bf2024c6ed5b0248a01`.
Program memory, EEPROM, all four user IDs, and configuration `0x33C4` passed
explicit verification. All 256 EEPROM bytes and user IDs were preserved.
Original and restored EEPROM input hashes both equal
`87211acf75f32b900cd887a3c98020f7a6dd95daa11559fea935f62cae810ee2`.
Backup files, programming inputs, manifest, and full operation log are in
`backups/program-pic12f683-20261019T215941999Z-afcb33/`.
Board acceptance and waveform measurements remain pending.

### Double-tap mode switching with flash

The previous double-tap controller, including the half-second mode-change flash, was rebuilt,
passed host tests, and was programmed with MiniPro 0.7.4 and TL866CS firmware
03.2.86 on 2026-10-19. Chip ID `0x0023`, revision `0x03`, matched PIC12F683.
The run completed at `2026-10-19T21:41:44Z`. The programmed HEX SHA-256 is
`8096fc0a4f300968fbe907348ae5022724d6cc94d9fbc79c22384b16c06682e4`.
Program memory, EEPROM, all four user IDs, and configuration `0x33C4` passed
explicit verification. All 256 EEPROM bytes and user IDs were preserved.
Original and restored EEPROM input hashes both equal
`1effe8384aa6acd598789cc5eb27ab574fd080369bc33edbf5bb1972fc28735f`.
Backup files, exact programming inputs, the manifest, and full operation log are in
`backups/program-pic12f683-20261019T214127681Z-847722/`.
Board acceptance and waveform measurements remain pending.

### Before the mode-change flash

The controller before adding the mode-change flash was rebuilt, passed host tests,
and was programmed using
`scripts/program.ps1 -Firmware .\build\led-controller.hex -Confirm:$false` on
2026-10-19. MiniPro 0.7.4 with TL866CS firmware 03.2.86 identified PIC12F683
chip ID `0x0023`, revision `0x03`. The run completed at `2026-10-19T20:54:35Z`.
The programmed HEX SHA-256 is
`673c87b7f4efad0b2a2cc7c79412faa21cbfe75322e354c9939905a70954fb92`.
Program memory, EEPROM, user IDs, and configuration `0x33C4` passed explicit
verification. All 256 EEPROM bytes and four user IDs were preserved. Original
and restored EEPROM input hashes both equal
`b8244ede662678294466d58ac0a3dfd4b2d01662c11dfc3bc68de1cdc1640182`.
The verified backup, programming inputs, manifest, and full operation log are in
`backups/program-pic12f683-20261019T205418608Z-1fdbeb/`.
Board acceptance and waveform measurements remain pending.

### Earlier programming run

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
