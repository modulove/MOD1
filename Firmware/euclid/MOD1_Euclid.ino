/*
This code was originally written by HAGIWO and released under CC0

HAGIWO MOD1 Euclidean rhythm sequencer Ver1.0
https://note.com/solder_state/n/n42841e48c0ea
8step or 16step sequencer. Adjustable output probability and number of hits.

--Pin assign---
POT1  A0  number of hits
POT2  A1  output probability
POT3  A2  step length 8(knob left side) <-> 16(knob right side)
F1    D17  reset step in
F2    D9  clock in
F3    D10  number of hits CV
F4    D11 Trigger outpu 
BUTTON    reset step
LED       Trigger output
EEPROM    N/A
*/

// Definition of Euclidean rhythms
const int euclidean_rhythm[9][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0}, // Hits: 0
    {1, 0, 0, 0, 0, 0, 0, 0}, // Hits: 1
    {1, 0, 0, 0, 1, 0, 0, 0}, // Hits: 2
    {1, 0, 1, 0, 0, 1, 0, 0}, // Hits: 3
    {1, 0, 1, 0, 1, 0, 1, 0}, // Hits: 4
    {1, 1, 0, 1, 1, 0, 1, 0}, // Hits: 5
    {1, 1, 1, 0, 1, 1, 1, 0}, // Hits: 6
    {1, 1, 1, 1, 1, 1, 1, 0}, // Hits: 7
    {1, 1, 1, 1, 1, 1, 1, 1}, // Hits: 8
};

const int euclidean_rhythm_16[17][16] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // Hits: 0
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // Hits: 1
    {1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}, // Hits: 2
    {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0}, // Hits: 3
    {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}, // Hits: 4
    {1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0}, // Hits: 5
    {1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0}, // Hits: 6
    {1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0}, // Hits: 7
    {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0}, // Hits: 8
    {1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0}, // Hits: 9
    {1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0}, // Hits: 10
    {1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0}, // Hits: 11
    {1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0}, // Hits: 12
    {1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0}, // Hits: 13
    {1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0}, // Hits: 14
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0}, // Hits: 15
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, // Hits: 16
};

// Pin definitions and timing settings
const int resetInputPin = 17; // Pin for step reset
const int resetButtonPin = 4; // Pin for reset button
const int outputPin = 11; // Trigger output pin
const int hitCVPin = A5; // Trigger CV input pin
const int extraLedPin = 3; // Extra LED connected to D3 pin
const int triggerInputPin = 9; // Trigger input pin
const int potPin = A0; // Potentiometer pin
const int stepModePin = A2; // Analog pin for selecting step mode
const unsigned long triggerTime = 10; // Trigger duration in milliseconds

// State management variables
unsigned long triggerStartMillis = 0; // Trigger start time
int currentStep = 0; // Current step index
bool isTriggering = false; // Triggering state flag
bool lastTriggerInputState = false; // Previous trigger input state
static bool use16Step = false; // Initial value for step mode
static bool lastUse16Step = false; // Last step mode state
unsigned long modeChangeLedStartMillis = 0; // LED start time for mode change
bool isModeChangeLedOn = false; // LED state for mode change
bool disableOutputLed = false; // Flag to disable output LED during mode change

// Debounce variables
unsigned long lastResetDebounceTime = 0; // Last debounce time for reset button
const unsigned long resetDebounceDelay = 50; // Debounce delay in milliseconds

void setup() {
  lastUse16Step = use16Step; // Initialize last mode state
  pinMode(resetInputPin, INPUT); // Set step reset pin as input
  pinMode(resetButtonPin, INPUT_PULLUP); // Set reset button pin as input with pull-up
  pinMode(outputPin, OUTPUT);
  pinMode(extraLedPin, OUTPUT);
  pinMode(triggerInputPin, INPUT);
  pinMode(potPin, INPUT);
  digitalWrite(outputPin, LOW);
}

void loop() {
  // Read A2 value to determine step mode
  int stepModeValue = analogRead(stepModePin);
  use16Step = stepModeValue > 511;

  // Turn off LED after the appropriate time
  if (isModeChangeLedOn) {
    if (use16Step && millis() - modeChangeLedStartMillis >= 1000) {
      digitalWrite(extraLedPin, LOW);
      isModeChangeLedOn = false;
      disableOutputLed = false; // Re-enable output LED
    } else if (!use16Step && millis() - modeChangeLedStartMillis >= 500) {
      digitalWrite(extraLedPin, LOW);
      isModeChangeLedOn = false;
      disableOutputLed = false; // Re-enable output LED
    }
  }

  // Read potentiometer value to select Hits
  int potValue = min(analogRead(potPin) + analogRead(hitCVPin), 1023);

  // Explicitly set range for Hits selection based on mode
  int selectedHits;
  if (use16Step) {
    selectedHits = map(potValue, 0, 1023, 0, 16); // For 16 steps
  } else {
    selectedHits = map(potValue, 0, 1023, 0, 8); // For 8 steps
  }

  // Set probability for triggering output
  int probabilityValue = analogRead(A1); // Read probability value from A1 pin
  int triggerProbability = map(probabilityValue, 0, 1023, 0, 100); // Map to 0-100%

  // Check trigger input
  bool triggerInput = digitalRead(triggerInputPin) == HIGH;
  if (triggerInput && !lastTriggerInputState) {
    // Advance step on LOW to HIGH transition of trigger input

    // Output based on current step
    if (!disableOutputLed) { // Skip output LED if mode change LED is active
      if (use16Step) {
        if (euclidean_rhythm_16[selectedHits][currentStep] == 1 && random(100) < triggerProbability) {
          digitalWrite(outputPin, HIGH); // Start trigger
          digitalWrite(extraLedPin, HIGH); // Turn on extra LED
          triggerStartMillis = millis();
          isTriggering = true;
        }
        currentStep = (currentStep + 1) % 16;
      } else {
        if (euclidean_rhythm[selectedHits][currentStep] == 1 && random(100) < triggerProbability) {
          digitalWrite(outputPin, HIGH); // Start trigger
          digitalWrite(extraLedPin, HIGH); // Turn on extra LED
          triggerStartMillis = millis();
          isTriggering = true;
        }
        currentStep = (currentStep + 1) % 8;
      }
    }
  }

  // Check reset input or reset button with debounce
  bool resetInput = digitalRead(resetInputPin) == HIGH;
  bool resetButton = digitalRead(resetButtonPin) == LOW; // Button is active LOW
  static bool lastResetInputState = false; // Previous reset input state

  if ((resetInput || resetButton) && !lastResetInputState) {
    if (millis() - lastResetDebounceTime > resetDebounceDelay) {
      lastResetDebounceTime = millis();
      currentStep = 0; // Reset to first step
    }
  }
  lastResetInputState = resetInput || resetButton;

  // Update trigger input state
  lastTriggerInputState = triggerInput;

  // Handle triggering process
  if (isTriggering) {
    if (millis() - triggerStartMillis >= triggerTime) {
      digitalWrite(outputPin, LOW); // End trigger
      if (!disableOutputLed) {
        digitalWrite(extraLedPin, LOW); // Turn off extra LED
      }
      isTriggering = false;
    }
  }
}
