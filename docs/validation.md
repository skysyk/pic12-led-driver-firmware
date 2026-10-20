# Validation record — current working tree

Implemented from the plan supplied in the project directory. The current controller
uses 20 kHz PWM, a 2,048-phase sine approximation, and raw PWM-count interpolation.
The current builds add watchdog recovery and a blinking maximum-editor indication.
Both have built and passed host tests. The current controller was programmed and
verified on a PIC12F683 with MiniPro at 2026-10-20T03:42:13Z (October 19 local
time), preserving EEPROM and user IDs.
It switches modes on a one-second power
hold and turns off on a short release without a recognition window. It retains
the half-second mode-change LED flash, persistent breathing,
the min/max editor with gesture rearming fix, and six-second settings reset.
The current indicator-only image has been built and tested but not programmed.
No PWM waveform measurements or completed board acceptance tests are recorded.

## Target builds

Compiler: Microchip MPLAB XC8 v4.00, Windows x64 portable archive, `-Os`, C99.
Device support: Microchip PIC10-12Fxxx_DFP v1.10.193, target PIC12F683.
Both final builds completed without warnings or errors.

| Image | Program words | RAM bytes | Configuration |
| --- | --- | --- | --- |
| `build/led-controller.hex` | 2039 / 2048 (99.6%) | 125 / 128 (97.7%) | `0x33C4` |
| `build/indicator-only.hex` | 1376 / 2048 (67.2%) | 89 / 128 (69.5%) | `0x33C4` |

The previous controller used 2014 words/113 RAM bytes with `-O2`. Switching that
same source to the compiler's supported `-Os` option measured 1715 words/105 RAM
bytes, making room for the editor and migration code. Diagnostics-only EEPROM
fault state is now compiled only into host tests. The final controller leaves
nine flash words and three RAM bytes free; the compiler includes allocated function
temporaries in this RAM total. Pre-editor images/map remain in `build/before-editor/`.
Sequential byte clearing of button events replaces repeated field-offset writes,
making room for the mode-flash timer. Images/maps from before that addition are
retained in `build/before-mode-flash/`.
Images/maps of the last programmed double-tap build are retained in
`build/before-power-hold/`. Removing its recognition timer reclaims two RAM bytes;
the button scanner now has separate brightness and power hold thresholds.
Images/maps of the last programmed triangle build are retained in
`build/before-sine/`. Byte-addressed event access and sequential five-byte settings
copies reduce XC8 pointer arithmetic enough to fit the sine implementation. The
five-byte layout is checked at compile time. A shared settings-copy function,
direct four-count percentage scaling, and division-free mode-entry comparison
make room for the wider duty path. The prior 38 kHz/2,048-phase sine image and maps
are retained in `build/before-20khz/`. PWM endpoint handling is simplified without
changing polarity or static endpoints. The compiler reports
maximum call depth of three foreground levels plus one interrupt level, within
the eight-level hardware stack. Actual foreground execution time remains a bench check.
The preceding 20 kHz/401-phase image and maps are retained in
`build/before-full-resolution/`. Byte-sized inverse-search bounds cover phase in
four-position increments; the midpoint calculation avoids overflowing the byte.
The last programmed image/maps/sources before watchdog and editor-LED changes are
retained in `build/before-watchdog-editor-led/`. EEPROM scanning now advances a
byte address by eight through all 256 bytes, and new-slot addresses advance by
sixteen with a `0xF0` alignment mask. Avoiding repeated half-slot conversions and
comparing the fixed requested/saved pair without generic pointer parameters makes
room for both changes. The v4 journal format, legacy migration, and save policy
are unchanged; all interruption tests pass with the address-based implementation.

The controller now targets 20 kHz: 8 MHz / (4 * 1 * 100) = 20,000 Hz nominally,
using Timer2 prescaler 1:1 and PR2=99. Each period has 400 duty intervals (about
8.6 bits of resolution). Duty codes 0..399 plus static full-on give 401 distinct
hardware levels. Percentages convert to counts by multiplying by four.
The duty interface, output, and cache are sixteen bits so counts above 255 are
preserved; the cache starts at the invalid sentinel 0xFFFF. Compile-time checks
require the 400-count period used by the scaling and sine table. Legacy duties
are still decoded to percentages for persistence compatibility. Static output
still supplies exact off and full-on endpoints. Status LED polarity matches
the reconstruction notes: GP0 sinks current when the indicator is on and drives
high when off. Initialization loads the high/off latch before enabling GP0 as an
output. It flashes low/on for 489 ticks (500.736 ms nominally) on each mode
toggle, then returns high/off. Every separate mode hold starts the timer; power-off
cancels it. Ordinary wake and restoring a saved mode do not flash. The timer
is transient and its expiration requests rendering without an EEPROM save,
including when an unchanged brightness adjustment coincides with expiration.
The LED is solid for minimum editing and toggles every 245 ticks (250.88 ms) for
maximum editing. Selecting either bound starts with the LED on. Only the indicator
blinks; the selected brightness stays steady. The editor's existing byte encodes
the dark phase in bit 2, and a byte view of the inactive animation timer supplies
the countdown. No extra persistent setting or RAM storage is required. Transitions
request rendering without an EEPROM save. Reset confirmation retains its slower
489-tick blink and both directions of mode switching retain the half-second flash.
The five-second disable feature
and its persistent state have been removed; legacy preference bits are ignored.
The controller's generated assembly was checked for the high GP0 latch before
output enable and for low/on versus high/off rendering.

Configuration retains software control of WDT (`0x33C4`), and startup writes
`WDTCON=0x15`: LFINTOSC/32768, about 1.06 seconds nominally. CLRWDT appears only in
initialization and at the end of completed Timer0 tick processing in main. Idle
iterations and the interrupt handler do not clear it. The Timer0 prescaler remains
assigned to Timer0; watchdog servicing does not change the 1.024 ms timebase.
Timer2 waiting is bounded by 256 failed polls, independent of Timer0. The final
HEX's failed-poll path is BTFSC/DECFSZ/GOTO, five instruction cycles per ordinary
poll. It forces inactive GPIO and disables CCP1 after about 0.64 ms plus interrupt
latency, then waits in a NOP/GOTO loop without servicing the watchdog. The watchdog
reset restarts normal initialization and EEPROM restoration; fault handling itself
does not request writes. Physical timeout and recovery measurements remain pending.

Breathing uses a 2,048-phase half-cosine envelope, equivalent to a shifted sine,
and keeps the steady brightness separate. Nine quarter-cycle samples,
symmetry, and integer interpolation produce all 513 Q9 weights from 0 through 512.
The quarter endpoint 256 is handled explicitly; its byte-table entry is encoded
modulo 256 so the last interpolation delta still fits in a byte. This avoids the
former Q8 ceiling of 257 weights, which would limit the new 400-count carrier.
Multiplying the
Q9 weight by the percentage span and dividing by 128 gives raw duty counts with
four counts per percent, without overflowing sixteen bits. The default 10–100%
range supports 361 hardware levels and the full range supports 401. All available
duties in every valid min/max range appear during complete cycles at period
settings 3..20 (nominal 1.5..10 seconds), including the default 3-second cycle.
At the 0.5- and 1-second settings, the 1.024 ms rendering interval limits full-range
coverage to 293 and 391 duties respectively. Phase count is separate from the
number of unique amplitudes visited. Narrow 99–100% and 0–1% cycles
visit all five duties in those ranges. A division-free binary inverse search
starts near the clamped steady brightness using the same envelope.
Period settings 1..20 use 488 Timer0 ticks per half-second step, giving
0.499712..9.994240 seconds per complete rise-and-fall cycle. The accumulator
advances phase by 2,048 positions per cycle, rendering at most once per 1.024 ms
tick. Up to five phases advance per tick at the shortest setting. The phase
counter wraps with a 2,047 mask. Period settings below 2.5 seconds skip some
phase positions; longer periods visit all 2,048. At 3 seconds, phase advances
average about 683/second. Holding power while
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
33e4190e58ed6909ff2bfa98283c9b1ecee85437549e9bc6964971013ca823cb

indicator-only.hex
c78eb7e0ba7ec279984a38be5c95af7207d230ad6932f80b8e7ef0451e6cd4f0
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

Sine tests check all 2,048 weights for symmetry, monotonic halves, endpoints, and
error against an independent double-precision cosine series. Weight error stays
below 1.8/512; output error stays within two duty counts for every span from 1
through 400 counts. Actual output cycles match an independent cosine reference
within two duty counts for default, full, and narrow ranges, stay inside their
bounds, reach both endpoints, and exercise the distinct-level counts recorded
above. Adjacent weights differ by at most one. All 20 periods match the exact
integer phase schedule and expected visited-position count. A separate production
animation test covers every percentage span 1..100 at every period, equivalent
to all 5,050 endpoint pairs because a minimum offset translates the result by
exactly four counts per percentage point. It verifies every duty appears at
settings 3..20 and records the two faster full-range coverage counts. Every valid min/max pair
combined with all 91 steady brightness settings (459,550 cases) enters within
three counts of the clamped steady
output at the mode-hold threshold.

`tests/test_hw.c` compiles production `hw_init.c` against a small PIC register
model. It verifies PR2=99/prescaler 1, all 401 output duties packed into CCPR1L and
DC1B, 0/256 and 1/257 cache transitions, static off/full-on, status LED updates
when duty is unchanged, Timer0 tick consumption, and preserving both enabled and
disabled interrupt states during EEPROM writes. A boundary arriving on the last
permitted poll succeeds. A stopped Timer2 forces GPIO inactive and disconnects
CCP1 before the simulated watchdog expires, both from static full-on and active
PWM. Neither failure clears the watchdog. The model supplies immediate, delayed,
or missing boundaries; it does not validate physical edge timing or CPU speed.

`tests/test_main.c` executes production `main.c` and `hw_init.c` together. It
injects Timer0 interrupts and logical buttons at HAL entry points and advances an
independent LF watchdog model. One thousand normal ticks each complete before
watchdog service. Missing ticks receive no service and reset; a PWM render blocked
on stopped Timer2 also resets with the output off and the incomplete tick unserviced.

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
solid minimum indication, exact maximum blink edges, steady preview during blinking,
editing and switching selection during the dark phase, 70,000 ticks with power held
while maximum editing, rendered transitions, no writes from blinking, and
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
fully simulate PIC special-function registers, pin voltages, PWM edge timing, electrical
reset behavior, or actual EEPROM endurance. Those checks are listed in
`bench-checklist.md`.

## Physical programming

### Watchdog recovery and maximum-editor blinking

The current normal controller was rebuilt and passed all three host test suites
and the programming-script mock tests before programming with MiniPro 0.7.4 and
TL866CS firmware 03.2.86. Chip ID `0x0023`, revision `0x03`, matched PIC12F683.
The run completed at `2026-10-20T03:42:13.7133703Z`; the manifest status is `verified`.
The programmed HEX SHA-256 is
`33e4190e58ed6909ff2bfa98283c9b1ecee85437549e9bc6964971013ca823cb`.
Flash, EEPROM, all four user IDs, and configuration `0x33C4` passed explicit
verification. All 256 EEPROM bytes and user IDs were preserved. Original and
restored EEPROM input SHA-256 hashes both equal
`cf18040d8822c7fde4aeae97635f7c6ecf7de6e0693a23b00dd0dca9874e4210`.
Backup files, programming inputs, manifest, and the operation log are retained in
`backups/program-pic12f683-20261020T034156507Z-684476/`.
Physical watchdog timing, waveform measurements, and board acceptance remain
pending. The indicator-only image has not been programmed.

### 20 kHz PWM with full-resolution sine breathing

The controller before watchdog and maximum-editor blinking was rebuilt, passed host and hardware-register model tests,
and was programmed using `scripts/program.ps1 -Firmware .\build\led-controller.hex
-Confirm:$false` with MiniPro 0.7.4 and TL866CS firmware 03.2.86. Chip ID `0x0023`,
revision `0x03`, matched PIC12F683. The run completed at
`2026-10-20T01:41:11.1317384Z`. The programmed HEX SHA-256 is
`a3ab2d0e3842e1935f0323de29e0fcce2a8bb073c42a2ec89b421aab6f07d82e`.
Program memory, EEPROM, all four user IDs, and configuration `0x33C4` passed
explicit verification. All 256 EEPROM bytes and user IDs were preserved.
Original and restored EEPROM input hashes both equal
`83ad4a9e848d3ad5d62c9d2e5757c1dad1fc11aa6e8af3bfc669c14b41f46e5c`.
Backup files, programming inputs, manifest, and full operation log are in
`backups/program-pic12f683-20261020T014054346Z-efacfc/`.
Board acceptance and waveform measurements remain pending. The indicator-only
image has not been programmed.

### Triangle breathing with one-second mode switching

The last programmed triangle-envelope, one-second mode-hold controller was rebuilt, passed host tests, and
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
