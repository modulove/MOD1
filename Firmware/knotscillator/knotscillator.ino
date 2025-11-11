/*
 * KNOSCILLATOR 
 * 
 * 
 * Three beautiful knot patterns:
 * 0. Trefoil Knot
 * 1. Lissajous Curve  
 * 2. Torus Knot
 * 
 * View on oscilloscope in X-Y mode!
 * 
 */

// ═════════════════════════════════════════════════════════════════
// PIN DEFINITIONS
// ═════════════════════════════════════════════════════════════════
#define POT1 A0  // Knot selection
#define POT2 A1  // Waveform variation
#define POT3 A2  // Frequency
#define CV1 A3   // CV input - parameter based on mode
#define LED_PIN 3
#define BUTTON_PIN 4

// ═════════════════════════════════════════════════════════════════
// STATE VARIABLES
// ═════════════════════════════════════════════════════════════════
volatile uint32_t phase = 0;
volatile uint32_t phaseInc = 0;
volatile uint8_t knotType = 0;     // 0, 1, or 2
volatile uint8_t scale = 128;       // Zoom amount
uint8_t cvMode = 0;                 // 0=knot selection, 1=scale, 2=frequency
bool buttonHeld = false;

// ═════════════════════════════════════════════════════════════════
// TREFOIL KNOT - 2D PROJECTION (X-Y)
// ═════════════════════════════════════════════════════════════════
// Pre-projected 3D trefoil to 2D plane
const uint8_t PROGMEM trefoilLUT_X[256] = {
  128,135,142,149,156,163,169,175,180,185,189,192,194,196,196,196,
  194,192,189,185,180,175,169,163,156,149,142,135,128,121,114,107,
  100,93,87,81,76,71,67,64,62,60,60,60,62,64,67,71,
  76,81,87,93,100,107,114,121,128,135,142,149,156,163,169,175,
  180,185,189,192,194,196,196,196,194,192,189,185,180,175,169,163,
  156,149,142,135,128,121,114,107,100,93,87,81,76,71,67,64,
  62,60,60,60,62,64,67,71,76,81,87,93,100,107,114,121,
  128,135,142,149,156,163,169,175,180,185,189,192,194,196,196,196,
  194,192,189,185,180,175,169,163,156,149,142,135,128,121,114,107,
  100,93,87,81,76,71,67,64,62,60,60,60,62,64,67,71,
  76,81,87,93,100,107,114,121,128,135,142,149,156,163,169,175,
  180,185,189,192,194,196,196,196,194,192,189,185,180,175,169,163,
  156,149,142,135,128,121,114,107,100,93,87,81,76,71,67,64,
  62,60,60,60,62,64,67,71,76,81,87,93,100,107,114,121,
  128,135,142,149,156,163,169,175,180,185,189,192,194,196,196,196,
  194,192,189,185,180,175,169,163,156,149,142,135,128,121,114,107
};

const uint8_t PROGMEM trefoilLUT_Y[256] = {
  128,121,114,108,102,97,92,88,85,83,81,81,81,82,84,87,
  90,94,99,104,110,116,123,130,137,144,150,157,163,168,173,177,
  180,183,184,185,185,183,181,178,174,169,164,158,151,144,137,129,
  121,113,105,97,90,83,77,71,66,62,59,57,56,56,57,59,
  62,66,71,77,83,90,97,105,113,121,129,137,144,151,158,164,
  169,174,178,181,183,185,185,184,183,180,177,173,168,163,157,150,
  144,137,130,123,116,110,104,99,94,90,87,84,82,81,81,81,
  83,85,88,92,97,102,108,114,121,128,135,142,148,154,159,164,
  168,171,174,175,176,176,174,172,169,165,160,155,149,142,135,128,
  121,114,108,102,97,92,88,85,83,81,81,81,82,84,87,90,
  94,99,104,110,116,123,130,137,144,150,157,163,168,173,177,180,
  183,184,185,185,183,181,178,174,169,164,158,151,144,137,129,121,
  113,105,97,90,83,77,71,66,62,59,57,56,56,57,59,62,
  66,71,77,83,90,97,105,113,121,129,137,144,151,158,164,169,
  174,178,181,183,185,185,184,183,180,177,173,168,163,157,150,144,
  137,130,123,116,110,104,99,94,90,87,84,82,81,81,81,83
};

// ═════════════════════════════════════════════════════════════════
// LISSAJOUS CURVE - 2D (3:2 ratio)
// ═════════════════════════════════════════════════════════════════
const uint8_t PROGMEM lissajousLUT_X[256] = {
  128,131,134,137,140,143,146,149,152,155,158,161,164,167,170,173,
  176,179,182,184,187,190,192,195,197,199,201,203,205,207,209,210,
  212,213,214,215,216,217,218,218,219,219,219,219,219,219,218,218,
  217,216,215,214,213,212,210,209,207,205,203,201,199,197,195,192,
  190,187,184,182,179,176,173,170,167,164,161,158,155,152,149,146,
  143,140,137,134,131,128,125,122,119,116,113,110,107,104,101,98,
  95,92,89,86,83,80,77,74,72,69,66,64,61,59,57,55,
  53,51,49,47,46,44,43,42,41,40,39,38,38,37,37,37,
  37,37,37,38,38,39,40,41,42,43,44,46,47,49,51,53,
  55,57,59,61,64,66,69,72,74,77,80,83,86,89,92,95,
  98,101,104,107,110,113,116,119,122,125,128,131,134,137,140,143,
  146,149,152,155,158,161,164,167,170,173,176,179,182,184,187,190,
  192,195,197,199,201,203,205,207,209,210,212,213,214,215,216,217,
  218,218,219,219,219,219,219,219,218,218,217,216,215,214,213,212,
  210,209,207,205,203,201,199,197,195,192,190,187,184,182,179,176,
  173,170,167,164,161,158,155,152,149,146,143,140,137,134,131,128
};

const uint8_t PROGMEM lissajousLUT_Y[256] = {
  128,134,140,146,152,158,164,170,176,181,187,192,197,202,207,211,
  215,219,223,226,229,232,234,236,238,239,240,241,241,241,241,240,
  239,237,235,233,230,227,224,220,216,212,207,203,198,193,187,182,
  176,170,164,158,152,146,140,134,128,122,116,110,104,98,92,86,
  80,75,69,64,59,54,49,45,41,37,33,30,27,24,22,20,
  18,17,16,15,15,15,15,16,17,19,21,23,26,29,32,36,
  40,44,49,53,58,63,69,74,80,86,92,98,104,110,116,122,
  128,134,140,146,152,158,164,170,176,181,187,192,197,202,207,211,
  215,219,223,226,229,232,234,236,238,239,240,241,241,241,241,240,
  239,237,235,233,230,227,224,220,216,212,207,203,198,193,187,182,
  176,170,164,158,152,146,140,134,128,122,116,110,104,98,92,86,
  80,75,69,64,59,54,49,45,41,37,33,30,27,24,22,20,
  18,17,16,15,15,15,15,16,17,19,21,23,26,29,32,36,
  40,44,49,53,58,63,69,74,80,86,92,98,104,110,116,122,
  128,134,140,146,152,158,164,170,176,181,187,192,197,202,207,211,
  215,219,223,226,229,232,234,236,238,239,240,241,241,241,241,240
};

// ═════════════════════════════════════════════════════════════════
// TORUS KNOT - 2D PROJECTION (3:2)
// ═════════════════════════════════════════════════════════════════
const uint8_t PROGMEM torusLUT_X[256] = {
  188,189,189,188,186,183,179,174,168,161,153,145,136,126,116,106,
  96,85,75,65,55,46,37,29,22,15,9,4,1,0,0,2,
  6,11,18,26,35,45,56,68,80,93,106,119,132,145,157,169,
  180,190,199,207,214,219,223,226,227,227,225,222,217,211,204,196,
  187,177,166,155,143,131,119,107,96,85,74,65,56,48,41,35,
  30,27,25,24,25,28,32,37,44,52,61,71,82,93,105,117,
  129,141,153,164,175,185,194,202,209,214,218,221,222,222,220,217,
  212,206,199,191,182,172,161,150,139,127,116,105,94,84,75,67,
  59,53,48,44,41,40,40,42,45,49,55,62,70,79,88,98,
  109,119,130,140,151,161,170,179,187,194,200,205,208,210,211,210,
  208,205,200,194,187,179,170,161,151,140,130,119,109,98,88,79,
  70,62,55,49,45,42,40,40,41,44,48,53,59,67,75,84,
  94,105,116,127,139,150,161,172,182,191,199,206,212,217,220,222,
  222,221,218,214,209,202,194,185,175,164,153,141,129,117,105,93,
  82,71,61,52,44,37,32,28,25,24,25,27,30,35,41,48,
  56,65,74,85,96,107,119,131,143,155,166,177,187,196,204,211
};

const uint8_t PROGMEM torusLUT_Y[256] = {
  128,142,156,169,182,194,205,215,224,231,237,241,243,244,243,240,
  235,229,221,211,200,188,175,161,147,133,118,104,90,77,64,53,
  42,33,25,19,14,11,9,9,11,14,19,25,33,42,52,64,
  76,90,104,118,132,146,160,173,186,198,209,219,227,234,239,242,
  244,244,242,238,233,226,217,207,196,184,171,158,144,131,117,104,
  92,80,69,59,50,43,37,32,29,28,28,30,34,39,46,54,
  63,74,85,97,110,123,136,149,162,174,186,197,207,216,223,229,
  233,236,237,237,235,231,226,219,211,202,192,181,169,157,144,132,
  119,107,95,84,73,64,56,49,43,39,37,36,36,38,42,47,
  53,61,70,80,91,102,114,127,139,152,164,176,187,197,206,214,
  221,226,230,232,233,232,230,226,221,214,206,197,187,176,164,152,
  139,127,114,102,91,80,70,61,53,47,42,38,36,36,37,39,
  43,49,56,64,73,84,95,107,119,132,144,157,169,181,192,202,
  211,219,226,231,235,237,237,236,233,229,223,216,207,197,186,174,
  162,149,136,123,110,97,85,74,63,54,46,39,34,30,28,28,
  29,32,37,43,50,59,69,80,92,104,117,131,144,158,171,184
};

// ═════════════════════════════════════════════════════════════════
// TABLE POINTERS
// ═════════════════════════════════════════════════════════════════
const uint8_t* const PROGMEM knotTablesX[3] = {
  trefoilLUT_X, lissajousLUT_X, torusLUT_X
};
const uint8_t* const PROGMEM knotTablesY[3] = {
  trefoilLUT_Y, lissajousLUT_Y, torusLUT_Y
};

// ═════════════════════════════════════════════════════════════════
// ULTRA SIMPLE SAMPLE GENERATION - JUST TWO TABLE READS!
// ═════════════════════════════════════════════════════════════════
inline void __attribute__((always_inline)) generateSample() {
  // Get phase index (0-255)
  uint8_t t = phase >> 24;
  
  // Lookup tables for current knot
  const uint8_t* tableX = (const uint8_t*)pgm_read_ptr(&knotTablesX[knotType]);
  const uint8_t* tableY = (const uint8_t*)pgm_read_ptr(&knotTablesY[knotType]);
  
  // Read values
  uint8_t x = pgm_read_byte(&tableX[t]);
  uint8_t y = pgm_read_byte(&tableY[t]);
  
  // Apply scale (zoom)
  int16_t xScaled = ((int16_t)(x - 128) * scale >> 7) + 128;
  int16_t yScaled = ((int16_t)(y - 128) * scale >> 7) + 128;
  
  // Output
  OCR1A = constrain(xScaled, 0, 255);
  OCR1B = constrain(yScaled, 0, 255);
}

// ═════════════════════════════════════════════════════════════════
// TIMER1 ISR - AUDIO RATE (62.5kHz)
// ═════════════════════════════════════════════════════════════════
ISR(TIMER1_OVF_vect) {
  phase += phaseInc;
  generateSample();
}

// ═════════════════════════════════════════════════════════════════
// CONTROL UPDATE
// ═════════════════════════════════════════════════════════════════
void updateControls() {
  static uint8_t counter = 0;
  counter++;
  
  switch(counter & 0x07) {
    case 0: {
      // POT1: Knot selection (only if CV1 not in knot mode)
      int val = analogRead(POT1);
      if (cvMode != 0) {  // CV not controlling knot
        knotType = constrain(val / 342, 0, 2);  // 1024/3 = 342
      }
      break;
    }
    
    case 1: {
      // POT2: Scale/Zoom (only if CV1 not in scale mode)
      int val = analogRead(POT2);
      if (cvMode != 1) {  // CV not controlling scale
        scale = map(val, 0, 1023, 64, 255);  // 0.5x to 2x zoom
      }
      break;
    }
    
    case 2: {
      // POT3: Frequency (only if CV1 not in frequency mode)
      if (cvMode != 2) {  // CV not controlling frequency
        int val = analogRead(POT3);
        float freq = 0.1f + (val * 19.9f / 1023.0f);
        phaseInc = (uint32_t)(((uint64_t)freq * 68719477ULL) >> 10);
      }
      break;
    }
    
    case 3: {
      // CV1: Parameter based on cvMode
      int val = analogRead(CV1);
      if (val > 50) {  // Only if CV plugged in
        switch(cvMode) {
          case 0:  // Knot selection
            knotType = constrain(val / 342, 0, 2);
            break;
          case 1:  // Scale/Zoom
            scale = map(val, 0, 1023, 64, 255);
            break;
          case 2:  // Frequency
            float freq = 0.1f + (val * 19.9f / 1023.0f);
            phaseInc = (uint32_t)(((uint64_t)freq * 68719477ULL) >> 10);
            break;
        }
      }
      break;
    }
  }
}

// ═════════════════════════════════════════════════════════════════
// LED UPDATE - Shows CV mode
// ═════════════════════════════════════════════════════════════════
void updateLED() {
  static uint16_t blinkCounter = 0;
  blinkCounter++;
  
  // LED blink pattern indicates CV mode
  // Mode 0 (knot): slow blink (1 blink)
  // Mode 1 (scale): medium blink (2 blinks)  
  // Mode 2 (freq): fast blink (3 blinks)
  
  uint16_t period = 100;  // 100 x 10ms = 1 second
  uint16_t phase = blinkCounter % period;
  
  bool ledOn = false;
  
  switch(cvMode) {
    case 0:  // 1 blink
      ledOn = (phase < 10);
      break;
    case 1:  // 2 blinks
      ledOn = (phase < 10) || (phase >= 20 && phase < 30);
      break;
    case 2:  // 3 blinks
      ledOn = (phase < 10) || (phase >= 20 && phase < 30) || (phase >= 40 && phase < 50);
      break;
  }
  
  digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
}

// ═════════════════════════════════════════════════════════════════
// BUTTON HANDLER - Short press cycles knots, long press cycles CV mode
// ═════════════════════════════════════════════════════════════════
void handleButton() {
  static uint8_t lastButton = HIGH;
  static unsigned long pressStart = 0;
  static bool longPressHandled = false;
  
  uint8_t reading = digitalRead(BUTTON_PIN);
  
  // Button pressed
  if (reading == LOW && lastButton == HIGH) {
    pressStart = millis();
    longPressHandled = false;
  }
  
  // Button held - check for long press (500ms)
  if (reading == LOW && !longPressHandled) {
    if (millis() - pressStart >= 500) {
      // Long press - cycle CV mode
      cvMode = (cvMode + 1) % 3;
      longPressHandled = true;
      
      // Long flash to indicate mode change
      for(int i = 0; i < cvMode + 1; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(150);
        digitalWrite(LED_PIN, LOW);
        delay(150);
      }
    }
  }
  
  // Button released
  if (reading == HIGH && lastButton == LOW) {
    if (!longPressHandled && millis() - pressStart < 500) {
      // Short press - cycle knot type
      knotType = (knotType + 1) % 3;
      
      // Quick flash
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(50);
    }
  }
  
  lastButton = reading;
}

// ═════════════════════════════════════════════════════════════════
// SETUP
// ═════════════════════════════════════════════════════════════════
void setup() {
  pinMode(9, OUTPUT);
  pinMode(10, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // Startup sequence
  for(int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
  
  // Timer1 setup - Fast PWM, 8-bit
  cli();
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  TCCR1A = (1<<COM1A1) | (1<<COM1B1) | (1<<WGM10);
  TCCR1B = (1<<WGM12) | (1<<CS10);
  OCR1A = 128;
  OCR1B = 128;
  TIMSK1 = (1<<TOIE1);
  sei();
  
  // Defaults
  knotType = 0;
  scale = 128;
  cvMode = 0;  // Start with CV controlling knot selection
  phaseInc = (uint32_t)(((uint64_t)1 * 68719477ULL) >> 10);  // 1Hz
  
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  
  updateControls();
}

// ═════════════════════════════════════════════════════════════════
// MAIN LOOP
// ═════════════════════════════════════════════════════════════════
void loop() {
  static unsigned long last = 0;
  unsigned long now = millis();
  
  if (now - last >= 10) {
    last = now;
    updateControls();
    handleButton();
    updateLED();
  }
}

/*
 * ═══════════════════════════════════════════════════════════════
 * KNOSCILLATOR - CV MODE SWITCHING VERSION
 * ═══════════════════════════════════════════════════════════════
 * 
 * CONTROLS:
 * POT1: Knot selection (0=Trefoil, 1=Lissajous, 2=Torus) - unless CV mode 0
 * POT2: Zoom/Scale (0.5x to 2x) - unless CV mode 1
 * POT3: Frequency (0.1Hz - 20Hz) - unless CV mode 2
 * CV1 (A3): Modulates parameter based on CV mode
 * 
 * BUTTON:
 * - Short press: Cycle knot types
 * - Long press (500ms): Cycle CV modulation mode
 * 
 * CV MODES:
 * Mode 0: CV1 controls knot selection (LED: 1 blink)
 * Mode 1: CV1 controls scale/zoom (LED: 2 blinks)
 * Mode 2: CV1 controls frequency (LED: 3 blinks)
 * 
 * LED INDICATOR:
 * Blinks in pattern to show CV mode (count the blinks!)
 * 
 * When CV is plugged into CV1, it overrides the corresponding pot.
 * When no CV is present (reading < 50), the pot controls the parameter.
 * 
 */
