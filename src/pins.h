//
// these symbols should help make sense of the TB-303 CPU pinouts
//

/*
 * The PH pins are used to select which buttons/LEDs to engage using PG, PA, and PB.
 *
 * PA are receiving status info, and a few buttons.
 * PB are switched inputs for the buttons in the switch board.
 * PG are switched outputs for the LEDs.
 * PC are direct outputs for a few other LEDs (which also engage CMOS memory, unused).
 *
 * PD and PF are data bits for CV out, which is a 6-bit number for pitch in semitones.
 * PE0 is the Accent bit.
 *
 * PI1 is Clock for CV Out and Accent, and also engages Slide while held
 * PI2 is Gate out
 */

#include <Arduino.h>
//const int MAXPIN = 45;

// pinout with Teensy++ 2.0 fitted
// - enum symbol names correspond to the TB-303 CPU pins
// - comments indicate Teensy Port designations
enum TppPinout : uint8_t {
  MIDI_IN_PIN = 2, // PD2
  MIDI_OUT_PIN = 3, // PD3

  // Port C - Data inputs/outputs...?
  PC0_PIN = 4, // PD4
  PC1_PIN = 5, // PD5
  PC2_PIN = 6, // PD6
  PC3_PIN = 7, // PD7

  TIMEMODE_LED = PC0_PIN,
  ASHARP_LED = PC1_PIN,
  PITCHMODE_LED = PC2_PIN,
  FUNCTION_LED = PC3_PIN,

  // PI1 is Clock for the CV Out flip-flop, and also enables Slide while held
  PI1_PIN = 8, // Pitch data latch strobe - PE0
  PI2_PIN = 9, // Gate signal - PE1

  // Ports D & F - memory address to pitch data - CV out
  PD0_PIN = 10, // bit 0 - PC0
  PD1_PIN = 11, // bit 1 - PC1
  PD2_PIN = 12, // bit 2 - PC2
  PD3_PIN = 13, // bit 3 - PC3
  PF0_PIN = 14, // bit 4 - PC4
  PF1_PIN = 15, // bit 5 - PC5
  PF2_PIN = 16, // memory (unused) - PC6
  PF3_PIN = 17, // memory (unused) - PC7

  // Port E - memory address (probably unused)
  PE3_PIN = 1, // PD1
  PE2_PIN = 0, // PD0
  PE1_PIN = 19, // PE7
  PE0_PIN = 18, // used for Accent - PE6

  // Port B - Switch board INPUTS (buttons)
  PB3_PIN = 27, // PB7
  PB2_PIN = 26, // PB6
  PB1_PIN = 25, // PB5
  PB0_PIN = 24, // PB4

  // Port A - switched inputs to STATUS (TEMPO CLOCK, START/STOP, TAP)
  PA3_PIN = 23, // PB3
  PA2_PIN = 22, // PB2
  PA1_PIN = 21, // PB1
  PA0_PIN = 20, // PB0

  // Port H - switched outputs to STATUS, BUFFER, & GATE
  // These are the mux selectors for PG, PA, and PB
  PH0_PIN = 38, // PF0
  PH1_PIN = 39, // PF1
  PH2_PIN = 40, // PF2
  PH3_PIN = 41, // PF3

  // Port G - drive signals to switch board LEDs
  PG0_PIN = 42, // [1], [DEL], [DOWN], [5] - PF4
  PG1_PIN = 43, // [2], [INS], [UP], [6] - PF5
  PG2_PIN = 44, // [3], [F#], [ACCENT], [7] - PF6
  PG3_PIN = 45, // [4], [G#], [SLIDE], [8] - PF7
};

const uint8_t INPUTS[] = {
  // Teensy Port B
  PA0_PIN, PA1_PIN, PA2_PIN, PA3_PIN,
  PB0_PIN, PB1_PIN, PB2_PIN, PB3_PIN,
};
const uint8_t OUTPUTS[] = {
  // Teensy Port D
  PC0_PIN, PC1_PIN, PC2_PIN, PC3_PIN,
  PE2_PIN, PE3_PIN,

  // Teensy Port E
  PE0_PIN, PE1_PIN,
  PI1_PIN, PI2_PIN,

  // Teensy Port C
  PD0_PIN, PD1_PIN, PD2_PIN, PD3_PIN,
  PF0_PIN, PF1_PIN, PF2_PIN, PF3_PIN,
  // Teensy Port F
  PG0_PIN, PG1_PIN, PG2_PIN, PG3_PIN,
  PH0_PIN, PH1_PIN, PH2_PIN, PH3_PIN,

};

// switched inputs, polled sequentially
enum InputIndex : uint8_t {
  C_KEY, // 0 - PB0 with PH0 low
  D_KEY,
  E_KEY,
  F_KEY, // 3 - PB3 with PH0 low

  G_KEY, // 4 - PB0 with PH1 low
  A_KEY,
  B_KEY,
  C_KEY2, // 7 - PB3 with PH1 low

  DOWN_KEY, // 8 - PB0 + PH2 low
  UP_KEY,
  ACCENT_KEY,
  SLIDE_KEY, // 11 - PB3 + PH2 low

  FSHARP_KEY, // 12 - PB0 with PH3 low
  GSHARP_KEY,
  ASHARP_KEY,
  BACK_KEY, // 15 - PB3 with PH3 low

  WRITE_MODE, // 16 - PA0 with PH0 low
  TRACK_SEL,
  CSHARP_KEY,
  DSHARP_KEY, // 19 - PA3 with PH0 low

  TRACK_BIT0, // 20 - PA0 with PH1 low
  TRACK_BIT1, // 21 - PA1
  TRACK_BIT2, // 22 - PA2
  DUMMY_PIN, // 23 unused

  CLEAR_KEY, // 24 - PA0 with PH2 low
  FUNCTION_KEY,
  PITCH_KEY,
  TIME_KEY, // 27 - PA3 with PH2 low

  DUMMY0, // 28 - PA0 w/ PH3 low, unused
  DUMMY1,
  DUMMY2,
  DUMMY3, // 31 - PA3 w/ PH3 low

  // Extra status pins - read with PH0-PH3 all held high
  RUN, // PA0
  TAP_NEXT, // turns on when holding first 4 white keys
  NOTHING,
  CLOCK, // PA4

  // I don't think these 4 actually do anything... PB0-PB3
  PBUTTON0, PBUTTON1, PBUTTON2, PBUTTON3,

  INPUT_COUNT,
  EXTRA_PIN_OFFSET = RUN,
};


//
// --- useful pin correlations ---
//
const uint8_t select_pin[4] = {
  PH0_PIN, PH1_PIN, PH2_PIN, PH3_PIN,
};
const uint8_t button_pins[4] = {
  PB0_PIN, PB1_PIN, PB2_PIN, PB3_PIN,
};
const uint8_t status_pins[] = {
  PA0_PIN, PA1_PIN, PA2_PIN, PA3_PIN,
};

// The PG and PH pins are all part of PORTF on the Teensy,
// which can simply be written as one byte.
// Each LED in the switchboard matrix can be defined as series of bytes as addresses.
// Welcome to CS-450
const uint8_t led_bytes[16] = {
  // PG  PH
  0b00011110,
  0b00101110,
  0b01001110,
  0b10001110,

  0b00011101,
  0b00101101,
  0b01001101,
  0b10001101,

  0b00011011,
  0b00101011,
  0b01001011,
  0b10001011,

  0b00010111,
  0b00100111,
  0b01000111,
  0b10000111,
};

// data + function bundles
struct PinPair {
  uint8_t led, button, pitch;
};
struct MatrixPin {
  uint8_t select, led, pitch;
  InputIndex button;
};

enum SignalState {
  STATE_OFF     = 0x00,
  STATE_RISING  = 0x01,
  STATE_FALLING = 0x02,
  STATE_ON      = 0x03,
};
struct PinState {
  uint8_t state = 0; // shiftreg
  void push(bool high) {
    state = (state << 1) | high;
  }
  // using simple 2-bit rise/fall detection
  // I don't think we need debouncing
  const bool rising() const { return (state & 0x03) == STATE_RISING; }
  const bool falling() const { return (state & 0x03) == STATE_FALLING; }
  const bool held() const { return state & STATE_ON; }
};

// more useful correlations
const MatrixPin switched_leds[16] = {
  // select,  LED,   pitch, Button,
  {PH0_PIN, PG0_PIN,  1, C_KEY}, // [1] key, C
  {PH0_PIN, PG1_PIN,  3, D_KEY}, // [2] key, D
  {PH0_PIN, PG2_PIN,  5, E_KEY}, // [3] key, E
  {PH0_PIN, PG3_PIN,  6, F_KEY}, // [4] key, F

  {PH1_PIN, PG0_PIN,  8, G_KEY}, // [5] key, G
  {PH1_PIN, PG1_PIN, 10, A_KEY}, // [6] key, A
  {PH1_PIN, PG2_PIN, 12, B_KEY}, // [7] key, B
  {PH1_PIN, PG3_PIN, 13, C_KEY2}, // [8] key, C2

  {PH2_PIN, PG0_PIN,  0, DOWN_KEY}, // [9] or [DOWN]
  {PH2_PIN, PG1_PIN,  0, UP_KEY}, // [0] or [UP]
  {PH2_PIN, PG2_PIN,  0, ACCENT_KEY}, // [100] or [ACCENT]
  {PH2_PIN, PG3_PIN,  0, SLIDE_KEY}, // [200] or [SLIDE]

  {PH3_PIN, PG0_PIN,  2, CSHARP_KEY}, // [DEL] or C#
  {PH3_PIN, PG1_PIN,  4, DSHARP_KEY}, // [INS] or D#
  {PH3_PIN, PG2_PIN,  7, FSHARP_KEY}, // F#
  {PH3_PIN, PG3_PIN,  9, GSHARP_KEY}, // G#
};
const MatrixPin main_leds[4] = {
  // select, led,         pitch, button
  {0,        TIMEMODE_LED,    0, TIME_KEY},
  {0,        ASHARP_LED,     11, ASHARP_KEY},
  {0,        PITCHMODE_LED,   0, PITCH_KEY},
  {0,        FUNCTION_LED,    0, FUNCTION_KEY},
};
const InputIndex pitched_keys[] = {
  C_KEY, CSHARP_KEY, D_KEY, DSHARP_KEY, E_KEY, F_KEY, FSHARP_KEY,
  G_KEY, GSHARP_KEY, A_KEY, ASHARP_KEY, B_KEY, C_KEY2
};
