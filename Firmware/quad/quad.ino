/*
 * MOD1 Tesseract v2 Firmware "octachoron"
 * quad XY laser oscillator (also called 4D hypercube an 8-cell, C8, octachoron, or cubic prism)
 * ================================
 *
 * Outputs rotating geometric shapes projected to 2D for laser display using ILDA Interfacev:
 * - 4D Tesseract (hypercube) with 6 rotation planes
 * - 3D Cube with 3 rotation axes
 * - 2D Square with 1 rotation axis
 *
 * Based on VCV Rack module by kauewerner (Axioma/Tesseract)
 * Adapted for HAGIWO MOD1 hardware with PWM laser output
 *
 * Hardware: Arduino Nano (ATmega328P) on HAGIWO MOD1 PCB
 *
 * Outputs:
 * D9  (PWM Timer1) - X axis for laser/ILDA interface
 * D10 (PWM Timer1) - Y axis for laser/ILDA interface
 * D11 (PWM Timer2) - Blanking/Intensity (configurable polarity)
 *
 * Inputs:
 * A0 (POT1) - Rotation speed control 1
 * A1 (POT2) - Rotation speed control 2
 * A2 (POT3) - Perspective distance / Size
 * A4 (CV)   - Rotation modulation
 * D4        - Button (short press = cycle sub-mode, long press = cycle shape)
 * D3        - LED (status indicator)
 *
 * License: CC0 - Public Domain
 */

#include <Arduino.h>
#include <avr/pgmspace.h>

// ============================================================================
// COMPILE-TIME OPTIONS
// ============================================================================

// Blanking enable/disable
#define BLANKING_ENABLED 1

// Blanking polarity
#define BLANK_ACTIVE_HIGH 1

// Blanking intensity levels (0-255)
#if BLANK_ACTIVE_HIGH
#define BLANK_ON_VALUE 255  // Laser ON (full intensity)
#define BLANK_OFF_VALUE 0   // Laser OFF (blanked)
#else
#define BLANK_ON_VALUE 0     // Laser ON (active low)
#define BLANK_OFF_VALUE 255  // Laser OFF (blanked)
#endif

// Debug output via Serial
#define DEBUG_SERIAL 0

// ============================================================================
// PIN DEFINITIONS (MOD1 / ATmega328P)
// ============================================================================

#define PIN_X_PWM 9   // Timer1 OC1A - X output
#define PIN_Y_PWM 10  // Timer1 OC1B - Y output
#define PIN_BLANK 11  // Timer2 OC2A - Blanking PWM output

#define PIN_LED 3     // Status LED
#define PIN_BUTTON 4  // Mode button

#define PIN_POT1 A0  // Rotation control 1
#define PIN_POT2 A1  // Rotation control 2
#define PIN_POT3 A2  // Perspective / Size
#define PIN_CV A4    // CV modulation input

// ============================================================================
// CONFIGURATION
// ============================================================================

#define POINT_DWELL 3        // Microseconds to dwell on each point (increase to ~50-100 for real lasers)
#define BLANK_DWELL 2        // Microseconds blanking between jumps
#define EDGE_POINTS 8        // Points to interpolate per edge
#define LONG_PRESS_MS 800    // Milliseconds for long button press

// --- NEW FIXES ---
#define PROJECTION_SCALE 0.45f  // Master scale for 3D/4D to prevent screen clipping
#define JUMP_SETTLE_DWELL 60    // Microseconds to wait for mirrors to settle after a blanked jump

// ============================================================================
// SHAPES / MODES
// ============================================================================

enum ShapeType : uint8_t {
  SHAPE_TESSERACT = 0,  // 4D hypercube
  SHAPE_CUBE,           // 3D cube
  SHAPE_SQUARE,         // 2D square
  SHAPE_COUNT
};

static uint8_t currentShape = SHAPE_TESSERACT;
static uint8_t currentSubMode = 0;

// Sub-mode counts per shape
static const uint8_t subModeCounts[SHAPE_COUNT] = {
  5,  // Tesseract: Full, Outer, Inner, Cross, Vertices
  4,  // Cube:      Full, Front+Back, Sides, Vertices
  3   // Square:    Full, Corners only, Vertices
};

// ============================================================================
// TESSERACT (4D) DATA
// ============================================================================

#define TESS_VERTICES 16
#define TESS_EDGES 32

static const int8_t tessVertices[TESS_VERTICES][4] PROGMEM = {
  { -1, -1, -1, 1 }, { 1, -1, -1, 1 }, { 1, 1, -1, 1 }, { -1, 1, -1, 1 },
  { -1, -1, 1, 1 }, { 1, -1, 1, 1 }, { 1, 1, 1, 1 }, { -1, 1, 1, 1 },
  { -1, -1, -1, -1 }, { 1, -1, -1, -1 }, { 1, 1, -1, -1 }, { -1, 1, -1, -1 },
  { -1, -1, 1, -1 }, { 1, -1, 1, -1 }, { 1, 1, 1, -1 }, { -1, 1, 1, -1 }
};

static const uint8_t tessEdges[TESS_EDGES][2] PROGMEM = {
  { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
  { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }, { 8, 9 }, { 9, 10 }, { 10, 11 }, { 11, 8 },
  { 12, 13 }, { 13, 14 }, { 14, 15 }, { 15, 12 }, { 8, 12 }, { 9, 13 }, { 10, 14 }, { 11, 15 },
  { 0, 8 }, { 1, 9 }, { 2, 10 }, { 3, 11 }, { 4, 12 }, { 5, 13 }, { 6, 14 }, { 7, 15 }
};

// ============================================================================
// CUBE (3D) DATA
// ============================================================================

#define CUBE_VERTICES 8
#define CUBE_EDGES 12

static const int8_t cubeVertices[CUBE_VERTICES][3] PROGMEM = {
  { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 }, 
  { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 }
};

static const uint8_t cubeEdges[CUBE_EDGES][2] PROGMEM = {
  { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 }, 
  { 6, 7 }, { 7, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
};

// ============================================================================
// SQUARE (2D) DATA
// ============================================================================

#define SQUARE_VERTICES 4
#define SQUARE_EDGES 4

static const int8_t squareVertices[SQUARE_VERTICES][2] PROGMEM = {
  { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 }
};

static const uint8_t squareEdges[SQUARE_EDGES][2] PROGMEM = {
  { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }
};

// ============================================================================
// RUNTIME STORAGE
// ============================================================================

static float vertices4D[TESS_VERTICES][4];
static float vertices2D[TESS_VERTICES][2];
static float rotationSpeed[6] = { 0, 0, 0, 0, 0, 0 };

// ============================================================================
// BUTTON STATE & TIMING
// ============================================================================

static uint8_t buttonState = HIGH;
static uint8_t lastButtonState = HIGH;
static unsigned long buttonPressTime = 0;
static unsigned long lastDebounceTime = 0;
static bool longPressHandled = false;
static const unsigned long debounceDelay = 50;
static unsigned long lastUpdateTime = 0;
static const unsigned long updateInterval = 10;

// ============================================================================
// PWM INITIALIZATION
// ============================================================================

static void initPWM() {
  TCCR1A = 0; TCCR1B = 0; TCNT1 = 0;
  OCR1A = 127; OCR1B = 127;
  TCCR1A = _BV(COM1A1) | _BV(COM1B1) | _BV(WGM10);
  TCCR1B = _BV(WGM12) | _BV(CS10);

#if BLANKING_ENABLED
  TCCR2A = 0; TCCR2B = 0; TCNT2 = 0;
  OCR2A = BLANK_OFF_VALUE;
  TCCR2A = _BV(COM2A1) | _BV(WGM21) | _BV(WGM20);
  TCCR2B = _BV(CS20);
#endif
}

// ============================================================================
// OUTPUT HELPERS
// ============================================================================

static inline uint8_t floatToPWM(float v) {
  if (v < -1.0f) v = -1.0f;
  if (v > 1.0f) v = 1.0f;
  return (uint8_t)((v + 1.0f) * 127.5f);
}

static inline void setBlank(bool laserOn) {
#if BLANKING_ENABLED
  OCR2A = laserOn ? BLANK_ON_VALUE : BLANK_OFF_VALUE;
#else
  (void)laserOn;
#endif
}

static void outputPoint(float x, float y, bool laserOn) {
  OCR1A = floatToPWM(x);
  OCR1B = floatToPWM(y);
  setBlank(laserOn);
  delayMicroseconds(laserOn ? POINT_DWELL : BLANK_DWELL);
}

// --- UPDATED DRAW LINE LOGIC ---
static void drawLine(float x1, float y1, float x2, float y2) {
  // Move to the start point with laser OFF
  outputPoint(x1, y1, false);
  
  // Wait for the galvo mirrors to physically reach the start point
  delayMicroseconds(JUMP_SETTLE_DWELL);

  // Draw the line with the laser ON
  for (uint8_t i = 0; i <= EDGE_POINTS; i++) {
    const float t = (float)i / (float)EDGE_POINTS;
    const float x = x1 + (x2 - x1) * t;
    const float y = y1 + (y2 - y1) * t;
    outputPoint(x, y, true);
  }
  
  // Anchor the final point slightly to keep corners sharp before moving on
  outputPoint(x2, y2, true);
}

static void drawVertex(float x, float y) {
  outputPoint(x, y, false);
  delayMicroseconds(JUMP_SETTLE_DWELL); // Settle before firing laser
  for (uint8_t i = 0; i < 4; i++) {
    outputPoint(x, y, true);
  }
}

// ============================================================================
// SHAPE INITIALIZATION
// ============================================================================

static void initTesseract() {
  for (uint8_t i = 0; i < TESS_VERTICES; i++) {
    for (uint8_t j = 0; j < 4; j++) {
      vertices4D[i][j] = (float)((int8_t)pgm_read_byte(&tessVertices[i][j]));
    }
  }
}

static void initCube() {
  for (uint8_t i = 0; i < CUBE_VERTICES; i++) {
    for (uint8_t j = 0; j < 3; j++) {
      vertices4D[i][j] = (float)((int8_t)pgm_read_byte(&cubeVertices[i][j]));
    }
    vertices4D[i][3] = 0.0f; 
  }
  for (uint8_t i = CUBE_VERTICES; i < TESS_VERTICES; i++) {
    for (uint8_t j = 0; j < 4; j++) vertices4D[i][j] = 0.0f;
  }
}

static void initSquare() {
  for (uint8_t i = 0; i < SQUARE_VERTICES; i++) {
    vertices4D[i][0] = (float)((int8_t)pgm_read_byte(&squareVertices[i][0]));
    vertices4D[i][1] = (float)((int8_t)pgm_read_byte(&squareVertices[i][1]));
    vertices4D[i][2] = 0.0f;
    vertices4D[i][3] = 0.0f;
  }
  for (uint8_t i = SQUARE_VERTICES; i < TESS_VERTICES; i++) {
    for (uint8_t j = 0; j < 4; j++) vertices4D[i][j] = 0.0f;
  }
}

static void initCurrentShape() {
  switch (currentShape) {
    case SHAPE_TESSERACT: initTesseract(); break;
    case SHAPE_CUBE: initCube(); break;
    case SHAPE_SQUARE: initSquare(); break;
    default: initTesseract(); break;
  }
}

// ============================================================================
// ROTATION
// ============================================================================

static void rotateVertexPlane(uint8_t idx, uint8_t axis1, uint8_t axis2, float angle) {
  const float c = cos(angle);
  const float s = sin(angle);
  const float a = vertices4D[idx][axis1];
  const float b = vertices4D[idx][axis2];
  vertices4D[idx][axis1] = a * c - b * s;
  vertices4D[idx][axis2] = a * s + b * c;
}

// ============================================================================
// PROJECTION (WITH SCALING FIX)
// ============================================================================

static void projectTesseract(uint8_t idx, float distance) {
  float x = vertices4D[idx][0];
  float y = vertices4D[idx][1];
  float z = vertices4D[idx][2];
  float w = vertices4D[idx][3];

  const float wScale = 2.0f / (2.0f - w * 0.3f);
  x *= wScale; y *= wScale; z *= wScale;

  float div = distance - z;
  if (div < 0.5f) div = 0.5f;

  vertices2D[idx][0] = (x / div) * PROJECTION_SCALE;
  vertices2D[idx][1] = (y / div) * PROJECTION_SCALE;
}

static void projectCube(uint8_t idx, float distance) {
  const float x = vertices4D[idx][0];
  const float y = vertices4D[idx][1];
  const float z = vertices4D[idx][2];

  float div = distance - z;
  if (div < 0.5f) div = 0.5f;

  vertices2D[idx][0] = (x / div) * PROJECTION_SCALE;
  vertices2D[idx][1] = (y / div) * PROJECTION_SCALE;
}

static void projectSquare(uint8_t idx, float scale) {
  vertices2D[idx][0] = vertices4D[idx][0] * scale;
  vertices2D[idx][1] = vertices4D[idx][1] * scale;
}

// ============================================================================
// EDGE VISIBILITY (SUB-MODES)
// ============================================================================

static bool shouldDrawTessEdge(uint8_t edgeIdx) {
  switch (currentSubMode) {
    case 0: return true;                             
    case 1: return (edgeIdx < 12);                   
    case 2: return (edgeIdx >= 12 && edgeIdx < 24);  
    case 3: return (edgeIdx >= 24);                  
    case 4: return false;                            
    default: return true;
  }
}

static bool shouldDrawCubeEdge(uint8_t edgeIdx) {
  switch (currentSubMode) {
    case 0: return true;            
    case 1: return (edgeIdx < 8);   
    case 2: return (edgeIdx >= 8);  
    case 3: return false;           
    default: return true;
  }
}

static bool shouldDrawSquareEdge(uint8_t edgeIdx) {
  switch (currentSubMode) {
    case 0: return true;                            
    case 1: return (edgeIdx == 0 || edgeIdx == 2);  
    case 2: return false;                           
    default: return true;
  }
}

// ============================================================================
// DRAWING
// ============================================================================

static void drawTesseract() {
  if (currentSubMode == 4) {
    for (uint8_t i = 0; i < TESS_VERTICES; i++) {
      drawVertex(vertices2D[i][0], vertices2D[i][1]);
    }
    return;
  }
  for (uint8_t e = 0; e < TESS_EDGES; e++) {
    if (!shouldDrawTessEdge(e)) continue;
    const uint8_t v1 = pgm_read_byte(&tessEdges[e][0]);
    const uint8_t v2 = pgm_read_byte(&tessEdges[e][1]);
    drawLine(vertices2D[v1][0], vertices2D[v1][1], vertices2D[v2][0], vertices2D[v2][1]);
  }
}

static void drawCube() {
  if (currentSubMode == 3) {
    for (uint8_t i = 0; i < CUBE_VERTICES; i++) {
      drawVertex(vertices2D[i][0], vertices2D[i][1]);
    }
    return;
  }
  for (uint8_t e = 0; e < CUBE_EDGES; e++) {
    if (!shouldDrawCubeEdge(e)) continue;
    const uint8_t v1 = pgm_read_byte(&cubeEdges[e][0]);
    const uint8_t v2 = pgm_read_byte(&cubeEdges[e][1]);
    drawLine(vertices2D[v1][0], vertices2D[v1][1], vertices2D[v2][0], vertices2D[v2][1]);
  }
}

static void drawSquare() {
  if (currentSubMode == 2) {
    for (uint8_t i = 0; i < SQUARE_VERTICES; i++) {
      drawVertex(vertices2D[i][0], vertices2D[i][1]);
    }
    return;
  }
  for (uint8_t e = 0; e < SQUARE_EDGES; e++) {
    if (!shouldDrawSquareEdge(e)) continue;
    const uint8_t v1 = pgm_read_byte(&squareEdges[e][0]);
    const uint8_t v2 = pgm_read_byte(&squareEdges[e][1]);
    drawLine(vertices2D[v1][0], vertices2D[v1][1], vertices2D[v2][0], vertices2D[v2][1]);
  }
}

static void drawCurrentShape() {
  switch (currentShape) {
    case SHAPE_TESSERACT: drawTesseract(); break;
    case SHAPE_CUBE: drawCube(); break;
    case SHAPE_SQUARE: drawSquare(); break;
    default: drawTesseract(); break;
  }
}

// ============================================================================
// INPUTS & UPDATE
// ============================================================================

static void readInputs() {
  const float pot1 = analogRead(PIN_POT1) / 1023.0f;
  const float pot2 = analogRead(PIN_POT2) / 1023.0f;
  const float cv = analogRead(PIN_CV) / 1023.0f;

  switch (currentShape) {
    case SHAPE_TESSERACT:
      rotationSpeed[0] = pot1 * pot1 * 0.05f;                    
      rotationSpeed[1] = (1.0f - pot1) * (1.0f - pot1) * 0.03f;  
      rotationSpeed[2] = pot2 * pot2 * 0.04f;                    
      rotationSpeed[4] = (1.0f - pot2) * (1.0f - pot2) * 0.02f;  
      rotationSpeed[3] = cv * cv * 0.03f;                        
      rotationSpeed[5] = (1.0f - cv) * 0.02f;                    
      break;
    case SHAPE_CUBE:
      rotationSpeed[0] = pot1 * pot1 * 0.06f;  
      rotationSpeed[1] = pot2 * pot2 * 0.05f;  
      rotationSpeed[2] = cv * cv * 0.04f;      
      rotationSpeed[3] = 0.0f; rotationSpeed[4] = 0.0f; rotationSpeed[5] = 0.0f;
      break;
    case SHAPE_SQUARE:
      rotationSpeed[0] = pot1 * pot1 * 0.08f;  
      rotationSpeed[1] = 0.0f; rotationSpeed[2] = 0.0f; rotationSpeed[3] = 0.0f;
      rotationSpeed[4] = 0.0f; rotationSpeed[5] = 0.0f;
      break;
  }
}

static float getDistanceOrScale() {
  const float pot3 = analogRead(PIN_POT3) / 1023.0f;
  switch (currentShape) {
    case SHAPE_TESSERACT:
    case SHAPE_CUBE:
      return 1.5f + pot3 * 3.5f;  
    case SHAPE_SQUARE:
      return 0.3f + pot3 * 0.6f;  
    default:
      return 3.0f;
  }
}

static void updateRotationAndProjection() {
  const float distOrScale = getDistanceOrScale();
  switch (currentShape) {
    case SHAPE_TESSERACT:
      for (uint8_t i = 0; i < TESS_VERTICES; i++) {
        if (rotationSpeed[0] > 0.0001f) rotateVertexPlane(i, 0, 1, rotationSpeed[0]);  
        if (rotationSpeed[1] > 0.0001f) rotateVertexPlane(i, 1, 2, rotationSpeed[1]);  
        if (rotationSpeed[2] > 0.0001f) rotateVertexPlane(i, 0, 2, rotationSpeed[2]);  
        if (rotationSpeed[3] > 0.0001f) rotateVertexPlane(i, 1, 3, rotationSpeed[3]);  
        if (rotationSpeed[4] > 0.0001f) rotateVertexPlane(i, 0, 3, rotationSpeed[4]);  
        if (rotationSpeed[5] > 0.0001f) rotateVertexPlane(i, 2, 3, rotationSpeed[5]);  
        projectTesseract(i, distOrScale);
      }
      break;
    case SHAPE_CUBE:
      for (uint8_t i = 0; i < CUBE_VERTICES; i++) {
        if (rotationSpeed[0] > 0.0001f) rotateVertexPlane(i, 0, 1, rotationSpeed[0]);  
        if (rotationSpeed[1] > 0.0001f) rotateVertexPlane(i, 0, 2, rotationSpeed[1]);  
        if (rotationSpeed[2] > 0.0001f) rotateVertexPlane(i, 1, 2, rotationSpeed[2]);  
        projectCube(i, distOrScale);
      }
      break;
    case SHAPE_SQUARE:
      for (uint8_t i = 0; i < SQUARE_VERTICES; i++) {
        if (rotationSpeed[0] > 0.0001f) rotateVertexPlane(i, 0, 1, rotationSpeed[0]);  
        projectSquare(i, distOrScale);
      }
      break;
  }
}

// ============================================================================
// BUTTON HANDLING
// ============================================================================

static void flashLED(uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    digitalWrite(PIN_LED, HIGH); delay(80);
    digitalWrite(PIN_LED, LOW); delay(80);
  }
}

static void handleButton() {
  const uint8_t reading = digitalRead(PIN_BUTTON);
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        buttonPressTime = millis();
        longPressHandled = false;
      } else {
        if (!longPressHandled) {
          currentSubMode = (currentSubMode + 1) % subModeCounts[currentShape];
          flashLED(currentSubMode + 1);
        }
      }
    }
    if (buttonState == LOW && !longPressHandled) {
      if ((millis() - buttonPressTime) > LONG_PRESS_MS) {
        longPressHandled = true;
        currentShape = (currentShape + 1) % SHAPE_COUNT;
        currentSubMode = 0;
        initCurrentShape();
        digitalWrite(PIN_LED, HIGH); delay(300);
        digitalWrite(PIN_LED, LOW); delay(100);
        flashLED(currentShape + 1);
      }
    }
  }
  lastButtonState = reading;
}

// ============================================================================
// SETUP / LOOP
// ============================================================================

void setup() {
  pinMode(PIN_X_PWM, OUTPUT);
  pinMode(PIN_Y_PWM, OUTPUT);
  pinMode(PIN_BLANK, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  initPWM();
  initCurrentShape();

  const float dist = getDistanceOrScale();
  for (uint8_t i = 0; i < TESS_VERTICES; i++) {
    projectTesseract(i, dist);
  }

#if DEBUG_SERIAL
  Serial.begin(115200);
  Serial.println(F("MOD1 Tesseract Laser v2"));
  Serial.print(F("Blanking: ")); Serial.println(BLANKING_ENABLED ? F("Enabled") : F("Disabled"));
  Serial.print(F("Blank polarity: ")); Serial.println(BLANK_ACTIVE_HIGH ? F("Active HIGH") : F("Active LOW"));
#endif

  digitalWrite(PIN_LED, HIGH);
  delay(500);
  digitalWrite(PIN_LED, LOW);
}

void loop() {
  handleButton();
  const unsigned long now = millis();
  if (now - lastUpdateTime >= updateInterval) {
    lastUpdateTime = now;
    readInputs();
    updateRotationAndProjection();
    static uint8_t ledCounter = 0;
    if (++ledCounter >= 50) {
      ledCounter = 0;
      digitalWrite(PIN_LED, !digitalRead(PIN_LED));
    }
  }
  drawCurrentShape();
}
