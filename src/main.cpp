#include <Arduino.h>

//const int MAXPIN = 45;

enum PinoutDefs : uint8_t {
  MIDI_IN_PIN = 2,
  MIDI_OUT_PIN = 3,

  // Port C - Data inputs/outputs...?
  PC0_PIN = 4,
  PC1_PIN = 5,
  PC2_PIN = 6,
  PC3_PIN = 7,

  TIMEMODE_LED = PC0_PIN,
  ASHARP_LED = PC1_PIN,
  PITCHMODE_LED = PC2_PIN,
  FUNCTION_LED = PC3_PIN,

  // PI1 is Clock for the CV Out flip-flop
  PI1_PIN = 8, // Pitch data latch strobe
  PI2_PIN = 9, // Gate signal

  // Ports D & F - memory address to pitch data - CV out
  PD0_PIN = 10, // bit 0
  PD1_PIN = 11, // bit 1
  PD2_PIN = 12, // bit 2
  PD3_PIN = 13, // bit 3
  PF0_PIN = 14, // bit 4
  PF1_PIN = 15, // bit 5
  PF2_PIN = 16, // memory (unused)
  PF3_PIN = 17, // memory (unused)

  // Port E - memory address (probably unused)
  PE3_PIN = 1,
  PE2_PIN = 0,
  PE1_PIN = 19,
  PE0_PIN = 18, // used for Accent

  // Port B - Switch board INPUTS (buttons)
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
  // These are the mux selectors for PG, PA, and PB
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

const uint8_t INPUTS[] = {
  PA0_PIN, PA1_PIN, PA2_PIN, PA3_PIN,
  PB0_PIN, PB1_PIN, PB2_PIN, PB3_PIN,
};
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
 * The PH pins are used to select which buttons/LEDs to engage using PG, PA, and PB.
 * PA are receiving status info, and a few buttons.
 * PB are switched inputs for the buttons in the switch board.
 * PG are switched outputs for the LEDs.
 * PC are direct outputs for a few other LEDs (which also engage CMOS memory, unused).
 *
 * PD and PF are data bits for CV out, which is a 6-bit number for pitch in semitones.
 * PE0 is the Accent bit.
 *
 * PI1 is Clock for CV Out and Accent
 * PI2 is Gate out
 */

enum InputIndex : uint8_t {
  C_KEY, // 0
  D_KEY,
  E_KEY,
  F_KEY,

  G_KEY, // 4
  A_KEY,
  B_KEY,
  C_KEY2,

  DOWN_KEY, // 8
  UP_KEY,
  ACCENT_KEY,
  SLIDE_KEY,

  FSHARP_KEY, // 12
  GSHARP_KEY,
  ASHARP_KEY,
  BACK_KEY,

  WRITE_MODE, // 16
  TRACK_SEL,
  CSHARP_KEY,
  DSHARP_KEY,

  TRACK_BIT0, // 20
  TRACK_BIT1,
  TRACK_BIT2,
  DUMMY_PIN, // 23 unused

  CLEAR_KEY, // 24
  FUNCTION_KEY,
  PITCH_KEY,
  TIME_KEY,

  DUMMY0, // 28
  DUMMY1,
  DUMMY2,
  DUMMY3,
};

//
// *** Utilities ***
//
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

struct PinPair {
  uint8_t led, button, pitch;
};
struct MatrixPin {
  uint8_t select, led, pitch;
  InputIndex button;
};
struct PinState {
  uint8_t state = 0; // shiftreg for debounce
  void push(bool high) {
    state = (state << 1) | high;
  }
  const bool rising() const { return state == 0x01; }
  const bool falling() const { return state == 0xfe; }
  const bool held() const { return state != 0x00; }
};

// util functions
void SendCV() {
  // Clock for the D/A converter chip
  digitalWrite(PI1_PIN, HIGH);
  digitalWrite(PI1_PIN, LOW);
}

void SetGate(bool on) {
  digitalWriteFast(PI2_PIN, on? HIGH : LOW);
}

void SetAccent(bool on) {
  // Accent seems to use PE0
  digitalWrite(PE0_PIN, on ? LOW : HIGH);
}

void SetLed(PinPair pins, bool enable = true) {
  digitalWrite(pins.led, enable ? HIGH : LOW);
}
void SetLed(MatrixPin pins, bool enable = true) {
  digitalWrite(pins.led, enable ? HIGH : LOW);
  if (enable)
    digitalWrite(pins.select, LOW);
}

// --- useful pin mapping information ---
const uint8_t select_pin[4] = {
  PH0_PIN, PH1_PIN, PH2_PIN, PH3_PIN,
};
const uint8_t button_pins[4] = {
  PB0_PIN, PB1_PIN, PB2_PIN, PB3_PIN,
};
const uint8_t status_pins[] = {
  PA0_PIN, PA1_PIN, PA2_PIN, PA3_PIN,
};
const uint8_t direct_leds[] = {
  PC0_PIN, PC1_PIN, PC2_PIN, PC3_PIN,
};

const MatrixPin buttonLeds[16] = {
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
const PinPair extraButtonLeds[4] = {
  {TIMEMODE_LED,    TIME_KEY,   0},
  {ASHARP_LED,      ASHARP_KEY, 11},
  {PITCHMODE_LED,   PITCH_KEY,  0},
  {FUNCTION_LED,    FUNCTION_KEY,   0},
};

static constexpr uint16_t TEMPO = 90; // BPM
static constexpr uint16_t BEAT_MS = (60000 / TEMPO) / 4;

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
  static elapsedMillis timer = 0;
  static uint16_t ticks = 0;
  static PinState inputs[32];
  static uint8_t cv_out = 0;
  //static uint8_t octave = 0;

  // Poll all inputs
  // turn all LED pins off first
  digitalWrite(PG0_PIN, LOW);
  digitalWrite(PG1_PIN, LOW);
  digitalWrite(PG2_PIN, LOW);
  digitalWrite(PG3_PIN, LOW);
  for (size_t i = 0; i < 4; ++i) {
    // open each switched channel
    digitalWrite(select_pin[0], (i==0)?LOW:HIGH);
    digitalWrite(select_pin[1], (i==1)?LOW:HIGH);
    digitalWrite(select_pin[2], (i==2)?LOW:HIGH);
    digitalWrite(select_pin[3], (i==3)?LOW:HIGH);
    for (int j = 0; j < 4; ++j) {
      // read pins
      inputs[ 0 + i*4 + j].push(digitalRead(button_pins[j]));
      inputs[16 + i*4 + j].push(digitalRead(status_pins[j]));
    }
    digitalWrite(select_pin[i], HIGH);
  }

  cv_out = 0;
  // set LEDs last - leaves certain select pins low
  for (size_t i = 0; i < ARRAY_SIZE(buttonLeds); ++i) {
    if (inputs[i].rising()) {
      Serial.printf("Button index: %u\n", i);
    }
    if (inputs[i + 16].rising()) {
      Serial.printf("Status index: %u\n", i + 16);
    }

    const bool button_held = inputs[buttonLeds[i].button].held();
    SetLed(buttonLeds[i], button_held);
    if (button_held && buttonLeds[i].pitch > cv_out)
      cv_out = buttonLeds[i].pitch;
  }
  // extra non-switched LEDs
  for (size_t i = 0; i < 4; ++i) {
    const bool button_held = inputs[extraButtonLeds[i].button].held();
    SetLed(extraButtonLeds[i], button_held);
    if (button_held && extraButtonLeds[i].pitch > cv_out)
      cv_out = extraButtonLeds[i].pitch;
  }

  bool send_note = false;
  if (timer > BEAT_MS) {
    timer = 0;
    send_note = true;
  }
  if (cv_out && send_note) {
    // Accent on and off for testing
    SetAccent(ticks & 1); // basically random

    // DAC for CV Out
    uint8_t note = cv_out - 1;
    digitalWrite(PD0_PIN, (note >> 0) & 1);
    digitalWrite(PD1_PIN, (note >> 1) & 1);
    digitalWrite(PD2_PIN, (note >> 2) & 1);
    digitalWrite(PD3_PIN, (note >> 3) & 1);
    digitalWrite(PF0_PIN, (note >> 4) & 1);
    digitalWrite(PF1_PIN, (note >> 5) & 1);
    SendCV();
  }
  if (cv_out) {
    // gate pulse at 50%
    SetGate(timer < BEAT_MS / 2);
  }

  ++ticks;
}
