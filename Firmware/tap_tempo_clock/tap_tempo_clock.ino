/*
This code was originally written by HAGIWO and released under CC0

HAGIWO MOD1 Tap Tempo Clock Ver1.0
https://note.com/solder_state/n/nf004b748de9a
4-output tap tempo master clock.
Four outputs: 4x fixed output, 1~16x variable output, and two variable 1~/16 division outputs.

--Pin assign---
POT1  A0  F2out  multiple rate
POT2  A1  F3out  division rate
POT3  A2  F4out  division rate
F1    D17   4x clock out
F2    D9    1~16x variable output
F3    D10   1~/16 variable output
F4    D11   1~/16 variable output 
BUTTON    tap tempo (push 4times)
LED       1* output
EEPROM    N/A
*/

const byte buttonPin   = 4;    // button input
const byte ledPin      = 3;    // LED output
const byte trigPinA0   = 9;    // trigger for A0 control (frequency division)
const byte trigPinA1   = 10;   // trigger for A1 control (period expansion)
const byte trigPinA2   = 11;   // trigger for A2 control (period expansion)
const byte trigPinMain = 17;   // trigger 4 times in main period
const byte potPinA0    = A0;   // analog input for A0
const byte potPinA1    = A1;   // analog input for A1
const byte potPinA2    = A2;   // analog input for A2

unsigned long pressTimes[4] = {0,0,0,0};  
byte pressCount = 0;

unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

bool lastButtonState = HIGH;
bool stableButtonState = HIGH;
bool prevStableButtonState = HIGH;

unsigned long clockInterval = 500;  // basic clock period
unsigned long ledOnTime = 10;       // LED ON time
unsigned long lastBlinkTime = 0;    // start time of the basic clock period

// Common settings for trigger output
unsigned long triggerOnTime = 5;    // 5ms trigger pulse

// For A0 (D9)
unsigned int multiplierA0 = 1;
unsigned long triggerIntervalA0 = 500;
unsigned long nextTriggerTimeA0 = 0;
bool triggerStateA0 = false;
unsigned long triggerStartTimeA0 = 0;

// For A1 (D10)
unsigned int multiplierA1 = 1;
unsigned long triggerIntervalA1 = 500;
unsigned long nextTriggerTimeA1 = 0;
bool triggerStateA1 = false;
unsigned long triggerStartTimeA1 = 0;

// For A2 (D11)
unsigned int multiplierA2 = 1;
unsigned long triggerIntervalA2 = 500;
unsigned long nextTriggerTimeA2 = 0;
bool triggerStateA2 = false;
unsigned long triggerStartTimeA2 = 0;

// Trigger 4 times within main period (D17)
unsigned long mainTriggerInterval = 0; 
unsigned long nextMainTriggerTime = 0;
byte mainTriggerCount = 0;
bool mainTriggerState = false;
unsigned long mainTriggerStartTime = 0;

void setup() {
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  pinMode(trigPinA0, OUTPUT);
  pinMode(trigPinA1, OUTPUT);
  pinMode(trigPinA2, OUTPUT);
  pinMode(trigPinMain, OUTPUT);

  digitalWrite(ledPin, LOW);
  digitalWrite(trigPinA0, LOW);
  digitalWrite(trigPinA1, LOW);
  digitalWrite(trigPinA2, LOW);
  digitalWrite(trigPinMain, LOW);
}

void loop() {
  unsigned long currentMillis = millis();
  
  // Button input processing
  bool reading = digitalRead(buttonPin);
  if (reading != lastButtonState) {
    lastDebounceTime = currentMillis;
  }
  if ((currentMillis - lastDebounceTime) > debounceDelay) {
    if (reading != stableButtonState) {
      prevStableButtonState = stableButtonState;
      stableButtonState = reading;
      if (prevStableButtonState == HIGH && stableButtonState == LOW) {
        recordPressTime(currentMillis);
        calculateClockInterval(currentMillis);
        // Serial.println("Button Pressed");
      }
    }
  }
  lastButtonState = reading;

  // A0: get frequency multiplier (division within period)
  int potValA0 = analogRead(potPinA0);
  multiplierA0 = getMultiplierFromPot(potValA0);

  // A1: get period multiplier
  int potValA1 = analogRead(potPinA1);
  multiplierA1 = getMultiplierFromPot(potValA1);

  // A2: get period multiplier
  int potValA2 = analogRead(potPinA2);
  multiplierA2 = getMultiplierFromPot(potValA2);

  // For D9: period division
  triggerIntervalA0 = clockInterval / multiplierA0;
  if (triggerIntervalA0 < 1) triggerIntervalA0 = 1;

  // For D10: period expansion
  triggerIntervalA1 = clockInterval * multiplierA1;

  // For D11: period expansion
  triggerIntervalA2 = clockInterval * multiplierA2;

  // Trigger for 4 times in main period
  mainTriggerInterval = clockInterval / 4;
  // 0~3 times, next start is set when the period is reset

  // LED blinking (basic clock)
  if ((currentMillis - lastBlinkTime) >= clockInterval) {
    lastBlinkTime = currentMillis;
    digitalWrite(ledPin, HIGH);

    // Reset 4 triggers for the main period
    mainTriggerCount = 0;
    nextMainTriggerTime = lastBlinkTime;

    // Update trigger timing for A0
    nextTriggerTimeA0 = lastBlinkTime;

    // When the period changes, resync triggers for A1 and A2
    static unsigned long lastClockInterval = clockInterval;
    if (clockInterval != lastClockInterval) {
      nextTriggerTimeA1 = currentMillis + triggerIntervalA1;
      nextTriggerTimeA2 = currentMillis + triggerIntervalA2;
      lastClockInterval = clockInterval;
    }
  }

  // Turn LED off after 10ms
  if ((digitalRead(ledPin) == HIGH) && ((currentMillis - lastBlinkTime) >= ledOnTime)) {
    digitalWrite(ledPin, LOW);
  }

  // Trigger generation
  handleTrigger(currentMillis, trigPinA0, triggerIntervalA0, nextTriggerTimeA0, triggerStateA0, triggerStartTimeA0);
  handleTriggerPeriodic(currentMillis, trigPinA1, triggerIntervalA1, nextTriggerTimeA1, triggerStateA1, triggerStartTimeA1);
  handleTriggerPeriodic(currentMillis, trigPinA2, triggerIntervalA2, nextTriggerTimeA2, triggerStateA2, triggerStartTimeA2);

  // Generate 4 triggers within main period
  handleMainTriggers(currentMillis);
}

// Generate 4 triggers within the main period
void handleMainTriggers(unsigned long currentMillis) {
  if (mainTriggerCount < 4) {
    // Check if less than 4 times
    if (!mainTriggerState) {
      // Waiting for trigger
      if ((long)(currentMillis - nextMainTriggerTime) >= 0) {
        digitalWrite(trigPinMain, HIGH);
        mainTriggerState = true;
        mainTriggerStartTime = currentMillis;
      }
    } else {
      // Trigger is ON
      if ((currentMillis - mainTriggerStartTime) >= triggerOnTime) {
        digitalWrite(trigPinMain, LOW);
        mainTriggerState = false;
        mainTriggerCount++;
        // Next trigger timing
        if (mainTriggerCount < 4) {
          nextMainTriggerTime += mainTriggerInterval;
        }
      }
    }
  }
}

// Record button press time
void recordPressTime(unsigned long t) {
  for (byte i=0; i<3; i++) {
    pressTimes[i] = pressTimes[i+1];
  }
  pressTimes[3] = t;
  if (pressCount < 4) pressCount++;
}

// Calculate average period
void calculateClockInterval(unsigned long currentMillis) {
  if (pressCount < 4) return;
  
  unsigned long interval1 = pressTimes[1] - pressTimes[0];
  unsigned long interval2 = pressTimes[2] - pressTimes[1];
  unsigned long interval3 = pressTimes[3] - pressTimes[2];
  unsigned long avgInterval = (interval1 + interval2 + interval3) / 3;

  if (avgInterval < (ledOnTime + 1)) {
    avgInterval = ledOnTime + 10;
  }

  clockInterval = avgInterval;
  lastBlinkTime = currentMillis;

  // Since the period has changed, resync triggers for A1 and A2
  nextTriggerTimeA1 = currentMillis + (clockInterval * multiplierA1);
  nextTriggerTimeA2 = currentMillis + (clockInterval * multiplierA2);
}

// Convert analog value to multiplier
unsigned int getMultiplierFromPot(int potVal) {
  if      (potVal < 170) return 1;
  else if (potVal < 340) return 2;
  else if (potVal < 510) return 3;
  else if (potVal < 680) return 4;
  else if (potVal < 850) return 8;
  else                   return 16;
}

// Trigger control function (for period division: A0)
void handleTrigger(unsigned long currentMillis, byte pin, unsigned long &interval, unsigned long &nextTime, 
                   bool &state, unsigned long &startTime) {
  if (!state) {
    if ((long)(currentMillis - nextTime) >= 0) {
      digitalWrite(pin, HIGH);
      state = true;
      startTime = currentMillis;
      nextTime += interval;
    }
  } else {
    if ((currentMillis - startTime) >= triggerOnTime) {
      digitalWrite(pin, LOW);
      state = false;
    }
  }
}

// Trigger control function (for period expansion: A1, A2)
void handleTriggerPeriodic(unsigned long currentMillis, byte pin, unsigned long &interval, unsigned long &nextTime, 
                           bool &state, unsigned long &startTime) {
  if (!state) {
    if ((long)(currentMillis - nextTime) >= 0) {
      digitalWrite(pin, HIGH);
      state = true;
      startTime = currentMillis;
      nextTime += interval; // update next trigger time
    }
  } else {
    if ((currentMillis - startTime) >= triggerOnTime) {
      digitalWrite(pin, LOW);
      state = false;
    }
  }
}
