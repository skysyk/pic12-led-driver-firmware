# EEPROM journal

[User guide](../README.md#user-guide) | [Bad-slot tracking](bad-slot-tracking.md) | [Programming and backups](programming.md)

The 256-byte EEPROM holds sixteen aligned 16-byte slots. The current **v6** format
stores settings and a durable bad-slot map together. Versions 1–5, unknown versions,
and records on eight-byte half-slot boundaries are ignored. If no valid v6 record
exists, startup uses defaults without writing EEPROM; the next changed settings
start a v6 journal. Existing v5 preferences are not migrated.

| Offset | Meaning |
| --- | --- |
| 0 | Commit marker `0xA5`, written last |
| 1 | Version `6` only |
| 2–3 | 16-bit sequence, little-endian |
| 4 | Bits 0–6: steady brightness 1–100%, or `0` for one PWM count; bit 7: power on |
| 5 | Minimum 0–99%, or `128` for one PWM count |
| 6 | Bits 0–6: maximum 1–100%; bit 7: breathing enabled |
| 7 | Period in half-second settings, 1–20 |
| 8–9 | Retired-slot bitmap, little-endian; bit N retires slot N |
| 10 | CRC-8/SMBUS over bytes 1–9: polynomial `0x07`, init `0`, no reflection/xorout |
| 11–15 | Reserved |

Boot validates records and ORs all their maps **before** comparing sequences. It
then excludes retired slots and restores the newest remaining settings. A stuck
commit marker cannot make an old retired record win after sequence wrap. Bounds
still require `minimum < maximum` after decoding the one-count sentinel.

A save snapshots the requested settings, invalidates its destination, writes and
verifies the payload, then writes and verifies the marker: twelve asynchronous
byte writes. The latest committed slot is protected. Only the EEPROM unlock
briefly disables interrupts. New requests remain pending during a transaction.

A verification failure retries the slot once after one second. A second failure
retires it and advances to another destination after another one-second delay.
The map forces a save even if preferences have reverted to their saved values.
New deferred adjustments restart the 900 ms quiet period without shortening a
failure backoff; the next attempt waits for the longer remaining delay.

The status LED blinks 131.072 ms on/off (about 3.8 Hz) while a new retirement awaits
a verified save. The warning overrides other LED patterns and clears after that
save unless saving capacity is exhausted. At least two healthy slots are needed
for ongoing atomic saves: the protected record and another destination. With no
destination remaining, writes stop and the warning continues; controls and main
lighting keep working from RAM. A reboot may rediscover retirements that could
not be committed, while restoring the last readable, unretired settings or defaults.

Retirement survives reboot and the six-second preference reset. Two consecutive
transient failures can retire a healthy slot; there is no automatic unretirement.
Only deliberate full EEPROM erasure, such as `scripts/program.ps1 -ResetSettings`,
clears the maps. It does not repair physically damaged cells. See
[bad-slot tracking](bad-slot-tracking.md) for the design and validation.

EEPROM is never initialized by the HEX. Programmer preservation retains all raw
bytes, but unsupported save versions remain unreadable. Abrupt USB loss can
discard changes in the quiet period or an incomplete transaction; the previous
committed settings/map remain available. The indicator-only bench image never
accesses EEPROM.
