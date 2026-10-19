# QY-K4 / OY-K4 LED Dimmer — Reverse-Engineering Notes (Rev A)

Based on multimeter measurements made during the October 15, 2026 troubleshooting session. The IC manufacturer and part number are unknown. This is a partial reconstruction, not an original manufacturer schematic or complete PCB netlist.

## U1 physical pin mapping

View the back of PCB with U1's indicator dot at the **bottom right**. Label left-hand pins **L1, L2, L3, L4**, top to bottom; right-hand pins **R1, R2, R3, R4**, top to bottom. The board component **R1** (47 Ω) is **not** the same as the temporary **R1** pin-position label.

| U1 pin | Photo position | Inferred role | Evidence |
|---|---|---|---|
| 1 | R4 | VDD | Direct R1 (47 Ω) downstream pad and D1 cathode-side connection. 4.68 V at max; 4.55 V in standby. |
| 2 | R3 | Brightness down input | Continuity to switch non-ground side; 4.65 V released, 0 V pressed (measured at switch). |
| 3 | R2 | Brightness up input | Continuity to switch; 4.52 V released, 0 V pressed. |
| 4 | R1 | Power switch input | Continuity to switch; 4.57 V released, 0 V pressed. |
| 5 | L1 | PWM output to MOSFET gate | 487.8 Ω from gate; DC average 4.57 V max, 0.48 V min, ~0 V standby. |
| 6 | L2 | Unknown | ~0 V in on/off states; 1.8–2.3 MΩ to ground unplugged; no continuity to C2. |
| 7 | L3 | Active-low standby LED driver | Continuity to indicator right contact; 0.095 V in standby (LED on), 4.50 V when LED output on. |
| 8 | L4 | Ground | Continuity to USB V− pad. |

## Supply and output

* USB V+ = 5.10 V (during one measurement). USB V+ feeds LED+ directly. Negative LED lead L1− is switched by Q1.
* R1 47 Ω (marked 470) is between USB V+ and U1 VDD. D1 lies between U1 VDD and USB ground, diode forward reading ~0.45 V from ground to VDD; 2.81 V reverse in-circuit diode-test reading. A Zener clamp around 4.7 V is plausible but not proven.
* Q1: top single terminal = LED negative (likely drain); bottom right = ground (likely source); bottom left = gate. U1 pin 5 → approximately 470 Ω (R2, marked 471) → Q1 gate; Q1 gate → approximately 10 kΩ (R4, marked 103) → ground. Resistance measured 487.8 Ω U1 to gate; 9.98 kΩ gate to ground.
* U1 VDD → R7 (506 Ω measured, ~510 Ω nominal) → indicator LED left terminal (anode inferred) → indicator LED right terminal → U1 pin 7. Pin 7 sinks current in standby.
* K1 = brightness up, K3 = power, K5 = brightness down. All active-low. Three input pull-ups appear internal; no low-value external pull-up measured on power button.

## Items requiring verification

1. D1 zener voltage, exact component identity, and waveform / USB voltage under both states.
2. C2 and other capacitors, unused footprints, full board nets.
3. Pin 6 function; no direct external connection demonstrated.
4. MOSFET part number, maximum gate-drive requirement, LED current, and PWM frequency.
5. Whether a proposed replacement MCU is *functionally* compatible in addition to sharing a package/pinout. Verify all peripheral routing and programming requirements before soldering.

Do not power the PCB when using resistance or continuity modes. Measurements at U1 pins while powered should avoid bridging adjacent pins.
