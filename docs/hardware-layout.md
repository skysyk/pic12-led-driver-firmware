# Hardware layout and configuration

[Build and flash runbook](../README.md#build-and-flash-runbook) | [Reconstruction notes](../QY-K4_reconstruction_notes.md) | [Board acceptance checklist](bench-checklist.md)

The pin map and polarities below are the assumptions compiled into
[board.h](../firmware/board.h). Physical board acceptance remains pending.

| Physical pin | Signal | Connection |
| --- | --- | --- |
| 1 | VDD | R4, measured controller supply |
| 2 | GP5 | R3, down, weak pull-up |
| 3 | GP4 | R2, up, weak pull-up |
| 4 | GP3 | R1, power, **external 10 kΩ pull-up to VDD** |
| 5 | GP2/CCP1 | L1, MOSFET gate circuit |
| 6 | GP1 | L2, input, no pull-up, never driven |
| 7 | GP0 | L3, edit/reset status LED (original standby indicator) |
| 8 | VSS | L4, ground |

`firmware/board.h` uses active-low buttons, active-high MOSFET drive, and an
active-low status LED, matching the reconstruction notes. Wire the
indicator as VDD → series resistor (~510 Ω) → LED anode → cathode → GP0 (pin 7).
GP0 is high when the indicator is off and low when it is on. Verify the wiring
against the board.
Change the polarity constants and rebuild if needed. An inverted MOSFET circuit
also needs an appropriate external bias to hold the load off while reset leaves
the PIC pins as inputs; firmware cannot control the reset interval.

Configuration selects internal oscillator GPIO mode, MCLR off, power-up timer
and brown-out reset on, software-controlled watchdog, code/data protection off,
and IESO/FCMEN off. Configuration stays `0x33C4`; startup enables the watchdog via
`WDTCON=0x15`, about 1.06 seconds nominally from LFINTOSC. Only completed Timer0
tick processing clears it; the interrupt handler, idle loop, and PWM wait do not.
Loss of ticks or a stalled foreground operation therefore permits a watchdog reset.
The Timer2 boundary wait has a 256-poll budget, about 0.64 ms at 8 MHz plus ISR
latency. Expiration drives the main output inactive, disconnects CCP1, and waits
without clearing the watchdog. Recovery reloads the last valid EEPROM settings.
Analog inputs, ADC, comparator, voltage reference, and interrupt-on-change are
disabled. GPIO latches are loaded with inactive levels before output enable.

References: [PIC12F683 datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/41211D.pdf).
