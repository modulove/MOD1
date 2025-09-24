# MOD1 Clock Divider/Multiplier v3.0

An enhanced Eurorack clock divider/multiplier module for the HAGIWO MOD1 platform (Arduino Nano based). 
Processes an external clock signal to generate three independent trigger outputs with selectable division or multiplication ratios.

## Features

- **Dual Mode Operation**: Switch between clock division and multiplication
- **3 Independent Outputs**: Each with its own rate control via potentiometer
- **Multiple Division Tables**: Choose from Powers of 2, Prime numbers, or Integers
- **EEPROM Memory**: Saves settings between power cycles
- **Visual Feedback**: LED indicates clock input and mode changes

## Operating Modes

### Divider Mode
Divides the incoming clock by the selected ratio. All outputs maintain phase relationship with the input clock.

### Multiplier Mode  
Multiplies the incoming clock by the selected ratio, generating multiple evenly-spaced triggers per input clock cycle.

## Division Tables

The module offers three selectable division/multiplication tables:

| Table | Values | Use Case |
|-------|--------|----------|
| **Powers of 2** | 1, 2, 4, 8, 16, 32, 64 | Traditional musical divisions |
| **Prime Numbers** | 1, 2, 3, 5, 7, 11, 13 | Polyrhythmic patterns |
| **Integers** | 1, 2, 3, 4, 5, 6, 7 | Linear divisions |

## Usage

### Basic Operation
1. **Connect clock source** to the clock input
2. **Adjust potentiometers** to set division/multiplication ratios for each output
3. **Connect outputs** to your destination modules

### Mode Switching

#### Short Press (<1 second)
Toggles between **Divider** and **Multiplier** modes
- **Divider Mode**: LED blinks 3 times slowly
- **Multiplier Mode**: LED blinks 5 times quickly

#### Long Press (>1 second)  
Cycles through division tables
- **Powers of 2**: LED blinks 1 time
- **Prime Numbers**: LED blinks 2 times
- **Integers**: LED blinks 3 times

### Potentiometer Positions

Each pot is divided into 7 zones corresponding to the 7 values in the active table:

| Pot Position | Powers | Primes | Integers |
|-------------|--------|--------|----------|
| Full CCW | ÷/×1 | ÷/×1 | ÷/×1 |
| 1/6 | ÷/×2 | ÷/×2 | ÷/×2 |
| 2/6 | ÷/×4 | ÷/×3 | ÷/×3 |
| 3/6 | ÷/×8 | ÷/×5 | ÷/×4 |
| 4/6 | ÷/×16 | ÷/×7 | ÷/×5 |
| 5/6 | ÷/×32 | ÷/×11 | ÷/×6 |
| Full CW | ÷/×64 | ÷/×13 | ÷/×7 |

## LED Behavior

- **Normal Operation**: Mirrors the incoming clock signal
- **Mode Change**: Displays blink pattern to indicate new mode
- **Startup**: Shows current mode and table settings

## Example Patch Ideas

- **Euclidean Rhythms**: Use prime number divisions for complex polyrhythms
- **Ratcheting**: Use multiplier mode to create burst triggers
- **Clock Distribution**: Drive multiple sequencers at related but different speeds
- **Swing Patterns**: Mix divided and multiplied outputs for syncopation


## Version History

- **v3.0** - Added selectable division tables and improved synchronization
- **v2.0** - Fixed timing drift and sync issues
- **v1.0** - Original HAGIWO release

## License

This project is released under Creative Commons CC0 (Public Domain).

## Credits

Original design by [HAGIWO](https://note.com/solder_state/)  
Enhanced version developed by the community

## Contributing

Feel free to submit issues, fork, and create pull requests for any improvements.

---

*For detailed circuit schematics and build instructions, visit the [HAGIWO website](https://note.com/solder_state/n/n991254a0f20a)*
