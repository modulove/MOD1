/*
  MOD1 Visual Performer (Complete with WebSerial Control)
  -------------------------------------------------------
  Purpose: Drive *external* oscillators (e.g., Behringer 112 Dual VCO)
           to make pretty laser/oscilloscope Lissajous shapes.

  Outputs (high-rate PWM -> RC filter -> CV):
    - D9  (OC1A): CV_A (suggested: VCO1 FM or TUNE)
    - D10 (OC1B): CV_B (suggested: VCO2 FM or TUNE)
    - D11 (OC2A): CV_C (optional; e.g., PWM, extra FM, or blanking)

  Inputs:
    - A0: POT1 (Mode macro 1)
    - A1: POT2 (Mode macro 2)
    - A2: POT3 (Mode macro 3)
    - A3/D17: CV INPUT (clock/trigger/modulation - configurable)
    - D4: BUTTON (short = next preset, long = save; double = next mode)

  Outputs:
    - D3: LED (mode feedback - blinks mode count on change)

  Hardware notes:
    - Add simple RC low-pass at each PWM output near the jack:
        series 10k from pin -> jack tip; 100nF from jack tip -> GND.
      (≈160 Hz cutoff yields low ripple for "set & move" CV.)
    - Scale to projector/ILDA chain with a proper interface. Stay inside ±5 V spec.

  Modes (select with double-click; stored per preset):
    0) PATTERN MIXER
        • Three internal pattern "anchors" A/B/C (per preset).
        • Pot1 blends A<->B, Pot2 blends result<->C, Pot3 adds LFO depth.
    1) XY ROTATE/ZOOM (detune emulation)
        • Pot1 = detune (ratio-ish) on B, Pot2 = zoom (both), Pot3 = rotate (phase-ish drift via slow offset).
    2) LFO DRIVER (dual LFO outs)
        • Pot1 = LFO rate, Pot2 = depth A, Pot3 = depth B (sine & triangle).
    3) PRESET PERFORMER
        • Pot1 = morph time, Pot2 = offset bias A/B, Pot3 = master amount (macro).
    4) SEQUENCED RATIOS (internal stepper)
        • Pot1 = step rate, Pot2 = scale/zoom, Pot3 = swing/randomness.
    5) MANUAL XY MODE (WebSerial control)
        • Direct control of A/B/C from WebSerial XY pad and sliders.
        • Pots have no effect in this mode.

  CV Input Modes (configurable via WebSerial):
    0) DISABLED - no CV input processing
    1) CLOCK - advances presets on rising edge
    2) MOD_DEPTH - modulates LFO/movement depth
    3) TRIGGER - triggers preset recall

  Presets:
    - 8 slots stored in EEPROM with version+CRC.
    - Each slot stores:
        baseA, baseB, baseC        (0..255)  three anchors for mixer
        lfoRateU8, lfoDepthA, lfoDepthB (0..255)
        morphMs (0..4000), mode (0..5)
    - Short press: recall next preset with morph.
    - Long press: save live state into current slot.
    - Double press: next MODE (also stored with preset).

  Serial Commands (115200 baud):
    MODE n       - Set mode (0..5)
    MORPH ms     - Set morph time (0..4000)
    XY a b       - Set manual XY values (0..255 each)
    C v          - Set C output (0..255)
    LOAD i       - Load preset i (0..7)
    SAVE i       - Save current state to preset i
    GET i        - Dump preset i data to console
    CVIN m       - Set CV input mode (0..3)
    STATUS       - Get current device status

  Pot "pickup" prevents jumps after recall.

  Timing:
    - D9/D10 via Timer1 @ 62.5 kHz 8-bit Fast PWM.
    - D11 via Timer2 @ ~31.25 kHz 8-bit Fast PWM (optional).

  License: CC0 — do what you want. Have fun & stay laser-safe!
*/

#include <EEPROM.h>

// ===================== Pins =====================
#define BTN_PIN   4
#define LED_PIN   3
#define POT1_PIN  A0
#define POT2_PIN  A1
#define POT3_PIN  A2
#define CV_IN_PIN A3  // Also accessible as D17

#define CV_A_PIN  9   // OC1A  -> VCO1 FM/TUNE
#define CV_B_PIN 10   // OC1B  -> VCO2 FM/TUNE
#define CV_C_PIN 11   // OC2A  -> optional (PWM/PW/FM/blanking)

#define ENABLE_CV_C 1  // set 0 to disable D11 output

// ===================== Button timing =====================
static const unsigned LONG_MS   = 750;
static const unsigned DC_GAP_MS = 250;  // double-click gap

// ===================== Modes =====================
enum Mode : uint8_t {
  MODE_MIXER = 0,
  MODE_ROTATE,
  MODE_LFO,
  MODE_PERFORMER,
  MODE_RATIOS,
  MODE_MANUAL_XY,
  MODE_COUNT
};

// ===================== CV Input Modes =====================
enum CVInputMode : uint8_t {
  CVIN_DISABLED = 0,
  CVIN_CLOCK,
  CVIN_MOD_DEPTH,
  CVIN_TRIGGER,
  CVIN_COUNT
};

// ===================== Preset structs =====================
struct Preset {
  uint8_t baseA;      // anchor A (0..255)
  uint8_t baseB;      // anchor B (0..255)
  uint8_t baseC;      // anchor C (0..255)

  uint8_t lfoRateU8;  // 0..255 -> 0.02..10 Hz
  uint8_t lfoDepthA;  // 0..255 (depth on A)
  uint8_t lfoDepthB;  // 0..255 (depth on B)

  uint16_t morphMs;   // 0..4000
  uint8_t  mode;      // 0..MODE_COUNT-1
};

struct Store {
  uint16_t magic;   // 'M1' 0x4D31
  uint8_t  version;
  uint8_t  active;  // 0..7
  Preset   slots[8];
  uint16_t crc;
};

static const uint16_t MAGIC   = 0x4D31;
static const uint8_t  VERSION = 4;

// ===================== Globals =====================
Store   gStore;
Preset  cur;      // current (live, morphed)
Preset  tgt;      // target (on recall)
bool    morphing = false;
unsigned long morphStart = 0;
uint16_t morphDur = 800;

uint8_t lastP1=0, lastP2=0, lastP3=0;
bool pickupP1=true, pickupP2=true, pickupP3=true;

// Manual XY mode state (controlled via WebSerial)
uint8_t g_xyA = 128;
uint8_t g_xyB = 128;
uint8_t g_c = 128;

// CV Input state
CVInputMode g_cvInputMode = CVIN_DISABLED;
bool g_cvLastState = false;
uint16_t g_cvModDepth = 0;  // 0..1023 from CV input

enum BtnState {B_IDLE, B_DOWN, B_LONGED};
BtnState bstate = B_IDLE;
unsigned long downAt=0, lastRelease=0;
bool dcArmed=false; // double-click

// ===================== Triple output struct =====================
struct Triple{ uint8_t a,b,c; };

// ===================== Math helpers =====================
static inline uint8_t clampU8(int v){ if(v<0) return 0; if(v>255) return 255; return (uint8_t)v; }
static inline uint16_t clampU16(int v){ if(v<0) return 0; if(v>65535) return 65535; return (uint16_t)v; }

static inline uint8_t map10to8(uint16_t v) { return (uint8_t)(v >> 2); }

static inline float mapf(float x, float inA, float inB, float outA, float outB) {
  return outA + (outB - outA) * ((x - inA) / (inB - inA));
}

uint16_t crc16(const uint8_t* d, size_t n){
  uint16_t c=0xFFFF;
  for(size_t i=0;i<n;i++){ c^=d[i]; for(uint8_t b=0;b<8;b++){ c=(c&1)?(c>>1)^0xA001:(c>>1);} }
  return c;
}

// ===================== LED Feedback =====================
void blinkMode(uint8_t mode) {
  uint8_t count = mode + 1;  // 1-6 blinks for modes 0-5
  for(uint8_t i=0; i<count; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(150);
    digitalWrite(LED_PIN, LOW);
    delay(150);
  }
}

// ===================== EEPROM =====================
void saveStore(){
  gStore.magic = MAGIC;
  gStore.version = VERSION;
  const size_t payLen = sizeof(Store)-sizeof(uint16_t);
  gStore.crc = crc16((const uint8_t*)&gStore, payLen);
  EEPROM.put(0, gStore);
}
bool loadStore(){
  EEPROM.get(0, gStore);
  if(gStore.magic!=MAGIC || gStore.version!=VERSION) return false;
  const size_t payLen = sizeof(Store)-sizeof(uint16_t);
  if(crc16((const uint8_t*)&gStore, payLen)!=gStore.crc) return false;
  if(gStore.active>7) gStore.active=0;
  return true;
}
Preset makeDefault(uint8_t i){
  Preset p{};
  // sets span a nice range of bases/lfo/morph
  p.baseA = 80 + i*12;          // 80..164
  p.baseB = 90 + (i*10)%140;    // varied
  p.baseC = 60 + (i*17)%150;    // varied

  p.lfoRateU8 = 40 + i*20;      // ~0.6.. a few Hz
  p.lfoDepthA = (i<4)? 30 : 80;
  p.lfoDepthB = (i<4)? 20 : 70;

  p.morphMs = 600 + i*200;
  p.mode    = (i%MODE_COUNT);
  return p;
}
void initStore(){
  if(!loadStore()){
    gStore.magic = MAGIC;
    gStore.version = VERSION;
    gStore.active = 0;
    for(uint8_t i=0;i<8;i++) gStore.slots[i]=makeDefault(i);
    saveStore();
  }
  cur = gStore.slots[gStore.active];
  tgt = cur;
}

// ===================== PWM setup (optimized) =====================
void setupTimer1_8bit(){ // D9/D10 → 62.5kHz
  // Use direct port manipulation for setup (compile-time optimization)
  DDRB |= (1<<PB1) | (1<<PB2);  // Set D9(PB1) and D10(PB2) as outputs
  TCCR1A = (1<<WGM10) | (1<<COM1A1) | (1<<COM1B1);  // 8-bit Fast PWM, non-inverting
  TCCR1B = (1<<WGM12) | (1<<CS10);  // No prescaler (16MHz/256 = 62.5kHz)
  OCR1A = 0;
  OCR1B = 0;
}

void setupTimer2_8bit(){ // D11 → ~31.25kHz
#if ENABLE_CV_C
  DDRB |= (1<<PB3);  // Set D11(PB3) as output
  TCCR2A = (1<<WGM21) | (1<<WGM20) | (1<<COM2A1);  // Fast PWM, non-inverting
  TCCR2B = (1<<CS20);  // No prescaler (16MHz/256 = 62.5kHz, but phase-correct makes it ~31kHz)
  OCR2A = 128;  // Start at mid-point for better output
#endif
}

// Use direct register access for maximum speed
static inline void outA(uint8_t v){ OCR1A = v; }
static inline void outB(uint8_t v){ OCR1B = v; }
static inline void outC(uint8_t v){
#if ENABLE_CV_C
  OCR2A = v;
#else
  (void)v;
#endif
}

// ===================== CV Input Processing =====================
void processCVInput() {
  if(g_cvInputMode == CVIN_DISABLED) return;
  
  uint16_t cvRaw = analogRead(CV_IN_PIN);
  g_cvModDepth = cvRaw;
  
  bool cvHigh = (cvRaw > 512);  // Simple threshold at 2.5V
  
  // Rising edge detection
  if(cvHigh && !g_cvLastState) {
    switch(g_cvInputMode) {
      case CVIN_CLOCK:
        // Advance to next preset
        gStore.active = (gStore.active + 1) & 7;
        startMorphTo(gStore.slots[gStore.active], gStore.slots[gStore.active].morphMs);
        pickupP1 = pickupP2 = pickupP3 = true;
        saveStore();
        Serial.print("CLK P");
        Serial.println(gStore.active);
        break;
        
      case CVIN_TRIGGER:
        // Recall current preset (reset morph)
        startMorphTo(gStore.slots[gStore.active], gStore.slots[gStore.active].morphMs);
        Serial.println("TRIG");
        break;
        
      default:
        break;
    }
  }
  
  g_cvLastState = cvHigh;
}

// ===================== LFO =====================
struct LFO { float phase=0.0f; };
LFO lfo1, lfo2;

float u8ToHz(uint8_t u){ return mapf(u,0,255,0.02f,10.0f); } // 0.02..10 Hz
float tri(float ph){ float p = ph - floorf(ph); return (p<0.5f)? (p*4-1) : ((1-p)*4-1); } // -1..+1
float sine(float ph){ return sinf(2.0f*3.1415926f*ph); }   // -1..+1

// ===================== Morphing =====================
uint8_t lerpU8(uint8_t a,uint8_t b,float t){ return clampU8((int)roundf(a + (b-a)*t)); }
uint16_t lerpU16(uint16_t a,uint16_t b,float t){ return clampU16((int)roundf(a + (int)b - (int)a * t)); }

void startMorphTo(const Preset& p, uint16_t ms){
  tgt = p;
  morphStart = millis();
  morphDur = ms;
  morphing = (ms>0);
  if(!morphing) cur = tgt;
}

void applyMorph(){
  if(!morphing) return;
  float t = float(millis() - morphStart) / float(morphDur);
  if(t>=1.0f){ t=1.0f; morphing=false; }
  cur.baseA     = lerpU8(cur.baseA,    tgt.baseA,    t);
  cur.baseB     = lerpU8(cur.baseB,    tgt.baseB,    t);
  cur.baseC     = lerpU8(cur.baseC,    tgt.baseC,    t);
  cur.lfoRateU8 = lerpU8(cur.lfoRateU8,tgt.lfoRateU8,t);
  cur.lfoDepthA = lerpU8(cur.lfoDepthA,tgt.lfoDepthA,t);
  cur.lfoDepthB = lerpU8(cur.lfoDepthB,tgt.lfoDepthB,t);
  cur.morphMs   = lerpU16(cur.morphMs, tgt.morphMs,  t);
  cur.mode      = tgt.mode; // snap (mode is discrete)
}

// ===================== Pot pickup =====================
bool pickupReached(uint8_t live, uint8_t stored){ return (abs(int(live)-int(stored))<=5); }

// ===================== Button =====================
// Use direct port read for speed
static inline bool btn(){ return !(PIND & (1<<PD4)); }


// ===================== Mode engines =====================
// All engines compute raw (0..255) outputs for A/B/C based on:
// - current preset 'cur'
// - three pot values p1/p2/p3 : 0..255
// - elapsed dt for LFOs
// Return: (a,b,c)

Triple modeMixer(uint8_t p1,uint8_t p2,uint8_t p3,float dt){
  // Anchors A (cur.baseA), B (cur.baseB), C (cur.baseC)
  // P1 blends A<->B, P2 blends (AB)<->C, P3 = LFO depth
  float tAB = p1/255.0f;
  float tABC= p2/255.0f;

  // base crossfades
  float ab = (1.0f-tAB)*cur.baseA + tAB*cur.baseB;
  float abc= (1.0f-tABC)*ab + tABC*cur.baseC;

  // LFO on top (to A & B symmetrically)
  float hz = u8ToHz(cur.lfoRateU8);
  lfo1.phase += hz*dt; if(lfo1.phase>1) lfo1.phase-=floorf(lfo1.phase);
  float l = sine(lfo1.phase); // -1..+1

  // Apply CV modulation depth if enabled
  float depthMod = (g_cvInputMode == CVIN_MOD_DEPTH) ? (g_cvModDepth / 1023.0f) : 1.0f;
  int add = int((p3/255.0f) * 0.5f * 127 * l * depthMod);
  
  int A = int(abc) + add;
  int B = int(abc) - add;
  int C = cur.baseC;

  return { clampU8(A), clampU8(B), clampU8(C) };
}

Triple modeRotate(uint8_t p1,uint8_t p2,uint8_t p3,float dt){
  // Emulate rotate/zoom by differential detune + slow phase drift
  // P1 = detune amount B, P2 = zoom (both), P3 = drift speed
  float det = mapf(p1,0,255, -40, +40);   // counts to add/sub
  float zoom= mapf(p2,0,255,  0.5f, 1.5f);

  // slow drift on top
  float driftHz = mapf(p3,0,255, 0.00f, 0.50f);
  lfo2.phase += driftHz*dt; if(lfo2.phase>1) lfo2.phase-=floorf(lfo2.phase);
  float d = tri(lfo2.phase); // -1..+1
  int drift = int(d * 20);   // ±20 counts swing

  int A = int(cur.baseA*zoom) + drift;
  int B = int(cur.baseB*zoom) + int(det) - drift;
  int C = clampU8(int(cur.baseC*zoom));

  return { clampU8(A), clampU8(B), clampU8(C) };
}

Triple modeLFO(uint8_t p1,uint8_t p2,uint8_t p3,float dt){
  // Dual LFO driver:
  // P1 = rate, P2 = depth A, P3 = depth B
  float hz = mapf(p1,0,255, 0.02f, 8.0f);
  lfo1.phase += hz*dt; if(lfo1.phase>1) lfo1.phase-=floorf(lfo1.phase);
  lfo2.phase += hz*dt*0.667f; if(lfo2.phase>1) lfo2.phase-=floorf(lfo2.phase);

  float s = sine(lfo1.phase); // -1..+1
  float t = tri(lfo2.phase);  // -1..+1

  // Apply CV modulation depth if enabled
  float depthMod = (g_cvInputMode == CVIN_MOD_DEPTH) ? (g_cvModDepth / 1023.0f) : 1.0f;

  int A = int(cur.baseA) + int((p2/255.0f)*127*s*depthMod);
  int B = int(cur.baseB) + int((p3/255.0f)*127*t*depthMod);
  int C = cur.baseC;

  return { clampU8(A), clampU8(B), clampU8(C) };
}

Triple modePerformer(uint8_t p1,uint8_t p2,uint8_t p3,float dt){
  // p1 sets morph time, p2 adds common bias, p3 master amount
  (void)dt;
  morphDur = (uint16_t)mapf(p1,0,255, 50, 4000);
  int bias = int(mapf(p2,0,255, -64, +64)) * (int)mapf(p3,0,255,0.0f,1.0f);

  int A = int(cur.baseA) + bias;
  int B = int(cur.baseB) + bias;
  int C = int(cur.baseC) + bias/2;

  return { clampU8(A), clampU8(B), clampU8(C) };
}

Triple modeRatios(uint8_t p1,uint8_t p2,uint8_t p3,float dt){
  // Internal "ratio stepper" → jumps A/B between anchor pairs rhythmically.
  static float stepPhase=0.0f;
  float rateHz = mapf(p1,0,255, 0.2f, 8.0f);
  stepPhase += rateHz*dt; if(stepPhase>1) stepPhase-=floorf(stepPhase);
  int idx = (int)(stepPhase*8) & 7;

  // Table of 8 pairs (A,B) in counts around anchors to emulate freq ratios
  const int tA[8]={ 0,  10, -15,  25, -30,  40, -45,  60};
  const int tB[8]={ 0, -18,  22, -28,  35, -40,  48, -62};

  int zoom = (int)mapf(p2,0,255, 64, 191) - 127;  // -63..+64
  int swing= (int)mapf(p3,0,255, 0, 24);

  int A = int(cur.baseA) + tA[idx] + zoom + ((idx&1)? swing: -swing/2);
  int B = int(cur.baseB) + tB[idx] + zoom - ((idx&1)? swing: -swing/2);
  int C = cur.baseC;

  return { clampU8(A), clampU8(B), clampU8(C) };
}

Triple modeManualXY(uint8_t p1, uint8_t p2, uint8_t p3, float dt){
  // Manual XY mode: direct control from WebSerial
  // Pots have no effect in this mode
  (void)p1; (void)p2; (void)p3; (void)dt;
  return { g_xyA, g_xyB, g_c };
}

// ===================== Serial Command Parser =====================
void parseCommand(String cmd) {
  cmd.trim();
  if(cmd.length() == 0) return;
  
  // MODE n
  if(cmd.startsWith("MODE ")) {
    int m = cmd.substring(5).toInt();
    if(m >= 0 && m < MODE_COUNT) {
      cur.mode = (Mode)m;
      tgt.mode = (Mode)m;
      blinkMode(m);
      Serial.print("OK MODE=");
      Serial.println(m);
    } else {
      Serial.print("ERR MODE 0..");
      Serial.println(MODE_COUNT-1);
    }
  }
  
  // MORPH ms
  else if(cmd.startsWith("MORPH ")) {
    int ms = cmd.substring(6).toInt();
    if(ms >= 0 && ms <= 4000) {
      morphDur = (uint16_t)ms;
      cur.morphMs = ms;
      tgt.morphMs = ms;
      Serial.print("OK MORPH=");
      Serial.println(ms);
    } else {
      Serial.println("ERR MORPH 0..4000");
    }
  }
  
  // XY a b
  else if(cmd.startsWith("XY ")) {
    int sp = cmd.indexOf(' ', 3);
    if(sp > 0) {
      int a = cmd.substring(3, sp).toInt();
      int b = cmd.substring(sp+1).toInt();
      if(a >= 0 && a <= 255 && b >= 0 && b <= 255) {
        g_xyA = (uint8_t)a;
        g_xyB = (uint8_t)b;
      } else {
        Serial.println("ERR XY 0..255");
      }
    }
  }
  
  // C v
  else if(cmd.startsWith("C ")) {
    int c = cmd.substring(2).toInt();
    if(c >= 0 && c <= 255) {
      g_c = (uint8_t)c;
    } else {
      Serial.println("ERR C 0..255");
    }
  }
  
  // LOAD i
  else if(cmd.startsWith("LOAD ")) {
    int i = cmd.substring(5).toInt();
    if(i >= 0 && i < 8) {
      gStore.active = i;
      Preset next = gStore.slots[i];
      startMorphTo(next, next.morphMs);
      pickupP1=pickupP2=pickupP3=true;
      saveStore();
      Serial.print("OK LOAD=");
      Serial.println(i);
    } else {
      Serial.println("ERR LOAD 0..7");
    }
  }
  
  // SAVE i
  else if(cmd.startsWith("SAVE ")) {
    int i = cmd.substring(5).toInt();
    if(i >= 0 && i < 8) {
      gStore.slots[i] = cur;
      gStore.active = i;
      saveStore();
      Serial.print("OK SAVE=");
      Serial.println(i);
    } else {
      Serial.println("ERR SAVE 0..7");
    }
  }
  
  // GET i (dump preset data)
  else if(cmd.startsWith("GET ")) {
    int i = cmd.substring(4).toInt();
    if(i >= 0 && i < 8) {
      Preset& p = gStore.slots[i];
      Serial.print("P");
      Serial.print(i);
      Serial.print(" M=");
      Serial.print(p.mode);
      Serial.print(" A=");
      Serial.print(p.baseA);
      Serial.print(" B=");
      Serial.print(p.baseB);
      Serial.print(" C=");
      Serial.print(p.baseC);
      Serial.print(" LR=");
      Serial.print(p.lfoRateU8);
      Serial.print(" DA=");
      Serial.print(p.lfoDepthA);
      Serial.print(" DB=");
      Serial.print(p.lfoDepthB);
      Serial.print(" T=");
      Serial.println(p.morphMs);
    } else {
      Serial.println("ERR GET 0..7");
    }
  }
  
  // CVIN m (set CV input mode)
  else if(cmd.startsWith("CVIN ")) {
    int m = cmd.substring(5).toInt();
    if(m >= 0 && m < CVIN_COUNT) {
      g_cvInputMode = (CVInputMode)m;
      Serial.print("OK CVIN=");
      Serial.println(m);
    } else {
      Serial.print("ERR CVIN 0..");
      Serial.println(CVIN_COUNT-1);
    }
  }
  
  // STATUS
  else if(cmd.equals("STATUS")) {
    Serial.print("Preset=");
    Serial.print(gStore.active);
    Serial.print(" Mode=");
    Serial.print(cur.mode);
    Serial.print(" CVIN=");
    Serial.print(g_cvInputMode);
    Serial.print(" Morph=");
    Serial.print(morphDur);
    Serial.print(" A=");
    Serial.print(OCR1A);
    Serial.print(" B=");
    Serial.print(OCR1B);
    Serial.print(" C=");
    Serial.println(OCR2A);
  }
  
  else {
    Serial.println("ERR");
  }
}

// ===================== Setup =====================
void setup(){
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  analogReference(DEFAULT);
  setupTimer1_8bit();
  setupTimer2_8bit();
  initStore();

  // Initialize serial communication
  Serial.begin(115200);
  delay(100);
  Serial.println("MOD1 v2 Ready");
  Serial.print("P");
  Serial.print(gStore.active);
  Serial.print(" M");
  Serial.println(cur.mode);

  // Blink LED on startup (mode count)
  blinkMode(cur.mode);

  // initialize pot cache for pickup
  lastP1 = map10to8(analogRead(POT1_PIN));
  lastP2 = map10to8(analogRead(POT2_PIN));
  lastP3 = map10to8(analogRead(POT3_PIN));
}

// ===================== Loop =====================
void loop(){
  // ----- Handle serial commands -----
  while(Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    parseCommand(cmd);
  }

  unsigned long now = millis();
  static unsigned long last = now;
  float dt = (now - last)/1000.0f; last = now;

  // ----- Process CV Input -----
  static unsigned long lastCVRead = 0;
  if(now - lastCVRead >= 5) {  // Read CV every 5ms
    processCVInput();
    lastCVRead = now;
  }

  // ----- Button handling (short / long / double) -----
  bool pressed = btn();
  if(bstate==B_IDLE && pressed){
    bstate = B_DOWN; downAt = now;
  }else if(bstate==B_DOWN){
    if(!pressed){
      // release
      bstate = B_IDLE;
      if((now - downAt) >= LONG_MS){
        // LONG: save current state to active slot
        gStore.slots[gStore.active] = cur;
        saveStore();
        digitalWrite(LED_PIN, HIGH);
        delay(500);
        digitalWrite(LED_PIN, LOW);
        Serial.print("SAV P");
        Serial.println(gStore.active);
      }else{
        // SHORT: check double-click
        if(dcArmed && (now - lastRelease) <= DC_GAP_MS){
          // DOUBLE: next mode
          uint8_t nm = (cur.mode + 1) % MODE_COUNT;
          cur.mode = nm; tgt.mode = nm;
          gStore.slots[gStore.active].mode = nm;
          saveStore();
          blinkMode(nm);
          Serial.print("BTN M");
          Serial.println(nm);
          dcArmed=false;
        }else{
          // single short → next preset (with morph)
          gStore.active = (gStore.active + 1) & 7;
          Preset next = gStore.slots[gStore.active];
          startMorphTo(next, next.morphMs);
          pickupP1=pickupP2=pickupP3=true;
          saveStore();
          Serial.print("BTN P");
          Serial.println(gStore.active);
          dcArmed=true;
          lastRelease=now;
        }
      }
    }else if(pressed && (now - downAt) >= LONG_MS){
      bstate = B_LONGED;
      // (keep waiting for release)
    }
  }else if(bstate==B_LONGED && !pressed){
    bstate = B_IDLE;
    dcArmed=false; // long press cancels double-click state
  }

  // ----- Morph step -----
  applyMorph();

  // ----- Read pots (8-bit) -----
  uint8_t p1 = map10to8(analogRead(POT1_PIN));
  uint8_t p2 = map10to8(analogRead(POT2_PIN));
  uint8_t p3 = map10to8(analogRead(POT3_PIN));

  // pickup after recall
  if(pickupP1 && pickupReached(p1, lastP1)) pickupP1=false;
  if(pickupP2 && pickupReached(p2, lastP2)) pickupP2=false;
  if(pickupP3 && pickupReached(p3, lastP3)) pickupP3=false;

  if(!pickupP1) lastP1=p1; if(!pickupP2) lastP2=p2; if(!pickupP3) lastP3=p3;

  // ----- Run selected mode engine -----
  Triple out;
  switch(cur.mode){
    default:
    case MODE_MIXER:     out = modeMixer( lastP1, lastP2, lastP3, dt ); break;
    case MODE_ROTATE:    out = modeRotate( lastP1, lastP2, lastP3, dt ); break;
    case MODE_LFO:       out = modeLFO(    lastP1, lastP2, lastP3, dt ); break;
    case MODE_PERFORMER: out = modePerformer(lastP1,lastP2,lastP3, dt ); break;
    case MODE_RATIOS:    out = modeRatios( lastP1, lastP2, lastP3, dt ); break;
    case MODE_MANUAL_XY: out = modeManualXY(lastP1,lastP2,lastP3, dt ); break;
  }

  // ----- Write CVs (using optimized direct register access) -----
  outA(out.a);
  outB(out.b);
  outC(out.c);
}
