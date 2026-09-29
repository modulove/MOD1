/*
HAGIWO MOD1 RandomCV Ver1.0  +  Quantized Notes + BOOT-HOLD SERIAL TUNING MENU
- RandomCV outputs QUANTIZED notes on D10 (uses calibrated table from EEPROM).
- Hold BUTTON (D4) at power-on for Serial Tuning Menu (115200 baud).
- Short press D4 = re-randomize. Long press (>800 ms) = cycle scale.

Pins (MOD1):
  POT1  A0  Step length 3,4,5,8,16,32
  POT2  A1  Output level → sets top note (C2..C5)
  POT3  A2  Trigger probability: fresh chance on every step; skipped steps hold the last note
  F1    D17 Clock in
  F2    D9  Random value update (TRIG IN)
  F3    D10 CV output (PWM @ 62.5 kHz, dithered)  <-- quantized + tuning menu out
  F4    D11 Gate output: opens 7 ms after the CV step (CV settle time), lasts the step
  BUTTON D4 Random value update / scale select (long press)
  LED   D3 CV level (PWM)

EEPROM: stores 37-note calibration table (C2..C5) in 1/16 PWM steps. Used by tuning menu AND
        runtime quantizer. A table saved by an older version (whole PWM steps) is converted when loaded.
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
int indexSel = 0;

unsigned long currentMillis  = 0;

// -------------------- Improved gate/trigger (D11) --------------------
bool gateHigh = false;
bool gatePending = false;             // gate waits for the CV to settle
uint32_t gateOnAtUs = 0;
uint16_t gateLenMs = 0;
uint32_t gateOffAtMs = 0;
uint32_t lastStepEdgeMs = 0;

// Adjust these to taste:
const uint16_t CV_SETTLE_US = 7000;   // gate opens this long after the CV step: the F3 output filter
                                      // (1k + 1uF, ~1 ms) needs ~7 ms to bring a 3-octave jump within
                                      // ~3 cents. Without it the note starts with a pitch slide.
const uint16_t GATE_MIN_MS  = 10;     // shortest gate
const uint16_t GATE_MAX_MS  = 120;    // clamp for very slow clocks
const bool     GATE_TIE     = false;  // if true: hold the gate across steps that don't fire

// -------------------- Button handling (debounced events) --------------------
enum BtnEvent : uint8_t { BTN_NONE=0, BTN_SHORT=1, BTN_LONG=2 };

// LGT8F328P: its internal pull-up (~4k) is stronger than the 10k resistor in series
// with the button, so a press never reads LOW. Switch the pull-up off for a moment
// instead: a pressed button drains the pin through the 10k, a released one stays high.
#if defined(__LGT8F__)
int readButton(uint8_t) {   // every caller passes buttonPin (D4)
  PORTD &= ~_BV(PD4);   // pull-up off (button = D4)
  delayMicroseconds(5);
  int level = (PIND & _BV(PD4)) ? HIGH : LOW;
  PORTD |= _BV(PD4);    // pull-up back on
  return level;
}
#else
#define readButton(pin) digitalRead(pin)
#endif

BtnEvent pollButton(uint8_t pin, unsigned long nowMs) {
  static bool rawPrev = HIGH;          // raw level (INPUT_PULLUP)
  static bool stable  = HIGH;          // debounced level
  static unsigned long lastEdgeMs = 0; // last raw change time
  static unsigned long pressStartMs = 0;
  static bool longSent = false;

  const unsigned long DEBOUNCE_MS = 30;
  const unsigned long LONG_MS     = 800;

  bool raw = readButton(pin);
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
// 37 semitones (C..B over 3 octaves + high C) in 1/16 PWM steps (0..4080 = PWM 0..255).
// One PWM step is 19.6 mV = 23.5 cents at 1 V/oct; the Timer1 ISR dithers between two
// neighbouring PWM values and the 1 ms output filter averages them, so a table step is
// ~1.5 cents.
const uint8_t CAL_FRAC = 16;           // table units per PWM step
const uint16_t CAL_MAX = 255 * CAL_FRAC;
const uint16_t factoryTuningValues[] PROGMEM = {
    16,   82,  148,  215,  281,  347,  413,  480,  546,  612,  678,  744,   // Octave 1 (C2..B2)
   811,  877,  943, 1009, 1076, 1142, 1208, 1274, 1340, 1407, 1473, 1539,   // Octave 2 (C3..B3)
  1605, 1672, 1738, 1804, 1870, 1936, 2003, 2069, 2135, 2201, 2268, 2334,   // Octave 3 (C4..B4)
  2400  // High C (C5)
};

const int totalNotes = sizeof(factoryTuningValues) / sizeof(factoryTuningValues[0]);
uint16_t calTable[37];  // working calibration table used by quantizer & menu

struct CalBlob   { char sig[4]; uint16_t table[37]; };  // "CVT2": 1/16 PWM steps
struct CalBlobV1 { char sig[4]; uint8_t  table[37]; };  // "CVT1": whole PWM steps
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

const uint8_t* scaleSteps(uint8_t scale, uint8_t &n) {
  switch (scale) {
    case MAJOR:        n = 7;  return steps_major;
    case AEOLIAN:      n = 7;  return steps_aeolian;
    case DORIAN:       n = 7;  return steps_dorian;
    case PHRYGIAN:     n = 7;  return steps_phrygian;
    case LYDIAN:       n = 7;  return steps_lydian;
    case PENT_MINOR:   n = 5;  return steps_pent_minor;
    case WHOLE_TONE:   n = 6;  return steps_whole_tone;
    case OCTATONIC_HW: n = 8;  return steps_octatonic_hw;
    default:           n = 12; return steps_chromatic;
  }
}

// Pick a note of the selected scale between C2 (index 0) and the top note from a random
// value 0..255. Every scale note in range is equally likely, the root and the top note
// included. Returns the note index 0..36.
uint8_t pickScaleNote(uint8_t rnd, uint8_t topNote) {
  uint8_t n;
  const uint8_t* steps = scaleSteps(currentScale, n);

  uint8_t count = 0;                       // scale notes from C2 up to topNote
  for (uint8_t base = 0; base <= topNote; base += 12)
    for (uint8_t i = 0; i < n && base + steps[i] <= topNote; i++) count++;

  uint8_t k = ((uint16_t)rnd * count) >> 8;  // 0..count-1
  for (uint8_t base = 0; ; base += 12)
    for (uint8_t i = 0; i < n; i++) {
      if (k == 0) return base + steps[i];
      k--;
    }
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
  TIMSK1 = _BV(TOIE1);       // dither ISR, every PWM cycle
}

// CV value in 1/16 PWM steps, split for the dither ISR
volatile uint8_t cvBase = 0;   // OCR1B
volatile uint8_t cvFrac = 0;   // fraction of a PWM step, 1/256
uint8_t ditherAcc = 0;

// Timer1 overflow, 62.5 kHz: first-order sigma-delta between cvBase and cvBase + 1.
// The pattern repeats at >= 3.9 kHz, which the 1 ms output filter averages to <1 mV ripple.
ISR(TIMER1_OVF_vect) {
  uint8_t prev = ditherAcc;
  ditherAcc += cvFrac;
  OCR1B = (ditherAcc < prev) ? cvBase + 1 : cvBase;
}

void setCV(uint16_t v) {     // D10, v in 1/16 PWM steps
  if (v > CAL_MAX) v = CAL_MAX;
  uint8_t base = v / CAL_FRAC;
  uint8_t frac = (v % CAL_FRAC) * (256 / CAL_FRAC);
  uint8_t sreg = SREG;
  cli();
  cvBase = base;
  cvFrac = frac;
  SREG = sreg;
}
static inline void analogWriteLED(uint8_t v) { OCR2B = v; } // D3

// -------------------- EEPROM helpers --------------------
void loadCalibration() {
  CalBlob blob; EEPROM.get(EEPROM_ADDR, blob);
  if (blob.sig[0]=='C' && blob.sig[1]=='V' && blob.sig[2]=='T' && blob.sig[3]=='2') {
    memcpy(calTable, blob.table, sizeof(calTable));
    Serial.println(F("Calibration loaded from EEPROM."));
    return;
  }
  CalBlobV1 old; EEPROM.get(EEPROM_ADDR, old);
  if (old.sig[0]=='C' && old.sig[1]=='V' && old.sig[2]=='T' && old.sig[3]=='1') {
    // Table saved by an older version, in whole PWM steps
    for (int i=0;i<totalNotes;i++) calTable[i] = old.table[i] * CAL_FRAC;
    Serial.println(F("Converted calibration from the older format."));
    return;
  }
  for (int i=0;i<totalNotes;i++) calTable[i] = pgm_read_word_near(factoryTuningValues + i);
  Serial.println(F("No calibration found; using factory table."));
}
void saveCalibration() {
  CalBlob blob; blob.sig[0]='C'; blob.sig[1]='V'; blob.sig[2]='T'; blob.sig[3]='2';
  memcpy(blob.table, calTable, sizeof(calTable));
  EEPROM.put(EEPROM_ADDR, blob);
  Serial.println(F("Calibration saved to EEPROM."));
}
void resetToFactory() {
  for (int i=0;i<totalNotes;i++) calTable[i] = pgm_read_word_near(factoryTuningValues + i);
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
static inline uint16_t clamp_add_cal(uint16_t a, uint16_t b) {
  uint16_t s = a + b;
  return (s > CAL_MAX) ? CAL_MAX : s;
}
static inline uint16_t clamp_sub_cal(uint16_t a, uint16_t b) {
  return (a > b) ? (uint16_t)(a - b) : (uint16_t)0;
}
static inline void printPwm(uint16_t v) { Serial.print(v / (float)CAL_FRAC, 2); }

// ---- NEW: pretty printer for copy-paste arrays ----
void printArrayAsC(const __FlashStringHelper* title, const char* name, const uint16_t* arr, int len, bool arrInProgmem) {
  Serial.println();
  Serial.print(F("// ")); Serial.println(title);
  Serial.print(F("const uint16_t ")); Serial.print(name); Serial.println(F("[] PROGMEM = {"));

  for (int i = 0; i < len; i++) {
    if (i % 12 == 0) {
      Serial.print(F("  "));
    }
    uint16_t v = arrInProgmem ? pgm_read_word_near(arr + i) : arr[i];
    Serial.print(v);
    if (i != len - 1) Serial.print(F(", "));

    if ((i % 12) == 11 || i == len - 1) {
      // octave comment after each C..B row, then the extra C
      if (i == len - 1) { Serial.print(F("  // High C (C")); Serial.print(i / 12 + 2); Serial.print(')'); }
      else { Serial.print(F("  // Octave ")); Serial.print(i / 12 + 1); Serial.print(F(" (C")); Serial.print(i / 12 + 2); Serial.print(F("..B")); Serial.print(i / 12 + 2); Serial.print(')'); }
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
  Serial.println(F("  S <1..64> step size in 1/16 PWM (1 = ~1.5 cents) | v <0..255> set PWM, decimals ok"));
  Serial.println(F("  a auto-scan (2s each) | w write | r reload | f factory"));
  Serial.println(F("  t target info | P print CURRENT table | F print FACTORY table | x exit\n"));
}
void printNoteState(int idx) {
  char nameBuf[6]; noteNameForIndex(idx, nameBuf, sizeof(nameBuf));
  Serial.print(F("[Note ")); Serial.print(idx); Serial.print(F("] "));
  Serial.print(nameBuf);
  Serial.print(F("  target=")); Serial.print(targetFreqForIndex(idx), 2); Serial.print(F(" Hz"));
  Serial.print(F("  PWM=")); printPwm(calTable[idx]); Serial.println();
}
void runTuningMenu() {
  Serial.begin(115200);
  Serial.println(F("\n== Entered TUNING MENU (hold button at boot) =="));
  // Seed then load from EEPROM (if any)
  for (int i=0;i<totalNotes;i++) calTable[i] = pgm_read_word_near(factoryTuningValues + i);
  loadCalibration();

  analogWriteLED(64);
  printTuningHelp();

  int idx = 0;          // start at C2
  uint8_t step = 1;     // 1/16 PWM
  bool running = true;

  setCV(calTable[idx]); // output selected note on D10
  printNoteState(idx);

  while (running) {
    if (Serial.available()) {
      char c = Serial.read();
      switch (c) {
        case 'h': printTuningHelp(); break;

        case 'n':
          if (idx < totalNotes-1) idx++;
          setCV(calTable[idx]); printNoteState(idx);
          break;

        case 'p':
          if (idx > 0) idx--;
          setCV(calTable[idx]); printNoteState(idx);
          break;

        case '+':
          calTable[idx] = clamp_add_cal(calTable[idx], step);
          setCV(calTable[idx]); printNoteState(idx);
          break;

        case '-':
          calTable[idx] = clamp_sub_cal(calTable[idx], step);
          setCV(calTable[idx]); printNoteState(idx);
          break;

        case 'S': {
          long s = Serial.parseInt();
          if (s>=1 && s<=64){ step=(uint8_t)s; Serial.print(F("Step=")); Serial.print(step); Serial.println(F("/16 PWM")); }
          else Serial.println(F("Use 1..64 (1/16 PWM each)"));
        } break;

        case 'i': {
          long ni= Serial.parseInt();
          if (ni>=0 && ni<totalNotes){ idx=(int)ni; setCV(calTable[idx]); printNoteState(idx); }
          else Serial.println(F("Index 0..36"));
        } break;

        case 'v': {
          float v = Serial.parseFloat();
          if (v>=0 && v<=255){ calTable[idx]=(uint16_t)(v * CAL_FRAC + 0.5f); setCV(calTable[idx]); printNoteState(idx); }
          else Serial.println(F("PWM 0..255"));
        } break;

        case 'a': {
          Serial.println(F("Auto-scan (no changes)..."));
          for (int k=0;k<totalNotes;k++){
            setCV(calTable[k]);
            char nameBuf[6]; noteNameForIndex(k,nameBuf,sizeof(nameBuf));
            Serial.print(F("Note ")); Serial.print(k); Serial.print(' ');
            Serial.print(nameBuf); Serial.print(F("  target="));
            Serial.print(targetFreqForIndex(k),2); Serial.print(F(" Hz  PWM="));
            printPwm(calTable[k]); Serial.println();
            delay(2000);
          }
          setCV(calTable[idx]);
          Serial.println(F("Auto-scan done."));
        } break;

        case 'w': saveCalibration(); break;
        case 'r': loadCalibration(); setCV(calTable[idx]); printNoteState(idx); break;
        case 'f': resetToFactory();  setCV(calTable[idx]); printNoteState(idx); break;
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
  setCV(0); // leave output idle
  Serial.println(F("Exiting menu → starting RandomCV..."));
}

// -------------------- RandomCV core --------------------
void reRandomizeCV() {
  // The moment of the press / trigger / first clock is unpredictable: mix it into the generator
  randomSeed(random(0x7FFFFFFFL) ^ micros());
  for (int i = 0; i < 32; i++) {
    cvValues[i] = random(256);
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
  for (int i=0;i<totalNotes;i++) calTable[i] = pgm_read_word_near(factoryTuningValues + i);
  loadCalibration();

  // Boot-hold to enter tuning menu
  if (readButton(buttonPin) == LOW) {
    delay(BOOT_HOLD_MS);
    if (readButton(buttonPin) == LOW) {
      runTuningMenu();       // returns when user exits
      loadCalibration();     // user may have saved changes
    }
  }

  // Seed from ADC noise (A6/A7 are not connected on MOD1) and the pots; the first clock
  // re-randomizes with its arrival time, so every power-up gets its own sequence
  uint32_t seed = 0;
  for (uint8_t i = 0; i < 32; i++) {
    seed = (seed << 5) ^ (seed >> 27) ^ analogRead(A6) ^ ((uint32_t)analogRead(A7) << 11) ^ micros();
  }
  seed ^= ((uint32_t)analogRead(A0) << 20) ^ ((uint32_t)analogRead(A1) << 10) ^ analogRead(A2);
  randomSeed(seed);
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
    if (lastStepEdgeMs == 0) reRandomizeCV();   // first clock: its arrival time seeds the sequence
    lastStepEdgeMs = now;

    updateStepCount();
    currentStep = (currentStep + 1) % currentTotalSteps;

    // POT3 (A2): chance that this step plays - a fresh dice roll on every step. Fully CCW
    // never, fully CW always (an LGT8F328P reads ~1016 fully CW)
    int prob = analogRead(trigProbPin);
    bool thisStepFires = (prob > 20) && (random(1000) < prob);

    // Close the running gate at the step edge (unless it is tied over a rest); the settle
    // time then doubles as the gap that lets the next note retrigger through F4's filter
    if (gateHigh && (thisStepFires || !GATE_TIE)) {
      digitalWrite(trigOutPin, LOW);
      gateHigh = false;
    }
    gatePending = false;

    // Steps that don't play hold the last note: CV and LED only move when a gate fires
    if (thisStepFires) {
      // POT2 (A1): "output level" → top note (0..36). Equal-width zones, so the top note is
      // reached below full scale too (an LGT8F328P reads ~1016 fully CW)
      int level = analogRead(potLevelPin);
      uint8_t topNote = ((long)level * totalNotes) >> 10;
      if (topNote > totalNotes - 1) topNote = totalNotes - 1;

      // This step's random value picks a scale note between C2 and the top note
      uint8_t note = pickScaleNote(cvValues[currentStep], topNote);

      setCV(calTable[note]);                    // Quantized CV out on D10
      analogWriteLED(calTable[note] / CAL_FRAC); // LED level shows CV (optional)

      // Gate lasts the step (clamped); the next clock edge closes it at the latest
      uint16_t gateMs = stepDurMs;
      if (gateMs < GATE_MIN_MS) gateMs = GATE_MIN_MS;
      if (gateMs > GATE_MAX_MS) gateMs = GATE_MAX_MS;
      gateLenMs = gateMs;

      gateOnAtUs = micros() + CV_SETTLE_US;
      gatePending = true;

    } else if (GATE_TIE && gateHigh) {
      // No new note, extend gate a bit into the rest
      uint16_t extendMs = stepDurMs;
      if (extendMs > GATE_MAX_MS) extendMs = GATE_MAX_MS;
      gateOffAtMs = now + extendMs;
    }
  }
  lastTriggerState = trigReading;

  // Open the gate once the CV has settled
  if (gatePending && (long)(micros() - gateOnAtUs) >= 0) {
    digitalWrite(trigOutPin, HIGH);
    gatePending = false;
    gateHigh = true;
    gateOffAtMs = millis() + gateLenMs;
  }

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
