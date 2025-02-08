/*
This code was originally written by HAGIWO and released under CC0

HAGIWO MOD1 RandomCV Ver1.0
https://note.com/solder_state/n/nd2af5f03a9c7
Periodic random CV sequencer.

--Pin assign---
POT1  A0  Step length 3,4,5,8,16,32
POT2  A1  output level
POT3  A2  Trigger probability
F1    D17  Clock in
F2    D9  Random value update
F3    D10  CV output
F4    D11 Trigger output
BUTTON    Random value update
LED       CV output
EEPROM    N/A
*/

unsigned long previousMillis = 0;
unsigned long currentMillis = 0;

bool lastButtonState = HIGH;
unsigned long buttonPreviousMillis = 0;
const unsigned long debounceDelay = 50;

const int triggerPin = 17;  // stepping trigger
const int reRandomPin = 9;  // re-randomize trigger input
const int cvOutPin = 10;
const int trigOutPin = 11;
const int ledPin = 3;
const int buttonPin = 4;       // momentary switch (INPUT_PULLUP)
const int potPin = A1;         // For CV scaling (A1)
const int stepSelectPin = A0;  // Variable number of steps (A0)
const int trigProbPin = A2;    // For trigger probability (A2)

int stepModes[] = { 3, 4, 5, 8, 16, 32 };
int currentStep = 0;
int currentTotalSteps = 8;
int cvValues[32];    // 0 to 255
int trigValues[32];  // 0 to 255, random values for trigger detection (cyclic pattern)
int index = 0;

bool lastTriggerState = HIGH;
bool lastReRandState = HIGH;

unsigned long trigOutTime = 0;
unsigned long trigOutStart = 0;
byte trigOutState = 2;

void setup() {
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  pinMode(trigOutPin, OUTPUT);

  pinMode(triggerPin, INPUT_PULLUP);
  pinMode(cvOutPin, OUTPUT);
  pinMode(reRandomPin, INPUT_PULLUP);

  TCCR2A = (1 << WGM21) | (1 << WGM20) | (1 << COM2B1);  //fast PWM 62.5kHz setting
  TCCR2B = (1 << CS20);                                  //fast PWM 62.5kHz setting
  TCCR1A = (1 << WGM11) | (1 << COM1A1);                 //fast PWM 62.5kHz setting
  TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS10);    //fast PWM 62.5kHz setting
  ICR1 = 255;                                            //fast PWM 62.5kHz setting

  digitalWrite(trigOutPin, LOW);

  randomSeed(analogRead(A0));
  reRandomizeCV();

  updateStepCount();
}

void loop() {
  currentMillis = millis();

  // Step progress on trigger input
  int trigReading = digitalRead(triggerPin);
  if (trigReading == HIGH && lastTriggerState == LOW) {
    updateStepCount();

    currentStep = (currentStep + 1) % currentTotalSteps;

    // CV scaling
    int potValue = analogRead(potPin);
    int cvMax = map(potValue, 0, 1023, 0, 255);
    uint16_t temp = (uint16_t)cvValues[currentStep] * (uint16_t)cvMax;
    int outputCV = temp / 255;  // 0～255
    analogWrite(cvOutPin, outputCV);
    analogWrite(ledPin, outputCV);

    // Apply trigger probability in real time
    int trigVal = analogRead(trigProbPin);
    int trigThreshold = map(trigVal, 0, 1023, 0, 255);
    // Fire if trigValues[currentStep] < trigThreshold
    if (trigValues[currentStep] < trigThreshold) {
      // Trigger out after 10ms
      trigOutTime = currentMillis + 10;
      trigOutState = 0;
    } else {
      trigOutState = 2;  // No trigger
    }
  }
  lastTriggerState = trigReading;

  // Re-randomize upon rising edge of D9 trigger
  int reRandReading = digitalRead(reRandomPin);
  if (reRandReading == HIGH && lastReRandState == LOW) {
    reRandomizeCV();
  }
  lastReRandState = reRandReading;

  // Trigger output process
  if (trigOutState == 0 && (long)(currentMillis - trigOutTime) >= 0) {
    digitalWrite(trigOutPin, HIGH);
    trigOutStart = currentMillis;
    trigOutState = 1;
  } else if (trigOutState == 1 && (currentMillis - trigOutStart) > 2) {
    digitalWrite(trigOutPin, LOW);
    trigOutState = 2;
  }

  // Re-randomize on D4 button press (debounce)
  int reading = digitalRead(buttonPin);
  if (currentMillis - buttonPreviousMillis > debounceDelay) {
    if (reading == LOW && lastButtonState == HIGH) {
      buttonPreviousMillis = currentMillis;
      reRandomizeCV();
    }
  }
  lastButtonState = reading;
}

void reRandomizeCV() {
  // Randomly regenerate values for CV and trigger (0 to 255)
  for (int i = 0; i < 32; i++) {
    cvValues[i] = random(256);
    trigValues[i] = random(256);
  }
}

void updateStepCount() {
  int val = analogRead(stepSelectPin);
  // int index = map(val, 0, 1023, 0, 5);

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

  int newTotalSteps = stepModes[index];

  if (newTotalSteps != currentTotalSteps) {
    currentStep = currentStep % newTotalSteps;
    currentTotalSteps = newTotalSteps;
  }
}
