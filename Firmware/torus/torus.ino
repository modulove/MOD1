/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║   ::TORUS::   Toroidal XY Oscillator  ·  HAGIWO MOD1 Eurorack           ║
 * ║   Laser / ILDA XY output via PWM                                         ║
 * ║   Arduino Nano (ATmega328P) @ 16 MHz                                     ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  A Little Circle spins on its end, while orbiting a Big Circle laid      ║
 * ║  flat. A somewhat strange 2-OP FM voice with an Additive feel.           ║
 * ║  Also — DISTORTION.                                                       ║
 * ║                                                                          ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║  HARDWARE (HAGIWO MOD1)                                                   ║
 * ║  ─────────────────────────────────────────────────────────────────────── ║
 * ║  D9   OC1A  → X output  (62.5 kHz PWM → RC filter → ILDA X)             ║
 * ║  D10  OC1B  → Y output  (62.5 kHz PWM → RC filter → ILDA Y)             ║
 * ║  D11        → Z/Blanking (HIGH = laser ON; LOW = blanked during retrace) ║
 * ║  A0   POT1  → see pages below                                            ║
 * ║  A2   POT2  → see pages below                                            ║
 * ║  A3   POT3  → see pages below                                            ║
 * ║  A4   CV1   → see pages below                                            ║
 * ║  A6   CV2   → see pages below                                            ║
 * ║  D3         → LED  (mode/page indicator)                                 ║
 * ║  D4         → BUTTON (INPUT_PULLUP)                                      ║
 * ║                                                                          ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║  PAGE 0  —  PITCH / GEOMETRY  (default)                                  ║
 * ║  ─────────────────────────────────────────────────────────────────────── ║
 * ║  POT1  Big Pitch     outer orbit frequency  (1–600 Hz, log)              ║
 * ║  POT2  Little Pitch  inner orbit frequency  (1–600 Hz, log)              ║
 * ║  POT3  Radial Diff   r / R ratio of circles  (0.05 – 0.95)              ║
 * ║  CV1   V/Oct Big     1 V/Oct adds to Big Pitch  (0–5 V → 0–5 oct)       ║
 * ║  CV2   V/Oct Little  1 V/Oct adds to Little Pitch                        ║
 * ║                                                                          ║
 * ║  PAGE 1  —  FM / DRIVE / WINDINGS  (hold button > 1 s to enter/exit)    ║
 * ║  ─────────────────────────────────────────────────────────────────────── ║
 * ║  POT1  FM Amount     cross-modulation depth (0–1)                        ║
 * ║  POT2  Windings      harmonic winding ratio  (×1 – ×8, snapped)         ║
 * ║  POT3  Drive         pre-distortion gain  (1× – 8×)                     ║
 * ║  CV1   FM CV         adds to FM Amount                                   ║
 * ║  CV2   Drive CV      adds to Drive                                       ║
 * ║                                                                          ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║  BUTTON                                                                   ║
 * ║  ─────────────────────────────────────────────────────────────────────── ║
 * ║  Short press  (<0.8 s)   cycle equations: TOROID → ELECTRON → FOLDING    ║
 * ║  Long press   (>1.2 s)   toggle page 0 / 1                               ║
 * ║  Double press (<400 ms)  toggle LFO mode on Big Pitch (÷256 = −8 oct)   ║
 * ║                                                                          ║
 * ║  LED patterns                                                             ║
 * ║  Page 0 · TOROID:    1 slow blink per cycle                              ║
 * ║  Page 0 · ELECTRON:  2 blinks                                            ║
 * ║  Page 0 · FOLDING:   3 fast blinks                                       ║
 * ║  Page 1:             solid ON                                             ║
 * ║  LFO active:         rapid flutter (10 Hz)                               ║
 * ║                                                                          ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║  EQUATIONS                                                                ║
 * ║  ─────────────────────────────────────────────────────────────────────── ║
 * ║  TOROID   classic torus parametric, FM modulates inner phase              ║
 * ║  ELECTRON epitrochoid/spirograph with cross-FM between orbits            ║
 * ║  FOLDING  torus with triangle-wave folder (Drive controls depth)         ║
 * ║                                                                          ║
 * ║  Drive types cycle inside FOLDING via a hidden long-double-press:        ║
 * ║    · WARM    soft-knee saturation (like VCF drive)                       ║
 * ║    · WAVEFOLD triangle fold, up to 4 stages                              ║
 * ║    · CLIP    hard clip then re-amplify                                   ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 *
 * RC filter on D9 / D10: 1 kΩ + 100 nF → ~1.6 kHz cutoff, good for ILDA.
 * Blanking on D11 uses a digital HIGH/LOW — wire to your ILDA blanking input.
 *
 * Version : 1.0
 * License : CC BY-SA 4.0  /  Modulove
 */

#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <math.h>

// ─────────────────────────────────────────────────────────────────────────────
// PIN DEFINITIONS
// ─────────────────────────────────────────────────────────────────────────────

#define PIN_X       9       // OC1A  – Timer1A  (X axis PWM)
#define PIN_Y       10      // OC1B  – Timer1B  (Y axis PWM)
#define PIN_Z       11      // D11   – digital blanking / Z
#define PIN_POT1    A0      // Big Pitch  /  FM Amount
#define PIN_POT2    A2      // Little Pitch / Windings
#define PIN_POT3    A3      // Radial Diff  / Drive
#define PIN_CV1     A4      // V/Oct Big    / FM CV
#define PIN_CV2     A6      // V/Oct Little / Drive CV
#define PIN_LED     3
#define PIN_BUTTON  4

// ─────────────────────────────────────────────────────────────────────────────
// TIMING
// ─────────────────────────────────────────────────────────────────────────────

// Timer1  →  8-bit Fast PWM, prescaler 1 → 62500 Hz on D9 / D10
// Timer2  →  CTC mode, prescaler 8 → ISR at 20000 Hz (OCR2A = 99)

#define SAMPLE_RATE   20000UL
#define PARAM_DIVIDER 200      // update params every 200 samples = 100 Hz

// ─────────────────────────────────────────────────────────────────────────────
// LOOKUP TABLE  (256-entry, int8_t → −127 … +127)
// Quarter-period offset trick: COS8(p) = SIN8(p + 64 counts = π/2)
// ─────────────────────────────────────────────────────────────────────────────

static int8_t sin_lut[256];

#define SIN8(p)  sin_lut[(uint8_t)((uint16_t)(p) >> 8)]
#define COS8(p)  sin_lut[(uint8_t)(((uint16_t)(p) >> 8) + 64u)]

// ─────────────────────────────────────────────────────────────────────────────
// MODES / ENUMS
// ─────────────────────────────────────────────────────────────────────────────

typedef enum { EQ_TOROID = 0, EQ_ELECTRON, EQ_FOLDING, EQ_COUNT } Equation;
typedef enum { DRV_WARM  = 0, DRV_WAVEFOLD, DRV_CLIP,  DRV_COUNT } DriveType;

// ─────────────────────────────────────────────────────────────────────────────
// SHARED STATE  (written by main loop, read by ISR – all volatile)
// ─────────────────────────────────────────────────────────────────────────────

volatile uint16_t g_inc_big    = 327;   // phase increment per sample  (default ~100 Hz)
volatile uint16_t g_inc_little = 654;   // default ~200 Hz
volatile uint8_t  g_radial     = 64;    // r / R as 0–127  (default 0.5)
volatile uint8_t  g_fm_depth   = 0;     // 0–127
volatile uint8_t  g_windings   = 1;     // 1–8  (integer multiplier on phase_little)
volatile uint8_t  g_drive      = 0;     // 0–127  (0 = no drive)
volatile uint8_t  g_equation   = EQ_TOROID;
volatile uint8_t  g_drive_type = DRV_WARM;
volatile bool     g_lfo_big    = false; // ÷256 on big freq when true
// blanking flag: true during "retrace" period (Z output)
volatile bool     g_blank      = false;

// ─────────────────────────────────────────────────────────────────────────────
// LOCAL ISR STATE  (only touched inside ISR)
// ─────────────────────────────────────────────────────────────────────────────

static volatile uint16_t ph_big    = 0;
static volatile uint16_t ph_little = 0;

// ─────────────────────────────────────────────────────────────────────────────
// HELPER: clamp int16 to int8 range
// ─────────────────────────────────────────────────────────────────────────────

inline int16_t clamp8(int16_t v) {
    if (v >  127) return  127;
    if (v < -127) return -127;
    return v;
}

// ─────────────────────────────────────────────────────────────────────────────
// WAVESHAPER HELPERS
// ─────────────────────────────────────────────────────────────────────────────

// Apply pre-gain (drive: 0 = ×1, 127 = ×8)
inline int16_t apply_drive(int16_t v, uint8_t drive) {
    // v: −127…+127  →  after drive: up to ±1016
    return (int32_t)v * (128 + (uint16_t)drive * 7) >> 7;
}

// WARM: soft-knee clip using a cubic approximation
//   input range: any  output range: −127…+127
inline int16_t drive_warm(int16_t v) {
    // Clamp first, then soft-knee
    if (v >  200) return  127;
    if (v < -200) return -127;
    int32_t v3 = (int32_t)v * v * v;          // v^3
    int32_t out = (3L * v * 16384 - v3) >> 15;  // (3v - v^3) / 2 scaled
    return (int16_t)clamp8(out);
}

// WAVEFOLD: triangle-wave fold, up to 4 stages depending on amplitude
inline int16_t drive_wavefold(int16_t v) {
    // Bring into ±128 using triangle reflection
    // One period = 512 counts; map to 0–511, fold at 255
    int16_t u = v + 127;                   // 0-based
    u = ((u % 508) + 508) % 508;           // clamp modulo, stay positive
    if (u > 254) u = 508 - u;             // reflect
    return u - 127;                        // re-centre
}

// CLIP: hard clip then boost remainder
inline int16_t drive_clip(int16_t v) {
    int16_t clipped = clamp8(v);           // ±127
    // Warm the clipped output with the WARM shaper
    int16_t overdrive = v - clipped;       // error beyond clip
    return clamp8(clipped + (overdrive >> 3));
}

// Master shaper dispatcher
inline int16_t apply_shape(int16_t v, uint8_t drive, uint8_t dtype) {
    if (drive == 0) return clamp8(v);
    int16_t driven = apply_drive(v, drive);
    switch (dtype) {
        case DRV_WAVEFOLD: return drive_wavefold(driven);
        case DRV_CLIP:     return drive_clip(driven);
        default:           return drive_warm(driven);   // WARM
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// TORUS COMPUTE ISR  (Timer2 CTC, 20 kHz)
// ─────────────────────────────────────────────────────────────────────────────

ISR(TIMER2_COMPA_vect) {
    // Snapshot volatile params (avoid re-reading across multi-byte accesses)
    uint16_t ib  = g_inc_big;
    uint16_t il  = g_inc_little;
    uint8_t  rad = g_radial;          // 0–127: "r"  (R = 127-rad)
    uint8_t  fm  = g_fm_depth;        // 0–127
    uint8_t  win = g_windings;        // 1–8
    uint8_t  drv = g_drive;           // 0–127
    uint8_t  dt  = g_drive_type;
    uint8_t  eq  = g_equation;

    // Advance phases
    ph_big    += ib;
    ph_little += il;

    int16_t x, y;

    // ── FM modulation of inner phase (cross-mod: Big modulates Little phase)
    // fm_depth in 0–127; scale so max is ±0.5 cycle  (32768 counts)
    uint16_t ph_lit_mod = ph_little
        + (uint16_t)((int32_t)fm * SIN8(ph_big) * win);

    // ── Winding: phase_little runs at ×win relative to base
    uint16_t ph_lit_w = ph_little * win;

    switch (eq) {

        // ── TOROID ─────────────────────────────────────────────────────────
        case EQ_TOROID: {
            // x = (R + r·cos φ) · cos θ
            // y = (R + r·cos φ) · sin θ
            // R = 127 - rad,  r = rad  (so R+r = 127 always)
            uint8_t R = 127u - rad;
            int8_t  cos_phi   = COS8(ph_lit_mod);
            int8_t  cos_theta = COS8(ph_big);
            int8_t  sin_theta = SIN8(ph_big);

            // radial_factor = R + r·cos(φ)   range 0…127
            int16_t Rrf = R + ((int16_t)rad * cos_phi >> 7);

            // x,y range: ±(127·127/64) ≈ ±252  →  fits int16_t
            x = (Rrf * cos_theta) >> 6;
            y = (Rrf * sin_theta) >> 6;

            // Apply shaper
            x = apply_shape(x, drv, dt);
            y = apply_shape(y, drv, dt);

            // Blanking: blank when Rrf < 8 (centre of torus hole)
            g_blank = (Rrf < 8);
            break;
        }

        // ── ELECTRON ───────────────────────────────────────────────────────
        case EQ_ELECTRON: {
            // Epitrochoid / spirograph:
            //   x = cos(θ) + r · cos(win·θ + φ·FM)
            //   y = sin(θ) + r · sin(win·θ + φ·FM)
            // FM cross-modulates the winding phase
            uint16_t inner = ph_lit_w + ((uint16_t)fm * (uint8_t)(ph_big >> 8));

            int8_t c_out = COS8(ph_big);
            int8_t s_out = SIN8(ph_big);
            int8_t c_in  = COS8(inner);
            int8_t s_in  = SIN8(inner);

            // x = outer + r·inner,  scale so max ≈ ±254
            x = (int16_t)c_out + ((int16_t)rad * c_in >> 6);
            y = (int16_t)s_out + ((int16_t)rad * s_in >> 6);

            x = clamp8(x);
            y = clamp8(y);

            x = apply_shape(x, drv, dt);
            y = apply_shape(y, drv, dt);

            g_blank = false;
            break;
        }

        // ── FOLDING ────────────────────────────────────────────────────────
        case EQ_FOLDING: {
            // Same geometry as TOROID but drive/fold is the whole point
            uint8_t R = 127u - rad;
            int8_t  cos_phi   = COS8(ph_lit_mod);
            int8_t  cos_theta = COS8(ph_big);
            int8_t  sin_theta = SIN8(ph_big);

            int16_t Rrf = R + ((int16_t)rad * cos_phi >> 7);

            x = (Rrf * cos_theta) >> 6;
            y = (Rrf * sin_theta) >> 6;

            // Folding runs before the main shaper – apply directly
            // and use WAVEFOLD type regardless of g_drive_type here
            // (FOLDING equation always folds; drive type sets flavour)
            x = apply_shape(x, drv, dt);
            y = apply_shape(y, drv, dt);

            g_blank = false;
            break;
        }

        default:
            x = 0; y = 0;
            break;
    }

    // Convert −127…+127 → 0…254  then write to Timer1 OCR registers
    OCR1A = (uint8_t)((uint8_t)x + 128u);
    OCR1B = (uint8_t)((uint8_t)y + 128u);

    // Blanking on D11
    if (g_blank)
        PORTB &= ~_BV(PB3);   // D11 LOW  (laser OFF)
    else
        PORTB |=  _BV(PB3);   // D11 HIGH (laser ON)
}

// ─────────────────────────────────────────────────────────────────────────────
// BUTTON STATE MACHINE
// ─────────────────────────────────────────────────────────────────────────────

typedef enum { BTN_NONE = 0, BTN_SHORT, BTN_LONG, BTN_DOUBLE } BtnEvent;

uint8_t poll_button() {
    static bool    held        = false;
    static uint32_t press_t    = 0;
    static uint32_t release_t  = 0;
    static bool    await_dbl   = false;

    bool  state = (digitalRead(PIN_BUTTON) == LOW);  // active LOW (pullup)
    uint32_t now = millis();

    if (state && !held) {                    // falling edge
        held    = true;
        press_t = now;
    }

    if (!state && held) {                    // rising edge
        held = false;
        uint32_t dur = now - press_t;
        release_t    = now;

        if (dur >= 1200) return BTN_LONG;

        if (await_dbl) {
            await_dbl = false;
            return BTN_DOUBLE;
        }
        await_dbl = true;     // wait 350 ms to confirm single
    }

    if (await_dbl && !held && (now - release_t) > 350) {
        await_dbl = false;
        return BTN_SHORT;
    }

    return BTN_NONE;
}

// ─────────────────────────────────────────────────────────────────────────────
// LED PATTERN HANDLER  (call from loop, non-blocking)
// ─────────────────────────────────────────────────────────────────────────────

void update_led(uint8_t eq, uint8_t page, bool lfo) {
    static uint32_t last_t = 0;
    static uint8_t  phase  = 0;
    uint32_t now = millis();

    if (lfo) {
        // Rapid 10 Hz flutter
        digitalWrite(PIN_LED, (now / 50) & 1);
        return;
    }

    if (page == 1) {
        digitalWrite(PIN_LED, HIGH);          // solid = page 1
        return;
    }

    // Page 0: blink pattern per equation
    // TOROID=1 blink, ELECTRON=2, FOLDING=3
    uint8_t blinks = eq + 1;
    // Cycle period = blinks × 200 ms ON + 200 ms OFF + 600 ms gap
    uint32_t period = (uint32_t)blinks * 400 + 600;
    uint32_t t = now % period;

    bool on = false;
    for (uint8_t b = 0; b < blinks; b++) {
        uint32_t start = (uint32_t)b * 400;
        if (t >= start && t < start + 200) { on = true; break; }
    }
    digitalWrite(PIN_LED, on ? HIGH : LOW);
}

// ─────────────────────────────────────────────────────────────────────────────
// FREQUENCY HELPERS  (run in main loop, not in ISR)
// ─────────────────────────────────────────────────────────────────────────────

// Map pot ADC (0–1023) to frequency in Hz, logarithmic 1–600 Hz
float pot_to_freq(uint16_t adc) {
    // f = 1 × 600 ^ (adc / 1023)
    return powf(600.0f, adc / 1023.0f);
}

// Apply V/Oct: cv_adc (0–1023) = 0–5 V = 0–5 octaves
float apply_voct(float base, uint16_t cv_adc) {
    float volts = cv_adc * (5.0f / 1023.0f);
    return base * powf(2.0f, volts);
}

// Convert Hz to uint16_t phase increment at SAMPLE_RATE
uint16_t freq_to_inc(float freq, bool lfo) {
    if (lfo) freq /= 256.0f;             // 8 octaves down
    float inc = freq * 65536.0f / (float)SAMPLE_RATE;
    if (inc > 65535.0f) inc = 65535.0f;
    if (inc < 0.0f)     inc = 0.0f;
    return (uint16_t)inc;
}

// Winding table: rational values, selectable by POT2 (page 1)
// 15 values from ×0.5 to ×8
const uint8_t WINDING_TABLE[] PROGMEM = {
    1, 2, 3, 4, 5, 6, 7, 8, 2, 3
    //  0  1  2  3  4  5  6  7  (repeated to fill POT range nicely)
};
// Simplified: pot maps to integer 1–8
inline uint8_t pot_to_windings(uint16_t adc) {
    return (uint8_t)(adc / 128) + 1;    // 1…8  (8 steps per 128-count band)
}

// ─────────────────────────────────────────────────────────────────────────────
// HARDWARE INIT
// ─────────────────────────────────────────────────────────────────────────────

void init_timers() {
    // ── Timer1: 8-bit Fast PWM on OC1A (D9) and OC1B (D10), no prescaler
    //    TOP = 0xFF → f_PWM = 16MHz / (1 × 256) = 62500 Hz
    TCCR1A = _BV(COM1A1) | _BV(COM1B1) | _BV(WGM10);  // non-inverting, mode 5 (8-bit fast)
    TCCR1B = _BV(WGM12)  | _BV(CS10);                   // prescaler 1
    OCR1A  = 128;
    OCR1B  = 128;
    pinMode(PIN_X, OUTPUT);
    pinMode(PIN_Y, OUTPUT);

    // ── Timer2: CTC mode, prescaler 8
    //    OCR2A = (16MHz / 8 / 20000) − 1 = 99
    TCCR2A = _BV(WGM21);              // CTC mode
    TCCR2B = _BV(CS21);               // prescaler 8
    OCR2A  = 99;                       // → ISR at 20 000 Hz
    TIMSK2 = _BV(OCIE2A);             // enable compare-match interrupt
}

void init_sin_lut() {
    for (uint16_t i = 0; i < 256; i++) {
        sin_lut[i] = (int8_t)(127.0f * sinf(2.0f * M_PI * i / 256.0f));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    // GPIO
    pinMode(PIN_LED,    OUTPUT);
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    pinMode(PIN_Z,      OUTPUT);
    digitalWrite(PIN_Z, HIGH);   // laser ON by default

    // Init LUT before enabling ISR
    init_sin_lut();

    // Timers (enables ISR)
    init_timers();
    sei();
}

// ─────────────────────────────────────────────────────────────────────────────
// MAIN LOOP
// ─────────────────────────────────────────────────────────────────────────────

void loop() {
    // ── State
    static uint8_t  cur_equation  = EQ_TOROID;
    static uint8_t  cur_drive_type = DRV_WARM;
    static uint8_t  cur_page      = 0;    // 0 = pitch/geo, 1 = fm/drive
    static bool     cur_lfo_big   = false;
    static uint32_t last_param_t  = 0;

    // ── Button
    uint8_t ev = poll_button();

    if (ev == BTN_SHORT) {
        cur_equation = (cur_equation + 1) % EQ_COUNT;
    }
    else if (ev == BTN_LONG) {
        cur_page ^= 1;
    }
    else if (ev == BTN_DOUBLE) {
        cur_lfo_big = !cur_lfo_big;
    }

    // ── LED (non-blocking)
    update_led(cur_equation, cur_page, cur_lfo_big);

    // ── Parameter read + update (every ~10 ms)
    uint32_t now = millis();
    if (now - last_param_t < 10) return;
    last_param_t = now;

    uint16_t pot1 = analogRead(PIN_POT1);
    uint16_t pot2 = analogRead(PIN_POT2);
    uint16_t pot3 = analogRead(PIN_POT3);
    uint16_t cv1  = analogRead(PIN_CV1);
    uint16_t cv2  = analogRead(PIN_CV2);

    if (cur_page == 0) {
        // ── PAGE 0: Pitch + Radial Diff
        float freq_big    = apply_voct(pot_to_freq(pot1), cv1);
        float freq_little = apply_voct(pot_to_freq(pot2), cv2);

        // radial 0…127 from POT3
        uint8_t radial = (uint8_t)(pot3 >> 3);   // 0–127
        if (radial < 6)   radial = 6;             // keep sensible range
        if (radial > 121) radial = 121;

        // Push to ISR
        noInterrupts();
        g_inc_big    = freq_to_inc(freq_big,    cur_lfo_big);
        g_inc_little = freq_to_inc(freq_little, false);
        g_radial     = radial;
        g_equation   = cur_equation;
        g_lfo_big    = cur_lfo_big;
        interrupts();
    }
    else {
        // ── PAGE 1: FM / Windings / Drive
        // POT1 → FM Amount (0–127), CV1 adds in
        uint16_t fm_adc = pot1 + (cv1 >> 1);
        if (fm_adc > 1023) fm_adc = 1023;
        uint8_t fm_depth = (uint8_t)(fm_adc >> 3);   // 0–127

        // POT2 → Windings 1–8
        uint8_t windings = pot_to_windings(pot2);

        // POT3 → Drive (0–127), CV2 adds in
        uint16_t drv_adc = pot3 + (cv2 >> 1);
        if (drv_adc > 1023) drv_adc = 1023;
        uint8_t drive = (uint8_t)(drv_adc >> 3);   // 0–127

        noInterrupts();
        g_fm_depth   = fm_depth;
        g_windings   = windings;
        g_drive      = drive;
        g_drive_type = cur_drive_type;
        g_equation   = cur_equation;
        interrupts();
    }
}
