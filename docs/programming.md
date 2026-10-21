# Program the PIC12F683 with open-source MiniPro

`scripts/program.ps1` programs the chip through the TL866CS/TL866A socket using
the locally built open-source MiniPro executable. Use the appropriate DIP8 or
SOIC8-to-DIP adapter and correct socket placement. Keep the target off the powered
LED board while programming. The script never selects ICSP mode.

The preceding 20 kHz / 2,048-phase sine controller was programmed and verified with
this script at 2026-10-20T04:49:53Z (October 19 local time), preserving EEPROM and
user IDs. It includes watchdog recovery, maximum-editor blinking, the one-count
steady/minimum brightness level, and the separate Down tap required to leave
that minimum for off in the editor. It additionally ramps through
steady 9% down to 1% before 0.25% when held, while keeping the direct tap from
10% to 0.25%. The run is recorded in `validation.md`;
one-count PWM measurements, board acceptance, and physical watchdog tests remain pending.
The current build uses the simulation's symmetric exponential curve, approximated
with a 33-byte half-cycle table and 2,048 phases. Breathing alone uses 10 kHz,
two-bit fractional dithering; the PWM carrier stays 20 kHz. Steady/editor duties
remain exact, and the selected one-count minimum is preserved. Held brightness
steps now repeat every 102.4 ms, while period controls still repeat at 20.48 ms.
This updated build was programmed and verified at 2026-10-21T09:45:27Z with all
256 EEPROM bytes reset to erased `0xFF` and user IDs preserved. Saved preferences
and the bad-slot map were cleared; the next boot uses defaults. The preceding exponential/Q2 build with
40.96 ms brightness steps was programmed and verified at 2026-10-21T09:07:48Z,
preserving EEPROM and user IDs. The preceding natural-envelope build without
dithering was programmed and verified at 2026-10-21T08:04:41Z, preserving EEPROM
and user IDs. Existing v6 settings need no reset for this update.
The current build uses v6 records with persistent bad-slot tracking. Two failed
transactions retire a slot while preserving the newest valid record. The status
LED rapidly blinks (131 ms on/off) until the map saves, or continuously if no
safe destination remains. The preceding natural-envelope build with dithering
was programmed and verified at 2026-10-21T06:05:35Z, preserving all EEPROM bytes
and user IDs; existing
v6 settings remain compatible and need no reset. The preceding sine/dithering
build was programmed and verified at 2026-10-20T09:03:08Z with all EEPROM bytes
reset to `0xFF`, user IDs preserved, and configuration `0x33C4`. Existing v5 and
earlier settings are still ignored. See the latest physical run in `validation.md`.
The indicator-only build has been built and host-tested
but has not been programmed. The software-enabled watchdog retains configuration
`0x33C4`.

## Commands

From the project directory in PowerShell, preview the controller operation:

```powershell
.\scripts\program.ps1 -WhatIf
```

This validates the firmware HEX and checks the local executable/database paths.
It does not invoke MiniPro, contact USB hardware, or create backup files.

Build and program the indicator-only bench image first:

```powershell
.\scripts\build.ps1 -IndicatorOnly
.\scripts\program.ps1 -Firmware .\build\indicator-only.hex
```

After the checks in `bench-checklist.md`, build and program the controller:

```powershell
.\scripts\build.ps1
.\scripts\program.ps1
```

Running without `-WhatIf` backs up the currently inserted chip, then **erases and
replaces its program memory and configuration**. Any failure stops the sequence.
The default image is `build/led-controller.hex`. The device profile defaults to
`PIC12F683`; `-Device 'PIC12F683@SOIC8'` and `-Device 'PIC12F683@DFN8'` select the
corresponding MiniPro aliases. A device name does not change physical adapter
wiring; check the programmer's placement guidance for the adapter being used.

EEPROM is copied back by default. Existing v6 records preserve brightness, power,
breathing mode, period, min/max limits, and retired slots. The main firmware reads
only v6; older records remain backed up/preserved as raw bytes but are not migrated.
The six-second button reset preserves the bad-slot map. The following programmer
reset deliberately erases both preferences and retirement maps; it does not repair
physically damaged cells.
To start with erased EEPROM (first-run defaults: off, 50%,
steady mode, 3-second breathing period, 10–100% breathing limits, status LED off):

```powershell
.\scripts\program.ps1 -ResetSettings
```

The same option can be used with the indicator image. Although that image does
not access EEPROM at runtime, programmer chip erase can affect EEPROM; the script
backs it up and restores it unless reset was requested. Settings from the original
controller's firmware are not converted into the replacement firmware's journal.

## What the script does

1. Validate HEX checksums, flash bounds, reset vector, and configuration `0x33C4`.
   Reject EEPROM payloads and unexpected address regions.
2. Split the combined HEX into a full 4096-byte program binary, padded with erased
   14-bit words `0x3FFF`, and a MiniPro text file containing `word1=0x33c4`.
3. Check programmer identity and chip ID. Read the current program, EEPROM, and
   configuration/user IDs into a fresh, uniquely named backup directory.
   Verify each backup against the chip before any erase. Stop if code/data
   protection would prevent a trustworthy backup.
4. Write program memory, allowing MiniPro's default chip erase and verification.
5. Restore all 256 EEPROM bytes, or write erased `0xFF` bytes with `-ResetSettings`.
   Restore the four original user IDs. Both operations use `-e` to prevent
   another erase.
6. Write only configuration fuses, last, with `-c config --fuses -e`.
7. Explicitly verify program memory, EEPROM, user IDs, and configuration. Record
   success only when all commands succeed.

MiniPro's `-w` defaults to program memory; it does not program the configuration
word at HEX byte address `0x400E` as part of that code-only write. The script uses
separate inputs and commands for these regions. Configuration verification is
explicit even though MiniPro writes normally verify automatically: the checked
source's `write_fuses()` can print a fuse mismatch without a failing return code,
whereas a separate `-m` operation returns failure for a mismatch.

Chip ID and write verification stay enabled. The script does not use force-ID,
skip-verification, ignore-size, unprotect, voltage override, or ICSP options.

## Backups and logs

Each real run saves files under `backups/program-pic12f683-<UTC timestamp>-<id>/`:

- `original.bin`: original 4096-byte program memory.
- `original.bin.eeprom.bin`: original 256-byte EEPROM.
- `original.bin.fuses.conf`: original configuration and user IDs.
- `firmware.bin`, `firmware.eeprom.bin`, `firmware.fuses.conf`: exact MiniPro inputs.
- `program.log`: commands, programmer output, and verification results.
- `manifest.json`: firmware and backup hashes, EEPROM policy, timestamps, and
  operation status (`preparing`, `backup verified`, `programming`, `verified`, or
  `failed`).

Use `-BackupDirectory 'C:\path\to\new-backup-folder'` to choose a location.
Existing directories are rejected to prevent overwriting an earlier backup.
Keep backups, especially the original firmware, after programming. A failure
after erase can leave the chip partly programmed; the script retains the backup
and logs and does not attempt an automatic rollback.

The pre-existing `backups/pic12f683-20261019T032414Z` backup is not modified by this
script. Compiler and MiniPro setup are described in `minipro-build.md` and the
project README.

## Script tests

```powershell
.\tests\test-program.ps1
```

These tests use a fake MiniPro, with no USB operations. They check both firmware
images, 14-bit blank-word padding, command ordering, fresh verified backups,
EEPROM preservation/reset, user ID retention, separate configuration verification,
failure stops, existing-directory protection, and `-WhatIf` isolation.

The implementation was checked against the local upstream MiniPro checkout at
commit `3e2f4911fa86f72a57ae0ace777342e75da5e7cc`:
[manual](https://gitlab.com/DavidGriffith/minipro/-/blob/3e2f4911fa86f72a57ae0ace777342e75da5e7cc/man/minipro.1),
[programming implementation](https://gitlab.com/DavidGriffith/minipro/-/blob/3e2f4911fa86f72a57ae0ace777342e75da5e7cc/src/main.c),
and [PIC12F683 database](https://gitlab.com/DavidGriffith/minipro/-/blob/3e2f4911fa86f72a57ae0ace777342e75da5e7cc/infoic.xml).
