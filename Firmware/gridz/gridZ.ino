/**
 * MOD1 Topograph (Grids-style) — Clocked 3-ch drum sequencer with CV Matrix
 * Board: Arduino Nano (ATmega328P)
 * Jacks / Pins (as requested):
 *   F1: A3 + D17  -> D17 = CLK IN (PCINT)
 *   F2: A4 + D9   -> D9  = BD OUT (or A4 = CV IN when mapped)
 *   F3: A5 + D10  -> D10 = SD OUT (or A5 = CV IN when mapped)
 *   F4: D11       -> D11 = HH OUT (always out)
 *
 * Factory pattern map: include `grids_factory_nodes.h` generated from MI lookup_tables.py
 * Web Serial API for configuration + CV matrix (up to 3 targets per CV input with attenuverters).
 *
 * © 2025 Modulove
 */

#include <Arduino.h>
#include <EEPROM.h>

// --------- Pins -------------------------------------------------------------
#define PIN_CLK_IN_DIG   17  // D17 from F1 (A3 as digital); PCINT11 (PCINT1 vector)
#define PIN_F2_CV        A4  // Jack2 analog when used as CV
#define PIN_F3_CV        A5  // Jack3 analog when used as CV
#define PIN_BD_OUT       9   // Jack2 digital out
#define PIN_SD_OUT       10  // Jack3 digital out
#define PIN_HH_OUT       11  // Jack4 digital out (always out)

// --------- Grids factory nodes ---------------------------------------------
// Generate this header with the helper python script (see README in comment below).
// It defines:
//   GRIDS_NUM_CELLS, GRIDS_STEPS(32), GRIDS_CHANNELS(3)
//   const uint8_t PROGMEM GRIDS_NODES[GRIDS_NUM_CELLS][96]; // [BD32, SD32, HH32]
#include "grids_factory_nodes.h"

// If you haven't generated the header yet, temporarily uncomment next lines
// to allow compiling (will just produce silence):
 #ifndef GRIDS_NUM_CELLS
 #define GRIDS_NUM_CELLS 1
 #define GRIDS_STEPS 32
 #define GRIDS_CHANNELS 3
 const uint8_t PROGMEM GRIDS_NODES[1][96] = { { /* 96 zeros */ } };
 #endif

// --------- Config / State ---------------------------------------------------
enum CvTarget : uint8_t {
  TGT_NONE=0, TGT_MAP_X=1, TGT_MAP_Y=2, TGT_DENS_BD=3, TGT_DENS_SD=4, TGT_DENS_HH=5,
  TGT_GLOBAL_DENS=6, TGT_CHAOS=7, TGT_GATE_BD=8, TGT_GATE_SD=9, TGT_GATE_HH=10
};

// Each CV input (F2, F3) can have up to 3 mappings: (target, depth -127..+127)
struct CvMapSlot {
  uint8_t target;   // CvTarget
  int8_t  depth;    // -127..+127 (attenuverter)
};
struct CvInputMap {
  // If this input is enabled as CV, its corresponding OUT is disabled
  bool     enabled; // true => analog in mode; false => keep jack as OUT
  CvMapSlot slot[3];
};

struct Config {
  // base parameters
  uint8_t baseMapX;      // 0..255
  uint8_t baseMapY;      // 0..255
  uint8_t baseDens[3];   // BD,SD,HH 0..255
  uint8_t baseChaos;     // 0..255
  uint8_t gateMs[3];     // 0..50 ms (0=1ms trigger)
  uint8_t clockDiv;      // 1,2,4,8

  // CV input modes for F2 (A4/D9) and F3 (A5/D10)
  CvInputMap in2; // F2
  CvInputMap in3; // F3
} cfg;

volatile uint8_t pcint1_prev = 0;
volatile bool clockEdge = false;
volatile uint32_t stepCount = 0;
volatile uint8_t divCount = 0;

// runtime (modulated) values
uint8_t mapX=128, mapY=128;
uint8_t dens[3] = {160,140,120};
uint8_t chaos=16;

// Non-blocking gate engine
struct GateState { uint8_t pin; uint32_t offAt_us; bool active; uint16_t lengthMs; };
GateState gates[3]; // BD,SD,HH

// --------- Utils ------------------------------------------------------------
static inline uint8_t prng8() {
  static uint16_t s=0xACE1; s^=s<<7; s^=s>>9; s^=s<<8; return (uint8_t)s;
}
static inline uint8_t clamp8(int v){ if(v<0) return 0; if(v>255) return 255; return (uint8_t)v; }
static inline uint8_t readProb(uint16_t cell, uint8_t chan, uint8_t step) {
  if (cell >= GRIDS_NUM_CELLS) cell = GRIDS_NUM_CELLS - 1;
  uint16_t idx = chan * GRIDS_STEPS + step; // 0..95
  return pgm_read_byte(&GRIDS_NODES[cell][idx]);
}
// Quantize XY over grid; if 25 cells, treat as 5x5. Otherwise uniform packing.
static inline uint16_t xyToCell(uint8_t x, uint8_t y) {
  uint8_t W;
  // rough integer sqrt for small tables
  uint16_t n=GRIDS_NUM_CELLS; uint8_t s=1; while((uint16_t)(s+1)*(s+1) <= n) ++s;
  W = s; if (n==25) W=5;
  uint8_t H = max<uint8_t>(1, n / W);
  uint8_t xi = (uint16_t)x * W / 256;
  uint8_t yi = (uint16_t)y * H / 256;
  uint16_t cell = (uint16_t)yi * W + xi;
  if (cell >= n) cell = n-1;
  return cell;
}

static inline void gateOn(uint8_t idx, uint16_t lenMs){
  GateState &g = gates[idx];
  digitalWrite(g.pin, HIGH);
  g.active = true;
  if (lenMs==0) lenMs=1; // 1ms trig
  g.offAt_us = micros() + (uint32_t)lenMs*1000UL;
}

// --------- Clock via PCINT on D17 (A3/PCINT11) ------------------------------
// We detect rising edges on D17. Uses PCINT1_vect (A0..A5 group).
ISR(PCINT1_vect) {
  uint8_t now = PINC; // A0..A5 on PORTC
  uint8_t mask = (1<<3); // PC3 = A3 = D17
  bool prevHigh = (pcint1_prev & mask);
  bool nowHigh  = (now & mask);
  // rising edge?
  if (!prevHigh && nowHigh) {
    // clock divide
    if (++divCount >= max<uint8_t>(1, cfg.clockDiv)) {
      divCount = 0;
      clockEdge = true;
    }
  }
  pcint1_prev = now;
}

// --------- Init / Defaults --------------------------------------------------
void loadDefaults(){
  cfg.baseMapX=128; cfg.baseMapY=128;
  cfg.baseDens[0]=160; cfg.baseDens[1]=140; cfg.baseDens[2]=120;
  cfg.baseChaos=16;
  cfg.gateMs[0]=0; cfg.gateMs[1]=0; cfg.gateMs[2]=0;
  cfg.clockDiv=1;

  cfg.in2.enabled=false; // F2 = BD out by default
  for (uint8_t i=0;i<3;i++){ cfg.in2.slot[i] = {TGT_NONE, 0}; }

  cfg.in3.enabled=false; // F3 = SD out by default
  for (uint8_t i=0;i<3;i++){ cfg.in3.slot[i] = {TGT_NONE, 0}; }
}

void applyNonBlockingPinSetup(){
  pinMode(PIN_BD_OUT, OUTPUT);
  pinMode(PIN_SD_OUT, OUTPUT);
  pinMode(PIN_HH_OUT, OUTPUT);
  digitalWrite(PIN_BD_OUT, LOW);
  digitalWrite(PIN_SD_OUT, LOW);
  digitalWrite(PIN_HH_OUT, LOW);

  gates[0] = {PIN_BD_OUT, 0, false, 0};
  gates[1] = {PIN_SD_OUT, 0, false, 0};
  gates[2] = {PIN_HH_OUT, 0, false, 0};
}

void setupClockPCINT(){
  // Prepare previous state
  pcint1_prev = PINC;
  // Enable PCINT on A0..A5 group
  PCICR  |= (1<<PCIE1);    // enable PCINT1 group
  PCMSK1 |= (1<<PCINT11);  // A3/D17
}

void setup() {
  loadDefaults();
  Serial.begin(115200);

  // F1 D17 as input for CLK (pulldown/up external; add PULLUP if needed)
  pinMode(PIN_CLK_IN_DIG, INPUT);
  setupClockPCINT();

  // Outs
  applyNonBlockingPinSetup();

  // CV analogs are read only if enabled
  analogReference(DEFAULT);
}

// --------- CV Matrix application --------------------------------------------
// scale raw analog 0..1023 to centered -127..+127
static inline int16_t cvNormCentered(int raw) {
  int c = raw - 512; // -512..+511
  // scale to ~-127..+127
  long v = (long)c * 127 / 512;
  if (v < -127) v = -127; if (v>127) v=127;
  return (int16_t)v;
}

void readAndApplyCvMatrix(){
  // start from bases
  int mX = cfg.baseMapX;
  int mY = cfg.baseMapY;
  int d0 = cfg.baseDens[0];
  int d1 = cfg.baseDens[1];
  int d2 = cfg.baseDens[2];
  int ch = cfg.baseChaos;
  int g0 = cfg.gateMs[0];
  int g1 = cfg.gateMs[1];
  int g2 = cfg.gateMs[2];

  // F2 (A4) if enabled => BD out muted
  if (cfg.in2.enabled) {
    int v = analogRead(PIN_F2_CV); // 0..1023
    int16_t c = cvNormCentered(v); // -127..127
    for (uint8_t i=0;i<3;i++){
      uint8_t t = cfg.in2.slot[i].target;
      int8_t  d = cfg.in2.slot[i].depth; // -127..127
      if (t==TGT_NONE || d==0) continue;
      int delta = (int)c * (int)d / 127; // -127..127
      switch(t){
        case TGT_MAP_X:        mX += delta; break;
        case TGT_MAP_Y:        mY += delta; break;
        case TGT_DENS_BD:      d0 += delta; break;
        case TGT_DENS_SD:      d1 += delta; break;
        case TGT_DENS_HH:      d2 += delta; break;
        case TGT_GLOBAL_DENS:  d0 += delta; d1 += delta; d2 += delta; break;
        case TGT_CHAOS:        ch += delta; break;
        case TGT_GATE_BD:      g0 += delta/8; break; // small effect per unit
        case TGT_GATE_SD:      g1 += delta/8; break;
        case TGT_GATE_HH:      g2 += delta/8; break;
      }
    }
  }
  // F3 (A5) if enabled => SD out muted
  if (cfg.in3.enabled) {
    int v = analogRead(PIN_F3_CV);
    int16_t c = cvNormCentered(v);
    for (uint8_t i=0;i<3;i++){
      uint8_t t = cfg.in3.slot[i].target;
      int8_t  d = cfg.in3.slot[i].depth;
      if (t==TGT_NONE || d==0) continue;
      int delta = (int)c * (int)d / 127;
      switch(t){
        case TGT_MAP_X:        mX += delta; break;
        case TGT_MAP_Y:        mY += delta; break;
        case TGT_DENS_BD:      d0 += delta; break;
        case TGT_DENS_SD:      d1 += delta; break;
        case TGT_DENS_HH:      d2 += delta; break;
        case TGT_GLOBAL_DENS:  d0 += delta; d1 += delta; d2 += delta; break;
        case TGT_CHAOS:        ch += delta; break;
        case TGT_GATE_BD:      g0 += delta/8; break;
        case TGT_GATE_SD:      g1 += delta/8; break;
        case TGT_GATE_HH:      g2 += delta/8; break;
      }
    }
  }

  // Clamp
  mapX  = clamp8(mX);
  mapY  = clamp8(mY);
  dens[0]=clamp8(d0); dens[1]=clamp8(d1); dens[2]=clamp8(d2);
  chaos = clamp8(ch);
  cfg.gateMs[0] = (uint8_t) constrain(g0, 0, 50);
  cfg.gateMs[1] = (uint8_t) constrain(g1, 0, 50);
  cfg.gateMs[2] = (uint8_t) constrain(g2, 0, 50);
}

// --------- Step Engine ------------------------------------------------------
void processStep(){
  uint16_t cell = xyToCell(mapX, mapY);
  uint8_t step  = stepCount % GRIDS_STEPS;

  // channel 0: BD -> D9 (mute if F2 is CV)
  // channel 1: SD -> D10 (mute if F3 is CV)
  // channel 2: HH -> D11 (always)
  for (uint8_t ch=0; ch<3; ++ch){
    if ((ch==0 && cfg.in2.enabled) || (ch==1 && cfg.in3.enabled)) continue;

    uint8_t p = readProb(cell, ch, step); // 0..255 factory prob
    uint16_t base = (uint16_t)p * (uint16_t)dens[ch] / 255; // 0..255
    uint8_t  r = prng8();
    bool fire = (r < base) || ((p == 0) && (r < chaos));

    if (fire){
      uint8_t idx = ch; // gates[0]=BD,1=SD,2=HH
      gateOn(idx, cfg.gateMs[ch]);
    }
  }

  stepCount++;
}

// --------- Web Serial API ---------------------------------------------------
// Commands (one per line):
//   GET
//   SAVE / LOAD
//   SET key=value;key=value...
// Keys:
//   mapX,mapY, dens0,dens1,dens2, chaos, gate0,gate1,gate2, clockDiv
//   in2,en=0/1, in2t0..t2 (target id), in2d0..d2 (depth -127..127)
//   in3,en=0/1, in3t0..t2, in3d0..d2
// Returns JSON on GET and after SET.

void sendState(){
  Serial.print(F("{\"ok\":true,"));
  Serial.print(F("\"mapX\":")); Serial.print(mapX); Serial.print(F(","));
  Serial.print(F("\"mapY\":")); Serial.print(mapY); Serial.print(F(","));
  Serial.print(F("\"dens\":["));
  Serial.print(dens[0]); Serial.print(F(",")); Serial.print(dens[1]); Serial.print(F(",")); Serial.print(dens[2]); Serial.print(F("],"));
  Serial.print(F("\"chaos\":")); Serial.print(chaos); Serial.print(F(","));
  Serial.print(F("\"gateMs\":["));
  Serial.print(cfg.gateMs[0]); Serial.print(F(",")); Serial.print(cfg.gateMs[1]); Serial.print(F(",")); Serial.print(cfg.gateMs[2]); Serial.print(F("],"));
  Serial.print(F("\"clockDiv\":")); Serial.print(cfg.clockDiv); Serial.print(F(","));

  Serial.print(F("\"in2\":{"));
  Serial.print(F("\"enabled\":")); Serial.print(cfg.in2.enabled?1:0); Serial.print(F(",\"map\":["));
  for(uint8_t i=0;i<3;i++){
    if (i) Serial.print(F(","));
    Serial.print(F("{\"t\":")); Serial.print(cfg.in2.slot[i].target);
    Serial.print(F(",\"d\":")); Serial.print(cfg.in2.slot[i].depth);
    Serial.print(F("}"));
  }
  Serial.print(F("]},")); // end in2

  Serial.print(F("\"in3\":{"));
  Serial.print(F("\"enabled\":")); Serial.print(cfg.in3.enabled?1:0); Serial.print(F(",\"map\":["));
  for(uint8_t i=0;i<3;i++){
    if (i) Serial.print(F(","));
    Serial.print(F("{\"t\":")); Serial.print(cfg.in3.slot[i].target);
    Serial.print(F(",\"d\":")); Serial.print(cfg.in3.slot[i].depth);
    Serial.print(F("}"));
  }
  Serial.print(F("]}")); // end in3

  Serial.println(F("}"));
}

void parseSetKV(const String& kvs){
  int p=0;
  while (p < (int)kvs.length()){
    int sep = kvs.indexOf(';', p);
    String kv = kvs.substring(p, sep<0?kvs.length():sep);
    int eq = kv.indexOf('=');
    if (eq>0){
      String k = kv.substring(0,eq);
      String v = kv.substring(eq+1);
      int vi = v.toInt();

      if (k=="mapX")        cfg.baseMapX = constrain(vi,0,255);
      else if (k=="mapY")   cfg.baseMapY = constrain(vi,0,255);
      else if (k=="dens0")  cfg.baseDens[0]=constrain(vi,0,255);
      else if (k=="dens1")  cfg.baseDens[1]=constrain(vi,0,255);
      else if (k=="dens2")  cfg.baseDens[2]=constrain(vi,0,255);
      else if (k=="chaos")  cfg.baseChaos = constrain(vi,0,255);
      else if (k=="gate0")  cfg.gateMs[0] = constrain(vi,0,50);
      else if (k=="gate1")  cfg.gateMs[1] = constrain(vi,0,50);
      else if (k=="gate2")  cfg.gateMs[2] = constrain(vi,0,50);
      else if (k=="clockDiv") cfg.clockDiv = max(1,min(8,vi));

      // CV input toggles
      else if (k=="in2en")  cfg.in2.enabled = (vi!=0);
      else if (k=="in3en")  cfg.in3.enabled = (vi!=0);

      // in2 mapping slots
      else if (k=="in2t0")  cfg.in2.slot[0].target = (uint8_t)constrain(vi,0,10);
      else if (k=="in2t1")  cfg.in2.slot[1].target = (uint8_t)constrain(vi,0,10);
      else if (k=="in2t2")  cfg.in2.slot[2].target = (uint8_t)constrain(vi,0,10);
      else if (k=="in2d0")  cfg.in2.slot[0].depth  = (int8_t)constrain(vi,-127,127);
      else if (k=="in2d1")  cfg.in2.slot[1].depth  = (int8_t)constrain(vi,-127,127);
      else if (k=="in2d2")  cfg.in2.slot[2].depth  = (int8_t)constrain(vi,-127,127);

      // in3 mapping slots
      else if (k=="in3t0")  cfg.in3.slot[0].target = (uint8_t)constrain(vi,0,10);
      else if (k=="in3t1")  cfg.in3.slot[1].target = (uint8_t)constrain(vi,0,10);
      else if (k=="in3t2")  cfg.in3.slot[2].target = (uint8_t)constrain(vi,0,10);
      else if (k=="in3d0")  cfg.in3.slot[0].depth  = (int8_t)constrain(vi,-127,127);
      else if (k=="in3d1")  cfg.in3.slot[1].depth  = (int8_t)constrain(vi,-127,127);
      else if (k=="in3d2")  cfg.in3.slot[2].depth  = (int8_t)constrain(vi,-127,127);
    }
    if (sep<0) break; p = sep+1;
  }
}

void handleSerial(){
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line=="GET"){ readAndApplyCvMatrix(); sendState(); return; }
  if (line=="SAVE"){ EEPROM.put(0, cfg); Serial.println(F("{\"ok\":true,\"saved\":1}")); return; }
  if (line=="LOAD"){ EEPROM.get(0, cfg); Serial.println(F("{\"ok\":true,\"loaded\":1}")); return; }
  if (line.startsWith("SET ")){
    parseSetKV(line.substring(4));
    Serial.println(F("{\"ok\":true}"));
    readAndApplyCvMatrix();
  }
}

// --------- Main loop --------------------------------------------------------
void loop(){
  // update modulation from CV matrix
  readAndApplyCvMatrix();

  // clocked step
  if (clockEdge){
    noInterrupts(); clockEdge=false; interrupts();
    processStep();
  }

  // gate engine off events
  uint32_t now = micros();
  for (uint8_t i=0;i<3;i++){
    GateState &g = gates[i];
    if (g.active && (int32_t)(now - g.offAt_us) >= 0){
      digitalWrite(g.pin, LOW);
      g.active = false;
    }
  }

  // web serial
  handleSerial();
}
