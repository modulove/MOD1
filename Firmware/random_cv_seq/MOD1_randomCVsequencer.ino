/*
HAGIWO MOD1 RandomCV Ver1.0  +  Quantized Notes + BOOT-HOLD SERIAL TUNING MENU
- RandomCV outputs QUANTIZED notes on D10 (uses calibrated table from EEPROM).
- Hold BUTTON (D4) at power-on for Serial Tuning Menu (115200 baud).
- Short press D4 = re-randomize. Long press (>800 ms) = cycle scale.

Pins (MOD1):
  POT1  A0  Step length 3,4,5,8,16,32
  POT2  A1  Output level → sets top note index (0..36)
  POT3  A2  Trigger probability
  F1    D17 Clock in
  F2    D9  Random value update (TRIG IN)
  F3    D10 CV output (PWM @ 62.5 kHz)  <-- quantized + tuning menu out
  F4    D11 Trigger output (improved pulse/gate)
  BUTTON D4 Random value update / scale select (long press)
  LED   D3 CV level (PWM)

EEPROM: stores 37-note calibration table (C2..C5). Used by tuning menu AND runtime quantizer.
Serial: 115200 baud (for menu / status)
*/

#include <Arduino.h>
#include <EEPROM.h>
#include <avr/pgmspace.h>
#include <math.h>

// -------------------- Pins & constants --------------------
const int triggerPin    = 17;  // stepping trigger (F1)
const int reRandomPin   = 9;   // re-randomize trigger input (F2)
const int cvOutPin      = 10;  // CV out (F3 / OCR1B)
const int trigOutPin    = 11;  // trigger/gate out (F4)
const int ledPin        = 3;   // LED (D3 / OC2B)
const int buttonPin     = 4;   // momentary (INPUT_PULLUP)
const int potLevelPin   = A1;  // Output level (sets top note index)
const int stepSelectPin = A0;  // Steps selector
const int trigProbPin   = A2;  // Trigger probability

const unsigned long BOOT_HOLD_MS = 600;

// RandomCV data
int stepModes[] = { 3, 4, 5, 8, 16, 32 };
int currentStep = 0;
int currentTotalSteps = 8;
int cvValues[32];    // 0..255
int trigValues[32];  // 0..255
int indexSel = 0;

unsigned long currentMillis  = 0;

// -------------------- Improved gate/trigger (D11) --------------------
bool gateHigh = false;
uint32_t gateOffAtMs = 0;
uint32_t lastStepEdgeMs = 0;

// Adjust these to taste:
const uint16_t GATE_BASE_MS = 10;     // default pulse width (works well with MOD1 RC)
const uint16_t GATE_MAX_MS  = 120;    // clamp for very slow clocks
const bool     GATE_TIE     = true;   // if true: extend gate across rests (longer when no new note)

// -------------------- Button handling (debounced events) --------------------
enum BtnEvent : uint8_t { BTN_NONE=0, BTN_SHORT=1, BTN_LONG=2 };

BtnEvent pollButton(uint8_t pin, unsigned long nowMs) {
  static bool rawPrev = HIGH;          // raw level (INPUT_PULLUP)
  static bool stable  = HIGH;          // debounced level
  static unsigned long lastEdgeMs = 0; // last raw change time
  static unsigned long pressStartMs = 0;
  static bool longSent = false;

  const unsigned long DEBOUNCE_MS = 30;
  const unsigned long LONG_MS     = 800;

  bool raw = digitalRead(pin);
  if (raw != rawPrev) { rawPrev = raw; lastEdgeMs = nowMs; }

  // Accept new stable level after debounce
  if ((nowMs - lastEdgeMs) >= DEBOUNCE_MS && raw != stable) {
    stable = raw;
    if (stable == LOW) {           // pressed
      pressStartMs = nowMs;
      longSent = false;
    } else {                       // released
      if (!longSent) return BTN_SHORT;
    }
  }

  // While held, fire long once
  if (stable == LOW && !longSent && (nowMs - pressStartMs) >= LONG_MS) {
    longSent = true;
    return BTN_LONG;
  }
  return BTN_NONE;
}

// -------------------- Calibration (tuning/quantizer) --------------------
// 37 semitones (C..B over 3 octaves + high C), 8-bit PWM (0..255)
const uint8_t factoryTuningValues[] PROGMEM = {
  1, 5, 9, 13, 17, 21, 25, 29, 33, 38, 42, 46,   // Octave 1 (C..B)
  50, 54, 58, 63, 67, 71, 75, 79, 83, 87, 92, 96,   // Octave 2 (C..B)
  100, 104, 108, 113, 117, 121, 125, 129, 133, 138, 142, 146,   // Octave 3 (C..B)
  150  // High C
};

const int totalNotes = sizeof(factoryTuningValues) / sizeof(factoryTuningValues[0]);
uint8_t calTable[37];  // working calibration table used by quantizer & menu

struct CalBlob { char sig[4]; uint8_t table[37]; };
const int EEPROM_ADDR = 0;

const char * const noteNames[12] PROGMEM = {
  "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

// -------------------- Scales / quantizer --------------------
// Scales commonly used in electronic music (house/techno/ambient), plus a few staples.
enum Scale : uint8_t {
  CHROMATIC=0,
  MAJOR,
  AEOLIAN,       // Natural minor
  DORIAN,
  PHRYGIAN,
  LYDIAN,
  PENT_MINOR,    // Minor pentatonic
  WHOLE_TONE,
  OCTATONIC_HW,  // Half–whole diminished
  NUM_SCALES
};
volatile uint8_t currentScale = AEOLIAN;

// Step sets (semitone offsets inside an octave)
static const uint8_t steps_chromatic[12]   = {0,1,2,3,4,5,6,7,8,9,10,11};
static const uint8_t steps_major[7]        = {0,2,4,5,7,9,11};
static const uint8_t steps_aeolian[7]      = {0,2,3,5,7,8,10}; // natural minor
static const uint8_t steps_dorian[7]       = {0,2,3,5,7,9,10};
static const uint8_t steps_phrygian[7]     = {0,1,3,5,7,8,10};
static const uint8_t steps_lydian[7]       = {0,2,4,6,7,9,11};
static const uint8_t steps_pent_minor[5]   = {0,3,5,7,10};
static const uint8_t steps_whole_tone[6]   = {0,2,4,6,8,10};
static const uint8_t steps_octatonic_hw[8] = {0,1,3,4,6,7,9,10};

const char* scaleName(uint8_t s){
  switch(s){
    case CHROMATIC:   return "Chromatic";
    case MAJOR:       return "Major";
    case AEOLIAN:     return "Aeolian (Nat.Minor)";
    case DORIAN:      return "Dorian";
    case PHRYGIAN:    return "Phrygian";
    case LYDIAN:      return "Lydian";
    case PENT_MINOR:  return "Pent.Minor";
    case WHOLE_TONE:  return "Whole-Tone";
    case OCTATONIC_HW:return "Octatonic H-W";
    default:          return "?";
  }
}

// Map 0..36 note index to nearest allowed degree in selected scale; return PWM via calTable
uint8_t quantizeIndexToPWM(int noteIndex) {
  if (noteIndex < 0) noteIndex = 0;
  if (noteIndex > totalNotes-1) noteIndex = totalNotes-1;
  int octave  = noteIndex / 12;
  int semit   = noteIndex % 12;

  const uint8_t* steps = steps_chromatic;
  uint8_t n = 12;
  switch (currentScale) {
    case CHROMATIC:    steps = steps_chromatic;    n = 12; break;
    case MAJOR:        steps = steps_major;        n = 7;  break;
    case AEOLIAN:      steps = steps_aeolian;      n = 7;  break;
    case DORIAN:       steps = steps_dorian;       n = 7;  break;
    case PHRYGIAN:     steps = steps_phrygian;     n = 7;  break;
    case LYDIAN:       steps = steps_lydian;       n = 7;  break;
    case PENT_MINOR:   steps = steps_pent_minor;   n = 5;  break;
    case WHOLE_TONE:   steps = steps_whole_tone;   n = 6;  break;
    case OCTATONIC_HW: steps = steps_octatonic_hw; n = 8;  break;
  }
  uint8_t chosen = steps[n-1];
  for (uint8_t i=0;i<n;i++){ if (semit <= steps[i]) { chosen = steps[i]; break; } }

  int idx = octave*12 + chosen;
  if (idx > totalNotes-1) idx = totalNotes-1;
  return calTable[idx];
}

// -------------------- Fast PWM setup --------------------
static inline void setupFastPWM() {
  // Timer2 for LED (D3 = OC2B): 8-bit Fast PWM, no prescaler
  TCCR2A = _BV(COM2B1) | _BV(WGM21) | _BV(WGM20);
  TCCR2B = _BV(CS20);

  // Timer1 for CV out (D10 = OC1B): 8-bit Fast PWM, no prescaler
  pinMode(cvOutPin, OUTPUT);
  TCCR1A = _BV(COM1B1) | _BV(WGM10);
  TCCR1B = _BV(WGM12) | _BV(CS10);
  OCR1B = 0;
}

static inline void analogWriteCV(uint8_t v)  { OCR1B = v; } // D10
static inline void analogWriteLED(uint8_t v) { OCR2B = v; } // D3

// -------------------- EEPROM helpers --------------------
void loadCalibration() {
  CalBlob blob; EEPROM.get(EEPROM_ADDR, blob);
  if (blob.sig[0]=='C' && blob.sig[1]=='V' && blob.sig[2]=='T' && blob.sig[3]=='1') {
    memcpy(calTable, blob.table, totalNotes);
    Serial.println(F("Calibration loaded from EEPROM."));
  } else {
    for (int i=0;i<totalNotes;i++) calTable[i] = pgm_read_byte_near(factoryTuningValues + i);
    Serial.println(F("No calibration found; using factory table."));
  }
}
void saveCalibration() {
  CalBlob blob; blob.sig[0]='C'; blob.sig[1]='V'; blob.sig[2]='T'; blob.sig[3]='1';
  memcpy(blob.table, calTable, totalNotes);
  EEPROM.put(EEPROM_ADDR, blob);
  Serial.println(F("Calibration saved to EEPROM."));
}
void resetToFactory() {
  for (int i=0;i<totalNotes;i++) calTable[i] = pgm_read_byte_near(factoryTuningValues + i);
  Serial.println(F("Reverted to factory table (not saved). Use 'w' to persist."));
}

// -------------------- Targets / Note helpers (menu display only) --------------------
double targetFreqForIndex(int idx) {
  int octave = 2 + (idx / 12);     // 0→C2 .. 36→C5
  int semit  = idx % 12;
  int absSemisFromC0 = octave * 12 + semit;
  return 16.3516 * pow(2.0, absSemisFromC0 / 12.0); // C0 = 16.3516 Hz
}
void noteNameForIndex(int idx, char *buf, size_t bufsz) {
  int octave = 2 + (idx / 12);
  int semit  = idx % 12;
  char nn[4];
  strncpy_P(nn, (PGM_P)pgm_read_word(&(noteNames[semit])), sizeof(nn));
  nn[sizeof(nn)-1]='\0';
  snprintf(buf, bufsz, "%s%d", nn, octave);
}

// -------------------- Serial tuning menu (manual) --------------------
// Helpers to avoid <algorithm> min/max templates
static inline uint8_t clamp_add_u8(uint8_t a, uint8_t b) {
  uint16_t s = (uint16_t)a + (uint16_t)b;
  return (s > 255) ? 255 : (uint8_t)s;
}
static inline uint8_t clamp_sub_u8(uint8_t a, uint8_t b) {
  return (a > b) ? (uint8_t)(a - b) : (uint8_t)0;
}

// ---- NEW: pretty printer for copy-paste arrays ----
void printArrayAsC(const __FlashStringHelper* title, const char* name, const uint8_t* arr, int len, bool arrInProgmem) {
  Serial.println();
  Serial.print(F("// ")); Serial.println(title);
  Serial.print(F("const uint8_t ")); Serial.print(name); Serial.println(F("[] PROGMEM = {"));

  for (int i = 0; i < len; i++) {
    if (i % 12 == 0) {
      Serial.print(F("  "));
    }
    uint8_t v = arrInProgmem ? pgm_read_byte_near(arr + i) : arr[i];
    Serial.print(v);
    if (i != len - 1) Serial.print(F(", "));

    if ((i % 12) == 11 || i == len - 1) {
      // octave comment at 12,24,36 indexes (C..B), then extra C
      if (i == 11)  Serial.print(F("  // Octave 1 (C..B)"));
      if (i == 23)  Serial.print(F("  // Octave 2 (C..B)"));
      if (i == 35)  Serial.print(F("  // Octave 3 (C..B)"));
      if (i == 36)  Serial.print(F("  // High C"));
      Serial.println();
    }
  }
  Serial.println(F("};"));
  Serial.println();
}

void printTuningHelp() {
  Serial.println(F("\n=== CV TUNING MENU (manual; D10 outputs selected note) ==="));
  Serial.println(F("Commands:"));
  Serial.println(F("  h help | n/p next/prev | i <0..36> index | + / - step"));
  Serial.println(F("  S <1..16> set step size | v <0..255> set PWM"));
  Serial.println(F("  a auto-scan (2s each) | w write | r reload | f factory"));
  Serial.println(F("  t target info | P print CURRENT table | F print FACTORY table | x exit\n"));
}
void printNoteState(int idx) {
  char nameBuf[6]; noteNameForIndex(idx, nameBuf, sizeof(nameBuf));
  Serial.print(F("[Note ")); Serial.print(idx); Serial.print(F("] "));
  Serial.print(nameBuf);
  Serial.print(F("  target=")); Serial.print(targetFreqForIndex(idx), 2); Serial.print(F(" Hz"));
  Serial.print(F("  PWM=")); Serial.println(calTable[idx]);
}
void runTuningMenu() {
  Serial.begin(115200);
  Serial.println(F("\n== Entered TUNING MENU (hold button at boot) =="));
  // Seed then load from EEPROM (if any)
  for (int i=0;i<totalNotes;i++) calTable[i] = pgm_read_byte_near(factoryTuningValues + i);
  loadCalibration();

  analogWriteLED(64);
  printTuningHelp();

  int idx = 0;          // start at C2
  uint8_t step = 1;
  bool running = true;

  analogWriteCV(calTable[idx]); // output selected note on D10
  printNoteState(idx);

  while (running) {
    if (Serial.available()) {
      char c = Serial.read();
      switch (c) {
        case 'h': printTuningHelp(); break;

        case 'n':
          if (idx < totalNotes-1) idx++;
          analogWriteCV(calTable[idx]); printNoteState(idx);
          break;

        case 'p':
          if (idx > 0) idx--;
          analogWriteCV(calTable[idx]); printNoteState(idx);
          break;

        case '+':
          calTable[idx] = clamp_add_u8(calTable[idx], step);
          analogWriteCV(calTable[idx]); printNoteState(idx);
          break;

        case '-':
          calTable[idx] = clamp_sub_u8(calTable[idx], step);
          analogWriteCV(calTable[idx]); printNoteState(idx);
          break;

        case 'S': {
          long s = Serial.parseInt();
          if (s>=1 && s<=16){ step=(uint8_t)s; Serial.print(F("Step=")); Serial.println(step); }
          else Serial.println(F("Use 1..16"));
        } break;

        case 'i': {
          long ni= Serial.parseInt();
          if (ni>=0 && ni<totalNotes){ idx=(int)ni; analogWriteCV(calTable[idx]); printNoteState(idx); }
          else Serial.println(F("Index 0..36"));
        } break;

        case 'v': {
          long v = Serial.parseInt();
          if (v>=0 && v<=255){ calTable[idx]=(uint8_t)v; analogWriteCV(calTable[idx]); printNoteState(idx); }
          else Serial.println(F("PWM 0..255"));
        } break;

        case 'a': {
          Serial.println(F("Auto-scan (no changes)..."));
          for (int k=0;k<totalNotes;k++){
            analogWriteCV(calTable[k]);
            char nameBuf[6]; noteNameForIndex(k,nameBuf,sizeof(nameBuf));
            Serial.print(F("Note ")); Serial.print(k); Serial.print(' ');
            Serial.print(nameBuf); Serial.print(F("  target="));
            Serial.print(targetFreqForIndex(k),2); Serial.print(F(" Hz  PWM="));
            Serial.println(calTable[k]);
            delay(2000);
          }
          analogWriteCV(calTable[idx]);
          Serial.println(F("Auto-scan done."));
        } break;

        case 'w': saveCalibration(); break;
        case 'r': loadCalibration(); analogWriteCV(calTable[idx]); printNoteState(idx); break;
        case 'f': resetToFactory();  analogWriteCV(calTable[idx]); printNoteState(idx); break;
        case 't': printNoteState(idx); break;

        // NEW: print tables as copy-paste arrays
        case 'P': // Print CURRENT calibration table
          printArrayAsC(F("Current calibration (calTable) — copy & paste:"), "factoryTuningValues", calTable, totalNotes, false);
          break;
        case 'F': // Print FACTORY table
          printArrayAsC(F("Factory table (factoryTuningValues) — copy & paste:"), "factoryTuningValues", factoryTuningValues, totalNotes, true);
          break;

        case 'x': running=false; break;
        case '\n': case '\r': case ' ': break; // ignore whitespace

        default:
          Serial.print(F("Unknown '")); Serial.print(c); Serial.println(F("'. Press 'h'."));
          break;
      }
    }
  }

  analogWriteLED(0);
  analogWriteCV(0); // leave output idle
  Serial.println(F("Exiting menu → starting RandomCV..."));
}

// -------------------- RandomCV core --------------------
void reRandomizeCV() {
  for (int i = 0; i < 32; i++) {
    cvValues[i]   = random(256);
    trigValues[i] = random(256);
  }
}

void updateStepCount() {
  int val = analogRead(stepSelectPin);
  if      (val <= 102) indexSel = 0;
  else if (val <= 308) indexSel = 1;
  else if (val <= 514) indexSel = 2;
  else if (val <= 720) indexSel = 3;
  else if (val <= 926) indexSel = 4;
  else                  indexSel = 5;

  int newTotalSteps = stepModes[indexSel];
  if (newTotalSteps != currentTotalSteps) {
    currentStep = currentStep % newTotalSteps;
    currentTotalSteps = newTotalSteps;
  }
}

// -------------------- Setup / Loop --------------------
void setup() {
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  pinMode(trigOutPin, OUTPUT);
  pinMode(triggerPin, INPUT_PULLUP);
  pinMode(reRandomPin, INPUT_PULLUP);
  pinMode(cvOutPin, OUTPUT);

  setupFastPWM();            // configure Timer1 (D10) & Timer2 (D3)
  digitalWrite(trigOutPin, LOW);

  Serial.begin(115200);      // for menu / status

  // Load calibration for runtime quantizer (even if we don't enter menu)
  for (int i=0;i<totalNotes;i++) calTable[i] = pgm_read_byte_near(factoryTuningValues + i);
  loadCalibration();

  // Boot-hold to enter tuning menu
  if (digitalRead(buttonPin) == LOW) {
    delay(BOOT_HOLD_MS);
    if (digitalRead(buttonPin) == LOW) {
      runTuningMenu();       // returns when user exits
      loadCalibration();     // user may have saved changes
    }
  }

  randomSeed(analogRead(A0));
  reRandomizeCV();
  updateStepCount();

  Serial.print(F("Scale: ")); Serial.println(scaleName(currentScale));
}

void loop() {
  currentMillis = millis();

  // --- Clocked step on rising edge ---
  static bool lastTriggerState = HIGH;
  int trigReading = digitalRead(triggerPin);
  if (trigReading == HIGH && lastTriggerState == LOW) {
    // Step edge time for gate math
    uint32_t now = currentMillis;
    uint16_t stepDurMs = (lastStepEdgeMs == 0) ? 20 : (uint16_t)(now - lastStepEdgeMs);
    lastStepEdgeMs = now;

    updateStepCount();
    currentStep = (currentStep + 1) % currentTotalSteps;

    // POT2 (A1): "output level" → top of note range (0..36)
    int level = analogRead(potLevelPin);
    int maxNoteIndex = map(level, 0, 1023, 0, totalNotes - 1);

    // Map current random value (0..255) into 0..maxNoteIndex, then quantize to selected scale
    int rawNoteIndex = map(cvValues[currentStep], 0, 255, 0, maxNoteIndex);
    uint8_t pwm = quantizeIndexToPWM(rawNoteIndex);

    analogWriteCV(pwm);        // Quantized CV out on D10
    analogWriteLED(pwm);       // LED level shows CV (optional)

    // --- Gate/trigger generation on D11 ---
    int trigVal = analogRead(trigProbPin);
    int trigThreshold = map(trigVal, 0, 1023, 0, 255);
    bool thisStepFires = (trigValues[currentStep] < trigThreshold);

    if (thisStepFires) {
      digitalWrite(trigOutPin, HIGH);
      gateHigh = true;

      uint16_t gateMs = GATE_BASE_MS;
      if (gateMs > stepDurMs - 1) gateMs = (stepDurMs > 1) ? (uint16_t)(stepDurMs - 1) : GATE_BASE_MS;
      if (gateMs > GATE_MAX_MS)   gateMs = GATE_MAX_MS;

      gateOffAtMs = now + gateMs;

    } else if (GATE_TIE && gateHigh) {
      // No new note, extend gate a bit into the rest
      uint16_t extendMs = stepDurMs;
      if (extendMs > GATE_MAX_MS) extendMs = GATE_MAX_MS;
      gateOffAtMs = now + extendMs;
    }
  }
  lastTriggerState = trigReading;

  // Re-randomize upon rising edge of D9 trigger
  static bool lastReRandState = HIGH;
  int reRandReading = digitalRead(reRandomPin);
  if (reRandReading == HIGH && lastReRandState == LOW) {
    reRandomizeCV();
  }
  lastReRandState = reRandReading;

  // Turn gate off when its time elapses
  if (gateHigh && (long)(currentMillis - gateOffAtMs) >= 0) {
    digitalWrite(trigOutPin, LOW);
    gateHigh = false;
  }

  // --- Button: short = re-randomize, long = cycle scale (debounced & latched) ---
  switch (pollButton(buttonPin, currentMillis)) {
    case BTN_LONG:
      currentScale = (currentScale + 1) % NUM_SCALES;
      Serial.print(F("Scale: ")); Serial.println(scaleName(currentScale));
      break;
    case BTN_SHORT:
      reRandomizeCV();
      break;
    default:
      break;
  }
}
