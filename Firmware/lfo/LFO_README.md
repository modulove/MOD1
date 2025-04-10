# HAGIWO MOD1 LFO

_HAGIWO MOD1 LFO Ver1.0_ is a simple low-frequency oscillator (LFO) module designed for Eurorack synthesizer systems. It generates various chaotic control voltages (CV) using different waveforms. The code and design were originally written by HAGIWO and released under [CC0](https://creativecommons.org/publicdomain/zero/1.0/).

## Features

- **Multiple Waveform Generation:**  
  Supports Sine, Triangle, Square, Saw, Reverse Saw, and a Maximum (flat) waveform via precomputed wavetables stored in flash.
  
- **Dual Control for Each Parameter:**  
  - **Frequency:** Combines a potentiometer and external CV input.  
  - **Waveform Selection:** Controlled by a potentiometer with optional CV modulation.  
  - **Amplitude/Level:** Uses another potentiometer along with a dedicated CV input.

- **Frequency Range Toggle:**  
  A momentary button changes the operating frequency range (1 or 10), with the choice saved to EEPROM.

- **PWM Output with LED Visualization:**  
  Uses fast PWM (62.5 kHz) for clean waveform generation and visual feedback through an LED.

## Hardware Requirements

- **Microcontroller:** Arduino (or compatible board) with PWM output and EEPROM support.
- **Potentiometers and CV Inputs:**
  - **POT1:** Controls frequency (Analog pin A0)
  - **POT2:** Selects the waveform (Analog pin A1)
  - **POT3:** Adjusts output level (Analog pin A2)
  - **F1:** Frequency CV input (Analog pin A3)
  - **F2:** Waveform CV input (Analog pin A4)
  - **F3:** Level CV input (Analog pin A5)
- **Outputs:**
  - **F4 (Waveform Output):** Digital pin 11 (PWM)
  - **LED Indicator:** Digital pin 3 (PWM), displays the output intensity (scaled by a brightness constant)
- **Button:**  
  A momentary button connected to Digital pin 4 (with internal pull-up enabled) changes the frequency range.
- **EEPROM:**  
  Used to store the frequency range setting.

## Pin Assignments

| Function                            | Signal       | Pin  |
| ----------------------------------- | ------------ | ---- |
| Frequency Potentiometer (POT1)      | Analog Input | A0   |
| Waveform Select (POT2)              | Analog Input | A1   |
| Level Potentiometer (POT3)          | Analog Input | A2   |
| Frequency CV Input (F1)             | Analog Input | A3   |
| Waveform CV Input (F2)              | Analog Input | A4   |
| Level CV Input (F3)                 | Analog Input | A5   |
| Waveform Output (F4)                | PWM Output   | D11  |
| Button (Frequency Range Toggle)     | Digital Input (with pull-up) | D4   |
| LED Indicator                       | PWM Output   | D3   |

## Software Overview

### Initialization & Setup

- **PWM Configuration:**  
  The firmware configures Timer1 and Timer2 for fast PWM (62.5 kHz) to generate a clean, low-noise output.
  
- **EEPROM Use:**  
  Reads from EEPROM to determine the last stored frequency range on startup.

- **Pin Modes:**  
  The button pin is set with an internal pull-up, while the LED and PWM output pins are configured as outputs.

### Main Loop

1. **Debounce and Frequency Range Toggle:**  
   - Reads the button state (with debounce logic).
   - Toggles between frequency ranges (1 and 10) when pressed.
   - Saves the selection to EEPROM.

2. **Analog Input Processing:**
   - **Frequency:**  
     Reads values from POT1 (A0) and an external CV (F1, A3) and calculates the LFO rate.
   - **Waveform Selection:**  
     Combines values from POT2 (A1) and its corresponding CV input (F2, A4).
   - **Level/Amplitude:**  
     Merges POT3 (A2) readings with the level CV input (F3, A5) to set the output level.

3. **Waveform Generation:**  
   - Based on the waveform selection, a specific wavetable is chosen from the six available (Sine, Triangle, Square, Saw, Reverse Saw, and Maximum).
   - A table index (`waveIndex`) is incremented by the calculated frequency (plus a small offset) and wrapped around to create continuous oscillation.
   - The selected waveform value is scaled by the level and written to the PWM output (D11).

4. **LED Output:**  
   Updates the LED brightness on D3 in proportion to the generated waveform output.


### Uploading the Firmware

1. Open the Arduino IDE.
2. Load the provided `HAGIWO_MOD1_LFO.ino` (or similarly named) sketch.
3. Connect your Arduino board.
4. Compile and upload the sketch to the board.

### Operating the Module

- **Adjusting Frequency:**  
  Turn POT1 or apply a CV to F1 to set the LFO rate. The frequency is additionally scaled by a range multiplier toggled via the button.
  
- **Selecting Waveform:**  
  Rotate POT2 or apply a CV to F2 to select among the available waveforms.
  
- **Setting Level:**  
  Use POT3 or feed a CV into F3 to control the amplitude of the output signal.
  
- **Toggling Frequency Range:**  
  Press the button on D4 to switch between a base frequency range and a higher range. The selection is saved to EEPROM.
  
- **Visual Feedback:**  
  The LED on D3 provides visual feedback that corresponds to the output intensity.

## Customization

- **Brightness Adjustment:**  
  Modify the `Brightness` constant in the code to change the LED’s apparent brightness.
  
- **Wavetable Expansion:**  
  Modify or add new wavetables in the code for additional waveform shapes.
  
- **PWM and Timer Settings:**  
  The fast PWM settings are defined in the `setup()` function. Adjust these if you need different output frequencies or behavior.

## Contributing

Contributions, improvements, and bug fixes are welcome. Please open an [issue](https://github.com/modulove/MOD1/issues) or submit a pull request if you have suggestions.

## License

This project is released under the CC0 license. For full details, see the [LICENSE](LICENSE) file.

## Credits

- **Original Author:** HAGIWO  
- **License:** CC0

## Additional Resources

- [Arduino Documentation](https://www.arduino.cc/reference/en/)
- [EEPROM Library Documentation](https://www.arduino.cc/en/Reference/EEPROM)
- [Eurorack Community Forums and Resources](https://www.modwiggler.com/)
