# Native Windows minipro build

Built on October 18, 2026, from https://gitlab.com/DavidGriffith/minipro.

- Source: `minipro/`, upstream `master`, commit
  `3e2f4911fa86f72a57ae0ace777342e75da5e7cc` (September 21, 2026).
- Reported version: 0.7.4.
- Compiler: existing portable Zig 0.15.2 in `.tools/zig-x86_64-windows-0.15.2/`.
- Target: `x86_64-windows-gnu`, optimized with `-O2`.
- Compression library: zlib 1.3.2, compiled directly into the executable.
  Source: https://zlib.net/zlib-1.3.2.tar.gz.
  Archive SHA-256 verified against https://zlib.net/:
  `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`.
- USB backend: upstream `src/usb_win.c`, linked with Windows SetupAPI and WinUSB.

The upstream checkout is unmodified. `scripts/minipro-windows-compat.h` supplies
the POSIX `strndup` and `stpcpy` functions missing from the compiler's Windows
runtime. `scripts/build-minipro.ps1` generates upstream version metadata in the
output directory, compiles the source list from the upstream Makefile, and copies
the databases and licenses. No MSYS2 or WSL installation is needed.

## Rebuild and run

With the downloaded source and dependencies still present, run from the project
root in PowerShell:

```powershell
.\scripts\build-minipro.ps1
.\scripts\minipro.ps1 -V
.\scripts\minipro.ps1 -q tl866a -d PIC12F683
```

The launcher passes absolute database paths while preserving the current working
directory for input/output files. When running `minipro.exe` directly, first
change to `build/minipro/`, or pass `--infoic` and `--logicic` paths explicitly.
The upstream Windows backend searches for TL866A and TL866CS using the vendor's
device interface. Any necessary hardware driver setup is a separate step.

## Verification

The executable starts successfully and `-V` exits with status 0. It loads all
five programmer databases and the logic database, reporting 14,162 standard
TL866A/CS devices plus 46 custom entries. The `-q tl866a -d PIC12F683` check exits
with status 0 and reports DIP8, 2,048 words of program memory, and 256 bytes of
data memory. The launcher was also checked from the project root with
`-q tl866a -L PIC12F683`, which lists the DIP8, DFN8, and SOIC8 variants.

Initially, `-V` reported `No programmer found.` Windows detected the programmer
as `USB\VID_04D8&PID_E11C` but reported Code 28 because no driver was installed.

## Driver installation and USB verification

On October 18, 2026, the signed 64-bit vendor driver was extracted from the
[archived MiniPro 6.85 package](https://github.com/Kreeblah/XGecu_Software/blob/master/minipro/6/minipro_setup685.rar).
The manufacturer's download page timed out. The archive SHA-256 matched the
mirror's published checksum:
`2ede2c40229aac497f4f08720e41da914df42e2bf79ed2fc1bf8957958b9f918`.

The INF matches `USB\VID_04D8&PID_E11C` and the interface GUID required by
`src/usb_win.c`. Windows validated the embedded driver signature and catalog
signature. Only the driver package was installed; the vendor application was
not launched.

Windows PnPUtil installed `MiniProWDF.inf` as `oem39.inf`, driver version
1.0.0.0, service `MiniProWdf`. Device status is now OK with problem code 0.
Downloads, extracted files, and the installation log are under
`.tools/tl866-driver/`.

Both `scripts/minipro.ps1 -k` and `scripts/minipro.ps1 -V` passed with exit
status 0. The programmer reports:

- Model: TL866CS.
- Firmware: 03.2.63 (`0x23f`).
- USB speed: 12 Mbps.

At this stage minipro warned that it expected firmware 03.2.86 (`0x256`).

## Firmware update

On October 18, 2026, the programmer was updated from 03.2.63 (`0x23f`) to
03.2.86 (`0x256`) using `Update.dat` extracted from the same MiniPro 6.85
archive. The firmware file is 312,348 bytes, with SHA-256
`18c209a364f111bc44a5d0354b685568a9f95153ff90eca766ab17be928375d6`.

The command was `build/minipro/minipro.exe -F
.tools/tl866-driver/extracted/Update.dat`, selecting option 1, device default
(CS). Minipro validated the firmware checksums, switched to bootloader mode,
erased and flashed the firmware, and reset the programmer. It reported
`Reflash... OK` and exited with status 0.

After the update, `-V` reports `Found TL866CS 03.2.86 (0x256)` without the
outdated firmware warning. Detection with `-k` also succeeds. The device code
and serial code are unchanged, and Windows reports device status OK. The
post-update verification output is saved in
`.tools/tl866-driver/firmware-post-update.log`.

USB detection and identity communication are verified; chip programming and
the socket/power hardware self-test have not been performed.
