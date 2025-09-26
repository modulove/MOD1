/*
HAGIWO MOD1 Clock div/multi Ver2
Enhanced with Pin Change Interrupts and direct port manipulation


- Pin Change Interrupts for zero-latency edge detection  
- Direct port manipulation for fastest I/O
- Hardware timer for precise pulse width
- Interrupt-driven 

features

- 3 independent outputs with div/mult rates: 1,2,3,4,8,16
- Mode switching between divider and multiplier
- EEPROM mode storage
- LED indication with mode-switch blinking

Original code by HAGIWO - Released under CC0
*/

#include <EEPROM.h>

// Define mode constants
#define MODE_DIVIDER 1
#define MODE_MULTIPLIER 2

// Pin assignments
const int buttonPin = 4;        // D4
const int clockInputPin = 17;   // D17/A3  
const int ledPin = 3;           // D3
const int outPins[3] = {9, 10, 11};  // D9, D10, D11
const int analogPins[3] = {A0, A1, A2};

// Direct port manipulation for ATmega328P
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
  // Port definitions
  #define LED_PORT PORTD
  #define LED_DDR DDRD
  #define LED_BIT (1 << 3)  // D3
  
  #define OUT1_PORT PORTB
  #define OUT1_BIT (1 << 1)  // D9
  #define OUT2_PORT PORTB
  #define OUT2_BIT (1 << 2)  // D10
  #define OUT3_PORT PORTB
  #define OUT3_BIT (1 << 3)  // D11
  #define OUT_DDR DDRB
  
  #define BUTTON_PIN PIND
  #define BUTTON_PORT PORTD
  #define BUTTON_BIT (1 << 4)  // D4
  
  // For Pin Change Interrupt on A3/D17 (PC3)
  #define CLOCK_PIN PINC
  #define CLOCK_PORT PORTC
  #define CLOCK_BIT (1 << 3)  // A3/PC3
  #define CLOCK_DDR DDRC
  
  // Fast I/O macros
  #define LED_HIGH() (LED_PORT |= LED_BIT)
  #define LED_LOW() (LED_PORT &= ~LED_BIT)
  #define LED_TOGGLE() (LED_PORT ^= LED_BIT)
  
  #define OUT1_HIGH() (OUT1_PORT |= OUT1_BIT)
  #define OUT1_LOW() (OUT1_PORT &= ~OUT1_BIT)
  #define OUT2_HIGH() (OUT2_PORT |= OUT2_BIT)
  #define OUT2_LOW() (OUT2_PORT &= ~OUT2_BIT)
  #define OUT3_HIGH() (OUT3_PORT |= OUT3_BIT)
  #define OUT3_LOW() (OUT3_PORT &= ~OUT3_BIT)
  
  #define BUTTON_PRESSED() (!(BUTTON_PIN & BUTTON_BIT))
  #define CLOCK_READ() (CLOCK_PIN & CLOCK_BIT)
#else
  // Fallback for other boards
  #define LED_HIGH() digitalWrite(ledPin, HIGH)
  #define LED_LOW() digitalWrite(ledPin, LOW)
  #define OUT1_HIGH() digitalWrite(outPins[0], HIGH)
  #define OUT1_LOW() digitalWrite(outPins[0], LOW)
  #define OUT2_HIGH() digitalWrite(outPins[1], HIGH)
  #define OUT2_LOW() digitalWrite(outPins[1], LOW)
  #define OUT3_HIGH() digitalWrite(outPins[2], HIGH)
  #define OUT3_LOW() digitalWrite(outPins[2], LOW)
  #define BUTTON_PRESSED() (digitalRead(buttonPin) == LOW)
  #define CLOCK_READ() digitalRead(clockInputPin)
#endif

// Global variables
volatile uint8_t mode;
volatile bool clockRisingEdge = false;
volatile bool clockFallingEdge = false;
volatile uint32_t clockRiseTime = 0;
volatile uint32_t clockFallTime = 0;
volatile uint8_t lastClockState = 0;

// Clock tracking with microsecond precision
volatile uint32_t dividerCounter = 0;
uint32_t lastRiseTimeMicros = 0;
uint32_t clockPeriodMicros = 40000; // Default 25Hz (40ms)
uint32_t avgClockPeriod = 40000;

// Button debounce
uint8_t lastButtonState = HIGH;
uint32_t buttonDebounceTime = 0;

// Pulse management
const uint32_t pulseWidthMicros = 10000;  // 10ms in microseconds

struct OutputChannel {
  uint8_t rate;
  uint8_t divCount;
  bool pulseActive;
  uint32_t pulseEndTime;
  uint8_t pulsesGenerated;
  uint32_t nextPulseTime;
  uint32_t multiplierInterval;
};

OutputChannel channels[3];

// LED blinking for mode indication
bool ledBlinkingActive = false;
uint8_t ledBlinkCycles = 0;
uint8_t ledBlinkCount = 0;
uint32_t ledBlinkNextTime = 0;
bool ledBlinkState = false;

// Analog reading optimization
uint32_t lastAnalogReadTime = 0;
uint8_t analogReadIndex = 0;
uint8_t rateCache[3] = {1, 1, 1};

// Pin Change Interrupt for clock input (PC3/A3)
ISR(PCINT1_vect) {
  uint8_t clockState = CLOCK_READ();
  uint32_t currentTime = micros();
  
  if (clockState && !lastClockState) {
    // Rising edge detected
    clockRisingEdge = true;
    clockRiseTime = currentTime;
    if (!ledBlinkingActive) {
      LED_HIGH();
    }
  } else if (!clockState && lastClockState) {
    // Falling edge detected
    clockFallingEdge = true;
    clockFallTime = currentTime;
    if (!ledBlinkingActive) {
      LED_LOW();
    }
  }
  
  lastClockState = clockState;
}

// Timer1 Compare Match A interrupt for microsecond-accurate pulse timing
ISR(TIMER1_COMPA_vect) {
  uint32_t currentTime = micros();
  
  // Check all channels for pulse end
  for (uint8_t i = 0; i < 3; i++) {
    if (channels[i].pulseActive && currentTime >= channels[i].pulseEndTime) {
      switch(i) {
        case 0: OUT1_LOW(); break;
        case 1: OUT2_LOW(); break;
        case 2: OUT3_LOW(); break;
      }
      channels[i].pulseActive = false;
    }
  }
}

// Fast analog read with direct threshold comparison
inline uint8_t getRateFast(uint8_t analogPin) {
  int val = analogRead(analogPin);
  if (val < 102) return 1;
  if (val < 308) return 2;
  if (val < 514) return 3;
  if (val < 720) return 4;
  if (val < 926) return 8;
  return 16;
}

// Trigger pulse with microsecond precision
inline void triggerPulse(uint8_t ch) {
  if (!channels[ch].pulseActive) {
    switch(ch) {
      case 0: OUT1_HIGH(); break;
      case 1: OUT2_HIGH(); break;
      case 2: OUT3_HIGH(); break;
    }
    channels[ch].pulseEndTime = micros() + pulseWidthMicros;
    channels[ch].pulseActive = true;
  }
}

void setup() {
  // Disable interrupts during setup
  cli();
  
  Serial.begin(115200);
  Serial.println(F("Eurorack Clock Div/Mult v2.5"));
  Serial.println(F("Standalone - No Libraries Required"));
  
  // Configure ports for maximum speed
  #if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
    // Set pin directions
    LED_DDR |= LED_BIT;                          // LED output
    OUT_DDR |= (OUT1_BIT | OUT2_BIT | OUT3_BIT); // Channel outputs
    BUTTON_PORT |= BUTTON_BIT;                   // Button pullup
    CLOCK_DDR &= ~CLOCK_BIT;                     // Clock input
    CLOCK_PORT |= CLOCK_BIT;                     // Clock pullup
    
    // Clear outputs
    OUT1_LOW();
    OUT2_LOW();
    OUT3_LOW();
    LED_LOW();
    
    // Setup Pin Change Interrupt for clock (PC3)
    PCICR |= (1 << PCIE1);    // Enable PCINT for Port C
    PCMSK1 |= (1 << PCINT11); // Enable PCINT11 (PC3/A3)
    
    // Setup Timer1 for pulse timing (1kHz interrupt)
    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1 = 0;
    OCR1A = 249;  // 16MHz / 64 / 250 = 1kHz
    TCCR1B |= (1 << WGM12);  // CTC mode
    TCCR1B |= (1 << CS11) | (1 << CS10);  // Prescaler 64
    TIMSK1 |= (1 << OCIE1A);  // Enable compare interrupt
  #else
    // Fallback for other boards
    pinMode(ledPin, OUTPUT);
    pinMode(buttonPin, INPUT_PULLUP);
    pinMode(clockInputPin, INPUT_PULLUP);
    for (int i = 0; i < 3; i++) {
      pinMode(outPins[i], OUTPUT);
      digitalWrite(outPins[i], LOW);
    }
  #endif
  
  // Initialize channels
  for (uint8_t i = 0; i < 3; i++) {
    channels[i].rate = 1;
    channels[i].divCount = 0;
    channels[i].pulseActive = false;
    channels[i].pulsesGenerated = 0;
    channels[i].nextPulseTime = 0;
    channels[i].multiplierInterval = 0;
  }
  
  // Read mode from EEPROM
  mode = EEPROM.read(0);
  if (mode != MODE_DIVIDER && mode != MODE_MULTIPLIER) {
    mode = MODE_DIVIDER;
    EEPROM.write(0, mode);
  }
  
  Serial.print(F("Starting in "));
  Serial.print(mode == MODE_DIVIDER ? F("DIVIDER") : F("MULTIPLIER"));
  Serial.println(F(" mode"));
  
  // Enable interrupts
  sei();
  
  Serial.println(F("Ready!"));
}

void loop() {
  uint32_t currentMicros = micros();
  uint32_t currentMillis = currentMicros / 1000;
  
  // Handle button press (10ms check interval)
  static uint32_t lastButtonCheck = 0;
  if (currentMillis - lastButtonCheck >= 10) {
    lastButtonCheck = currentMillis;
    
    bool pressed = BUTTON_PRESSED();
    if (pressed && lastButtonState == HIGH && 
        currentMillis - buttonDebounceTime > 200) {
      switchMode();
      buttonDebounceTime = currentMillis;
    }
    lastButtonState = pressed ? LOW : HIGH;
  }
  
  // Update LED blinking
  if (ledBlinkingActive && currentMillis >= ledBlinkNextTime) {
    ledBlinkState = !ledBlinkState;
    if (ledBlinkState) {
      LED_HIGH();
    } else {
      LED_LOW();
      ledBlinkCount++;
      if (ledBlinkCount >= ledBlinkCycles) {
        ledBlinkingActive = false;
      }
    }
    // Different blink speeds for different modes
    uint32_t blinkSpeed = (mode == MODE_MULTIPLIER) ? 50 : 150;
    ledBlinkNextTime = currentMillis + blinkSpeed;
  }
  
  // Analog reading (rotate through pots every 30ms)
  if (currentMillis - lastAnalogReadTime >= 30) {
    lastAnalogReadTime = currentMillis;
    rateCache[analogReadIndex] = getRateFast(analogPins[analogReadIndex]);
    channels[analogReadIndex].rate = rateCache[analogReadIndex];
    analogReadIndex = (analogReadIndex + 1) % 3;
  }
  
  // Process clock rising edge from interrupt
  if (clockRisingEdge) {
    cli();  // Briefly disable interrupts for atomic read
    bool edge = clockRisingEdge;
    uint32_t riseTime = clockRiseTime;
    clockRisingEdge = false;
    sei();
    
    if (edge) {
      // Calculate period with exponential averaging for stability
      if (lastRiseTimeMicros > 0) {
        uint32_t newPeriod = riseTime - lastRiseTimeMicros;
        
        // Filter out unrealistic values (5ms to 2s range)
        if (newPeriod > 5000 && newPeriod < 2000000) {
          // Exponential moving average for smooth tracking
          avgClockPeriod = (avgClockPeriod * 3 + newPeriod) >> 2;
          clockPeriodMicros = avgClockPeriod;
        }
      }
      lastRiseTimeMicros = riseTime;
      
      if (mode == MODE_DIVIDER) {
        // Divider mode - count clocks and trigger on division
        dividerCounter++;
        for (uint8_t i = 0; i < 3; i++) {
          channels[i].divCount++;
          if (channels[i].divCount >= channels[i].rate) {
            channels[i].divCount = 0;
            triggerPulse(i);
          }
        }
      } else {
        // Multiplier mode - schedule multiple pulses
        for (uint8_t i = 0; i < 3; i++) {
          channels[i].pulsesGenerated = 0;
          channels[i].multiplierInterval = clockPeriodMicros / channels[i].rate;
          channels[i].nextPulseTime = currentMicros;
        }
      }
    }
  }
  
  // Process falling edge (for future gate length tracking)
  if (clockFallingEdge) {
    cli();
    clockFallingEdge = false;
    sei();
    // Could implement variable gate length here in future
  }
  
  // Multiplier mode pulse generation
  if (mode == MODE_MULTIPLIER) {
    for (uint8_t i = 0; i < 3; i++) {
      if (channels[i].pulsesGenerated < channels[i].rate) {
        if (currentMicros >= channels[i].nextPulseTime) {
          triggerPulse(i);
          channels[i].pulsesGenerated++;
          if (channels[i].pulsesGenerated < channels[i].rate) {
            channels[i].nextPulseTime += channels[i].multiplierInterval;
          }
        }
      }
    }
  }
}

void switchMode() {
  cli();  // Atomic operation
  
  if (mode == MODE_DIVIDER) {
    mode = MODE_MULTIPLIER;
    ledBlinkCycles = 5;  // 5 fast blinks for multiplier mode
    
    // Reset multiplier state
    lastRiseTimeMicros = 0;
    for (uint8_t i = 0; i < 3; i++) {
      channels[i].pulsesGenerated = 0;
      channels[i].divCount = 0;
    }
    
    Serial.println(F("Switched to MULTIPLIER mode"));
  } else {
    mode = MODE_DIVIDER;
    ledBlinkCycles = 3;  // 3 slow blinks for divider mode
    
    // Reset divider state
    dividerCounter = 0;
    for (uint8_t i = 0; i < 3; i++) {
      channels[i].divCount = 0;
    }
    
    Serial.println(F("Switched to DIVIDER mode"));
  }
  
  // Start LED blinking
  ledBlinkingActive = true;
  ledBlinkCount = 0;
  ledBlinkState = true;
  ledBlinkNextTime = millis() + 50;
  LED_HIGH();
  
  sei();
  
  // Save to EEPROM
  EEPROM.update(0, mode);
}

// Diagnostic function for testing
void printDiagnostics() {
  Serial.print(F("Mode: "));
  Serial.print(mode == MODE_DIVIDER ? F("DIV") : F("MULT"));
  Serial.print(F(" | Period: "));
  Serial.print(clockPeriodMicros);
  Serial.print(F("us | Rates: "));
  for (uint8_t i = 0; i < 3; i++) {
    Serial.print(rateCache[i]);
    Serial.print(F(" "));
  }
  Serial.println();
}
