# PWM and timing

[User guide](../README.md#user-guide) | [Breathing behavior](breathing.md) | [Validation record](validation.md)

User brightness settings are integer percentages plus a special one-count
(0.25%) level. EEPROM stores percentages or the one-count sentinel; only the
current v6 format is read on startup. Runtime percentages convert
directly to the 400-count PWM period (four counts per percent), with a
sixteen-bit Q3 duty interface (0..3200) between the state machine and hardware.
The driver rounds to Q2 (0..1600), packs it into a byte pair, and preserves
two fractional bits in a 10 kHz accumulator. The packed low bit stays zero so
the ISR can position the hardware duty bits with a single shift.
Intermediate levels use CCP1/Timer2 at **20,000 Hz** nominally with the 8 MHz clock
(`PR2=99`, prescaler 1:1). Off and maximum use
static inactive/active GP2 levels
for true 0% and 100%. Timer0 independently free-runs at **1.024 ms/tick**.
Intervals are rounded up: debounce/period repeat 20.48 ms, brightness repeat
102.4 ms, brightness hold 400.384 ms,
power mode hold 1000.448 ms after press debounce, and save 900.096 ms.
Power taps below that hold threshold act after release debounce (about 20 ms).
