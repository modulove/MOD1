/*
  MOD1 ADC + Button Test
  
  Verifies that A0-A3 and the button are working correctly.
  Open Serial Monitor at 115200 baud.
  
  Turn each pot and check if values change.
  Press button 
*/

#define PIN_LED    3
#define PIN_BUTTON 4

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n=== ADC + Button Test ==="));
  Serial.println(F("Turn pots and watch values. Press button to test."));
  Serial.println(F("Expected: 0-1023 for each ADC\n"));
  
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  
  // Blink to show we're alive
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_LED, HIGH);
    delay(100);
    digitalWrite(PIN_LED, LOW);
    delay(100);
  }
}

// Button handling
uint8_t btnLast = HIGH;
uint32_t btnDownTime = 0;
bool longFired = false;

void loop() {
  // Read all ADCs
  uint16_t a0 = analogRead(A0);
  uint16_t a1 = analogRead(A1);
  uint16_t a2 = analogRead(A2);
  uint16_t a3 = analogRead(A3);
  
  // Print values
  Serial.print(F("A0(Freq): "));
  Serial.print(a0);
  printBar(a0);
  
  Serial.print(F("  A1(Vert): "));
  Serial.print(a1);
  printBar(a1);
  
  Serial.print(F("  A2(Page): "));
  Serial.print(a2);
  printBar(a2);
  
  Serial.print(F("  A3(CV): "));
  Serial.print(a3);
  printBar(a3);
  
  // Button state
  uint8_t btnNow = digitalRead(PIN_BUTTON);
  Serial.print(F("  BTN: "));
  Serial.print(btnNow == LOW ? F("DOWN") : F("UP  "));
  
  // Detect short press
  if (btnLast == HIGH && btnNow == LOW) {
    btnDownTime = millis();
    longFired = false;
  }
  
  if (btnLast == LOW && btnNow == HIGH) {
    uint32_t held = millis() - btnDownTime;
    if (held >= 30 && held < 500 && !longFired) {
      Serial.print(F(" -> SHORT PRESS!"));
      digitalWrite(PIN_LED, HIGH);
      delay(50);
      digitalWrite(PIN_LED, LOW);
    }
  }
  
  // Detect long press
  if (btnNow == LOW && !longFired && (millis() - btnDownTime > 600)) {
    longFired = true;
    Serial.print(F(" -> LONG PRESS!"));
    for (int i = 0; i < 3; i++) {
      digitalWrite(PIN_LED, HIGH);
      delay(50);
      digitalWrite(PIN_LED, LOW);
      delay(50);
    }
  }
  
  btnLast = btnNow;
  
  Serial.println();
  delay(200);
}

void printBar(uint16_t val) {
  // Print a simple bar graph
  Serial.print(F("["));
  uint8_t bars = val / 64;  // 0-15 bars for 0-1023
  for (uint8_t i = 0; i < 16; i++) {
    Serial.print(i < bars ? '#' : '-');
  }
  Serial.print(F("]"));
}
