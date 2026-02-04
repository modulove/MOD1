# MOD1 SyncLFO with Self-Modulation

Enhanced clock-syncable LFO firmware for the **HAGIWO MOD1** Eurorack module, featuring self-modulation for complex waveform shaping and slew-limited sample & hold.

![License: CC0](https://img.shields.io/badge/License-CC0-blue.svg)

## Features

- **7 Waveforms**: Sine, Triangle, Square, Saw, Reverse Saw, Decay, Random (S&H)
- **Clock Sync**: Syncs to external clock/trigger input with automatic period detection
- **Internal LFO**: Freerunning mode when no external clock is present
- **Clock Division**: 1x to 6x division for all waveforms including random S&H
- **Self-Modulation**: Use the LFO output to modulate its own phase for FM-like waveshaping
- **Slew-Limited Random**: Adjustable exponential glide for smooth transitions between random values
- **Pickup Mode**: Prevents parameter jumps when switching between control modes

## Hardware

This firmware is designed for the [HAGIWO MOD1](https://note.com/solder_state/n/nb85380be5604) module.

### Pin Assignment

| Pin | Function |
|-----|----------|
| A0 (POT1) | Division rate / Internal rate |
| A1 (POT2) | Waveform select |
| A2 (POT3) | Output level |
| A3 (F1) | External clock input |
| A4 (F2) | Division rate CV |
| A5 (F3) | Waveform select CV |
| D11 (F4) | LFO output |
| D4 | Button (modifier) |
| D3 | LED output |
| D17 | External clock input (active) |

## Controls

### Normal Operation

| Control | Function |
|---------|----------|
| **POT1** | Clock divider (1x–6x) when external clock present, or internal LFO rate (0.25Hz–20Hz) when no clock |
| **POT2** | Waveform select: Sine → Triangle → Square → Saw → Reverse Saw → Decay → Random |
| **POT3** | Output level (0–100%) |

### Button Held (Modifier Mode)

| Control | Function |
|---------|----------|
| **POT3** | **Non-random waveforms**: Self-modulation type and depth |
| **POT3** | **Random mode**: Slew rate (smooth glide between random values) |

#### Self-Modulation (Non-Random Waveforms)

When holding the button and turning POT3, you select both the modulation waveform and its depth:

| POT3 Range | Modulation Type |
|------------|-----------------|
| 0–8% | Off |
| 8–25% | Sine modulation |
| 25–42% | Triangle modulation |
| 42–58% | Square modulation |
| 58–75% | Saw modulation |
| 75–92% | Reverse Saw modulation |
| 92–100% | Random modulation |

Within each range, turning the pot higher increases the modulation depth.

#### Slew Rate (Random Mode)

When in random waveform mode, holding the button and turning POT3 controls the slew/glide between random values:

| POT3 Position | Behavior |
|---------------|----------|
| **Fully CCW** | Instant steps (classic Sample & Hold) |
| **Low** | Quick glide |
| **Mid** | Medium glide |
| **High** | Slow glide |
| **Fully CW** | Very slow drift (several seconds) |

The slew uses an exponential curve - the output moves quickly at first then slows down as it approaches the target value. The pot mapping gives more resolution in the slower range where fine control matters most.

### Pickup Mode

When you release the button after adjusting self-mod or slew, POT3 enters "pickup mode" to prevent the output level from jumping. The pot won't affect the level until you physically move it back to match the current level value (within a small deadzone).

## Clock Behavior

### External Clock

The module automatically syncs to incoming triggers on the clock input. The LFO resets to the start of its cycle on each clock pulse.

### Internal Clock

If no external clock is detected for 8 seconds, the module switches to internal freerunning mode. POT1 then controls the LFO rate.

### Button as Trigger

The button can also be used as a manual trigger input.

### Clock Division

POT1 (combined with CV from A4) sets the clock division. This affects all waveforms including random mode:

| POT1 Range | Division | Random S&H Behavior |
|------------|----------|---------------------|
| 0–10% | 1x | New value every clock |
| 10–30% | 2x | New value every 2 clocks |
| 30–50% | 3x | New value every 3 clocks |
| 50–70% | 4x | New value every 4 clocks |
| 70–90% | 5x | New value every 5 clocks |
| 90–100% | 6x | New value every 6 clocks |

## Version History

- **v2.2** - Added slew rate control for random mode, divider now affects S&H rate
- **v2.1** - Added pickup mode to prevent parameter jumps
- **v2.0** - Initial release with self-modulation and internal clock features

## Credits

- Original SyncLFO code by [HAGIWO](https://note.com/solder_state) (CC0)
- Self-modulation concept from HAGIWO Sync Mod LFO
- Enhanced firmware by [Modulove](https://modulove.io)

## License

This firmware is released under **CC0 (Public Domain)**. Feel free to use, modify, and distribute.
