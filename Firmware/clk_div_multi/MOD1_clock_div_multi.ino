/*
This code was originally written by HAGIWO and released under CC0

HAGIWO MOD1 Clock div/multi Ver1.0
1 clock input , 3 trig output.
Clock Divider & Multiplier. Toggle div/multi function with a push button.
Div/multi rate : 1,2,3,4,8,16

https://note.com/solder_state/n/n991254a0f20a

--Pin assign---
POT1  A0  out1 rate
POT2  A1  out2 rate
POT3  A2  out3 rate
F1    A3  clock in
F2    A4  trig out1
F3    A5  trig out2
F4    D11 trig out3
BUTTON    sw divider or multiple
LED       clockin (It blinks when switching between div/multi.)
EEPROM    div/multi mode memory

The source code and circuit diagram are license-free. Creative Commons license CC0.
*/

#include <EEPROM.h>                          // Include EEPROM library for storing mode state

// Define mode constants for clarity
#define MODE_DIVIDER 1                       // Mode 1: Clock Divider (divider mode)
#define MODE_MULTIPLIER 2                    // Mode 2: Clock Multiplier (multiplier mode)

int mode;                                    // Global variable for current mode (divider or multiplier)

// Pin assignments
const int buttonPin = 4;                     // D4: Mode-switch push-button (internal pullup enabled)
const int clockInputPin = 17;                // D17: Clock source input
const int ledPin = 3;                        // D3: LED indicator (normally mirrors clock input)

// Output channel pins (synchronized in divider mode)
const int outPins[3] = {9, 10, 11};           

// Analog input pins for rate selection (channel 1 = A0, channel 2 = A1, channel 3 = A2)
const int analogPins[3] = {A0, A1, A2};

// Global variables for button debounce
int lastButtonState = HIGH;                  // Last button state (HIGH when not pressed)
unsigned long buttonPreviousMillis = 0;      // Timestamp of last valid button event
const unsigned long debounceDelay = 200;     // Debounce delay set to 200 msec

// Global variable for synchronized divider mode (mode1)
unsigned long dividerCounter = 0;            // Global divider counter for mode1 synchronization

// Global variables for clock timing in multiplier mode (mode2)
unsigned long lastClockTime = 0;             // Timestamp of previous rising edge for multiplier mode
unsigned long clockPeriod = 0;               // Measured period between rising edges in multiplier mode

// Fixed output pulse width for all channels (10 msec)
const unsigned long pulseWidth = 10;         // Duration of each output pulse in milliseconds

// Structure to hold parameters for each output channel (used for multiplier mode scheduling)
struct OutputChannel {
  uint8_t outPin;                // Digital output pin for pulse generation
  int rate;                      // Factor (1, 2, 3, 4, 8, 16) read from corresponding analog input
  bool pulseActive;              // Flag indicating if a pulse is currently active
  unsigned long pulseEndTime;    // Timestamp when the current pulse should end
  int pulsesGenerated;           // Count of pulses already generated in current multiplier cycle
  unsigned long nextPulseTime;   // Next scheduled time for a pulse in multiplier mode
  unsigned long multiplierInterval; // Time interval between pulses (clockPeriod / rate)
};

OutputChannel channels[3];       // Array for three output channels

// Global variables for LED blinking override during mode switching
bool ledBlinkingActive = false;    // Flag to indicate that LED blinking override is active
int ledBlinkTotalCycles = 0;         // Total number of full blink cycles to perform
int ledBlinkCycleCount = 0;          // Counter for completed blink cycles
unsigned long ledBlinkInterval = 0;  // Interval for one half-cycle (on or off duration)
unsigned long nextLedBlinkTime = 0;  // Timestamp for next LED state toggle
bool ledBlinkState = false;          // Current state of LED during blinking (true = HIGH)

// Function prototypes
void handleButtonInput();
void switchMode();
void startLedBlinking(int cycles, unsigned long fullCyclePeriod);
void updateLedBlinking();
void triggerPulse(OutputChannel &channel);
int getFactorFromAnalog(int analogPin);

void setup() {
  Serial.begin(9600);                        // Initialize serial communication for debugging
  
  pinMode(buttonPin, INPUT_PULLUP);          // Configure D4 as input with internal pullup for button
  pinMode(ledPin, OUTPUT);                     // Configure D3 as output for LED indicator
  pinMode(clockInputPin, INPUT);               // Configure D17 as input for clock source
  
  // Initialize each output channel and set its corresponding pin as OUTPUT
  for (int i = 0; i < 3; i++) {
    pinMode(outPins[i], OUTPUT);             // Set output channel pin as OUTPUT
    digitalWrite(outPins[i], LOW);           // Initialize output to LOW
    channels[i].outPin = outPins[i];         // Assign digital output pin for the channel
    channels[i].pulseActive = false;         // No active pulse initially
    channels[i].pulsesGenerated = 0;         // Reset multiplier pulse count
    channels[i].nextPulseTime = 0;           // Clear next scheduled pulse time
    channels[i].multiplierInterval = 0;      // Initialize multiplier interval to 0
  }
  
  // Read stored mode from EEPROM (address 0) and validate it
  mode = EEPROM.read(0);                     // Read mode from EEPROM
  if (mode != MODE_DIVIDER && mode != MODE_MULTIPLIER) {
    mode = MODE_DIVIDER;                     // Default to divider mode if invalid value
  }
  
  // Reset global counters for both modes
  dividerCounter = 0;                        // Reset divider counter for mode1
  lastClockTime = 0;                         // Reset last clock time for mode2
  clockPeriod = 0;                           // Clear measured clock period for mode2
}

void loop() {
  unsigned long currentMillis = millis();    // Get current time in milliseconds
  
  handleButtonInput();                       // Check and process button press with debounce
  
  // If LED blinking override is active, update its state; otherwise mirror clock input to LED
  if (ledBlinkingActive) {
    updateLedBlinking();                     // Update LED blinking sequence during mode change
  } else {
    int clockInput = digitalRead(clockInputPin); // Read clock input from D17
    digitalWrite(ledPin, clockInput);          // Mirror D17 clock input to LED on D3
  }
  
  // Edge detection for clock input (rising edge detection)
  static int prevClockState = LOW;           // Static variable to hold previous clock state
  int clockInput = digitalRead(clockInputPin); // Read current clock input state
  if (prevClockState == LOW && clockInput == HIGH) { // Check for rising edge (transition LOW->HIGH)
    if (mode == MODE_MULTIPLIER) {           // Process multiplier mode (mode2)
      if (lastClockTime == 0) {              // For the first rising edge (no valid period yet)
        lastClockTime = currentMillis;       // Save current time as last clock time
        clockPeriod = 0;                     // No period measured yet
        for (int i = 0; i < 3; i++) {
          channels[i].rate = getFactorFromAnalog(analogPins[i]); // Read multiplier factor from analog input
          channels[i].pulsesGenerated = channels[i].rate;        // Mark cycle complete (only one pulse output)
          triggerPulse(channels[i]);         // Trigger immediate pulse on the channel
        }
      } else {                               // For subsequent rising edges in multiplier mode
        clockPeriod = currentMillis - lastClockTime; // Calculate clock period from previous rising edge
        lastClockTime = currentMillis;       // Update last clock time to current time
        for (int i = 0; i < 3; i++) {
          channels[i].rate = getFactorFromAnalog(analogPins[i]); // Get multiplier factor from analog input
          channels[i].pulsesGenerated = 0;   // Reset multiplier pulse count for new cycle
          if (clockPeriod > 0 && channels[i].rate > 0) {
            channels[i].multiplierInterval = clockPeriod / channels[i].rate; // Compute interval between pulses
          } else {
            channels[i].multiplierInterval = 0; // Fallback to 0 if period is invalid
          }
          channels[i].nextPulseTime = currentMillis; // Schedule first pulse immediately in new cycle
        }
      }
    } else if (mode == MODE_DIVIDER) {       // Process divider mode (mode1) with synchronized outputs
      dividerCounter++;                      // Increment global divider counter on each rising edge
      for (int i = 0; i < 3; i++) {
        int factor = getFactorFromAnalog(analogPins[i]); // Read division factor from analog input
        // Trigger pulse if global divider counter is a multiple of the division factor
        if (dividerCounter % factor == 0) {
          triggerPulse(channels[i]);       // Generate a 10-msec pulse on the channel
        }
      }
    }
  }
  prevClockState = clockInput;               // Update previous clock state for next iteration
  
  // In multiplier mode, check for scheduled pulses and trigger them as needed
  if (mode == MODE_MULTIPLIER) {
    for (int i = 0; i < 3; i++) {
      if (channels[i].pulsesGenerated < channels[i].rate) { // Only trigger if not all pulses are generated
        if (currentMillis >= channels[i].nextPulseTime) {   // Check if it's time for the next pulse
          triggerPulse(channels[i]);          // Trigger the pulse on the channel
          channels[i].pulsesGenerated++;        // Increment the count of pulses generated in this cycle
          if (channels[i].pulsesGenerated < channels[i].rate) {
            channels[i].nextPulseTime += channels[i].multiplierInterval; // Schedule next pulse
          }
        }
      }
    }
  }
  
  // For all channels, check if an active pulse has reached its 10-msec duration and end it if so
  for (int i = 0; i < 3; i++) {
    if (channels[i].pulseActive && currentMillis >= channels[i].pulseEndTime) {
      digitalWrite(channels[i].outPin, LOW); // End the pulse by setting the output LOW
      channels[i].pulseActive = false;       // Mark that no pulse is active on the channel
    }
  }
}

// Function to handle button input with debounce and switch mode upon a valid button press
void handleButtonInput() {
  int reading = digitalRead(buttonPin);      // Read current state of button (LOW when pressed)
  unsigned long currentMillis = millis();      // Get current time in milliseconds
  
  if (currentMillis - buttonPreviousMillis > debounceDelay) { // Check if debounce period has passed
    if (reading == LOW && lastButtonState == HIGH) { // Detect HIGH-to-LOW transition (button press)
      switchMode();                          // Switch mode upon valid button press
      buttonPreviousMillis = currentMillis;  // Update timestamp of last valid button event
    }
  }
  lastButtonState = reading;                 // Save current button state for next iteration
}

// Function to switch between divider (mode1) and multiplier (mode2) modes,
// initiate the appropriate LED blink sequence, and store the new mode in EEPROM.
void switchMode() {
  int oldMode = mode;                        // Store current mode for reference
  if (mode == MODE_DIVIDER) {                // If currently in divider mode (mode1)
    mode = MODE_MULTIPLIER;                  // Switch to multiplier mode (mode2)
    startLedBlinking(5, 100);                // Blink LED 5 times with 100 msec cycle (mode1->mode2)
    lastClockTime = 0;                       // Reset multiplier timing variables
    clockPeriod = 0;                         
    for (int i = 0; i < 3; i++) {            // Reset multiplier channel counters
      channels[i].pulsesGenerated = 0;
      channels[i].nextPulseTime = 0;
    }
  } else {                                   // If currently in multiplier mode (mode2)
    mode = MODE_DIVIDER;                     // Switch to divider mode (mode1)
    startLedBlinking(3, 300);                // Blink LED 3 times with 300 msec cycle (mode2->mode1)
    dividerCounter = 0;                      // Reset global divider counter for synchronized outputs
  }
  EEPROM.update(0, mode);                    // Store the new mode in EEPROM at address 0
  
  // Print mode switch info to Serial for debugging
  Serial.print("Switched mode from ");
  Serial.print(oldMode == MODE_DIVIDER ? "Divider" : "Multiplier");
  Serial.print(" to ");
  Serial.println(mode == MODE_DIVIDER ? "Divider" : "Multiplier");
}

// Function to start the LED blinking override sequence during mode switching.
// 'cycles' is the number of full on-off blink cycles, and 'fullCyclePeriod' is the period (in ms) of one full cycle.
void startLedBlinking(int cycles, unsigned long fullCyclePeriod) {
  ledBlinkingActive = true;                  // Activate LED blinking override
  ledBlinkTotalCycles = cycles;              // Set the total number of blink cycles to perform
  ledBlinkCycleCount = 0;                    // Reset the blink cycle counter
  ledBlinkInterval = fullCyclePeriod / 2;      // Calculate half-cycle duration (on or off period) for 50% duty cycle
  nextLedBlinkTime = millis() + ledBlinkInterval; // Schedule next LED toggle
  ledBlinkState = true;                      // Start with LED turned ON
  digitalWrite(ledPin, HIGH);                // Immediately set LED HIGH to begin blinking
}

// Function to update the LED blinking sequence using non-blocking timing (millis()).
// While blinking is active, this overrides the normal clock-mirroring on the LED.
void updateLedBlinking() {
  unsigned long currentMillis = millis();    // Get current time in milliseconds
  if (currentMillis >= nextLedBlinkTime) {     // Check if it's time to toggle the LED state
    ledBlinkState = !ledBlinkState;            // Toggle the LED state (ON to OFF or vice versa)
    digitalWrite(ledPin, ledBlinkState ? HIGH : LOW); // Update the LED output accordingly
    nextLedBlinkTime = currentMillis + ledBlinkInterval; // Schedule the next toggle
    if (!ledBlinkState) {                      // When LED turns OFF, count one complete blink cycle
      ledBlinkCycleCount++;                    // Increment blink cycle counter
      if (ledBlinkCycleCount >= ledBlinkTotalCycles) { // If desired blink cycles are complete,
        ledBlinkingActive = false;             // Disable LED blinking override
        digitalWrite(ledPin, LOW);             // Ensure LED is turned OFF
      }
    }
  }
}

// Function to trigger an output pulse on the given channel with a fixed pulse width (10 msec).
void triggerPulse(OutputChannel &channel) {
  digitalWrite(channel.outPin, HIGH);          // Set the output pin HIGH to start the pulse
  channel.pulseEndTime = millis() + pulseWidth;  // Set the timestamp when the pulse should end
  channel.pulseActive = true;                    // Mark that the pulse is active on this channel
}

// Function to map an analog input value to one of the rate factors (1,2,3,4,8,16)
// based on defined thresholds.
int getFactorFromAnalog(int analogPin) {
  int selectValue = analogRead(analogPin);       // Read the analog value from the specified pin
  if (selectValue < 102) {
    return 1;                                  // Return factor 1 for values less than 102
  } else if (selectValue < 308) {
    return 2;                                  // Return factor 2 for values between 102 and 307
  } else if (selectValue < 514) {
    return 3;                                  // Return factor 3 for values between 308 and 513
  } else if (selectValue < 720) {
    return 4;                                  // Return factor 4 for values between 514 and 719
  } else if (selectValue < 926) {
    return 8;                                  // Return factor 8 for values between 720 and 925
  } else {
    return 16;                                 // Return factor 16 for values 926 and above
  }
}
