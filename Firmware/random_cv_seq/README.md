# MOD1 RandomCV (Quantized) — Firmware

Random CV sequencer firmware for the **HAGIWO MOD1** (Arduino Nano): a clocked **Periodic Random CV Sequencer** with **quantized pitch** and a **serial tuning helper menu** to calibrate per‑note PWM values (C2…C5).

---

## Features
- Quantized CV on **F3 / D10** (62.5 kHz PWM), 3 octaves C2…C5, per-note table stored in **EEPROM**.
- **Fine tuning by dithering**: the table is in 1/16 PWM steps (~1.5 cents) instead of whole steps (23.5 cents); the output filter averages the dither away.
- **Even note choice**: every note of the scale between C2 and the top note is equally likely, root and top note included.
- **Probability** (POT3): a fresh dice roll on every step (fully CCW never, fully CW always); steps that don't play **hold the last note**.
- Boot-hold **Serial Tuning Menu** (115200) — adjust PWM per note; export tables.
- Musical scales: Chromatic, Major, **Aeolian (Nat. Minor)**, Dorian, Phrygian, Lydian, **Minor Pentatonic**, **Whole‑tone**, **Octatonic (H–W)**.
- **Gate** on **F4 / D11**: opens **7 ms after the CV step**, so the note starts on a settled pitch; lasts the step (10–120 ms); optional **gate tie** across rests.
- Debounced/latched button: **short** = re-randomize, **long** = cycle scales.

## I/O (MOD1)
| Function | Jack / Pin | Notes |
|---|---|---|
| Clock In | F1 / D17 (A3) | rising-edge step |
| Re-rand In | F2 / D9 | rising-edge re-roll |
| **Quantized CV Out** | **F3 / D10 (OC1B)** | 0–5 V (after RC/op-amp) |
| **Gate Out** | **F4 / D11** | 7 ms after the CV step, lasts the step (10–120 ms) |
| Step Length | POT1 / A0 | 3,4,5,8,16,32 |
| Range / Level | POT2 / A1 | sets **top note** (C2…C5) |
| Trigger Probability | POT3 / A2 | 0–100% chance, rolled fresh on every step |
| Button | D4 | short: re‑rand, long: scale |

> MOD1 limits: 8‑bit PWM CV, ~25 mV ripple, 1 kΩ out. The 1 kΩ + 1 µF output filter (~1 ms) makes the CV take ~7 ms to settle on a big jump, which is why the gate waits for it.

## Build & Flash
1. Arduino IDE → **Arduino Nano (ATmega328P, 16 MHz)** (use *Old Bootloader* if needed).
2. Open `.ino`, compile & upload.
3. Serial monitor at **115200** for the tuning menu.

## Use
**Normal:** Patch clock → F1; D10 → VCO 1V/Oct; D11 → ENV gate. Set POT2 to choose top note.  
**Buttons:** short = re‑randomize, long = change scale (name prints on Serial).

**Boot‑hold Tuning Menu:** hold the button while powering on.
- Commands: `h` help, `n/p` next/prev, `i <0..36>`, `+/-`, `S <1..64>` (step in 1/16 PWM, 1 ≈ 1.5 cents), `v <0..255>` (decimals ok, e.g. `v 123.25`), `a` autoscan, `w` write, `r` reload, `f` factory, `t` target info, **`P`** print current table, **`F`** print factory table, `x` exit.
- Tip: set VCO so **0 V ≈ C2 (65.41 Hz)** for good headroom.
- A calibration saved by an older version (whole PWM steps) is converted on load.
- LGT8F328P boards erase the EEPROM on every upload: export your table with `P` and paste it over `factoryTuningValues` to keep it.

## Config (in code)
- `CV_SETTLE_US` (**7000**), `GATE_MIN_MS` (**10**), `GATE_MAX_MS` (**120**), `GATE_TIE` (**false**).
- Default scale: change `currentScale` (see scale list).

## Troubleshooting
- Envelope not triggering → increase `GATE_MIN_MS` to 15–20 ms.
- Notes still slide in at the start → increase `CV_SETTLE_US` (10000 = HAGIWO's original 10 ms); lower it for tighter timing on small intervals.
- High notes won’t reach pitch → raise VCO coarse tune and re‑calibrate (0 V ≈ C2).
