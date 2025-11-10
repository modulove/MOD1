/*
 * MOD1 Chiptune VCO + Retro Sequences — External Clock Sync (A3/D17)
 * D10 as V/Oct CV output (0-5V PWM) for external modulation (sub oscillator control)
 */

#include <Arduino.h>
#include <avr/pgmspace.h>
#include <math.h>

// ---------- Constants ----------
#ifndef TWO_PI
#define TWO_PI (2.0f * PI)
#endif

#ifndef log2f
#define log2f(x) (logf(x) / logf(2.0f))
#endif

const float VIB_DEPTH_MIN = 0.002f;
const float VIB_DEPTH_MAX = 0.020f;
const float VIB_HZ_MIN = 3.0f;
const float VIB_HZ_MID = 6.0f;
const float VIB_HZ_MAX = 12.0f;
const float CRUSH_STEP_MAX = 150.0f;
const unsigned long CRUSH_HOLD_MAX = 50;
const unsigned long MODE_BLINK_INTERVAL = 120;
const unsigned long BUTTON_LONG_PRESS = 800;
const float TONE_MIN_HZ = 31.0f;
const float TONE_MAX_HZ = 65535.0f;
const float TONE_UPDATE_THRESHOLD = 0.25f;

// V/Oct CV output constants
const float CV_BASE_NOTE = 24.0f;  // C1 = 0V reference (MIDI note 24)
const float CV_VOLTS_PER_OCTAVE = 1.0f;
const uint8_t CV_PWM_MAX = 255;

// ---------- Types ----------
struct Step { int8_t note; uint8_t dur; }; // dur in ticks (tick = 1/4 beat)

// ---------------- Pins ----------------
const uint8_t PIN_AUDIO   = 11;   // D11 main tone() - uses Timer2
const uint8_t PIN_CV_OUT  = 10;   // D10 V/Oct CV output (PWM) - uses Timer1
const uint8_t PIN_BUTTON  = 4;    // Button (INPUT_PULLUP)
const uint8_t PIN_LED     = 3;    // LED - uses Timer2
const uint8_t POT_FREQ    = A0;   // Modulation / Tempo
const uint8_t POT_TRANS   = A1;   // Transpose base
const uint8_t POT_VIBCR   = A2;   // Vibrato vs Crush mix
const uint8_t PIN_SHARED  = A3;   // A3 analog or D17 digital (clock)
// Fix this!
const uint8_t CV_TRANS    = A4;   // Transpose CV add
const uint8_t CV_VIBCR    = A5;   // Vibrato/Crush CV add
const uint8_t PIN_CLOCK_DIGITAL = 17; // A3 as digital

// ---------------- Modulation Modes ----------------
enum ModMode { 
  MOD_TEMPO = 0,      // Internal tempo (default for sequences)
  MOD_TUNE,           // VCO tuning 1..2x
  MOD_PWM,            // Pulse width modulation feel (duty cycle variation)
  MOD_DETUNE,         // Slight detuning oscillator drift
  MOD_FILTER_SWEEP,   // Simulated filter sweep via freq modulation
  MOD_COUNT 
};
ModMode currentModMode = MOD_TEMPO;

// ---------------- UI / Modes ----------------
enum Mode { 
  MODE_VCO = 0, 
  MODE_TETRIS, 
  MODE_MARIO, 
  MODE_ZELDA, 
  MODE_MEGAMAN,
  MODE_ODE, 
  MODE_ARP, 
  MODE_SFX, 
  MODE_COUNT 
};
volatile Mode currentMode = MODE_VCO;
bool isPlaying = true;

// Button debounce/long-press
uint8_t lastBtn = HIGH;
unsigned long lastDebounceMs = 0;
const unsigned long debounceDelay = 30;
unsigned long pressStartMs = 0;
bool wasPressed = false;
unsigned long lastShortPressMs = 0;  // For double-tap detection

// LED blink feedback
unsigned long ledBlinkMs = 0;
uint8_t blinkCount = 0, blinkTarget = 0;
bool ledState = false;

// ---------------- Pitch helpers ----------------
static inline float midiToFreq(float midi) {
  return 440.0f * powf(2.0f, (midi - 69.0f) / 12.0f);
}

static inline float midiToVolts(float midi) {
  // V/Oct: 0V = C1 (MIDI 24), each octave = 1V
  // So MIDI note to volts: (midi - 24) / 12 volts
  return (midi - CV_BASE_NOTE) / 12.0f * CV_VOLTS_PER_OCTAVE;
}

static inline uint8_t voltsToPWM(float volts) {
  // 0-5V maps to 0-255 PWM
  if (volts < 0.0f) volts = 0.0f;
  if (volts > 5.0f) volts = 5.0f;
  return (uint8_t)(volts * CV_PWM_MAX / 5.0f);
}

const int TRANSPOSE_SEMITONES_MIN = -24;
const int TRANSPOSE_SEMITONES_MAX = +24;

// ---------------- Vibrato ----------------
float lfoPhase = 0.0f;
unsigned long lastLfoMs = 0;

// ---------------- Bit-Crush feel ----------------
struct Crush {
  float stepHz = 0.0f;
  unsigned long holdMs = 0;
  unsigned long lastUpdateMs = 0;
  float lastOutHz = 0.0f;
} crush;

static inline float quantizeHz(float hz, float stepHz) {
  if (stepHz <= 0.0f) return hz;
  long step = lroundf(hz / stepHz);
  return step * stepHz;
}

// ---------------- V/Oct CV Output ----------------
float currentMidiNote = 0.0f;  // Track current MIDI note for CV output
uint8_t lastCVPWM = 0;

void updateCVOutput() {
  // Convert MIDI note to V/Oct voltage
  float volts = midiToVolts(currentMidiNote);
  uint8_t pwmValue = voltsToPWM(volts);
  
  // Only update if changed to reduce PWM noise
  if (pwmValue != lastCVPWM) {
    analogWrite(PIN_CV_OUT, pwmValue);
    lastCVPWM = pwmValue;
  }
}

void stopCVOutput() {
  analogWrite(PIN_CV_OUT, 0);
  lastCVPWM = 0;
  currentMidiNote = 0.0f;
}

// ---------------- Public-domain & Classic Game sequences ----------------
// Tetris Theme A (Korobeiniki)
const Step SEQ_TETRIS[] PROGMEM = {
  {64,2},{66,2},{67,2},{69,2},{67,2},{66,2},{64,2},{62,2},
  {60,2},{62,2},{64,2},{66,2},{64,2},{62,2},{60,2},{-1,2},
  {57,2},{60,2},{64,2},{62,2},{60,2},{62,2},{64,2},{66,2},
  {64,2},{62,2},{60,2},{-1,2}
};
const uint16_t SEQ_TETRIS_LEN = sizeof(SEQ_TETRIS)/sizeof(SEQ_TETRIS[0]);

// Super Mario Bros Theme (simplified main melody)
const Step SEQ_MARIO[] PROGMEM = {
  {64,1},{64,1},{-1,1},{64,1},{-1,1},{60,1},{64,1},{-1,1},{67,2},{-1,2},{55,2},{-1,2},
  {60,2},{-1,1},{55,2},{-1,1},{52,2},{-1,1},{57,1},{-1,1},{59,1},{-1,1},{58,1},{57,1},{-1,1},
  {55,2},{67,2},{69,2},{65,1},{66,1},{64,2},{60,1},{62,1},{59,2},{-1,2}
};
const uint16_t SEQ_MARIO_LEN = sizeof(SEQ_MARIO)/sizeof(SEQ_MARIO[0]);

// Legend of Zelda Main Theme (opening)
const Step SEQ_ZELDA[] PROGMEM = {
  {57,2},{57,1},{57,1},{57,3},{60,3},{-1,1},
  {57,2},{57,1},{57,1},{57,3},{62,3},{-1,1},
  {57,2},{57,1},{57,1},{57,3},{64,3},{-1,1},
  {65,4},{64,4},{-1,2}
};
const uint16_t SEQ_ZELDA_LEN = sizeof(SEQ_ZELDA)/sizeof(SEQ_ZELDA[0]);

// Mega Man 2 - Dr. Wily Stage (simplified intro riff)
const Step SEQ_MEGAMAN[] PROGMEM = {
  {64,1},{67,1},{69,1},{71,1},{72,2},{71,1},{69,1},{67,2},{-1,1},
  {65,1},{69,1},{71,1},{72,1},{74,2},{72,1},{71,1},{69,2},{-1,1},
  {62,1},{65,1},{67,1},{69,1},{71,2},{69,1},{67,1},{65,2},{-1,1},
  {64,1},{67,1},{69,1},{71,1},{72,4},{-1,2}
};
const uint16_t SEQ_MEGAMAN_LEN = sizeof(SEQ_MEGAMAN)/sizeof(SEQ_MEGAMAN[0]);

// Ode to Joy
const Step SEQ_ODE[] PROGMEM = {
  {64,2},{64,2},{65,2},{67,2},{67,2},{65,2},{64,2},{62,2},
  {60,2},{60,2},{62,2},{64,2},{64,3},{62,1},{62,4}
};
const uint16_t SEQ_ODE_LEN = sizeof(SEQ_ODE)/sizeof(SEQ_ODE[0]);

// Arpeggio pattern
const Step SEQ_ARP[] PROGMEM = {
  {60,1},{64,1},{67,1},{72,1},
  {60,1},{64,1},{67,1},{79,1},
  {60,1},{64,1},{67,1},{74,1},
  {60,1},{64,1},{67,1},{76,1},
};
const uint16_t SEQ_ARP_LEN = sizeof(SEQ_ARP)/sizeof(SEQ_ARP[0]);

// SFX: -2 up sweep, -3 down sweep, -1 rest
const Step SEQ_SFX[] PROGMEM = {
  {-2,4}, {-1,2}, {-3,4}, {-1,2}, {84,2}, {96,2}, {-1,2}, {72,4}
};
const uint16_t SEQ_SFX_LEN = sizeof(SEQ_SFX)/sizeof(SEQ_SFX[0]);

// ---------------- Transport ----------------
unsigned long lastTickMs = 0;
uint16_t tickLenMs = 125; // internal: 120 BPM -> 1/4 beat = 125 ms
uint16_t stepIndex = 0;
uint8_t stepTicksRemaining = 0;

// ---------------- Controls ----------------
struct Controls {
  float tempoBpm;            // 60..240
  int   transpose;           // -24..+24
  float vibDepth;            // vibrato depth factor
  float vibHz;               // 3..12 Hz
  float crushStepHz;         // 0..150 Hz
  unsigned long crushHoldMs; // 0..50 ms
  float modValue;            // 0..1 (depends on modMode)
} ctrl;

unsigned long lastCtrlMs = 0;

// PWM simulation parameters
float pwmPhase = 0.0f;
unsigned long lastPwmMs = 0;

// Detune drift
float detuneOffset = 0.0f;
unsigned long lastDetuneMs = 0;

// Filter sweep simulation
float filterSweepPhase = 0.0f;
unsigned long lastFilterMs = 0;

static inline int read12bitLike(int base, int add) {
  int v = base + add;
  if (v < 0) v = 0;
  if (v > 1023) v = 1023;
  return v;
}

// V/Oct factor from A3 ADC in VCO mode
static inline float voctFactorFromAdc(int adc) {
  float volts = (adc * (5.0f/1023.0f));  // 0..5 V -> 0..5 octaves
  return powf(2.0f, volts);
}

void readControls() {
  unsigned long now = millis();
  if (now - lastCtrlMs < 10) return;
  lastCtrlMs = now;

  int potMod = analogRead(POT_FREQ); // 0..1023
  ctrl.modValue = potMod / 1023.0f;

  // Apply modulation based on mode
  switch (currentModMode) {
    case MOD_TEMPO:
      ctrl.tempoBpm = 60.0f + ctrl.modValue * 180.0f;
      break;
    case MOD_TUNE:
      // For VCO: tune factor 1..2x
      // For sequences: ignored (uses internal clock)
      break;
    case MOD_PWM:
    case MOD_DETUNE:
    case MOD_FILTER_SWEEP:
      // These use ctrl.modValue directly in their processing
      break;
    default:
      ctrl.tempoBpm = 120.0f;
      break;
  }

  int potTr   = analogRead(POT_TRANS);
  int cvTr    = analogRead(CV_TRANS);
  int combTr  = read12bitLike(potTr, cvTr);
  ctrl.transpose = (int)map(combTr, 0, 1023, TRANSPOSE_SEMITONES_MIN, TRANSPOSE_SEMITONES_MAX);

  int potVC   = analogRead(POT_VIBCR);
  int cvVC    = analogRead(CV_VIBCR);
  int combVC  = read12bitLike(potVC, cvVC);

  if (combVC <= 512) {
    float t = combVC / 512.0f;
    ctrl.vibDepth = VIB_DEPTH_MIN * t;
    ctrl.vibHz    = VIB_HZ_MIN + (VIB_HZ_MID - VIB_HZ_MIN) * t;
    ctrl.crushStepHz  = CRUSH_STEP_MAX * (1.0f - t);
    ctrl.crushHoldMs  = (unsigned long)(CRUSH_HOLD_MAX * (1.0f - t));
  } else {
    float t = (combVC - 512) / 511.0f;
    ctrl.vibDepth = VIB_DEPTH_MIN + (VIB_DEPTH_MAX - VIB_DEPTH_MIN) * t;
    ctrl.vibHz    = VIB_HZ_MID + (VIB_HZ_MAX - VIB_HZ_MID) * t;
    ctrl.crushStepHz = 0.0f;
    ctrl.crushHoldMs = 0;
  }
}

void updateTickLen() {
  float bpm = (ctrl.tempoBpm > 1.0f) ? ctrl.tempoBpm : 1.0f;
  float beatMs = 60000.0f / bpm;
  tickLenMs = (uint16_t)(beatMs / 4.0f);
}

// ---------------- External clock detection ----------------
bool useExternalClock = false;
unsigned long lastClockEdgeMs = 0;
uint8_t lastClockLevel = HIGH;
const unsigned long clockLostMs = 1500;
const unsigned int  minEdgeGapMs = 2;
unsigned long lastEdgeMs = 0;

void configureSharedPin() {
  if (currentMode == MODE_VCO) {
    pinMode(PIN_SHARED, INPUT);
    useExternalClock = false;
    lastClockEdgeMs = 0;
  } else {
    pinMode(PIN_CLOCK_DIGITAL, INPUT);
  }
}

bool pollExternalClockEdge() {
  uint8_t lvl = digitalRead(PIN_CLOCK_DIGITAL);
  unsigned long now = millis();
  bool edge = false;

  if (lvl == HIGH && lastClockLevel == LOW) {
    if ((now - lastEdgeMs) >= minEdgeGapMs) {
      edge = true;
      lastEdgeMs = now;
      lastClockEdgeMs = now;
    }
  }
  lastClockLevel = lvl;

  if (currentMode != MODE_VCO) {
    useExternalClock = ((now - lastClockEdgeMs) < clockLostMs);
  } else {
    useExternalClock = false;
  }
  return edge;
}

// ---------------- LED feedback ----------------
void startModeBlink(uint8_t count) {
  blinkTarget = count; 
  blinkCount = 0; 
  ledBlinkMs = millis(); 
  ledState = false;
  digitalWrite(PIN_LED, LOW);
}

void serviceBlink() {
  if (!blinkTarget) return;
  unsigned long now = millis();
  if ((now - ledBlinkMs) >= MODE_BLINK_INTERVAL) {
    ledBlinkMs = now;
    ledState = !ledState;
    digitalWrite(PIN_LED, ledState ? HIGH : LOW);
    if (!ledState) {
      blinkCount++;
      if (blinkCount >= blinkTarget) { 
        blinkTarget = 0; 
        digitalWrite(PIN_LED, LOW); 
      }
    }
  }
}

// ---------------- Button ----------------
void serviceButton() {
  unsigned long now = millis();
  uint8_t r = digitalRead(PIN_BUTTON);
  if (r != lastBtn) { 
    lastDebounceMs = now; 
    lastBtn = r; 
  }
  if ((now - lastDebounceMs) > debounceDelay) {
    if (r == LOW && !wasPressed) { 
      wasPressed = true; 
      pressStartMs = now; 
    }
    if (r == HIGH && wasPressed) {
      unsigned long dur = now - pressStartMs; 
      wasPressed = false;
      
      if (dur > BUTTON_LONG_PRESS) {
        // Long press: play/pause (sequence modes only)
        if (currentMode != MODE_VCO) {
          isPlaying = !isPlaying;
          if (!blinkTarget) {
            digitalWrite(PIN_LED, isPlaying ? HIGH : LOW);
          }
        }
      } else {
        // Short press: check for double-tap
        if ((now - lastShortPressMs) < 400) {
          // Double-tap: cycle modulation mode
          currentModMode = (ModMode)((currentModMode + 1) % MOD_COUNT);
          // Brief blink to indicate mod mode change
          startModeBlink(1);
          lastShortPressMs = 0; // Reset to prevent triple-tap
        } else {
          // Single tap: next mode
          currentMode = (Mode)((currentMode + 1) % MODE_COUNT);
          configureSharedPin();
          startModeBlink(currentMode + 1);
          // Reset transport
          stepIndex = 0; 
          stepTicksRemaining = 0; 
          lastTickMs = millis();
          lastShortPressMs = now;
        }
      }
    }
  }
}

// ---------------- Helpers to read PROGMEM steps ----------------
bool fetchStep(const Step* seq, uint16_t len, uint16_t idx, Step& out) {
  if (idx >= len) return false;
  memcpy_P(&out, &seq[idx], sizeof(Step));
  return true;
}

bool getCurrentStep(Step& out) {
  switch (currentMode) {
    case MODE_TETRIS:  return fetchStep(SEQ_TETRIS, SEQ_TETRIS_LEN, stepIndex, out);
    case MODE_MARIO:   return fetchStep(SEQ_MARIO, SEQ_MARIO_LEN, stepIndex, out);
    case MODE_ZELDA:   return fetchStep(SEQ_ZELDA, SEQ_ZELDA_LEN, stepIndex, out);
    case MODE_MEGAMAN: return fetchStep(SEQ_MEGAMAN, SEQ_MEGAMAN_LEN, stepIndex, out);
    case MODE_ODE:     return fetchStep(SEQ_ODE, SEQ_ODE_LEN, stepIndex, out);
    case MODE_ARP:     return fetchStep(SEQ_ARP, SEQ_ARP_LEN, stepIndex, out);
    case MODE_SFX:     return fetchStep(SEQ_SFX, SEQ_SFX_LEN, stepIndex, out);
    default:           return false;
  }
}

uint16_t getCurrentSeqLen() {
  switch (currentMode) {
    case MODE_TETRIS:  return SEQ_TETRIS_LEN;
    case MODE_MARIO:   return SEQ_MARIO_LEN;
    case MODE_ZELDA:   return SEQ_ZELDA_LEN;
    case MODE_MEGAMAN: return SEQ_MEGAMAN_LEN;
    case MODE_ODE:     return SEQ_ODE_LEN;
    case MODE_ARP:     return SEQ_ARP_LEN;
    case MODE_SFX:     return SEQ_SFX_LEN;
    default:           return 1;
  }
}

void advanceStep() {
  stepIndex = (stepIndex + 1) % getCurrentSeqLen();
}

void restartSeq() { 
  stepIndex = 0; 
  stepTicksRemaining = 0; 
}

// ---------------- SFX helpers ----------------
float renderSfxHz(int8_t flag, uint8_t durTicks, uint8_t ticksLeft, float baseHz) {
  uint8_t d = (durTicks == 0) ? 1 : durTicks;
  float t = 1.0f - (float)ticksLeft / (float)d;
  switch (flag) {
    case -2: return baseHz * (1.0f + 2.5f * t); // up sweep
    case -3: return baseHz * (1.0f - 0.8f * t); // down sweep
    default: return baseHz;
  }
}

// ---------------- Modulation Processors ----------------
float applyPWM(float hz, float modValue) {
  // Simulate PWM by creating rhythmic frequency wobble
  unsigned long now = millis();
  float dt = (now - lastPwmMs) / 1000.0f;
  lastPwmMs = now;
  
  float pwmHz = 8.0f + modValue * 40.0f; // 8-48 Hz PWM rate
  pwmPhase += TWO_PI * pwmHz * dt;
  if (pwmPhase > TWO_PI) pwmPhase -= TWO_PI;
  
  // Create duty cycle feel with square-ish wave
  float pwm = (sinf(pwmPhase) > (modValue * 2.0f - 1.0f)) ? 1.0f : -1.0f;
  return hz * (1.0f + pwm * 0.01f * modValue); // Subtle frequency variation
}

float applyDetune(float hz, float modValue) {
  // Slow random drift simulation
  unsigned long now = millis();
  if ((now - lastDetuneMs) > 50) {
    lastDetuneMs = now;
    detuneOffset += (random(-100, 100) / 10000.0f) * modValue;
    // Clamp drift
    if (detuneOffset > 0.02f) detuneOffset = 0.02f;
    if (detuneOffset < -0.02f) detuneOffset = -0.02f;
  }
  return hz * (1.0f + detuneOffset);
}

float applyFilterSweep(float hz, float modValue) {
  // Simulate filter sweep by periodic frequency emphasis
  unsigned long now = millis();
  float dt = (now - lastFilterMs) / 1000.0f;
  lastFilterMs = now;
  
  float sweepHz = 0.2f + modValue * 2.0f; // 0.2-2.2 Hz sweep
  filterSweepPhase += TWO_PI * sweepHz * dt;
  if (filterSweepPhase > TWO_PI) filterSweepPhase -= TWO_PI;
  
  float sweep = sinf(filterSweepPhase) * 0.5f + 0.5f; // 0..1
  float emphasis = 1.0f + (sweep * modValue * 0.3f); // Up to +30% boost
  return hz * emphasis;
}

// Helper to convert frequency back to approximate MIDI note
float freqToMidi(float hz) {
  return 69.0f + 12.0f * log2f(hz / 440.0f);
}

// ---------------- Tone update (with crush + vibrato + modulation) ----------------
float lastToneHz = -1.0f;

void setToneHz(float hz) {
  // Store the clean frequency as MIDI note for CV output BEFORE any processing
  currentMidiNote = freqToMidi(hz);
  
  // Apply modulation effects to audio
  switch (currentModMode) {
    case MOD_PWM:
      hz = applyPWM(hz, ctrl.modValue);
      break;
    case MOD_DETUNE:
      hz = applyDetune(hz, ctrl.modValue);
      break;
    case MOD_FILTER_SWEEP:
      hz = applyFilterSweep(hz, ctrl.modValue);
      break;
    default:
      break;
  }

  // Clamp to valid Arduino tone() range
  if (hz < TONE_MIN_HZ) hz = TONE_MIN_HZ;
  if (hz > TONE_MAX_HZ) hz = TONE_MAX_HZ;

  unsigned long now = millis();
  
  // Apply hold (sample & hold effect)
  if (crush.holdMs > 0) {
    if ((now - crush.lastUpdateMs) < crush.holdMs) {
      float h = (crush.lastOutHz < TONE_MIN_HZ) ? TONE_MIN_HZ : crush.lastOutHz;
      tone(PIN_AUDIO, (unsigned int)h);
      // Update CV output every loop regardless of hold
      updateCVOutput();
      return;
    }
    crush.lastUpdateMs = now;
  }
  
  // Apply frequency quantization
  if (crush.stepHz > 0.0f) {
    hz = quantizeHz(hz, crush.stepHz);
  }
  
  crush.lastOutHz = hz;
  
  // Only update main tone if frequency changed significantly
  if (fabsf(hz - lastToneHz) > TONE_UPDATE_THRESHOLD) {
    lastToneHz = hz;
    tone(PIN_AUDIO, (unsigned int)hz);
  }
  
  // ALWAYS update CV output (it tracks the clean MIDI note)
  updateCVOutput();
}

void stopTone() {
  noTone(PIN_AUDIO);
  stopCVOutput();
  lastToneHz = -1.0f;
}

// ---------------- Setup/Loop ----------------
void setup() {
  // Configure pins
  pinMode(PIN_AUDIO, OUTPUT);    // D11 - Timer2 (used by tone())
  pinMode(PIN_CV_OUT, OUTPUT);   // D10 - Timer1 PWM
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);      // D3 - Timer2 PWM
  digitalWrite(PIN_LED, LOW);

  // Set PWM frequency for D10 to be high (reduces audio-range noise)
  // Timer1 is 16-bit, pins 9 and 10
  // Set to Fast PWM mode with higher frequency
  TCCR1A = _BV(COM1B1) | _BV(WGM11);  // Non-inverting mode on OC1B (pin 10), Fast PWM
  TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10);  // Fast PWM, no prescaler
  ICR1 = 255;  // TOP value for 8-bit resolution (same as analogWrite)
  OCR1B = 0;   // Initial duty cycle = 0

  randomSeed(analogRead(A6)); // Seed for detune randomness

  configureSharedPin();

  lastLfoMs = millis();
  lastPwmMs = millis();
  lastDetuneMs = millis();
  lastFilterMs = millis();
  
  readControls();
  updateTickLen();
  restartSeq();

  lastClockLevel = digitalRead(PIN_CLOCK_DIGITAL);
  lastClockEdgeMs = 0;
  useExternalClock = false;
}

void loop() {
  serviceButton();
  serviceBlink();
  readControls();
  
  // Only update tick length if in tempo mode
  if (currentModMode == MOD_TEMPO || useExternalClock) {
    updateTickLen();
  }

  // Vibrato LFO
  unsigned long now = millis();
  float dt = (now - lastLfoMs) / 1000.0f; 
  lastLfoMs = now;
  lfoPhase += TWO_PI * ctrl.vibHz * dt;
  if (lfoPhase > TWO_PI) lfoPhase -= TWO_PI;
  float vib = sinf(lfoPhase) * ctrl.vibDepth;

  // Apply crush settings
  crush.stepHz = ctrl.crushStepHz;
  crush.holdMs = ctrl.crushHoldMs;

  // External clock polling (only relevant outside VCO)
  bool extEdge = false;
  if (currentMode != MODE_VCO) {
    extEdge = pollExternalClockEdge();
  }

  switch (currentMode) {
    case MODE_VCO: {
      int adc = analogRead(PIN_SHARED); // A3 as V/Oct
      float tuneFactor = (currentModMode == MOD_TUNE) ? 
                         (1.0f + ctrl.modValue) : 1.5f; // Default 1.5x if not in tune mode
      float baseHz = 40.0f * tuneFactor * voctFactorFromAdc(adc);
      float transHz = baseHz * powf(2.0f, ctrl.transpose / 12.0f);
      float outHz = transHz * (1.0f + vib);
      setToneHz(outHz);
    } break;

    case MODE_TETRIS:
    case MODE_MARIO:
    case MODE_ZELDA:
    case MODE_MEGAMAN:
    case MODE_ODE:
    case MODE_ARP:
    case MODE_SFX: {
      if (!isPlaying) { 
        stopTone();
        break; 
      }

      // Determine if we need to advance to next tick
      bool tickNow = false;
      if (useExternalClock) {
        if (extEdge) tickNow = true;
      } else {
        if ((now - lastTickMs) >= tickLenMs) { 
          lastTickMs = now; 
          tickNow = true; 
        }
      }

      // Advance sequencer on tick
      if (tickNow) {
        if (stepTicksRemaining > 0) {
          stepTicksRemaining--;
        }
        
        if (stepTicksRemaining == 0) {
          Step st{};
          if (!getCurrentStep(st)) { 
            restartSeq(); 
            break; 
          }
          
          stepTicksRemaining = (st.dur == 0) ? 1 : st.dur;
          advanceStep();
        }
      }

      // Render current step (always, for continuous vibrato/sweep)
      Step currentSt{};
      if (getCurrentStep(currentSt)) {
        if (currentSt.note == -1) {
          stopTone();
        } else {
          float baseHz;
          if (currentSt.note >= 0) {
            baseHz = midiToFreq((float)currentSt.note + ctrl.transpose);
          } else {
            // SFX sweep
            uint8_t dur = (currentSt.dur == 0) ? 1 : currentSt.dur;
            uint8_t left = (stepTicksRemaining == 0) ? 1 : stepTicksRemaining;
            baseHz = midiToFreq(60.0f + ctrl.transpose);
            baseHz = renderSfxHz(currentSt.note, dur, left, baseHz);
          }
          float outHz = baseHz * (1.0f + vib);
          setToneHz(outHz);
        }
      }
    } break;

    default: break;
  }

  // LED state management (don't interfere with blink sequence)
  if (!blinkTarget) {
    if (currentMode == MODE_VCO) {
      digitalWrite(PIN_LED, LOW);
    } else {
      digitalWrite(PIN_LED, isPlaying ? HIGH : LOW);
    }
  }
}
