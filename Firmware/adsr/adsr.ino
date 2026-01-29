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

#include <EEPROM.h>

// ========== Pin Definitions ==========
int triggerPin = 17;  // Gate input pin (D17 = A3 on Nano)
int buttonPin  = 4;   // Button input pin, uses internal pull-up (LOW pressed)
int attackPin  = A0;  // Attack pot
int decayPin   = A1;  // Decay pot
int releasePin = A2;  // Release pot

int attackLedPin = 9;   // Gate out: HIGH only during ATTACK
int decayLedPin  = 10;  // Gate out: HIGH during DECAY/SUSTAIN/RELEASE

// ========== "Trigger -> Gate" Lengthening (minimum gate hold) ==========
// Makes short triggers behave like a usable gate for ADSR.
// Add tiny jitter so it feels less robotic.
const uint16_t MIN_GATE_HOLD_MS = 100; // requested 100ms minimum
const uint8_t  GATE_JITTER_MS   = 10;  // tiny random +/- jitter (set 0 to disable)
unsigned long  gateHoldUntilMs  = 0;

// ========== Sustain Levels (top step changed from 1023 -> 1000) ==========
int sustainLevels[6] = {
  146, 292, 438, 584, 730, 1000
};
int sustainIndex = 0;
int sustainLevel = sustainLevels[sustainIndex];

// ========== ADSR State Machine ==========
enum {IDLE, ATTACK, DECAY, SUSTAIN, RELEASE};
int envelopeState = IDLE;
float envelope = 0.0f;  // 0..1023

// ========== Timing & Rates ==========
unsigned long lastUpdateTime = 0;
float attackRate  = 0.0f;
float decayRate   = 0.0f;
float releaseRate = 0.0f;

// Phase start levels (fix correct distances for legato/retrigger)
float decayStartLevel   = 1023.0f;
float releaseStartLevel = 0.0f;

// Attack time=0 flag (A0 <=10 => instant to max)
bool attackIsZero = false;

// ========== Button Debounce Variables ==========
bool lastButtonState = HIGH;
unsigned long buttonPreviousMillis = 0;
const unsigned long debounceDelay  = 30;

// ========== Time Mapping (more usable ranges) ==========
const int ATTACK_MIN_MS  = 5;
const int ATTACK_MAX_MS  = 5000;
const int DECAY_MIN_MS   = 10;
const int DECAY_MAX_MS   = 5000;
const int RELEASE_MIN_MS = 10;
const int RELEASE_MAX_MS = 5000;

// ========== Function Prototypes ==========
void updateADSR();
void handleButtonInput();

// Cubic mapping for better resolution at short times (cheap vs pow()).
static inline int potToTimeMs(int pot, int minMs, int maxMs) {
  float x = (float)pot / 1023.0f;
  float y = x * x * x; // cubic
  return (int)(minMs + (maxMs - minMs) * y + 0.5f);
}

void setup() {
  // Timer2 ~62.5kHz PWM: Fast PWM 8-bit, no prescaler
  TCCR2A = 0;
  TCCR2B = 0;
  TCCR2A |= (1 << WGM21) | (1 << WGM20);
  TCCR2B |= (1 << CS20);
  TCCR2A |= (1 << COM2A1) | (1 << COM2B1);

  pinMode(triggerPin, INPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(11, OUTPUT);               // OCR2A => Envelope output (PWM)
  pinMode(3, OUTPUT);                // OCR2B => LED indicator (PWM)
  pinMode(attackLedPin, OUTPUT);
  pinMode(decayLedPin, OUTPUT);

  OCR2A = 0;
  OCR2B = 0;

  // Seed RNG for the tiny gate jitter (best-effort: uses timing + gate pin noise)
  randomSeed((unsigned long)micros() ^ (unsigned long)analogRead(triggerPin));

  // Read sustainIndex from EEPROM
  sustainIndex = EEPROM.read(0);
  if (sustainIndex >= 6) sustainIndex = 0;
  sustainLevel = sustainLevels[sustainIndex];
}

void loop() {
  // (1) Button sustain select
  handleButtonInput();

  // (2) Read raw gate (could be a short trigger pulse)
  bool rawGate = digitalRead(triggerPin);

  // Extend short pulses to a minimum gate length (+/- small jitter)
  static bool prevRawGate = false;
  if (rawGate && !prevRawGate) {
    long hold = (long)MIN_GATE_HOLD_MS;
    if (GATE_JITTER_MS > 0) {
      long j = random(-(long)GATE_JITTER_MS, (long)GATE_JITTER_MS + 1); // +/- jitter
      hold += j;
      if (hold < 0) hold = 0;
    }
    gateHoldUntilMs = millis() + (unsigned long)hold;
  }
  prevRawGate = rawGate;

  // Effective gate is rawGate OR still within hold window
  bool gate = rawGate || (millis() < gateHoldUntilMs);

  // Gate rising/falling detection (use EFFECTIVE gate!)
  static bool prevGate = false;
  if (gate && !prevGate) {
    envelopeState = ATTACK;
  }
  if (!gate && prevGate) {
    if (envelopeState != IDLE) {
      releaseStartLevel = envelope;   // capture actual level for correct release time
      envelopeState = RELEASE;
    }
  }
  prevGate = gate;

  // (3) Read pots
  int aVal = analogRead(attackPin);
  int dVal = analogRead(decayPin);
  int rVal = analogRead(releasePin);

  // Attack
  if (aVal <= 10) {
    attackIsZero = true;
    attackRate   = 999999.0f;
  } else {
    attackIsZero = false;
    int attackTimeMs = potToTimeMs(aVal, ATTACK_MIN_MS, ATTACK_MAX_MS);
    attackRate       = 1023.0f / (float)attackTimeMs;
  }

  // Decay (rate based on actual start -> sustain)
  int decayTimeMs = potToTimeMs(dVal, DECAY_MIN_MS, DECAY_MAX_MS);
  float decDist   = decayStartLevel - (float)sustainLevel;

  if (decDist <= 0.0f) {
    decayRate = 999999.0f; // DECAY will settle immediately
  } else {
    decayRate = decDist / (float)decayTimeMs;
    if (decayRate < 0.001f) decayRate = 0.001f;
  }

  // Release (rate based on actual level at gate-off)
  int releaseTimeMs = potToTimeMs(rVal, RELEASE_MIN_MS, RELEASE_MAX_MS);
  float relDist     = releaseStartLevel;

  if (relDist <= 0.0f) {
    releaseRate = 999999.0f;
  } else {
    releaseRate = relDist / (float)releaseTimeMs;
    if (releaseRate < 0.001f) releaseRate = 0.001f;
  }

  // (4) Update ADSR at ~1ms
  unsigned long now = millis();
  if (now - lastUpdateTime >= 1) {
    lastUpdateTime = now;
    updateADSR();

    // Envelope to PWM (0..1023 -> 0..255)
    int duty = (int)(envelope / 4.0f);
    if (duty < 0) duty = 0;
    if (duty > 255) duty = 255;
    OCR2A = duty;
    OCR2B = duty;

    // Gate outs
    digitalWrite(attackLedPin, (envelopeState == ATTACK) ? HIGH : LOW);
    digitalWrite(decayLedPin,
      (envelopeState == DECAY || envelopeState == SUSTAIN || envelopeState == RELEASE) ? HIGH : LOW
    );
  }
}

void updateADSR() {
  switch (envelopeState) {
    case IDLE:
      envelope = 0.0f;
      break;

    case ATTACK:
      if (attackIsZero) {
        envelope = 1023.0f;
        decayStartLevel = envelope;  // capture peak for correct decay distance
        envelopeState = DECAY;
      } else {
        envelope += attackRate;
        if (envelope >= 1023.0f) {
          envelope = 1023.0f;
          decayStartLevel = envelope; // capture peak
          envelopeState = DECAY;
        }
      }
      break;

    case DECAY:
      // If sustain is at/above start level, settle immediately
      if (decayStartLevel <= (float)sustainLevel) {
        envelope = (float)sustainLevel;
        envelopeState = SUSTAIN;
        break;
      }

      envelope -= decayRate;
      if (envelope <= (float)sustainLevel) {
        envelope = (float)sustainLevel;
        envelopeState = SUSTAIN;
      }
      break;

    case SUSTAIN:
      envelope = (float)sustainLevel; // enforce stable sustain
      break;

    case RELEASE:
      if (envelope > 0.0f) envelope -= releaseRate;
      if (envelope <= 0.0f) {
        envelope = 0.0f;
        envelopeState = IDLE;
      }
      break;
  }
}

void handleButtonInput() {
  int reading = digitalRead(buttonPin); // LOW when pressed
  unsigned long now = millis();

  if (now - buttonPreviousMillis > debounceDelay) {
    if (reading == LOW && lastButtonState == HIGH) {
      sustainIndex++;
      if (sustainIndex >= 6) sustainIndex = 0;
      sustainLevel = sustainLevels[sustainIndex];
      EEPROM.write(0, sustainIndex);
      buttonPreviousMillis = now;
    }
  }
  lastButtonState = reading;
}
