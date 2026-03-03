/*
QUANTUM WALK - Quantum-Inspired Random CV Generator for HAGIWO MOD1
by Rob Heel, enhanced with quantum mechanics-inspired behavior

Quantum Features:
- Quantum Tunneling: Rare large jumps outside normal distribution
- Dual Mode: Classic Quantum Walk vs. Gravity Well (potential energy)
- Entangled Outputs: F2 and F4 create correlated but independent CV paths

Hardware Configuration:
- Potentiometer 1 (Rate)         → A0
- Potentiometer 2 (Bias/Offset)  → A1
- Potentiometer 3 (Quantum Flux) → A2 (was ChaosDepth)

- LED Indicator                  → Pin 3 (OCR2B) - Brightness = mode indication
- Push Button                    → Pin 4 - Toggle modes

Inputs/Outputs:
- F1    A3  CV input (Entanglement - controls correlation between F2/F4)
- F2    D9  Lagged Output (quantum-entangled follower)
- F3    A5  CV input (adds to Quantum Flux)
- F4    D11 Primary Quantum Walk Output

Modes (Button Toggle):
- Classic Quantum Walk: Free random drift with quantum tunneling events
- Gravity Well Mode: Pull toward equilibrium (0) - like a potential well
*/

#include <EEPROM.h>
#include <Arduino.h>

// ---------------- Quantum Parameters ----------------
static const unsigned int TABLE_SIZE = 1024;
static const unsigned long UPDATE_INTERVAL_US = 400;

// Quantum tunneling parameters
const float QUANTUM_TUNNEL_PROBABILITY = 5.0f; // 5% chance per step
const float QUANTUM_TUNNEL_MULTIPLIER = 3.5f;  // Jump 3.5x larger
const float SUPERPOSITION_DECAY = 0.995f;      // For gravity mode

uint8_t waveTable[TABLE_SIZE];

// Quantum state variables
float walkPhase = 0.5f;      // Start at equilibrium
float laggedPhase = 0.5f;    // Entangled particle state
float quantumFlux = 0.0;     // Fluctuation amplitude
float rate = 0.001f;
float bias = 0.0f;

// Entanglement parameters
float baseEntanglement = 0.9995f;
float entanglement = 0.9995f;

// Mode state
bool gravityWellMode = false;
unsigned long lastButtonPress = 0;
const unsigned long DEBOUNCE_TIME = 200;

// Visual feedback
uint8_t ledPulse = 0;
bool modeJustChanged = false;

void setup() {
  pinMode(11, OUTPUT);         // F4 - Primary output
  pinMode(9, OUTPUT);          // F2 - Entangled output
  pinMode(3, OUTPUT);          // LED
  pinMode(4, INPUT_PULLUP);    // Mode button

  configurePWM();
  
  // Initialize in superposition (middle state)
  walkPhase = 0.5f;
  laggedPhase = 0.5f;
  
  // Flash LED on startup
  for(int i = 0; i < 3; i++) {
    OCR2B = 255;
    delay(100);
    OCR2B = 0;
    delay(100);
  }
}

void loop() {
  // Read quantum control parameters
  rate = readFrequency(A0);
  // In loop(), replace the quantumFlux calculation:
float rawFlux = (analogRead(A2) / 1023.0f) + (analogRead(A5) / 1023.0f);
rawFlux = constrain(rawFlux, 0.0f, 1.0f);

// Apply curve for finer control in the lower range
quantumFlux = rawFlux * rawFlux * 0.5f;  // Squared curve, halved max
  
  bias = (analogRead(A1) / 1023.0f) * 0.8f - 0.4f; // -0.4 to +0.4 offset

  // F1 CV controls entanglement (correlation between outputs)
  float entanglementCV = analogRead(A3) / 1023.0f;
  entanglement = baseEntanglement - (entanglementCV * 0.015f);
  entanglement = constrain(entanglement, 0.98f, 0.9995f);

  // Handle mode button with debouncing
  if (digitalRead(4) == LOW && (millis() - lastButtonPress > DEBOUNCE_TIME)) {
    gravityWellMode = !gravityWellMode;
    lastButtonPress = millis();
    modeJustChanged = true;
    ledPulse = 255; // Flash LED on mode change
  }

  // Quantum walk evolution
  if (gravityWellMode) {
    updateGravityWell(walkPhase, rate, quantumFlux);
  } else {
    updateQuantumWalk(walkPhase, rate, quantumFlux);
  }

  // Update entangled output
  updateEntangledOutput(walkPhase, laggedPhase, entanglement);

  // Apply bias and constrain
  int primaryOutput = (int)((walkPhase + bias) * 255.0f);
  primaryOutput = constrain(primaryOutput, 0, 255);
  
  int entangledOutput = (int)((laggedPhase + bias) * 255.0f);
  entangledOutput = constrain(entangledOutput, 0, 255);

  // Output to jacks
  analogWrite(11, primaryOutput);   // F4 - Primary quantum walk
  analogWrite(9, entangledOutput);  // F2 - Entangled output
  
  // LED behavior - indicates mode and activity
  updateLEDFeedback(primaryOutput);
}

void configurePWM() {
  TCCR1A = 0; TCCR1B = 0;
  TCCR1A |= (1 << WGM10) | (1 << COM1A1) | (1 << COM1B1);
  TCCR1B |= (1 << WGM12) | (1 << CS10);

  TCCR2A = 0; TCCR2B = 0;
  TCCR2A |= (1 << WGM20) | (1 << WGM21) | (1 << COM2A1) | (1 << COM2B1);
  TCCR2B |= (1 << CS20);
}

// Classic quantum walk with tunneling events
void updateQuantumWalk(float &phase, float rate, float flux) {
  float randomStep = (random(-100, 100) / 100.0f) * flux;
  
  // Quantum tunneling - rare large jumps!
  if (random(1000) < (QUANTUM_TUNNEL_PROBABILITY * 10)) {
    randomStep *= QUANTUM_TUNNEL_MULTIPLIER;
    ledPulse = 255; // Flash LED on quantum tunnel event
  }
  
  phase += randomStep * rate;
  phase = constrain(phase, 0.0f, 1.0f);
}

// Gravity well - pull toward equilibrium (potential energy minimum)
void updateGravityWell(float &phase, float rate, float flux) {
  float randomStep = (random(-100, 100) / 100.0f) * flux;
  phase += randomStep * rate;

  // Pull toward equilibrium (0.5 or center)
  // Simulates a potential well at center
  float equilibrium = 0.5f;
  float pullStrength = 0.02f; // Adjust for stronger/weaker pull
  phase += (equilibrium - phase) * pullStrength;
  
  phase = constrain(phase, 0.0f, 1.0f);
}

// Entangled output - correlated but independent evolution
void updateEntangledOutput(float mainPhase, float &entangledPhase, float currentEntanglement) {
  // Quantum entanglement simulation
  // High entanglement = outputs move together
  // Low entanglement = independent evolution
  
  entangledPhase = (entangledPhase * currentEntanglement) + 
                   (mainPhase * (1.0f - currentEntanglement));
}

// LED feedback system
void updateLEDFeedback(int outputValue) {
  if (modeJustChanged && ledPulse > 0) {
    // Flash on mode change
    OCR2B = ledPulse;
    ledPulse -= 15;
    if (ledPulse < 15) {
      modeJustChanged = false;
      ledPulse = 0;
    }
  } else if (gravityWellMode) {
    // Pulsing in gravity mode
    OCR2B = (outputValue * 0.7f) + (sin(millis() * 0.003f) * 40.0f + 40.0f);
  } else {
    // Steady in quantum walk mode
    OCR2B = outputValue * 0.5f;
  }
}

float readFrequency(int analogPin) {
  int rawVal = analogRead(analogPin);
  float fMin = 0.001f;
  float fMax = 0.1f;
  return fMin * powf(fMax / fMin, rawVal / 1023.0f);
}
