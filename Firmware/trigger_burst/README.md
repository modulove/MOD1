# HAGIWO MOD1 Trigger Burst Generator

## Overview
A clock-syncable trigger burst generator for Eurorack modular synthesizers. Creates musical ratcheting effects by generating rapid bursts of triggers at subdivisions of an input clock. Perfect for creating drum rolls, rhythmic variations, and dynamic percussion patterns.

## Features
- **6 Burst Modes**: 1, 3, 4, 6, 8, or 16 triggers per burst
- **Fast Subdivisions**: 2x, 3x, 4x, 6x, 8x, or 16x the clock speed
- **Clock Flexibility**: Internal clock (80-280 BPM) or external clock sync
- **CV Modulation**: External control voltage input for dynamic burst count modulation
- **Multiple Trigger Sources**: Manual button, trigger input, or CV trigger
- **Visual Feedback**: LED indicator shows trigger output state

## Musical Applications
- **Drum Rolls**: Create snare rolls and hi-hat patterns
- **Ratcheting**: Add rhythmic subdivisions to sequences
- **Generative Rhythms**: Use CV to dynamically vary burst patterns
- **Clock Multiplication**: Generate faster clock divisions from a master clock
- **Percussion Effects**: Create flams, drags, and other ornamentations

## Hardware Requirements
- Arduino (Mega recommended for pin 17, or modify for Uno/Nano)
- Eurorack power supply (+5V from USB or +12V with regulation)
- Basic components (resistors, capacitors, jacks, potentiometers)

## Pin Assignments

### Inputs
| Pin | Function | Description |
|-----|----------|-------------|
| A0 | POT1 | Burst number selection (base value) |
| A1 | POT2 | Burst frequency/speed division |
| A2 | POT3 | Clock source & internal BPM |
| A5 | CV IN | External CV for burst number modulation (0-5V) |
| D4 | BUTTON | Manual trigger button (internal pull-up) |
| D9 | TRIG IN | External trigger input |
| D17 | CLK IN | External clock input (F1) |

### Outputs
| Pin | Function | Description |
|-----|----------|-------------|
| D11 | TRIG OUT | Burst trigger output (F4) |
| D3 | LED | Visual trigger indicator |

## Control Details

### POT1 (Burst Number) - A0
Selects the number of triggers in each burst:
- Position 1: 1 trigger (normal pass-through)
- Position 2: 3 triggers (triplet)
- Position 3: 4 triggers 
- Position 4: 6 triggers
- Position 5: 8 triggers
- Position 6: 16 triggers (roll)

### POT2 (Burst Speed) - A1
Sets the speed of triggers within the burst:
- Position 1: ÷2 (2x speed - eighth notes from quarter notes)
- Position 2: ÷3 (3x speed - triplets)
- Position 3: ÷4 (4x speed - sixteenth notes)
- Position 4: ÷6 (6x speed - sextuplets)
- Position 5: ÷8 (8x speed - thirty-second notes)
- Position 6: ÷16 (16x speed - very fast rolls)

### POT3 (Clock Source) - A2
- **Fully CCW (< 5%)**: External clock mode - syncs to CLK IN
- **Above 9 o'clock**: Internal clock mode - adjustable 80-280 BPM

### CV Input - A5
Accepts 0-5V control voltage to modulate the burst number selection. The CV is summed with POT1, allowing dynamic control over burst patterns. Perfect for:
- LFO modulation for evolving patterns
- Envelope control for velocity-sensitive bursts
- Sequencer CV for programmed burst variations

## Usage Examples

### Basic Drum Roll
1. Set POT1 to position 5 (8 triggers)
2. Set POT2 to position 4 (6x speed)
3. Trigger with a gate signal for instant snare rolls

### Clock-Synced Ratcheting
1. Turn POT3 fully counter-clockwise
2. Patch master clock to CLK IN (D17)
3. Set POT2 to desired subdivision
4. Patch trigger sequence to TRIG IN
5. Bursts will be perfectly synced to master tempo

### Dynamic CV Modulation
1. Set POT1 to middle position (base burst count)
2. Patch LFO or envelope to CV IN (A5)
3. CV will shift between different burst counts
4. Creates evolving, organic rhythmic patterns

## Circuit Notes

- CV input (A5) expects 0-5V range


### Power Requirements
- 5V @ ~50mA (typical)
- Can be powered via Arduino USB or Eurorack +5V rail
- If using +12V rail, uses nano regulator

## Modifications & Customization

### Adjusting Gate Length
Modify the trigger on-time (default 5ms):
```cpp
unsigned long triggerOnTime = 10;  // 10ms gates
```

### Higher Timing Precision
Uncomment the USE_MICROS define for microsecond timing:
```cpp
#define USE_MICROS  // Enables microsecond precision
```

### Debug Output
Uncomment the Serial debug section in loop() to monitor values:
```cpp
Serial.begin(115200);
// Then uncomment the debug print section
```

## Version History

### v1.1 (Current)
- Fixed unsigned integer underflow in timing calculations
- Improved external clock detection and averaging
- Added proper CV input handling on A5
- Enhanced state machine reliability
- Added microsecond timing option

### v1.0 (Original HAGIWO)
- Initial release by HAGIWO
- Basic burst generation functionality

## Credits & License

Original design and code by **HAGIWO**  
- Website: https://note.com/solder_state/n/n59f617fd0657
- Released under CC0 (Public Domain)

Fixes and optimizations applied while maintaining HAGIWO's original features.

## Contributing

Feel free to submit issues or pull requests

---

*For more HAGIWO projects and Eurorack DIY designs, visit the [HAGIWO note page](https://note.com/solder_state)*
