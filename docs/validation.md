# Validation record — current working tree

[Build and flash runbook](../README.md#build-and-flash-runbook) |
[Coverage overview](test-coverage.md) | [Board acceptance checklist](bench-checklist.md)

The current build uses the simulator's exponential curve, 10 kHz two-bit
fractional dithering for breathing only, and 1% brightness steps every 102.4 ms
while held. It was programmed and verified at 2026-10-21T09:45:27Z with all EEPROM
bytes erased to `0xFF` and user IDs preserved. The preceding 40.96 ms ramp build
was verified at 2026-10-21T09:07:48Z with EEPROM and user IDs preserved. The preceding
natural-envelope build without dithering was verified at 2026-10-21T08:04:41Z.
The programming records remain below. EEPROM stays at v6: existing
v6 records remain compatible, versions 1–5 are ignored, and no migration is needed.

The analytical curve is
`(exp(-cos(2*pi*phase/2048)) - exp(-1)) / (exp(1) - exp(-1))`, exactly the
simulator's Exponential option. It has symmetric rise/fall, a narrower peak and
broader trough than sine, and no explicit peak/minimum holds. The rising half is
stored as 33 Q8 knots, with 256 encoded modulo 256 only at the final knot. The
falling half reflects phase onto the rising half. Q13 interpolation and bounded
16-bit products produce Q3 targets. Continuous interpolation error is at most
approximately 0.823 hardware counts at full span; tests bound final error to two
counts. A monotone four-phase-grid entry search retains the three-count entry
bound. Maximum entry uses phase 1024. The cycle retains 2,048 phases and all
20 nominal 0.5–10 second periods.

The hardware rounds Q3 to Q2: 1,601 available average-duty commands at 0.0625%
increments, over 401 instantaneous hardware levels. Four 100 us update slots
average exactly to a constant fractional target; the longest pattern is 0.4 ms
(2.5 kHz). Timer2 postscale is 1:2, so the carrier remains 20 kHz. The residual
survives frame/mode changes. Packed targets retain the former Q3 bit positions
with bit 0 forced zero, avoiding a two-bit shift in the ISR. Only breathing
produces fractional targets. All steady levels and editor previews are integer
duties, disabling the update interrupt after one write. The selected 0.25%
minimum remains a hard floor for every output slot. A full-span curve visits
816 Q2 targets across all phase positions; available commands are not a promise
that every command is visited in a cycle.

Steady/editor brightness holds still start after 400.384 ms and change by 1%,
but consume one in five button-repeat events: 102.4 ms per brightness step.
A three-bit skip count shares an existing gesture byte and resets on a new
brightness press. The first step still occurs at the hold threshold. Taps, power gestures, editor chords, bounds, and the one-count
stop before off remain unchanged. Breathing-period controls still consume every
20.48 ms repeat. EEPROM quiet time and asynchronous saves are unchanged.

## Target builds

Compiler: MPLAB XC8 v4.00, `-Os`, C99. Device pack: PIC10-12Fxxx_DFP 1.10.193.
Both images build for PIC12F683 without warnings or errors.

| Image | Program words | RAM bytes | Configuration |
| --- | --- | --- | --- |
| `build/led-controller.hex` | 2013 / 2048 (98.3%) | 120 / 128 (93.8%) | `0x33C4` |
| `build/indicator-only.hex` | 1433 / 2048 (70.0%) | 85 / 128 (66.4%) | `0x33C4` |

The controller leaves thirty-five flash words and eight RAM bytes free,
including compiler temporaries. Slowing holds to 102.4 ms adds four words with
the same RAM usage versus the preceding image (2009 words/120 bytes).
The indicator-only image also adds four words and retains 85 RAM bytes.
Boot settings and button events share
one union after restoration, gesture flags share one byte, and the EEPROM busy
check is a direct register read on PIC. The production storage format and behavior
remain v6; no EEPROM migration or additional writes are introduced by animation.

The prior main firmware, tests, images/maps/assembly, and prototype are retained
in `build/before-bad-slot-integration/`. The pre-dithering sources and main image/map
are in `build/before-dithering/`. Earlier implementation snapshots remain
under `build/before-*`. Git history retains earlier validation descriptions.
The pre-boundary-fix hardware source, linked image/map, assembly, and HEX test
are retained in `build/before-boundary-fix/`.
The last sine sources, tests, documentation, linked images/maps, and assembly
are retained in `build/before-natural/`. The last programmed natural/dithering
sources, tests, documentation, images/maps, and assembly are retained in
`build/before-dither-removal/`. The last programmed no-dither sources, tests,
documentation, images/maps, and assembly are in `build/before-exponential-q2/`.
The preceding 40.96 ms exponential/Q2 image and supporting sources/tests/docs are
in `build/before-100ms-ramp/`.

The compiler reports at most four foreground call levels plus one interrupt
level, within the eight-level hardware stack. All five settings buffers fit
the byte-address copy interface and remain within implemented RAM.
Generated C startup clears button state, including on watchdog reset.

HEX checks validate checksums/lengths, flash bounds, complete words, reset vector,
configuration, and absence of EEPROM payload. Both builds contain zero EEPROM
initialization bytes. SHA-256 of the current exponential/Q2, 102.4 ms ramp images
(main image programmed; indicator-only image unprogrammed):

```text
led-controller.hex
383acb8f665379eb332775aa595fe8b1749afc75e107e87e270ea30854d05d03

indicator-only.hex
c0a9a28016a3b12d383c8914fcf0902400c688f548e3d11e2de4b2ac93edb650
```

## Storage behavior

The v6 journal has sixteen aligned 16-byte slots. Each eleven-byte record includes
the existing settings, sequence, two-byte retirement map, and CRC over bytes 1–9.
Each transaction uses twelve asynchronous, verified byte writes. Boot unions maps
from all valid records before selecting settings from non-retired slots. This
prevents the previously reproduced stale-record sequence-age failure.

The second failed transaction retires a destination, with one-second backoff
between attempts. A new map forces persistence even if settings have reverted.
The 879-tick quiet period still applies to fresh deferred adjustments and cannot
shorten a failure backoff. While retirement is undurable, the LED blinks 128 ticks
on/off (131.072 ms each). Saving a usable record clears the warning.

Two healthy slots support continued atomic rotation. With no destination beyond
the protected record, writes stop, the rapid warning continues, and buttons and
lighting continue from RAM. There is no repeated ten-second exhausted-sweep retry
and no automatic unretirement. The six-second reset preserves the retirement map;
explicit programmer erasure clears it. See `bad-slot-tracking.md` for details.

## Host and linked-image validation

Zig 0.15.2 (`zig cc`), C99, `-Wall -Wextra -Werror -pedantic`, assertions enabled.
`scripts/test.ps1` passes all four suites against production sources:

- `tests/test_firmware.c`: existing debounce/gesture priorities, steady levels and
  ramps, all 5,050 min/max ranges, editor/reset timing, exponential-curve error and phase/duty
  coverage, save policy, power restoration, and EEPROM field/CRC validation.
  All 808 steady transitions and 2,000 one-count-minimum range/period combinations
  remain covered. Preference reset is checked with a durable retirement present.
  Unrounded Q3 outputs are checked through all 5,050 percentage ranges plus all
  100 one-count-minimum ranges over a default cycle, against a double-precision
  reference. All seven fractional levels below one count occur when off is the
  selected breathing minimum; Q2 rounding makes three fractional targets below
  one count available to the dither driver. Selecting one count prevents any
  lower output. 102.4 ms holds are checked before the next step and at the exact step,
  after release/repress with a partly consumed skip count, in steady mode and
  both editor states; period repeats
  retain their previous cadence.
- Settings torn writes: 6,144 initial/reused-journal cases, 3,072 reset cases,
  14,448 lowest-level cases, and 3,072 saves after retiring a failed slot.
- Failures at each of twelve transaction stages with every newest-slot position:
  192 cases. All seventeen newest/empty layouts exhaust destinations, protect
  the committed record, verify warning timing, and stop further writes.
  Eighty-four cases check deferred quiet time at retry boundaries and during
  in-flight retries. Single transient failures do not retire a slot.
- Rejection of valid versions 1–5 at their former positions: 128 layouts, followed
  by successful v6 saves. An odd half-slot v6 record is ignored; all byte values
  in each of five reserved tail bytes leave valid v6 settings unchanged.
- `tests/test_storage.c`: 70,000 verified saves across sequence age and wrap with
  repeated reboots and a marker stuck at `0xA5`; durable retirement without future
  writes to the bad slot; multiple physical faults; metadata-only commits;
  union of maps in retired records; rejection of CRC-damaged maps; continued saves
  with two healthy slots; stopping with one or fully unwritable EEPROM.
- Additional metadata torn writes: 6,144 cases, all twelve write positions and
  256 torn values for retirement bits in both bitmap bytes, crossing sequence wrap.
  Across both suites there are **32,880 interrupted-write cases**.
- `tests/test_hw.c`: 20 kHz PWM / 10 kHz dithering, all 3,201 Q3 inputs at all
  four starting residuals, exact four-slot means, Q2 rounding ties, 100,000 target
  changes with accumulated error below one count, unchanged-target caching,
  endpoints, integer-IRQ shutoff, indicator polarity, ticks, EEPROM interrupt
  restoration, bounded Timer2 wait, and watchdog recovery. Fractional publication
  retains pending flags; integer/idle publication discards stale flags.
- `tests/test_main.c`: production main/HAL/watchdog integration, normal service,
  missing Timer0 and stopped Timer2 recovery, real GPIO warning phases, twelve-byte
  retirement persistence/clearing, and brightness response after writes stop.
  A production dimmer/HAL test confirms breathing enables ongoing fractional
  updates while all steady levels and both editor previews shut them off.

`node tests/test_hex.js` runs a targeted instruction model of the actual linked
HEX. Settings copies between all five buffers, every payload byte, interrupt
boundaries, and startup button-state clearing pass 14,950 cases. Another 102,432
ISR entries check every internal input and residual, both RAM banks, coincident
Timer0 ticks/saturation, context preservation, exact four-slot means, and integer
IRQ shutoff. The last duty write completes within 34 cycles (17 us); the full ISR
takes at most 53 cycles (26.5 us), including four-cycle hardware entry latency.
Another 103,200 time-driven schedules cover all 200 postscaled Timer2 cycle
alignments, four target pairs, and 129 nearby Timer0 arrival offsets. They check
no discarded slot or torn target and both duty fields written before the next
50 us latch. The worst publication/Timer0 boundary-to-write delay is 91 cycles
(45.5 us), leaving nine cycles (4.5 us) to the latch.

The linked exponential function passes 23,723 cases: all 2,048 phases at eight
spans and per-instruction interrupt injection at representative peak/wrap/reflection
boundaries. Knots are generated independently from the simulator formula. The
isolated calculation takes at most 502 cycles (251 us), excluding interrupts.

The linked production main-loop model runs 18 real-time schedules covering slow
and fastest breathing, multiple starting phases, held/released period buttons,
and asynchronous EEPROM writes modeled at 4 ms per byte. Across 19,800 completed
ticks and 144 EEPROM writes, no Timer0 tick or PWM latch is lost. Worst tick arrival
to watchdog feed is 3,098 cycles (1.549 ms); 137 ticks exceed 2,048 cycles because
of settings/save work, with at most one additional tick queued and bounded catchup.
This is not a guarantee that every tick completes within 1.024 ms. Duty writes in
these main-loop schedules finish within 72 cycles (36 us) of the update boundary.
Normal sustained frame processing keeps up; storage still receives service.

These checks do not fully simulate the PIC or measure physical jitter, CPU time,
EEPROM endurance, physical watchdog timing, PWM
edges, supply behavior, or emissions. Board acceptance remains pending in
`bench-checklist.md`.


## Physical programming

### Exponential/Q2 breathing, 102.4 ms brightness holds, and erased EEPROM

Programmed with `scripts/program.ps1 -ResetSettings` using open-source MiniPro
0.7.4 and TL866CS firmware 03.2.86. Chip ID `0x0023`, revision `0x03`, matched
PIC12F683. Completed at `2026-10-21T09:45:27.8619035Z` (02:45 PDT, October 21);
manifest status is `verified`. The main image uses 2013 program words and 120 RAM bytes.

The exact main HEX SHA-256 was
`383acb8f665379eb332775aa595fe8b1749afc75e107e87e270ea30854d05d03`.
Program memory, all 256 erased EEPROM bytes (`0xFF`), preserved user IDs, and
configuration `0x33C4` passed explicit verification. The reset cleared saved
preferences and the retirement map. The next boot uses defaults: output off,
saved steady brightness 50%, steady mode, 3 s breathing period, and 10–100% limits.

Original flash, EEPROM, configuration, exact programming inputs, logs, and the
verified manifest are retained in
`backups/program-pic12f683-20261021T094505091Z-5e6db3/`.
The firmware was rebuilt and all linked-HEX checks passed before programming;
all four host suites passed the preceding review. Physical waveform, smoothness,
flicker, watchdog timing, and board acceptance remain pending. The indicator-only
image has not been programmed.

### Exponential breathing with 10 kHz two-bit dithering and slower brightness holds

Programmed with `scripts/program.ps1` using open-source MiniPro 0.7.4 and TL866CS
firmware 03.2.86. Chip ID `0x0023`, revision `0x03`, matched PIC12F683. Completed
at `2026-10-21T09:07:48.3243258Z` (02:07 PDT, October 21); manifest status is
`verified`. The main image uses 2009 program words and 120 RAM bytes.

The exact main HEX SHA-256 was
`5482e98a5a6f0f8d5dd6f06152e6f971a9bbc7d0f52d47413f4baa44ed00e7b0`.
Program memory, all 256 EEPROM bytes, four user IDs, and configuration `0x33C4`
passed explicit verification. EEPROM and user IDs were preserved. Original and
restored EEPROM input SHA-256 hashes both equal
`e90922b0f736812ba00daafb0a2fc713a75a4cc37934adad54218c419ea3a9f7`.
Backups, exact inputs, manifest, and operation log are retained in
`backups/program-pic12f683-20261021T090731322Z-9b848a/`.

The firmware was rebuilt and all linked-HEX checks passed before programming;
all four host suites also passed the preceding review. Physical smoothness,
flicker, waveform measurements, watchdog timing, and board acceptance remain
pending. The indicator-only image has not been programmed.

### Natural breathing without dithering

Programmed with `scripts/program.ps1` using open-source MiniPro 0.7.4 and TL866CS
firmware 03.2.86. Chip ID `0x0023`, revision `0x03`, matched PIC12F683. Completed
at `2026-10-21T08:04:41.4556782Z` (01:04 PDT, October 21); manifest status is
`verified`. The main image uses 2015 program words and 120 RAM bytes.

The exact main HEX SHA-256 was
`1ed72811d4e72c730c01ecb852e9dc8ac92f2f4871cf79dff807eab61ff63ee9`.
Program memory, all 256 EEPROM bytes, four user IDs, and configuration `0x33C4`
passed explicit verification. EEPROM and user IDs were preserved. Original and
restored EEPROM input SHA-256 hashes both equal
`77aebb29b5c2803bc42486fe0ce876763194dd9106d5029ba5abc2e23b407397`.
Backups, exact inputs, manifest, and operation log are retained in
`backups/program-pic12f683-20261021T080424886Z-05e145/`.

The firmware was rebuilt and all linked-HEX checks passed before programming;
all four host test suites also passed the preceding review. Physical waveform,
breathing smoothness, watchdog timing, and board acceptance remain pending.
The indicator-only image has not been programmed.

### Natural breathing with fractional dithering

Programmed with `scripts/program.ps1` using open-source MiniPro 0.7.4 and TL866CS
firmware 03.2.86. Chip ID `0x0023`, revision `0x03`, matched PIC12F683. Completed
at `2026-10-21T06:05:35.4020567Z` (23:05 PDT, October 20); manifest status is
`verified`. The main image uses 2029 program words and 122 RAM bytes.

The exact main HEX SHA-256 was
`2bae1f71e3102c450724f8febd98868b8f7e42d48e13bf74f11dd83255522e74`.
Program memory, all 256 EEPROM bytes, four user IDs, and configuration `0x33C4`
passed explicit verification. EEPROM and user IDs were preserved. Original and
restored EEPROM input SHA-256 hashes both equal
`90529b7e2bb9ffff896321bd185e3a0010a848c9352131e37934d9e50700c192`.
Backups, exact inputs, manifest, and operation log are retained in
`backups/program-pic12f683-20261021T060518480Z-c6bc3f/`.

The first attempt stopped at chip identification (`0x0000` instead of `0x0023`),
before any erase or write. The user reseated the chip before this successful run.
That failed attempt is logged in
`backups/program-pic12f683-20261021T060155283Z-40f045/`.
The firmware was rebuilt and its linked-HEX checks passed before programming;
all four host test suites had also passed the preceding review. Physical breathing
smoothness, waveform measurements, watchdog timing, and board acceptance remain
pending. The indicator-only image has not been programmed.

### Fractional sine dithering, boundary fix, and erased EEPROM

Programmed with `scripts/program.ps1 -ResetSettings` using open-source MiniPro
0.7.4 and TL866CS firmware 03.2.86. Chip ID `0x0023`, revision `0x03`, matched.
Completed at `2026-10-20T09:03:08.4239641Z` (02:03 PDT, October 20).

The exact main HEX SHA-256 was
`ed3565fb15b53550d9539cde8ddfcb6427d155e40c41643ea37eeed4acc2f18d`.
Program memory, all 256 erased EEPROM bytes (`0xFF`), preserved user IDs, and
configuration `0x33C4` passed explicit verification. The reset clears both saved
preferences and the retirement map. First boot uses defaults: output off, saved
steady brightness 50%, steady mode, 3 s breathing period, and 10–100% limits.

Original flash, EEPROM, configuration, operation logs, and the verified manifest
are retained in `backups/program-pic12f683-20261020T090251049Z-6d0617/`.
All four host suites, programming-script mock tests, and linked-HEX checks passed
before programming. This verifies programmed contents; physical PWM smoothness,
watchdog timing, and board acceptance remain pending.

### Smooth steady hold ramp below 10%

The preceding normal controller was rebuilt and passed all three host suites and
programming-script mock tests. The linked HEX's settings-copy and reset-startup
instruction checks passed 14,950 cases. MiniPro 0.7.4 with TL866CS firmware
03.2.86 matched chip ID `0x0023`, revision `0x03`, for PIC12F683.
Programming completed at `2026-10-20T04:49:53.1927579Z`; manifest status is `verified`.
The programmed HEX SHA-256 is
`4a331013988d615f3168b4426c9ba00a749f5d1444e9becd447957ba97722d5c`.
Flash, EEPROM, all four user IDs, and configuration `0x33C4` passed explicit
verification. All 256 EEPROM bytes and user IDs were preserved. Original and
restored EEPROM input SHA-256 hashes both equal
`ac9e80f8a8b89a8658a44aafb9d00aa942e0be23b0df5d71580791aa42f61936`.
Backup files, programming inputs, manifest, and the operation log are retained in
`backups/program-pic12f683-20261020T044930492Z-06c247/`.
Physical ramp smoothness, one-count PWM measurements, watchdog timing, and board
acceptance remain pending. The indicator-only image has not been programmed.

### One-count brightness level and minimum hold stop

The preceding normal controller was rebuilt and passed all three host suites and
the programming-script mock tests. A review-only instruction model also checked
the linked HEX's settings-copy routine in 14,875 cases, including interrupts,
and verified C startup clearing of button state. MiniPro 0.7.4 with TL866CS
firmware 03.2.86 matched chip ID `0x0023`, revision `0x03`, for PIC12F683.
Programming completed at `2026-10-20T04:29:35.2928538Z`; manifest status is `verified`.
The programmed HEX SHA-256 is
`bef447a10581f2e9744666034d68de378204d79ec73281ad31524deb9bc8df69`.
Flash, EEPROM, all four user IDs, and configuration `0x33C4` passed explicit
verification. All 256 EEPROM bytes and user IDs were preserved. Original and
restored EEPROM input SHA-256 hashes both equal
`9fd7b1ecee875ed101096437f2c1a532b5624ee6cfac675cbcbddcd7b89788ac`.
Backup files, programming inputs, manifest, and the operation log are retained in
`backups/program-pic12f683-20261020T042912619Z-0ab329/`.
One-count PWM measurements, physical watchdog timing, and board acceptance remain
pending. The indicator-only image has not been programmed.

### Watchdog recovery and maximum-editor blinking

The preceding normal controller was rebuilt and passed all three host test suites
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
