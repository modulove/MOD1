# HAGIWO MOD1 3-Channel LF0

<1-- [![Modulove](https://img.shields.io/badge/Modulove-MOD1-orange)](https://dl.modulove.de/module/mod1/) -->

3-channel Low Frequency Oscillator (LFO) for HAGIWO MOD1 module. 
This version adds few features and fixes.

## Features

### Core Functionality
- **3 Independent LFO Channels**
- **8 Waveform Types** including random modes
- **Frequency Range**: 0.02 Hz to 5 Hz (Normal mode) / 0.0005 Hz to 0.5 Hz (slow mode)
- **Fixed-Point Math** smooth, jitter-free operation
- **EEPROM Settings Storage**

### Enhanced Features
- **Frequency Ratio Mode**: Lock channels to create polyrhythmic modulation patterns
- **Multi-Random Mode**: Each channel uses different random algorithms
- **Offset Control**: Global frequency offset via 4th potentiometer
- **Persistent Settings**: Remembers waveform, speed mode, and ratios

## Installation

### Browser Upload (Recommended - No Software Required!)
The easiest way to install the firmware is using our **browser-based installer**:

1. **Connect** your Arduino Nano to your computer via USB
2. **Visit** [https://dl.modulove.de/module/mod1/](https://dl.modulove.de/module/mod1/)
3. **Click** to upload - that's it!

*Works with Chrome, Edge, and other browsers supporting Web Serial API*


## 🎮 Usage Guide

### Button Controls

| Action | Function | LED Feedback |
|--------|----------|--------------|
| **Short Press** (<150ms) | Cycle through waveforms | Pattern indicates waveform |
| **Medium Hold** (150-800ms) | Toggle Ratio Mode on/off | 3 flashes (on) / 1 flash (off) |
| **Hold + Turn Pots 2&3** | Adjust frequency ratios | Flash on change |
| **Long Press** (>800ms) | Toggle Normal/VLF speed | 2 flashes (VLF) / 1 flash (Normal) |

### Waveform Types

#### Standard Waveforms (0-4)
1. **Triangle** - Smooth bipolar modulation
2. **Square** - Gate/trigger generation
3. **Sine** - Natural oscillation
4. **Ramp Up** - Sawtooth ascending
5. **Ramp Down** - Sawtooth descending

#### Random Modes (5-7) - Multi-Algorithm
When any random mode is selected, each channel automatically uses a different random algorithm:

6. **Random Slope** - Smooth random transitions
7. **Sample & Hold** - Stepped random values
8. **Smooth Random** - Filtered noise

Each channel cycles through:
- Channel 1: Primary algorithm
- Channel 2: Secondary algorithm  
- Channel 3: Tertiary algorithm

Hidden 4th algorithm: **Drunk Walk** (random walk with boundaries)

### Frequency Ratio Mode

When enabled, Channels 2 and 3 are locked to Channel 1 at specific ratios, creating **polyrhythmic modulation patterns**. This is useful for:
- **Synchronized modulation** that repeats in predictable cycles
- **Complex rhythmic patterns** for modulating filters, VCAs, or other parameters
- **Polyrhythmic effects** where LFOs align at different intervals

| Ratio | Pattern Type | Ratio | Pattern Type |
|-------|--------------|-------|--------------|
| 1:1 | Synchronized | 3:2 | Polyrhythm (3 against 2) |
| 2:1 | Double speed | 4:3 | Complex polyrhythm |
| 3:1 | Triple speed | 5:4 | Slower polyrhythm |
| 4:1 | Quadruple speed | 7:5 | Irregular pattern |
| 5:1 | 5x speed | 9:8 | Subtle variation |
| 6:1 | 6x speed | 11:8 | Complex relationship |
| 8:1 | 8x speed | 13:8 | Evolving pattern |
| 9:1 | 9x speed | 15:8 | Near double speed |

**Example Use Cases:**
- **2:1 ratio**: Channel 2 cycles twice for every Channel 1 cycle
- **3:2 ratio**: Creates a "3 against 2" polyrhythm - they align every 6 beats
- **4:3 ratio**: Complex pattern that repeats every 12 cycles

**To Set Ratios:**
1. Hold button (150-800ms) while turning Pots 2 & 3
2. Each pot position selects from 16 ratio presets
3. Release button to save

### Speed Modes

| Mode | Frequency Range | Use Case |
|------|----------------|----------|
| **Normal** | 0.02 - 5 Hz | Standard modulation |
| **VLF** (Very Low) | 0.0005 - 0.5 Hz | Slow evolving patches |


### Multi-Random Architecture
Each random mode employs different algorithms:
- **Slope**: Linear interpolation between random targets
- **Sample & Hold**: Instant transitions at phase wrap
- **Smooth**: Low-pass filtered noise
- **Drunk Walk**: Constrained random walk

## Contributing

Contributions are welcome! Please feel free to submit pull requests or open issues for bugs and feature requests.

## Acknowledgments

- Original hardware design by [HAGIWO](https://www.youtube.com/c/HAGIWO)
- Contributors and testers from the DIY synthesizer community


## Version History

### v4.0 (Current)
- Added frequency ratio mode 
- multi-random mode
- Fixed-point math

### v3.0

- slow speed mode

### v2.0
- More waveform types
- EEPROM settings storage

### v1.0
- Initial 3-channel LFO implementation (HAGIWO)

---

⭐ Star this repository if you find it useful!
