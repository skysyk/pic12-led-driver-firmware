# Persistent bad-slot tracking

Implemented in `firmware/storage.c`. The main controller now uses **version 6**;
versions 1–5 are ignored. Nothing in this change has been programmed to the chip.

## Durable retirement

The 256-byte EEPROM has sixteen aligned 16-byte slots. A failed byte verification
retries the same transaction once after one second. A second failure retires the
entire slot and moves to the next unretired, unprotected destination after another
one-second delay. The latest successfully committed slot remains protected.

Every new record includes a 16-bit retirement bitmap under its CRC. Learning a
retirement forces a save even if the requested preferences revert to their saved
values. Maps rotate with settings, avoiding a fixed metadata wear/failure point.

Boot uses two passes:

1. Validate records and OR their retirement maps, independently of sequence order.
   Maps in retired records may still contribute bits. Older maps cannot clear bits.
2. Exclude all retired slots, then select the newest remaining settings record.

This prevents a read-only committed record from becoming "newest" after roughly
32,768 later saves. Finding the retirement map must precede ordering settings;
choosing a map only from the apparent newest record would recreate that problem.

No new sequence is committed without the current map. Power loss before a commit
keeps the previous settings/map. Reboot may retry an undurable retirement, but no
successful new save has advanced without recording it. Once the marker commits,
the new settings/map are accepted together, including loss before RAM bookkeeping.

The two bitmap bytes in the record buffer also hold the live map after boot. Only
a failed/aborted transaction can learn another retirement. Thus a successful
transaction's bitmap cannot change during its payload writes. A dirty bit in the
existing failure byte requests metadata-only saves. Record byte 0 is reused as
the free-running warning clock outside boot and is excluded from the CRC.

## v6 format

| Offset | Meaning |
| --- | --- |
| 0 | Commit marker `0xA5`, written last |
| 1 | Version `6` |
| 2–3 | Sequence, little-endian, modulo 65536 |
| 4 | Steady brightness 0–100 in bits 0–6; power in bit 7; zero means one PWM count |
| 5 | Breathing minimum 0–99%, or `128` for one PWM count |
| 6 | Maximum 1–100% in bits 0–6; breathing mode in bit 7 |
| 7 | Period, 1–20 half-second settings |
| 8–9 | Retirement bitmap, little-endian; bit N retires slot N |
| 10 | CRC-8/SMBUS over bytes 1–9 |
| 11–15 | Reserved |

A normal transaction uses twelve asynchronous, verified byte writes: invalidate
the marker, ten payload bytes, and the final marker. No new save starts before
both the remaining failure backoff and any new 879-tick quiet period expire.
Power/preferences remain prompt in healthy operation.

## Warning, exhaustion, and reset

The status LED blinks 131.072 ms on/off while a new retirement awaits verified
persistence. It clears after that save succeeds, unless capacity is exhausted.
The fault blink overrides mode/editor/reset indication, including their lit phases.

Ongoing atomic saves require two usable slots: the protected current record and
another destination. Two healthy slots continue rotating. If no other destination
remains, writes stop and the rapid warning continues. Controls and lighting still
operate from RAM. Reboot loads the last readable, unretired record or defaults;
retirements that could not be committed may be discovered again.

Retirement is permanent until deliberate full EEPROM erasure. Two consecutive
transient failures can therefore retire a healthy slot. There is no automatic
unretirement or repeated exhausted-sweep retry. The six-second button reset resets
preferences and preserves retirement; it cannot repair EEPROM or clear the map.
An explicit programmer `-ResetSettings` operation erases all settings/maps and
allows rediscovery of faults, without repairing physical damage.

## Upgrade and resource usage

Existing v5 and older settings/maps are not migrated. If no valid v6 record
exists, the controller starts off at the existing defaults without writing a
record; the next changed settings create a v6 journal. Programmer preservation
still backs up/restores all EEPROM bytes, but that does not make old formats
readable. Downgrading can likewise restore old records or defaults.

| Complete controller | Flash words | RAM bytes | Free flash / RAM |
| --- | --- | --- | --- |
| Before compatibility removal | 2043 / 2048 | 125 / 128 | 5 / 3 |
| Previous v5-only build | 1897 / 2048 | 123 / 128 | 151 / 5 |
| v6 tracking before dithering | 1994 / 2048 | 125 / 128 | 54 / 3 |
| v6 + sine dithering | 2044 / 2048 | 122 / 128 | 4 / 6 |
| v6 + natural breathing/dithering | 2029 / 2048 | 122 / 128 | 19 / 6 |
| v6 + natural breathing, no dithering | 2015 / 2048 | 120 / 128 | 33 / 8 |
| v6 + exponential / 10 kHz Q2 dithering, 40.96 ms holds | 2009 / 2048 | 120 / 128 | 39 / 8 |
| Current v6 + exponential / Q2, 102.4 ms holds | 2013 / 2048 | 120 / 128 | 35 / 8 |

Measured with XC8 v4.00, `-Os`, PIC10-12Fxxx_DFP 1.10.193, PIC12F683. Configuration
is `0x33C4`; the HEX contains no EEPROM payload. Prototype sources/images/tests
and the preceding main build are retained in `build/before-bad-slot-integration/`.
There is one production storage implementation; its tests are part of
`scripts/test.ps1` and `scripts/build.ps1` builds it by default.

## Validation

All four host suites pass against production sources:

- Existing PWM, exponential envelope, 102.4 ms brightness holds, controls, editor, reset, save timing, and watchdog tests.
- 70,000 verified saves with a marker stuck at `0xA5`, crossing both the half-range
  age boundary and full sequence wrap with repeated reboot. The retired marker
  receives exactly two attempts and is then skipped.
- Failures at all twelve transaction stages for all sixteen newest-slot positions;
  marker and bitmap-byte failures in different slots; newer pending requests.
- Metadata-only saves after preferences revert, boot union of maps from retired
  records, and rejection of maps with corrupted CRCs.
- One hundred saves/reboots with only two healthy slots. One remaining slot and
  fully unwritable EEPROM stop without damaging the protected copy.
- 32,880 interrupted-write cases across settings, resets, lowest levels, and new
  retirement maps. The metadata matrix covers every possible torn byte at each
  write for both low- and high-byte bitmap bits, including sequence rollover.
- All seventeen newest/empty layouts through destination exhaustion, exact
  warning blink timing, and no further writes after stopping.
- Production main-loop GPIO, warning clearing after twelve-byte persistence,
  continued watchdog service, and button response after EEPROM exhaustion.
- Six-second preference reset preserving the durable retirement map.
- Linked HEX state-copy/startup checks in a review-only instruction model.

The tests model EEPROM writes and PIC registers; they do not measure actual
EEPROM endurance, waveform timing, or physical watchdog timing. Bench acceptance
remains pending.
