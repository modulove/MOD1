/*
 * BELLZ SEQUENCER - Festive Firmware for HAGIWO MOD1
 * 
 * A Christmas-themed step sequencer for Eurorack
 * Sequences classic holiday melodies with bell accent triggers
 * Including MUTATION, loop control, and pitch offset!
 * 
 * Hardware:
 *   D17 - Clock/Trigger Input (external clock advances sequence)
 *   D9  - Gate Output (note on/off) [SWAPPED]
 *   D10 - Pitch CV Output (PWM, filter to smooth) [SWAPPED]
 *   D11 - Bell Trigger Output (accent hits for rings/bells)
 *   D3  - LED (visual feedback)
 *   D4  - Song Select Button (active low)
 *   A0  - Grid Length (16/32/64 steps)
 *   A1  - Pitch Offset (center = no change, bipolar +/- 2 octaves)
 * 
 * Mutation Modes:
 *   Melodies gradually mutate over time - notes shift, octaves jump,
 *   bells appear/disappear. Hold song button to reset mutations.
 * 
 * Songs included:
 *   1. Jingle Bells
 *   2. We Wish You a Merry Christmas
 *   3. O Tannenbaum
 *   4. Deck the Halls
 *   5. Silent Night
 * 
 * Modulove 2025
 */

#include <avr/pgmspace.h>
#include <EEPROM.h>

// ============================================
// EEPROM ADDRESSES
// ============================================
#define EEPROM_SONG_ADDR     0   // Address to store last song
#define EEPROM_MAGIC_ADDR    1   // Magic byte to check if EEPROM is initialized
#define EEPROM_MAGIC_VALUE  0xA5 // Magic value to verify EEPROM data

// ============================================
// PIN DEFINITIONS
// ============================================
#define PIN_CLOCK_IN    17   // Clock/trigger input
#define PIN_GATE_OUT     9   // Gate output (swapped)
#define PIN_PITCH_OUT   10   // PWM pitch CV output (swapped)
#define PIN_BELL_OUT    11   // Bell trigger output
#define PIN_LED          3   // LED for feedback
#define PIN_SONG_BTN     4   // Song select button (active low)
#define PIN_LOOP_LEN    A0   // Loop length pot (grid select)
#define PIN_PITCH_OFF   A1   // Pitch offset pot

// ============================================
// TIMING CONSTANTS
// ============================================
#define MIN_GATE_MS      20  // Minimum gate high duration
#define BELL_TIME_MS     30  // Bell trigger duration
#define RETRIGGER_US   2000  // Retrigger gap in microseconds (2ms)
#define BTN_DEBOUNCE_MS  50  // Button debounce
#define BTN_HOLD_MS    1000  // Hold time to reset mutations

// ============================================
// MUTATION SETTINGS
// ============================================
#define MUTATION_PROBABILITY  25   // % chance to mutate 1 step per loop (0-100)
#define MAX_MUTATED_STEPS     64   // Max steps we can store mutations for
// Mutations now only happen at loop boundaries, so the song
// plays through completely before evolving on the next pass.

// ============================================
// GRID SETTINGS
// ============================================
// Available grid lengths - melody will stretch/compress to fit
const uint8_t GRID_LENGTHS[] = {16, 32, 64};
#define NUM_GRIDS 3

// Debug settings
#define DEBUG_STEPS false          // Set true to print every step (verbose!)

// ============================================
// MUTATION TYPES
// ============================================
enum MutationType {
  MUT_NONE = 0,
  MUT_SHIFT_UP,      // Shift note up 1-3 semitones
  MUT_SHIFT_DOWN,    // Shift note down 1-3 semitones
  MUT_OCTAVE_UP,     // Jump up an octave
  MUT_OCTAVE_DOWN,   // Jump down an octave
  MUT_REST,          // Replace with rest
  MUT_DOUBLE,        // Copy previous note
  MUT_FIFTH_UP,      // Add a fifth above
  NUM_MUTATIONS
};

// ============================================
// NOTE DEFINITIONS
// PWM values 0-255 mapping to pitch CV
// ~4 PWM units per semitone gives good range
// Calibrate by adjusting these for your VCO
// ============================================
#define NOTE_REST   0    // Rest (gate stays low)

// Note PWM values (roughly 4 units per semitone)
// Octave 3
#define NOTE_C3    48
#define NOTE_D3    56
#define NOTE_E3    64
#define NOTE_F3    68
#define NOTE_G3    76
#define NOTE_A3    84
#define NOTE_Bb3   88   // For F major scale
#define NOTE_B3    92
// Octave 4
#define NOTE_C4    96
#define NOTE_D4   104
#define NOTE_E4   112
#define NOTE_F4   116
#define NOTE_G4   124
#define NOTE_A4   132
#define NOTE_Bb4  136   // For F major scale
#define NOTE_B4   140
// Octave 5
#define NOTE_C5   144
#define NOTE_D5   152
#define NOTE_E5   160
#define NOTE_F5   164
#define NOTE_G5   172

// Special markers
#define TIE 0xFE        // Tie to previous note (gate stays high)

// ============================================
// SCALE DEFINITIONS
// ============================================
// C Major scale (C D E F G A B)
const uint8_t SCALE_C_MAJOR[] PROGMEM = {
  NOTE_C3, NOTE_D3, NOTE_E3, NOTE_F3, NOTE_G3, NOTE_A3, NOTE_B3,
  NOTE_C4, NOTE_D4, NOTE_E4, NOTE_F4, NOTE_G4, NOTE_A4, NOTE_B4,
  NOTE_C5, NOTE_D5, NOTE_E5, NOTE_F5, NOTE_G5
};
#define SCALE_C_MAJOR_LEN 19

// F Major scale (F G A Bb C D E)
const uint8_t SCALE_F_MAJOR[] PROGMEM = {
  NOTE_F3, NOTE_G3, NOTE_A3, NOTE_Bb3,
  NOTE_C4, NOTE_D4, NOTE_E4,
  NOTE_F4, NOTE_G4, NOTE_A4, NOTE_Bb4,
  NOTE_C5, NOTE_D5, NOTE_E5,
  NOTE_F5, NOTE_G5
};
#define SCALE_F_MAJOR_LEN 16

// Scale type for each song
#define SCALE_TYPE_C_MAJOR 0
#define SCALE_TYPE_F_MAJOR 1

const uint8_t song_scales[] PROGMEM = {
  SCALE_TYPE_C_MAJOR,  // Jingle Bells
  SCALE_TYPE_C_MAJOR,  // We Wish You
  SCALE_TYPE_F_MAJOR,  // O Tannenbaum
  SCALE_TYPE_C_MAJOR,  // Deck the Halls
  SCALE_TYPE_C_MAJOR   // Silent Night
};

// ============================================
// SONG DATA STRUCTURE
// ============================================
// Each song has:
//   - melody[] : pitch values (0x00=rest, 0xFE=tie)
//   - rhythm[] : bell trigger pattern (1=hit, 0=silent)
//   - length   : number of steps
//
// Clock should be set to 8th notes for proper feel
// (e.g., 120 BPM = clock at 240 pulses/min = 4Hz)
// ============================================

// --------------------------------------------
// JINGLE BELLS (Chorus)
// Time signature: 4/4, swung eighths feel
// --------------------------------------------
const uint8_t melody_jingle_bells[] PROGMEM = {
  // Bar 1-2: "Jingle bells, jingle bells"
  NOTE_E4, NOTE_E4, NOTE_E4, NOTE_REST,   // Jin-gle bells,
  NOTE_E4, NOTE_E4, NOTE_E4, NOTE_REST,   // jin-gle bells,
  
  // Bar 3-4: "Jingle all the way"
  NOTE_E4, NOTE_G4, NOTE_C4, NOTE_D4,     // jin-gle all the
  NOTE_E4, TIE,     TIE,     NOTE_REST,   // way!
  
  // Bar 5-6: "Oh what fun it is to ride"
  NOTE_F4, NOTE_F4, NOTE_F4, NOTE_F4,     // Oh what fun it
  NOTE_F4, NOTE_E4, NOTE_E4, NOTE_E4,     // is to ride in
  
  // Bar 7-8: "In a one-horse open sleigh"
  NOTE_E4, NOTE_D4, NOTE_D4, NOTE_E4,     // a one-horse o-
  NOTE_D4, TIE,     NOTE_G4, NOTE_REST,   // pen sleigh! Hey!
  
  // Bar 9-10: "Jingle bells, jingle bells" (repeat)
  NOTE_E4, NOTE_E4, NOTE_E4, NOTE_REST,
  NOTE_E4, NOTE_E4, NOTE_E4, NOTE_REST,
  
  // Bar 11-12: "Jingle all the way"
  NOTE_E4, NOTE_G4, NOTE_C4, NOTE_D4,
  NOTE_E4, TIE,     TIE,     NOTE_REST,
  
  // Bar 13-14: "Oh what fun..."
  NOTE_F4, NOTE_F4, NOTE_F4, NOTE_F4,
  NOTE_F4, NOTE_E4, NOTE_E4, NOTE_E4,
  
  // Bar 15-16: "...one-horse open sleigh"
  NOTE_G4, NOTE_G4, NOTE_F4, NOTE_D4,
  NOTE_C4, TIE,     TIE,     NOTE_REST,
};

// Bell pattern - classic sleigh bell rhythm
// Hits on 1, 2+, 3, 4+ (driving eighth note feel)
const uint8_t rhythm_jingle_bells[] PROGMEM = {
  1, 0, 1, 0,   1, 0, 1, 0,   // Bars 1-2
  1, 0, 1, 0,   1, 0, 0, 0,   // Bars 3-4
  1, 0, 1, 0,   1, 0, 1, 0,   // Bars 5-6
  1, 0, 1, 0,   1, 0, 1, 0,   // Bars 7-8
  1, 0, 1, 0,   1, 0, 1, 0,   // Bars 9-10
  1, 0, 1, 0,   1, 0, 0, 0,   // Bars 11-12
  1, 0, 1, 0,   1, 0, 1, 0,   // Bars 13-14
  1, 0, 1, 0,   1, 0, 0, 0,   // Bars 15-16
};
#define JINGLE_BELLS_LEN 64

// --------------------------------------------
// WE WISH YOU A MERRY CHRISTMAS
// Time signature: 3/4 (waltz feel!)
// --------------------------------------------
const uint8_t melody_we_wish[] PROGMEM = {
  // Pickup + Bar 1: "We wish you a mer-ry"
  NOTE_G3, NOTE_C4, NOTE_C4, NOTE_D4, NOTE_C4, NOTE_B3,
  
  // Bar 2: "Christ-mas, we"
  NOTE_A3, NOTE_A3, NOTE_A3,
  
  // Bar 3: "wish you a mer-ry"
  NOTE_D4, NOTE_D4, NOTE_E4, NOTE_D4, NOTE_C4,
  
  // Bar 4: "Christ-mas, we"
  NOTE_B3, NOTE_G3, NOTE_G3,
  
  // Bar 5: "wish you a mer-ry"
  NOTE_E4, NOTE_E4, NOTE_F4, NOTE_E4, NOTE_D4,
  
  // Bar 6: "Christ-mas and a"
  NOTE_C4, NOTE_A3, NOTE_G3, NOTE_G3,
  
  // Bar 7-8: "hap-py new year!"
  NOTE_A3, NOTE_D4, NOTE_B3, NOTE_C4, TIE, NOTE_REST,
  
  // Bar 9-10: "Good ti-dings we bring"
  NOTE_C4, NOTE_G4, NOTE_G4, NOTE_G4, NOTE_F4, TIE,
  
  // Bar 11-12: "to you and your kin"
  NOTE_F4, NOTE_E4, NOTE_E4, NOTE_E4, NOTE_D4, TIE,
  
  // Bar 13-14: "Good ti-dings for Christ-mas"
  NOTE_D4, NOTE_E4, NOTE_D4, NOTE_C4, NOTE_G3, NOTE_A3,
  
  // Bar 15-16: "and a hap-py new year!"
  NOTE_B3, NOTE_C4, TIE, TIE, NOTE_REST, NOTE_REST,
};

// Waltz rhythm - emphasis on beat 1
const uint8_t rhythm_we_wish[] PROGMEM = {
  1, 0, 0,  1, 0, 0,    // Bars 1-2
  1, 0, 0,  1, 0, 0,    // Bars 3-4 (adjusted)
  1, 0, 0,  1, 0,       // Bar 5
  1, 0, 0,  1,          // Bar 6
  1, 0, 0,  1, 0, 0,    // Bars 7-8
  1, 0, 0,  1, 0, 0,    // Bars 9-10
  1, 0, 0,  1, 0, 0,    // Bars 11-12
  1, 0, 0,  1, 0, 0,    // Bars 13-14
  1, 0, 0,  0, 0, 0,    // Bars 15-16
};
#define WE_WISH_LEN 48

// --------------------------------------------
// O TANNENBAUM  
// Time signature: 3/4
// --------------------------------------------
const uint8_t melody_tannenbaum[] PROGMEM = {
  // Pickup: "O"
  NOTE_C4,
  
  // Bar 1-2: "Tan-nen-baum, o Tan-nen-baum"
  NOTE_F4, NOTE_F4, NOTE_F4, NOTE_G4, TIE,
  NOTE_A4, NOTE_A4, NOTE_A4, NOTE_G4,
  
  // Bar 3-4: "wie treu sind dei-ne Blät-ter"
  NOTE_A4, NOTE_B4, NOTE_C5, TIE, TIE,
  NOTE_C5, NOTE_A4, NOTE_B4, NOTE_G4,
  
  // Bar 5-6: (repeat) "O Tan-nen-baum"
  NOTE_C4, NOTE_F4, NOTE_F4, NOTE_F4,
  NOTE_G4, TIE, NOTE_A4, NOTE_A4,
  
  // Bar 7-8: "wie treu sind dei-ne Blät-ter"
  NOTE_A4, NOTE_G4, NOTE_A4, NOTE_B4,
  NOTE_C5, TIE, TIE, NOTE_REST,
  
  // Bar 9-10: "Du grünst nicht nur zur Som-mer-zeit"
  NOTE_C5, NOTE_C5, NOTE_A4, NOTE_A4,
  NOTE_B4, NOTE_B4, NOTE_G4, NOTE_G4,
  
  // Bar 11-12: "nein auch im Win-ter wenn es schneit"
  NOTE_C5, NOTE_C5, NOTE_A4, NOTE_A4,
  NOTE_B4, NOTE_B4, NOTE_G4, NOTE_REST,
  
  // Bar 13-14: "O Tan-nen-baum"
  NOTE_C4, NOTE_F4, NOTE_F4, NOTE_F4,
  NOTE_G4, TIE, NOTE_A4, NOTE_A4,
  
  // Bar 15-16: "wie treu sind dei-ne Blät-ter"
  NOTE_A4, NOTE_G4, NOTE_A4, NOTE_B4,
  NOTE_C5, TIE, TIE, NOTE_REST,
};

// 3/4 waltz with gentle bell on 1
const uint8_t rhythm_tannenbaum[] PROGMEM = {
  0,                      // Pickup
  1, 0, 0,  1, 0,         // Bars 1-2
  1, 0, 0,  1,
  1, 0, 0,  0, 0,         // Bars 3-4
  1, 0, 0,  1,
  1, 0, 0,  1,            // Bars 5-6
  1, 0, 0,  1,
  1, 0, 0,  1,            // Bars 7-8
  1, 0, 0,  0,
  1, 0, 1, 0,             // Bars 9-10
  1, 0, 1, 0,
  1, 0, 1, 0,             // Bars 11-12
  1, 0, 1, 0,
  1, 0, 0,  1,            // Bars 13-14
  1, 0, 0,  1,
  1, 0, 0,  1,            // Bars 15-16
  1, 0, 0,  0,
};
#define TANNENBAUM_LEN 64

// --------------------------------------------
// DECK THE HALLS
// Time signature: 4/4, bright and quick
// --------------------------------------------
const uint8_t melody_deck_halls[] PROGMEM = {
  // Bar 1: "Deck the halls with"
  NOTE_C5, NOTE_B4, NOTE_A4, NOTE_G4,
  
  // Bar 2: "boughs of hol-ly"
  NOTE_A4, NOTE_G4, NOTE_F4, NOTE_E4,
  
  // Bar 3-4: "Fa la la la la, la la la la"
  NOTE_F4, NOTE_E4, NOTE_D4, NOTE_E4,
  NOTE_F4, NOTE_D4, NOTE_E4, TIE,
  
  // Bar 5: "Tis the sea-son"
  NOTE_G4, NOTE_A4, NOTE_G4, NOTE_F4,
  
  // Bar 6: "to be jol-ly"
  NOTE_E4, NOTE_F4, NOTE_E4, NOTE_D4,
  
  // Bar 7-8: "Fa la la la la, la la la la"  
  NOTE_C4, TIE, TIE, NOTE_REST,
  NOTE_REST, NOTE_REST, NOTE_REST, NOTE_REST,
  
  // Repeat
  NOTE_C5, NOTE_B4, NOTE_A4, NOTE_G4,
  NOTE_A4, NOTE_G4, NOTE_F4, NOTE_E4,
  NOTE_F4, NOTE_E4, NOTE_D4, NOTE_E4,
  NOTE_F4, NOTE_D4, NOTE_E4, TIE,
  NOTE_G4, NOTE_A4, NOTE_G4, NOTE_F4,
  NOTE_E4, NOTE_F4, NOTE_E4, NOTE_D4,
  NOTE_C4, TIE, TIE, NOTE_REST,
};
#define DECK_HALLS_LEN 60

// Bright, driving quarter note feel
const uint8_t rhythm_deck_halls[] PROGMEM = {
  1, 0, 1, 0,   1, 0, 1, 0,   // Bars 1-2
  1, 0, 1, 0,   1, 0, 1, 0,   // Bars 3-4
  1, 0, 1, 0,   1, 0, 1, 0,   // Bars 5-6
  1, 0, 0, 0,   0, 0, 0, 0,   // Bars 7-8
  1, 0, 1, 0,   1, 0, 1, 0,   // Repeat
  1, 0, 1, 0,   1, 0, 1, 0,
  1, 0, 1, 0,   1, 0, 1, 0,
  1, 0, 0, 0,
};

// --------------------------------------------
// SILENT NIGHT
// Time signature: 6/8 (compound duple - gentle sway)
// --------------------------------------------
const uint8_t melody_silent_night[] PROGMEM = {
  // Bar 1-2: "Si-lent night"
  NOTE_G4, NOTE_A4, NOTE_G4, NOTE_E4, TIE, TIE,
  
  // Bar 3-4: "Ho-ly night"
  NOTE_G4, NOTE_A4, NOTE_G4, NOTE_E4, TIE, TIE,
  
  // Bar 5-6: "All is calm"
  NOTE_D5, TIE, NOTE_D5, NOTE_B4, TIE, TIE,
  
  // Bar 7-8: "All is bright"
  NOTE_C5, TIE, NOTE_C5, NOTE_G4, TIE, TIE,
  
  // Bar 9-10: "Round yon Vir-gin"
  NOTE_A4, TIE, NOTE_A4, NOTE_C5, NOTE_B4, NOTE_A4,
  
  // Bar 11-12: "Mo-ther and Child"
  NOTE_G4, NOTE_A4, NOTE_G4, NOTE_E4, TIE, TIE,
  
  // Bar 13-14: "Ho-ly In-fant so"
  NOTE_A4, TIE, NOTE_A4, NOTE_C5, NOTE_B4, NOTE_A4,
  
  // Bar 15-16: "ten-der and mild"
  NOTE_G4, NOTE_A4, NOTE_G4, NOTE_E4, TIE, TIE,
  
  // Bar 17-18: "Sleep in hea-ven-ly"
  NOTE_D5, TIE, NOTE_D5, NOTE_F5, NOTE_D5, NOTE_B4,
  
  // Bar 19-20: "peace"
  NOTE_C5, TIE, TIE, NOTE_E5, TIE, TIE,
  
  // Bar 21-22: "Sleep in hea-ven-ly"
  NOTE_C5, NOTE_G4, NOTE_E4, NOTE_G4, NOTE_F4, NOTE_D4,
  
  // Bar 23-24: "peace"
  NOTE_C4, TIE, TIE, TIE, NOTE_REST, NOTE_REST,
};
#define SILENT_NIGHT_LEN 72

// Gentle 6/8 - bell on beat 1 and 4 (compound feel)
const uint8_t rhythm_silent_night[] PROGMEM = {
  1, 0, 0,  1, 0, 0,    // Bars 1-2
  1, 0, 0,  1, 0, 0,    // Bars 3-4
  1, 0, 0,  1, 0, 0,    // Bars 5-6
  1, 0, 0,  1, 0, 0,    // Bars 7-8
  1, 0, 0,  1, 0, 0,    // Bars 9-10
  1, 0, 0,  1, 0, 0,    // Bars 11-12
  1, 0, 0,  1, 0, 0,    // Bars 13-14
  1, 0, 0,  1, 0, 0,    // Bars 15-16
  1, 0, 0,  1, 0, 0,    // Bars 17-18
  1, 0, 0,  1, 0, 0,    // Bars 19-20
  1, 0, 0,  1, 0, 0,    // Bars 21-22
  1, 0, 0,  0, 0, 0,    // Bars 23-24
};

// ============================================
// SONG INDEX
// ============================================
const uint8_t* const melodies[] PROGMEM = {
  melody_jingle_bells,
  melody_we_wish,
  melody_tannenbaum,
  melody_deck_halls,
  melody_silent_night
};

const uint8_t* const rhythms[] PROGMEM = {
  rhythm_jingle_bells,
  rhythm_we_wish,
  rhythm_tannenbaum,
  rhythm_deck_halls,
  rhythm_silent_night
};

const uint8_t song_lengths[] PROGMEM = {
  JINGLE_BELLS_LEN,
  WE_WISH_LEN,
  TANNENBAUM_LEN,
  DECK_HALLS_LEN,
  SILENT_NIGHT_LEN
};

const char* song_names[] = {
  "Jingle Bells",
  "We Wish You",
  "O Tannenbaum", 
  "Deck the Halls",
  "Silent Night"
};

// Root notes for each song (for display)
const char* song_keys[] = {
  "C major",
  "C major", 
  "F major",
  "C major",
  "C major"
};

// Recommended tempo (BPM) and time signature
const uint8_t song_tempos[] = {
  120,  // Jingle Bells - upbeat
  100,  // We Wish You - moderate waltz
  72,   // O Tannenbaum - slow waltz
  120,  // Deck the Halls - bright
  60    // Silent Night - slow, peaceful
};

const char* song_time_sigs[] = {
  "4/4",
  "3/4",
  "3/4", 
  "4/4",
  "6/8"
};

#define NUM_SONGS 5

// ============================================
// STATE VARIABLES
// ============================================
uint8_t current_song = 0;
uint16_t step = 0;
uint8_t last_note = 0;

bool gate_active = false;
bool bell_active = false;
unsigned long gate_start = 0;
unsigned long bell_start = 0;
bool last_clock_state = false;

// Loop control
uint8_t grid_length = 64;      // Current grid length (16, 32, or 64)
uint8_t grid_index = 2;        // Index into GRID_LENGTHS array
uint8_t song_length = 64;      // Original song length
uint16_t loop_count = 0;       // How many times we've looped

// Pitch offset
int8_t pitch_offset = 0;       // -24 to +24 semitone offset

// Button state
bool last_btn_state = true;    // Active low, so true = released
unsigned long btn_press_time = 0;
bool btn_held = false;

// Mutation storage - stores modified notes in RAM
uint8_t mutated_notes[MAX_MUTATED_STEPS];
bool mutations_active[MAX_MUTATED_STEPS];  // Which steps have mutations
bool mutations_enabled = true;

// ============================================
// SETUP
// ============================================
void setup() {
  // Initialize Serial for debug output
  Serial.begin(115200);
  while (!Serial && millis() < 1000); // Wait up to 1s for Serial
  
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("  XMAS SEQUENCER - Modulove 2024"));
  Serial.println(F("  Festive Firmware for HAGIWO MOD1"));
  Serial.println(F("========================================"));
  Serial.println();
  Serial.println(F("Hardware pins:"));
  Serial.println(F("  D17 = Clock In"));
  Serial.println(F("  D9  = Gate Out [SWAPPED]"));
  Serial.println(F("  D10 = Pitch CV (PWM) [SWAPPED]"));
  Serial.println(F("  D11 = Bell Out"));
  Serial.println(F("  D3  = LED"));
  Serial.println(F("  D4  = Song Button"));
  Serial.println(F("  A0  = Grid Length"));
  Serial.println(F("  A1  = Pitch Offset"));
  Serial.println();
  
  // Configure pins
  pinMode(PIN_CLOCK_IN, INPUT);
  pinMode(PIN_PITCH_OUT, OUTPUT);
  pinMode(PIN_GATE_OUT, OUTPUT);
  pinMode(PIN_BELL_OUT, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_SONG_BTN, INPUT_PULLUP);  // Button with internal pullup
  pinMode(PIN_LOOP_LEN, INPUT);
  pinMode(PIN_PITCH_OFF, INPUT);
  
  // Set up Timer1 for higher PWM frequency on D9/D10
  // 31.25kHz PWM - easier to filter for clean CV
  TCCR1A = _BV(COM1A1) | _BV(COM1B1) | _BV(WGM10);
  TCCR1B = _BV(CS10); // No prescaler
  
  // Initialize random seed from floating analog pin
  randomSeed(analogRead(A2) + analogRead(A3));
  
  // Load last song from EEPROM
  load_song_from_eeprom();
  
  // Clear mutation storage
  reset_mutations();
  
  // Initial state
  analogWrite(PIN_PITCH_OUT, 0);
  digitalWrite(PIN_GATE_OUT, LOW);
  digitalWrite(PIN_BELL_OUT, LOW);
  
  // Get initial song length
  song_length = pgm_read_byte(&song_lengths[current_song]);
  grid_length = GRID_LENGTHS[grid_index];
  
  // Read initial button state
  last_btn_state = digitalRead(PIN_SONG_BTN);
  
  // Print initial song info
  print_song_info();
  
  // Startup animation
  startup_flash();
  
  Serial.println(F("Ready! Waiting for clock..."));
  Serial.println();
}

void startup_flash() {
  // Festive startup sequence
  for (int i = 0; i < 4; i++) {
    digitalWrite(PIN_GATE_OUT, HIGH);
    digitalWrite(PIN_BELL_OUT, i % 2);
    digitalWrite(PIN_LED, HIGH);
    analogWrite(PIN_PITCH_OUT, 100 + i * 35);
    delay(60);
    digitalWrite(PIN_GATE_OUT, LOW);
    digitalWrite(PIN_LED, LOW);
    delay(40);
  }
  digitalWrite(PIN_BELL_OUT, LOW);
  analogWrite(PIN_PITCH_OUT, 0);
}

// ============================================
// MAIN LOOP
// ============================================
void loop() {
  unsigned long now = millis();
  
  // Read loop length pot (A0) - snap to grid values
  uint16_t loop_pot = analogRead(PIN_LOOP_LEN);
  uint8_t new_grid_index = map(loop_pot, 0, 1023, 0, NUM_GRIDS - 1);
  new_grid_index = constrain(new_grid_index, 0, NUM_GRIDS - 1);
  
  // Grid changed?
  if (new_grid_index != grid_index) {
    grid_index = new_grid_index;
    grid_length = GRID_LENGTHS[grid_index];
    Serial.print(F(">> Grid changed to "));
    Serial.print(grid_length);
    Serial.println(F(" steps"));
    
    // LED feedback for grid change
    digitalWrite(PIN_LED, HIGH);
    delay(20);
    digitalWrite(PIN_LED, LOW);
  }
  
  // Read pitch offset pot (A1) - bipolar
  uint16_t pitch_pot = analogRead(PIN_PITCH_OFF);
  // Map to -24 to +24 (roughly +/- 2 octaves in our scale)
  // Center (512) = 0 offset
  pitch_offset = map(pitch_pot, 0, 1023, -24, 24);
  // Add dead zone in center
  if (abs(pitch_offset) < 2) pitch_offset = 0;
  
  // Handle song button
  handle_button(now);
  
  // Read clock input - edge detection
  bool clock_state = digitalRead(PIN_CLOCK_IN);
  
  // Rising edge triggers next step
  if (clock_state && !last_clock_state) {
    process_step();
  }
  last_clock_state = clock_state;
  
  // Handle bell trigger timeout
  if (bell_active && (now - bell_start >= BELL_TIME_MS)) {
    digitalWrite(PIN_BELL_OUT, LOW);
    bell_active = false;
  }
}

// ============================================
// BUTTON HANDLING
// ============================================
void handle_button(unsigned long now) {
  bool btn_state = digitalRead(PIN_SONG_BTN);
  
  // Button just pressed (falling edge - active low)
  if (!btn_state && last_btn_state) {
    btn_press_time = now;
    btn_held = false;
    Serial.println(F("BTN: pressed"));
  }
  
  // Button is being held
  if (!btn_state && !btn_held) {
    if (now - btn_press_time >= BTN_HOLD_MS) {
      // Long press - reset mutations
      Serial.println(F("BTN: long press - reset mutations"));
      reset_mutations();
      btn_held = true;
      // Flash LED to indicate reset
      for (int i = 0; i < 3; i++) {
        digitalWrite(PIN_LED, HIGH);
        delay(30);
        digitalWrite(PIN_LED, LOW);
        delay(30);
      }
    }
  }
  
  // Button just released (rising edge)
  if (btn_state && !last_btn_state) {
    Serial.println(F("BTN: released"));
    // Short press = next song (only if not held)
    if (!btn_held && (now - btn_press_time >= BTN_DEBOUNCE_MS)) {
      Serial.println(F("BTN: short press - next song"));
      next_song();
    }
    btn_held = false;
  }
  
  last_btn_state = btn_state;
}

// ============================================
// EEPROM FUNCTIONS
// ============================================
void load_song_from_eeprom() {
  // Check if EEPROM has been initialized
  if (EEPROM.read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC_VALUE) {
    uint8_t saved_song = EEPROM.read(EEPROM_SONG_ADDR);
    if (saved_song < NUM_SONGS) {
      current_song = saved_song;
      Serial.print(F("Loaded song #"));
      Serial.print(current_song + 1);
      Serial.println(F(" from EEPROM"));
    }
  } else {
    Serial.println(F("EEPROM not initialized, using default song"));
  }
}

void save_song_to_eeprom() {
  // Only write if value changed (reduces EEPROM wear)
  EEPROM.update(EEPROM_SONG_ADDR, current_song);
  EEPROM.update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VALUE);
  Serial.print(F("Saved song #"));
  Serial.print(current_song + 1);
  Serial.println(F(" to EEPROM"));
}

// ============================================
// SONG MANAGEMENT
// ============================================
void print_song_info() {
  Serial.println(F("----------------------------------------"));
  Serial.print(F("Song #"));
  Serial.print(current_song + 1);
  Serial.print(F(": "));
  Serial.println(song_names[current_song]);
  Serial.println(F("----------------------------------------"));
  
  Serial.print(F("  Key:        "));
  Serial.println(song_keys[current_song]);
  
  Serial.print(F("  Scale:      "));
  uint8_t scale_type = pgm_read_byte(&song_scales[current_song]);
  Serial.println(scale_type == SCALE_TYPE_F_MAJOR ? F("F Major") : F("C Major"));
  
  Serial.print(F("  Time Sig:   "));
  Serial.println(song_time_sigs[current_song]);
  
  Serial.print(F("  Tempo:      "));
  Serial.print(song_tempos[current_song]);
  Serial.println(F(" BPM"));
  
  // Calculate recommended clock rate
  // Clock is in 8th notes, so multiply BPM by 2
  uint16_t clock_rate = song_tempos[current_song] * 2;
  Serial.print(F("  Clock:      "));
  Serial.print(clock_rate);
  Serial.print(F(" PPM ("));
  Serial.print((float)clock_rate / 60.0, 2);
  Serial.println(F(" Hz)"));
  
  Serial.print(F("  Song steps: "));
  Serial.println(pgm_read_byte(&song_lengths[current_song]));
  
  Serial.print(F("  Grid:       "));
  Serial.print(grid_length);
  Serial.println(F(" steps"));
  
  // Show stretch/compress ratio
  float ratio = (float)grid_length / (float)pgm_read_byte(&song_lengths[current_song]);
  Serial.print(F("  Ratio:      "));
  if (ratio < 1.0) {
    Serial.print(F("compress "));
  } else if (ratio > 1.0) {
    Serial.print(F("stretch "));
  } else {
    Serial.print(F("1:1 "));
  }
  Serial.print(ratio, 2);
  Serial.println(F("x"));
  
  Serial.println(F("----------------------------------------"));
  Serial.println();
}

void next_song() {
  current_song = (current_song + 1) % NUM_SONGS;
  step = 0;
  loop_count = 0;
  song_length = pgm_read_byte(&song_lengths[current_song]);
  reset_mutations();
  
  // Save to EEPROM for next power-on
  save_song_to_eeprom();
  
  // Print new song info
  Serial.println();
  Serial.println(F(">> SONG CHANGED <<"));
  print_song_info();
  
  // LED feedback for song change (blink song number)
  for (uint8_t i = 0; i <= current_song; i++) {
    digitalWrite(PIN_LED, HIGH);
    delay(100);
    digitalWrite(PIN_LED, LOW);
    delay(100);
  }
}

void reset_mutations() {
  for (uint8_t i = 0; i < MAX_MUTATED_STEPS; i++) {
    mutations_active[i] = false;
    mutated_notes[i] = 0;
  }
  Serial.println(F("* Mutations reset to original melody"));
}

// ============================================
// STEP PROCESSING
// ============================================

// Map a grid step to the corresponding original song step
// This stretches or compresses the melody to fit the grid
uint8_t map_step_to_song(uint8_t grid_step, uint8_t grid_len, uint8_t song_len) {
  // Linear interpolation: which song step does this grid step represent?
  // grid_step / grid_len = song_step / song_len
  // song_step = (grid_step * song_len) / grid_len
  return (uint16_t)grid_step * song_len / grid_len;
}

void process_step() {
  unsigned long now = millis();
  
  // Get song data from PROGMEM
  song_length = pgm_read_byte(&song_lengths[current_song]);
  const uint8_t* melody_ptr = (const uint8_t*)pgm_read_ptr(&melodies[current_song]);
  const uint8_t* rhythm_ptr = (const uint8_t*)pgm_read_ptr(&rhythms[current_song]);
  
  // At loop restart, apply mutations for next pass
  if (step == 0) {
    loop_count++;
    
    // LED pulse at loop start
    digitalWrite(PIN_LED, HIGH);
    
    Serial.print(F(">> Loop #"));
    Serial.print(loop_count);
    Serial.print(F(" | Grid: "));
    Serial.print(grid_length);
    Serial.print(F(" steps | Song: "));
    Serial.print(song_length);
    Serial.println(F(" steps"));
    
    // Mutate a few random steps for the next loop
    if (mutations_enabled) {
      apply_loop_mutations(melody_ptr, grid_length, song_length);
    }
  }
  
  // Turn off LED after first few steps
  if (step == 2) {
    digitalWrite(PIN_LED, LOW);
  }
  
  // Map current grid step to original song step
  uint8_t song_step = map_step_to_song(step, grid_length, song_length);
  
  // Read melody - check for mutation first (mutations stored by grid position)
  uint8_t note_data;
  bool is_mutated = false;
  if (step < MAX_MUTATED_STEPS && mutations_active[step]) {
    note_data = mutated_notes[step];
    is_mutated = true;
  } else {
    note_data = pgm_read_byte(&melody_ptr[song_step]);
  }
  
  // Read rhythm pattern (also mapped)
  uint8_t rhythm_step = map_step_to_song(step, grid_length, song_length);
  uint8_t rhythm_hit = pgm_read_byte(&rhythm_ptr[rhythm_step]);
  
  // Debug output for each step (if enabled)
  #if DEBUG_STEPS
  Serial.print(F("  Grid "));
  Serial.print(step);
  Serial.print(F(" -> Song "));
  Serial.print(song_step);
  Serial.print(F(": "));
  if (note_data == TIE) {
    Serial.print(F("TIE"));
  } else if (note_data == NOTE_REST) {
    Serial.print(F("REST"));
  } else {
    Serial.print(F("PWM="));
    Serial.print(note_data);
  }
  if (is_mutated) Serial.print(F(" [M]"));
  if (rhythm_hit) Serial.print(F(" *BELL*"));
  Serial.println();
  #endif
  
  // Bell trigger from rhythm pattern
  if (rhythm_hit) {
    fire_bell(now);
  }
  
  // Handle TIE - keep gate high, maintain pitch
  if (note_data == TIE) {
    // Just advance, keep everything else unchanged
    step = (step + 1) % grid_length;
    return;
  }
  
  // Handle REST - turn off gate
  if (note_data == NOTE_REST) {
    digitalWrite(PIN_GATE_OUT, LOW);
    gate_active = false;
    step = (step + 1) % grid_length;
    return;
  }
  
  // ========================================
  // NORMAL NOTE - Reliable gate triggering
  // ========================================
  
  // 1. Turn gate OFF for retrigger (if it was on)
  if (gate_active) {
    digitalWrite(PIN_GATE_OUT, LOW);
    delayMicroseconds(RETRIGGER_US);
  }
  
  // 2. Set pitch CV first
  int16_t adjusted_note = (int16_t)note_data + pitch_offset;
  adjusted_note = constrain(adjusted_note, 20, 250);
  analogWrite(PIN_PITCH_OUT, (uint8_t)adjusted_note);
  last_note = adjusted_note;
  
  // 3. Small delay for pitch to settle
  delayMicroseconds(200);
  
  // 4. Gate ON
  digitalWrite(PIN_GATE_OUT, HIGH);
  gate_active = true;
  gate_start = millis();
  
  // 5. BLOCKING delay to guarantee minimum gate time
  //    This ensures envelope triggers reliably
  delay(MIN_GATE_MS);
  
  // Advance step (loop within grid length)
  step = (step + 1) % grid_length;
}

// ============================================
// MUTATION ENGINE - Scale Aware
// ============================================

// Get the scale array and length for current song
void get_current_scale(const uint8_t** scale_ptr, uint8_t* scale_len) {
  uint8_t scale_type = pgm_read_byte(&song_scales[current_song]);
  if (scale_type == SCALE_TYPE_F_MAJOR) {
    *scale_ptr = SCALE_F_MAJOR;
    *scale_len = SCALE_F_MAJOR_LEN;
  } else {
    *scale_ptr = SCALE_C_MAJOR;
    *scale_len = SCALE_C_MAJOR_LEN;
  }
}

// Find the index of the closest note in the scale
int8_t find_scale_index(uint8_t pwm_note, const uint8_t* scale, uint8_t scale_len) {
  int8_t closest_idx = 0;
  uint8_t closest_dist = 255;
  
  for (uint8_t i = 0; i < scale_len; i++) {
    uint8_t scale_note = pgm_read_byte(&scale[i]);
    uint8_t dist = abs((int16_t)pwm_note - (int16_t)scale_note);
    if (dist < closest_dist) {
      closest_dist = dist;
      closest_idx = i;
    }
  }
  return closest_idx;
}

// Get a note from the scale by index (clamped)
uint8_t get_scale_note(int8_t idx, const uint8_t* scale, uint8_t scale_len) {
  if (idx < 0) idx = 0;
  if (idx >= scale_len) idx = scale_len - 1;
  return pgm_read_byte(&scale[idx]);
}

// Called once per loop - mutates 1-3 random steps musically
void apply_loop_mutations(const uint8_t* melody_ptr, uint8_t grid_len, uint8_t song_len) {
  // Get the scale for this song
  const uint8_t* scale;
  uint8_t scale_len;
  get_current_scale(&scale, &scale_len);
  
  // Count current mutations
  uint8_t current_mutations = 0;
  for (uint8_t i = 0; i < grid_len && i < MAX_MUTATED_STEPS; i++) {
    if (mutations_active[i]) current_mutations++;
  }
  
  // Decide how many steps to mutate this loop (0-3)
  uint8_t num_mutations = 0;
  if (random(100) < MUTATION_PROBABILITY) num_mutations++;
  if (random(100) < MUTATION_PROBABILITY / 2) num_mutations++;
  if (random(100) < MUTATION_PROBABILITY / 4) num_mutations++;
  
  if (num_mutations == 0) {
    Serial.print(F("   ["));
    Serial.print(current_mutations);
    Serial.println(F(" steps mutated]"));
    return;
  }
  
  Serial.print(F("  ~ Mutating "));
  Serial.print(num_mutations);
  Serial.print(F(" step(s) ["));
  Serial.print(current_mutations);
  Serial.println(F(" already mutated]"));
  
  uint8_t attempts = 0;
  for (uint8_t i = 0; i < num_mutations && attempts < 20; i++) {
    attempts++;
    
    // Pick a random grid step
    uint8_t grid_step = random(grid_len);
    if (grid_step >= MAX_MUTATED_STEPS) continue;
    
    // Map to original song step to get base note
    uint8_t song_step = map_step_to_song(grid_step, grid_len, song_len);
    
    // Get the current note
    uint8_t original;
    if (mutations_active[grid_step]) {
      original = mutated_notes[grid_step];
    } else {
      original = pgm_read_byte(&melody_ptr[song_step]);
    }
    
    // Don't mutate ties or rests
    if (original == TIE || original == NOTE_REST) {
      i--;  // Try again
      continue;
    }
    
    // Apply a musical mutation
    uint8_t note = apply_musical_mutation(original, grid_step, scale, scale_len);
    
    // Store if changed
    if (note != original && note != NOTE_REST) {
      mutated_notes[grid_step] = note;
      mutations_active[grid_step] = true;
    }
  }
}

// Apply one musical mutation - stays in scale
uint8_t apply_musical_mutation(uint8_t original, uint8_t grid_step, const uint8_t* scale, uint8_t scale_len) {
  // Find where this note is in the scale
  int8_t scale_idx = find_scale_index(original, scale, scale_len);
  
  uint8_t mut_type = random(NUM_MUTATIONS);
  int8_t new_idx = scale_idx;
  uint8_t note = original;
  bool changed = false;
  
  switch (mut_type) {
    case MUT_SHIFT_UP:
      // Move up 1-2 scale degrees
      new_idx = scale_idx + random(1, 3);
      note = get_scale_note(new_idx, scale, scale_len);
      if (note != original) {
        Serial.print(F("    Grid "));
        Serial.print(grid_step);
        Serial.print(F(": step up -> PWM="));
        Serial.println(note);
        changed = true;
      }
      break;
      
    case MUT_SHIFT_DOWN:
      // Move down 1-2 scale degrees
      new_idx = scale_idx - random(1, 3);
      note = get_scale_note(new_idx, scale, scale_len);
      if (note != original) {
        Serial.print(F("    Grid "));
        Serial.print(grid_step);
        Serial.print(F(": step down -> PWM="));
        Serial.println(note);
        changed = true;
      }
      break;
      
    case MUT_OCTAVE_UP:
      // Move up an octave (7 scale degrees)
      new_idx = scale_idx + 7;
      if (new_idx < scale_len) {
        note = get_scale_note(new_idx, scale, scale_len);
        Serial.print(F("    Grid "));
        Serial.print(grid_step);
        Serial.print(F(": octave up -> PWM="));
        Serial.println(note);
        changed = true;
      }
      break;
      
    case MUT_OCTAVE_DOWN:
      // Move down an octave (7 scale degrees)
      new_idx = scale_idx - 7;
      if (new_idx >= 0) {
        note = get_scale_note(new_idx, scale, scale_len);
        Serial.print(F("    Grid "));
        Serial.print(grid_step);
        Serial.print(F(": octave down -> PWM="));
        Serial.println(note);
        changed = true;
      }
      break;
      
    case MUT_REST:
      // Occasionally add a rest (sparse)
      if (random(5) == 0) {
        note = NOTE_REST;
        Serial.print(F("    Grid "));
        Serial.print(grid_step);
        Serial.println(F(": -> REST"));
        changed = true;
      }
      break;
      
    case MUT_DOUBLE:
      // Skip - keep original
      break;
      
    case MUT_FIFTH_UP:
      // Move up a fifth (4 scale degrees in major scale)
      new_idx = scale_idx + 4;
      if (new_idx < scale_len) {
        note = get_scale_note(new_idx, scale, scale_len);
        Serial.print(F("    Grid "));
        Serial.print(grid_step);
        Serial.print(F(": fifth up -> PWM="));
        Serial.println(note);
        changed = true;
      }
      break;
      
    default:
      break;
  }
  
  return note;
}

void fire_bell(unsigned long now) {
  digitalWrite(PIN_BELL_OUT, HIGH);
  bell_active = true;
  bell_start = now;
}
