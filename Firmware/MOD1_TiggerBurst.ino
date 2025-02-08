/*
This code was originally written by HAGIWO and released under CC0

HAGIWO MOD1 Trigger burst Ver1.0
https://note.com/solder_state/n/n59f617fd0657

Clock-syncable trigger burst.
When POT3 is turned fully left, it accepts an external clock for F1.
When POT3 is to the right of 9:00, it switches to the internal clock. The internal clock can be adjusted with POT3.

--Pin assign---
POT1  A0  burst number 1,3,4,6,8,16
POT2  A1  burst frequency (master clock bpm /2,/3,/4,/6,/8,/16)
POT3  A2  internal clock rate ~bpm280
F1    D17 external clock in
F2    D9  trigger in
F3    D10 burst number CVin
F4    D11 trigger output
BUTTON    trigger in
LED       trigger output
EEPROM    N/A
*/

const int pinTriggerIn       = 9;   
const int pinTriggerOut      = 11;  
const int pinLed             = 3;   
const int pinNumPot          = A0;  
const int pinNumPot2         = A5;  
const int pinDivPot          = A1;  
const int pinBpmPot          = A2;  
const int pinButton          = 4;   
const int pinExternalClock   = 17;  

// Number of triggers (6 options)
int triggerOptions[6]        = {1, 3, 4, 6, 8, 16};

// Division ratios (6 options)
float divisionOptions[6]     = {0.5, 0.3333, 0.25, 0.1667, 0.125, 0.0625};

// Selected settings
int   selectedTriggers       = 1;   
float selectedDivision       = 0.5; 
float currentBpm             = 80;  

// Burst state
bool  burstActive            = false; 
int   triggersRemaining      = 0;
unsigned long triggerOnTime  = 5;    // 5ms
unsigned long nextActionTime = 0;    
bool  triggerIsHigh          = false;

// Trigger input state
int   lastTriggerInState     = LOW;  
int   currentTriggerInState  = LOW;

// Button debouncing
int   buttonState            = HIGH;         
int   lastButtonReading      = HIGH;         
unsigned long lastDebounceTime = 0;          
unsigned long debounceDelay     = 20;        
static int lastStableButtonState = HIGH;     

// External clock usage
bool  useExternalClock       = false;        
unsigned long externalClockPeriods[3] = {0, 0, 0}; 
byte  externalIndex          = 0;            
unsigned long lastExternalMillis = 0;        
unsigned long externalPeriodMs   = 0;        
int   lastExternalState      = LOW;          

//----------------------------------------------------------------------------------
// readNumberOfTriggersFromSum
// Reads A0 + A5 -> clamp 0..1023 -> threshold-based index -> pick triggerOptions
int readNumberOfTriggersFromSum() {
  int valA0 = analogRead(pinNumPot);   // 0..1023
  int valA5 = analogRead(pinNumPot2);  // 0..1023
  int sum   = valA0 + valA5;          // 0..2046

  if (sum > 1023) {
    sum = 1023;
  }

  int index;
  // Threshold-based classification
  if (sum <= 102) {
    index = 0;
  } else if (sum <= 308) {
    index = 1;
  } else if (sum <= 514) {
    index = 2;
  } else if (sum <= 720) {
    index = 3;
  } else if (sum <= 926) {
    index = 4;
  } else {
    index = 5;
  }

  return triggerOptions[index];
}

//----------------------------------------------------------------------------------
// readDivisionSelection
// Reads A1 -> threshold-based index -> pick divisionOptions
float readDivisionSelection() {
  int val = analogRead(pinDivPot);

  int index;
  if (val <= 102) {
    index = 0;
  } else if (val <= 308) {
    index = 1;
  } else if (val <= 514) {
    index = 2;
  } else if (val <= 720) {
    index = 3;
  } else if (val <= 926) {
    index = 4;
  } else {
    index = 5;
  }

  return divisionOptions[index];
}

//----------------------------------------------------------------------------------
// readBpm
// Reads A2 -> maps to 80..280 BPM
float readBpm() {
  int rawValue = analogRead(pinBpmPot);
  // Range: 80..280
  float bpm = 80.0 + ((float)rawValue * (280.0 - 80.0) / 1023.0);
  return bpm;
}

//----------------------------------------------------------------------------------
// readButtonDebounced
// Returns stable button state (LOW=pressed, HIGH=not pressed)
int readButtonDebounced() {
  int reading = digitalRead(pinButton);
  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
    lastButtonReading = reading;
  }
  if ((millis() - lastDebounceTime) > debounceDelay) {
    buttonState = reading;
  }
  return buttonState;
}

//----------------------------------------------------------------------------------
// checkExternalClock
// Detect rising edge on D17, average last 3 intervals
void checkExternalClock() {
  int currentExternalState = digitalRead(pinExternalClock);
  if (currentExternalState == HIGH && lastExternalState == LOW) {
    unsigned long now = millis();
    unsigned long cycle = now - lastExternalMillis;
    lastExternalMillis = now;

    externalClockPeriods[externalIndex] = cycle;
    externalIndex++;
    if (externalIndex >= 3) {
      externalIndex = 0;
    }

    unsigned long sum = 0;
    for (int i = 0; i < 3; i++) {
      sum += externalClockPeriods[i];
    }
    externalPeriodMs = sum / 3;
  }
  lastExternalState = currentExternalState;
}

//----------------------------------------------------------------------------------
// calculateOffDuration
// Determines interval between triggers (external or internal)
unsigned long calculateOffDuration() {
  float oneBeatMs;
  if (useExternalClock && externalPeriodMs > 0) {
    oneBeatMs = (float)externalPeriodMs;
  } else {
    oneBeatMs = 60000.0 / currentBpm;
  }

  float periodMs = oneBeatMs * selectedDivision;
  unsigned long offDuration = (unsigned long)periodMs - triggerOnTime;
  if (offDuration < 0) offDuration = 0;
  return offDuration;
}

//----------------------------------------------------------------------------------
// startBurst
// Initiates a new burst
void startBurst() {
  if (!burstActive) {
    burstActive = true;
    triggersRemaining = selectedTriggers;
    triggerIsHigh = false;

    unsigned long currentMillis = millis();
    digitalWrite(pinTriggerOut, HIGH);
    digitalWrite(pinLed, HIGH);
    triggerIsHigh = true;
    nextActionTime = currentMillis + triggerOnTime;
  }
}

//----------------------------------------------------------------------------------
// setup
// Pin config
void setup() {
  pinMode(pinTriggerIn, INPUT);
  pinMode(pinTriggerOut, OUTPUT);
  pinMode(pinLed, OUTPUT);
  pinMode(pinButton, INPUT_PULLUP);
  pinMode(pinExternalClock, INPUT);

  digitalWrite(pinTriggerOut, LOW);
  digitalWrite(pinLed, LOW);
}

//----------------------------------------------------------------------------------
// loop
// Main logic
void loop() {
  unsigned long currentMillis = millis();

  // 1) Get number of triggers (A0 + A5 -> thresholds)
  selectedTriggers = readNumberOfTriggersFromSum();

  // 2) Get division from A1 -> thresholds
  selectedDivision = readDivisionSelection();

  // 3) Decide internal or external clock from A2
  int potValue = analogRead(pinBpmPot);
  if (potValue < 50) {
    useExternalClock = true;
  } else {
    useExternalClock = false;
    currentBpm = readBpm();  // 80..280
  }

  // 4) Check external clock
  checkExternalClock();

  // 5) Check D9 rising edge
  currentTriggerInState = digitalRead(pinTriggerIn);
  if (currentTriggerInState == HIGH && lastTriggerInState == LOW) {
    startBurst();
  }
  lastTriggerInState = currentTriggerInState;

  // 6) Check button (D4) with debounce
  int stableButtonState = readButtonDebounced();
  if (stableButtonState == LOW && lastStableButtonState == HIGH) {
    startBurst();
  }
  lastStableButtonState = stableButtonState;

  // 7) Handle burst timing
  if (burstActive && currentMillis >= nextActionTime) {
    if (triggerIsHigh) {
      // Turn OFF
      digitalWrite(pinTriggerOut, LOW);
      digitalWrite(pinLed, LOW);
      triggerIsHigh = false;
      triggersRemaining--;

      if (triggersRemaining > 0) {
        nextActionTime = currentMillis + calculateOffDuration();
      } else {
        burstActive = false;
      }
    } else {
      // Turn ON
      digitalWrite(pinTriggerOut, HIGH);
      digitalWrite(pinLed, HIGH);
      triggerIsHigh = true;
      nextActionTime = currentMillis + triggerOnTime;
    }
  }
}
