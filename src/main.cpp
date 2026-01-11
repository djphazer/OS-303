#include <Arduino.h>

const int MAXPIN = 45;

enum PinoutDefs : uint8_t {
  MIDI_IN_PIN = 2,
  MIDI_OUT_PIN = 3,

  // Port C - Data inputs/outputs...?
  PC0_PIN = 4,
  PC1_PIN = 5,
  PC2_PIN = 6,
  PC3_PIN = 7,

  TIMEMODE_LED = PC0_PIN,
  BLACKKEY5_LED = PC1_PIN, // aka PC1
  PITCHMODE_LED = PC2_PIN, // aka PC2
  FUNCTION_LED = PC3_PIN, // aka PC3

  // PI1 is Clock for the CV Out flip-flop
  PI1_PIN = 8, // Pitch data latch strobe
  PI2_PIN = 9, // Gate signal

  // Ports D & F - memory address to pitch data - CV out
  PD0_PIN = 10,
  PD1_PIN = 11,
  PD2_PIN = 12,
  PD3_PIN = 13,
  PF0_PIN = 14,
  PF1_PIN = 15,
  PF2_PIN = 16,
  PF3_PIN = 17,

  // Port E - memory address (probably unused)
  PE3_PIN = 1,
  PE2_PIN = 0,
  PE1_PIN = 19,
  PE0_PIN = 18,

  // Port B - Switch board INPUTS
  PB3_PIN = 27,
  PB2_PIN = 26,
  PB1_PIN = 25,
  PB0_PIN = 24,

  // Port A - switched inputs to STATUS (TEMPO CLOCK, START/STOP, TAP)
  PA3_PIN = 23,
  PA2_PIN = 22,
  PA1_PIN = 21,
  PA0_PIN = 20,

  // Port H - switched outputs to STATUS, BUFFER, & GATE
  // ...I think these are the mux selectors for PG and PB
  PH0_PIN = 38,
  PH1_PIN = 39,
  PH2_PIN = 40,
  PH3_PIN = 41,

  // Port G - drive signals to switch board LEDs
  PG0_PIN = 42, // [1], [DEL], [DOWN], [5]
  PG1_PIN = 43, // [2], [INS], [UP], [6]
  PG2_PIN = 44, // [3], [F#], [ACCENT], [7]
  PG3_PIN = 45, // [4], [G#], [SLIDE], [8]
};

// Ports A and B are inputs
const uint8_t INPUTS[] = {
  PA0_PIN, PA1_PIN, PA2_PIN, PA3_PIN,
  PB0_PIN, PB1_PIN, PB2_PIN, PB3_PIN,
};
// Ports C, D, E, and F are both ways?
// Ports G and H are outputs
const uint8_t OUTPUTS[] = {
  PC0_PIN, PC1_PIN, PC2_PIN, PC3_PIN,
  PD0_PIN, PD1_PIN, PD2_PIN, PD3_PIN,
  PE0_PIN, PE1_PIN, PE2_PIN, PE3_PIN,
  PF0_PIN, PF1_PIN, PF2_PIN, PF3_PIN,
  PG0_PIN, PG1_PIN, PG2_PIN, PG3_PIN,
  PH0_PIN, PH1_PIN, PH2_PIN, PH3_PIN,
  PI1_PIN, PI2_PIN,
};

/*
 * I think PH pins are used to select which buttons/LEDs to engage using PG and PB
 * and probably other stuff, too - C# & D# buttons, which status lines to read on PA,
 * and the Time, Pitch, Function, and Clear buttons
 *
 * PA are receiving status info - selected pattern, run mode, tempo
 *
 * PI1 is Clock for CV Out and maybe Accent?
 * PI2 is Gate out
 *
 * CV Out seems more complex... a D/A converter is fed by the PD & PF pins
 */

//
// *** Utilities ***
//
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

struct PinPair {
  uint8_t led, button;
};
struct MatrixPin {
  uint8_t select, led, button;
};

bool Input(MatrixPin pair) {
  digitalWriteFast(pair.select, LOW);
  return digitalRead(pair.button);
}

void SendCV() {
  digitalWrite(PI1_PIN, HIGH);
  digitalWrite(PI1_PIN, LOW);
}

void SetGate(bool on) {
  digitalWriteFast(PI2_PIN, on? HIGH : LOW);
}

//
// --- useful pin information ---
//
const uint8_t select_pin[4] = {
  PH0_PIN, PH1_PIN, PH2_PIN, PH3_PIN,
};

const PinPair button_led_pairs[4] = {
    {PB0_PIN, PG0_PIN},
    {PB1_PIN, PG1_PIN},
    {PB2_PIN, PG2_PIN},
    {PB3_PIN, PG3_PIN},
};

const uint8_t direct_leds[] = {
  PC0_PIN, PC1_PIN, PC2_PIN, PC3_PIN,
};

/*
  PG0_PIN = 42, // [1], [DEL], [DOWN], [5]
  PG1_PIN = 43, // [2], [INS], [UP], [6]
  PG2_PIN = 44, // [3], [F#], [ACCENT], [7]
  PG3_PIN = 45, // [4], [G#], [SLIDE], [8]
*/
const MatrixPin buttonLeds[] = {
  {PH0_PIN, PG0_PIN, PB0_PIN}, // [1] key, C
  {PH0_PIN, PG1_PIN, PB1_PIN}, // [2] key, D
  {PH0_PIN, PG2_PIN, PB2_PIN}, // [3] key, E
  {PH0_PIN, PG3_PIN, PB3_PIN}, // [4] key, F

  {PH1_PIN, PG0_PIN, PB0_PIN}, // [5] key, G
  {PH1_PIN, PG1_PIN, PB1_PIN}, // [6] key, A
  {PH1_PIN, PG2_PIN, PB2_PIN}, // [7] key, B
  {PH1_PIN, PG3_PIN, PB3_PIN}, // [8] key, C

  {PH2_PIN, PG0_PIN, PB0_PIN}, // [9] or [DOWN]
  {PH2_PIN, PG1_PIN, PB1_PIN}, // [0] or [UP]
  {PH2_PIN, PG2_PIN, PB2_PIN}, // [100] or [ACCENT]
  {PH2_PIN, PG3_PIN, PB3_PIN}, // [200] or [SLIDE]

  {PH3_PIN, PG0_PIN, PB0_PIN}, // [DEL] or C#
  {PH3_PIN, PG1_PIN, PB1_PIN}, // [INS] or D#
  {PH3_PIN, PG2_PIN, PB2_PIN}, // F#
  {PH3_PIN, PG3_PIN, PB3_PIN}, // G#
};

void setup() {
  Serial.begin(9600);
  for (size_t i = 0; i < ARRAY_SIZE(INPUTS); ++i) {
    pinMode(INPUTS[i], INPUT);
  }
  for (size_t i = 0; i < ARRAY_SIZE(OUTPUTS); ++i) {
    pinMode(OUTPUTS[i], OUTPUT);
  }
  for (uint8_t i = 0; i < 4; ++i) {
    digitalWrite(select_pin[i], HIGH);
  }
}

void loop() {
  static uint32_t ticks = 0;
  /*
  for (size_t i = 0; i < ARRAY_SIZE(direct_leds); ++i) {
    digitalWrite(direct_leds[i], HIGH);
    delay(100);
    digitalWrite(direct_leds[i], LOW);
    delay(50);
  }
  */

  // Accent seems to use PE0
  /*
  digitalWrite(PE0_PIN, LOW);
  SendCV();
  delay(100);
  digitalWrite(PE0_PIN, HIGH);
  SendCV();
  delay(50);
  */

  // switched LED matrix here
  // Starting to look like we gotta read and write each matrix cell at the same time
  for (size_t i = 0; i < ARRAY_SIZE(select_pin); ++i) {
    bool buttonstate[4] = {false};

    // read buttons
    for (int j = 0; j < 4; ++j) {
      buttonstate[j] = digitalRead(button_led_pairs[j].button);
    }

    digitalWrite(select_pin[i], LOW); // open channel for new values

    // echo the buttons with the LEDs
    for (int j = 0; j < 4; ++j) {
      digitalWrite(button_led_pairs[j].led, !buttonstate[j]);
    }

    digitalWrite(select_pin[i], HIGH); // hold values
  }

  // DAC for CV Out
  // sending a ramp up as test
  digitalWrite(PD0_PIN, (ticks >> 0) & 1);
  digitalWrite(PD1_PIN, (ticks >> 1) & 1);
  digitalWrite(PD2_PIN, (ticks >> 2) & 1);
  digitalWrite(PD3_PIN, (ticks >> 3) & 1);
  digitalWrite(PF0_PIN, (ticks >> 4) & 1);
  digitalWrite(PF1_PIN, (ticks >> 5) & 1);
  SendCV();

  ++ticks;
  delay(1);
}
