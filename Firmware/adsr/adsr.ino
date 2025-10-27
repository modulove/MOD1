/* 
This code was originally written by HAGIWO and released under CC0

HAGIWO MOD1 ADSR Envelope generator Ver1.0
https://note.com/solder_state/n/nf3bca61c8073

ADSR output and 2 gate out.
The first gate out outputs HIGH only during the attack phase. This can be used as a gate-to-trigger conversion.
The second gate out outputs HIGH during the decay to release phases. This can be used as a gate delay or trigger-to-gate conversion.

--Pin assign---
POT1  A0  atk time
POT2  A1  decay time
POT3  A2  rel time
F1    D17  trig in
F2    D9  atk phase gate out
F3    D10  decay ~ rel phase gate out
F4    D11 EG output
BUTTON    sustain level (6 range)
LED       EG output
EEPROM    sustain level
*/

#include <EEPROM.h> // Include EEPROM library for read/write

// ========== Pin Definitions ==========
int triggerPin = 17;  // Gate input pin
int buttonPin  = 4;   // Button input pin, uses internal pull-up (LOW pressed)
int attackPin  = A0;  // Attack pot
int decayPin   = A1;  // Decay pot
int releasePin = A2;  // Release pot

int attackLedPin = 9;   // Indicator for ATTACK state
int decayLedPin  = 10;  // Indicator for DECAY->RELEASE states

// ========== Sustain Levels (6 examples) ==========
int sustainLevels[6] = {
  146, 292, 438, 584, 730, 1023
};
int sustainIndex = 0;
int sustainLevel = sustainLevels[sustainIndex];

// ========== ADSR State Machine ==========
enum {IDLE, ATTACK, DECAY, SUSTAIN, RELEASE};
int envelopeState = IDLE;
float envelope = 0.0;  // 0..1023

// ========== Timing & Rates ==========
unsigned long lastUpdateTime = 0;  // For ADSR step every 1ms
float attackRate  = 0.0;
float decayRate   = 0.0;
float releaseRate = 0.0;

// Attack time=0 flag (A0 <=10 => instant to max)
bool attackIsZero = false;

// ========== Button Debounce Variables ==========
bool lastButtonState = HIGH;             // pull-up => default HIGH
unsigned long buttonPreviousMillis = 0;
const unsigned long debounceDelay  = 30; // ms

// ========== Function Prototypes ==========
void updateADSR(); 
void handleButtonInput();

void setup() {
  // Configure Timer2 for ~62.5kHz PWM
  // Fast PWM 8-bit, no prescaler => 16MHz / 256 = 62.5kHz
  TCCR2A = 0;
  TCCR2B = 0;

  // Set Fast PWM mode
  TCCR2A |= (1 << WGM21) | (1 << WGM20);
  // No prescaler
  TCCR2B |= (1 << CS20);
  // Non-inverting PWM on OC2A / OC2B
  TCCR2A |= (1 << COM2A1) | (1 << COM2B1);

  pinMode(triggerPin, INPUT);       
  pinMode(buttonPin, INPUT_PULLUP);  // Internal pull-up => LOW when pressed
  pinMode(11, OUTPUT);               // OCR2A => Envelope output
  pinMode(3, OUTPUT);                // OCR2B => LED indicator
  pinMode(attackLedPin, OUTPUT);     // D9 => ATTACK indicator
  pinMode(decayLedPin, OUTPUT);      // D10 => DECAY/RELEASE indicator

  // Initialize duty cycle
  OCR2A = 0;
  OCR2B = 0;

  // Read sustainIndex from EEPROM at startup
  sustainIndex = EEPROM.read(0); // Read index from address 0
  if (sustainIndex >= 6) {
    sustainIndex = 0; // Ensure valid range
  }
  sustainLevel = sustainLevels[sustainIndex];
}

void loop() {
  // ----- (1) Handle button to update sustain level & EEPROM -----
  handleButtonInput();

  // ----- (2) Read gate input -----
  bool gate = digitalRead(triggerPin);

  // Gate rising/falling detection
  static bool prevGate = false;
  if (gate && !prevGate) {
    // Gate rising => Attack
    envelopeState = ATTACK;
    // Do not reset envelope => start from current value
  }
  if (!gate && prevGate) {
    // Gate falling => Release
    if (envelopeState != IDLE) {
      envelopeState = RELEASE;
    }
  }
  prevGate = gate;

  // ----- (3) Read pot values for Attack/Decay/Release -----
  int aVal = analogRead(attackPin);
  int dVal = analogRead(decayPin);
  int rVal = analogRead(releasePin);

  // If Attack pot <=10 => Attack=0ms => envelope instantly 1023
  if (aVal <= 10) {
    attackIsZero = true;
    attackRate   = 999999.0; // Arbitrary large
  } else {
    attackIsZero = false;
    int attackTimeMs = map(aVal, 0, 1023, 1, 1000);
    attackRate       = 1023.0 / attackTimeMs;
  }

  // Decay
  int decayTimeMs   = map(dVal, 0, 1023, 1, 1000);
  float decDist     = 1023.0 - sustainLevel;
  decayRate         = decDist / decayTimeMs;

  // Release
  int releaseTimeMs = map(rVal, 0, 1023, 1, 1000);
  float relDist     = (float)sustainLevel;
  releaseRate       = relDist / releaseTimeMs;

  // ----- (4) Update ADSR at ~1ms intervals -----
  unsigned long currentMillis = millis();
  if (currentMillis - lastUpdateTime >= 1) {
    lastUpdateTime = currentMillis;
    updateADSR(); // Update envelope state machine

    // Write envelope to PWM: 0..1023 => 0..255
    int duty = (int)(envelope / 4.0);
    if (duty < 0) duty = 0;
    if (duty > 255) duty = 255;
    OCR2A = duty;
    OCR2B = duty;

    // Update ATTACK indicator (D9) => HIGH only in ATTACK
    if (envelopeState == ATTACK) {
      digitalWrite(attackLedPin, HIGH);
    } else {
      digitalWrite(attackLedPin, LOW);
    }

    // Update DECAY/RELEASE indicator (D10) => HIGH if DECAY,SUSTAIN,RELEASE
    if (envelopeState == DECAY || envelopeState == SUSTAIN || envelopeState == RELEASE) {
      digitalWrite(decayLedPin, HIGH);
    } else {
      digitalWrite(decayLedPin, LOW);
    }
  }
}

/*
   updateADSR()
   This function updates the envelope based on the current state.
   Attack starts from the current envelope value.
*/
void updateADSR() {
  switch (envelopeState) {
    case IDLE:
      envelope = 0.0;
      break;

    case ATTACK:
      if (attackIsZero) {
        envelope = 1023.0;
        envelopeState = DECAY;
      } else {
        envelope += attackRate;
        if (envelope >= 1023.0) {
          envelope = 1023.0;
          envelopeState = DECAY;
        }
      }
      break;

    case DECAY:
      envelope -= decayRate;
      if (envelope <= (float)sustainLevel) {
        envelope = (float)sustainLevel;
        envelopeState = SUSTAIN;
      }
      break;

    case SUSTAIN:
      // Keep sustain level until gate is released
      break;

    case RELEASE:
      if (envelope > 0.0) {
        envelope -= releaseRate;
      }
      if (envelope <= 0.0) {
        envelope = 0.0;
        envelopeState = IDLE;
      }
      break;
  }
}

/*
   handleButtonInput()
   This function handles the button press using a debounce approach.
   The button is internal pull-up => reads HIGH normally, LOW when pressed.
   Also stores sustainIndex to EEPROM on press.
*/
void handleButtonInput() {
  int reading = digitalRead(buttonPin); // LOW when pressed
  unsigned long currentMillis = millis();

  // Debounce check
  if (currentMillis - buttonPreviousMillis > debounceDelay) {
    // Detect transition HIGH->LOW (button press)
    if (reading == LOW && lastButtonState == HIGH) {
      sustainIndex++;
      if (sustainIndex >= 6) sustainIndex = 0;
      sustainLevel = sustainLevels[sustainIndex];
      EEPROM.write(0, sustainIndex); // Store index to EEPROM
      buttonPreviousMillis = currentMillis;
    }
  }
  lastButtonState = reading;
}
