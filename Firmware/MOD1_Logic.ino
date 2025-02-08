/*
This code was originally written by HAGIWO and released under CC0

HAGIWO MOD1 LOGIC Ver1.0
https://note.com/solder_state/n/nd51dc7253afc
AND/NAND, OR/NOR, XOR/XNOR, COMPARE, MAX/MIN, FLIP-FLOP

--Pin assign---
POT1  A0  mode select (AND/NAND, OR/NOR, XOR/XNOR, COMPARE, MAX/MIN, FLIP-FLOP)
POT2  A1  input A value
POT3  A2  input B value
F1    A3  CV input A value
F2    A4  CV input B value
F3    D10 PWM output A  
F4    D11 PWM output B
BUTTON    N/A
LED       PWM output A
EEPROM    N/A
*/
#include <Arduino.h>

// Pin definitions
#define LOGIC_SELECT_PIN A0 // Pin for selecting logic type
#define POT_A_PIN A1        // Additional pot for input A
#define POT_B_PIN A2        // Additional pot for input B
#define IN_A_PIN A3         // Input A
#define IN_B_PIN A4         // Input B
#define LED_PIN 3           // LED output (PWM) using Timer2 OCR2B
#define OUT_A_PIN 10        // PWM output for result A side (Timer1 OCR1B)
#define OUT_B_PIN 11        // PWM output for result B side (Timer2 OCR2A)

// Global variables for Flip-Flop mode
bool flipA = false;        // Flip-Flop state for input A
bool flipB = false;        // Flip-Flop state for input B
bool lastA = false;        // Previous digital state for input A
bool lastB = false;        // Previous digital state for input B

void setup() {
  // Set pin modes
  pinMode(OUT_A_PIN, OUTPUT);
  pinMode(OUT_B_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(IN_A_PIN, INPUT);
  pinMode(IN_B_PIN, INPUT);
  pinMode(POT_A_PIN, INPUT);
  pinMode(POT_B_PIN, INPUT);

  // Setup Timer1 for ~62.5kHz PWM on pin D10 (OCR1B)
  // 8-bit Fast PWM, no prescaler
  TCCR1A = 0;
  TCCR1B = 0;
  // WGM10 and WGM12 set 8-bit Fast PWM mode
  // COM1B1=1 => non-inverted PWM on OCR1B (pin D10)
  // COM1A1=1 => non-inverted PWM on OCR1A (pin D9) but we won't use D9 here
  TCCR1A = (1 << WGM10) | (1 << COM1B1) | (1 << COM1A1);
  TCCR1B = (1 << WGM12) | (1 << CS10);
  OCR1B = 0; // Initialize duty cycle to 0

  // Setup Timer2 for ~62.5kHz PWM on pin D11 (OCR2A) and pin D3 (OCR2B)
  // 8-bit Fast PWM, no prescaler
  TCCR2A = 0;
  TCCR2B = 0;
  // WGM20 and WGM21 set Fast PWM mode
  // COM2A1=1 => non-inverted PWM on OCR2A (pin D11)
  // COM2B1=1 => non-inverted PWM on OCR2B (pin D3)
  TCCR2A = (1 << WGM20) | (1 << WGM21) | (1 << COM2A1) | (1 << COM2B1);
  TCCR2B = (1 << CS20);
  OCR2A = 0; // Initialize duty cycle to 0
  OCR2B = 0; // Initialize duty cycle to 0
}

void loop() {
  // Read logic selection from A0
  int selectValue = analogRead(LOGIC_SELECT_PIN); // 0 to 1023

  // Determine logic type based on A0 range
  byte logicMode = 0;
  if (selectValue < 102) {
    logicMode = 0; // AND
  } else if (selectValue < 308) {
    logicMode = 1; // OR
  } else if (selectValue < 514) {
    logicMode = 2; // XOR
  } else if (selectValue < 720) {
    logicMode = 3; // COMPARE
  } else if (selectValue < 926) {
    logicMode = 4; // MAX/MIN
  } else {
    logicMode = 5; // FLIP-FLOP
  }

  // Read inputs for A and B with additional pots
  // Ensure the sum does not exceed 1023
  int rawA1 = analogRead(POT_A_PIN); // 0..1023
  int rawA3 = analogRead(IN_A_PIN);  // 0..1023
  int sumA  = rawA1 + rawA3;
  if (sumA > 1023) sumA = 1023;

  int rawB2 = analogRead(POT_B_PIN); // 0..1023
  int rawB4 = analogRead(IN_B_PIN);  // 0..1023
  int sumB  = rawB2 + rawB4;
  if (sumB > 1023) sumB = 1023;

  int valA = sumA;
  int valB = sumB;

  // Prepare output variables
  int outA = 0; // 0..255 for OCR1B
  int outB = 0; // 0..255 for OCR2A

  // Threshold for digital logic
  bool digitalA = (valA > 512);
  bool digitalB = (valB > 512);

  // Logic processing
  switch (logicMode) {
    case 0: // AND
      // outA => AND
      // outB => NAND
      outA = (digitalA && digitalB) ? 255 : 0;
      outB = (!(digitalA && digitalB)) ? 255 : 0;
      break;

    case 1: // OR
      // outA => OR
      // outB => NOR
      outA = (digitalA || digitalB) ? 255 : 0;
      outB = (!(digitalA || digitalB)) ? 255 : 0;
      break;

    case 2: // XOR
      // outA => XOR
      // outB => XNOR
      outA = ((digitalA ^ digitalB)) ? 255 : 0;
      outB = (! (digitalA ^ digitalB)) ? 255 : 0;
      break;

    case 3: // COMPARE
      // If A>B => D10=HIGH, else if B>A => D11=HIGH
      if (valA > valB) {
        outA = 255;
        outB = 0;
      } else if (valB > valA) {
        outA = 0;
        outB = 255;
      } else {
        // If equal, both 0
        outA = 0;
        outB = 0;
      }
      break;

    case 4: // MAX/MIN
      // D10 => MAX, D11 => MIN, 0~1023 -> 0~255
      if (valA > valB) {
        outA = valA >> 2; // MAX => A
        outB = valB >> 2; // MIN => B
      } else {
        outA = valB >> 2; // MAX => B
        outB = valA >> 2; // MIN => A
      }
      break;

    case 5: // FLIP-FLOP
    {
      // T-type flip-flop style
      bool currentA = digitalA;
      bool currentB = digitalB;

      // Rising edge detection for A
      if (currentA && !lastA) {
        flipA = !flipA;
      }
      // Rising edge detection for B
      if (currentB && !lastB) {
        flipB = !flipB;
      }

      lastA = currentA;
      lastB = currentB;

      // Output states
      outA = flipA ? 255 : 0;
      outB = flipB ? 255 : 0;
    }
    break;
  }

  // Write results to PWM registers
  OCR1B = outA; // D10 output
  OCR2A = outB; // D11 output

  // For LED on D3, same duty cycle as outA
  OCR2B = outA; // D3 output
}
