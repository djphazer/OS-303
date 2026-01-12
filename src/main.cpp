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
  uint8_t button, led;
};
struct MatrixPin {
  uint8_t select, led, button, pitch;
};

void SendCV() {
  digitalWrite(PI1_PIN, HIGH);
  digitalWrite(PI1_PIN, LOW);
}

void SetGate(bool on) {
  digitalWriteFast(PI2_PIN, on? HIGH : LOW);
}

void SetAccent(bool on) {
  // Accent seems to use PE0
  digitalWrite(PE0_PIN, on ? LOW : HIGH);
  SendCV();
}

void SetLed(MatrixPin pins, bool enable = true) {
  digitalWrite(pins.led, enable ? HIGH : LOW);
  if (enable)
    digitalWrite(pins.select, LOW);
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
const uint8_t status_pins[] = {
  PA0_PIN, PA1_PIN, PA2_PIN, PA3_PIN,
};

/*
  PG0_PIN = 42, // [1], [DEL], [DOWN], [5]
  PG1_PIN = 43, // [2], [INS], [UP], [6]
  PG2_PIN = 44, // [3], [F#], [ACCENT], [7]
  PG3_PIN = 45, // [4], [G#], [SLIDE], [8]
*/
const MatrixPin buttonLeds[] = {
  // select,  LED,   Button
  {PH0_PIN, PG0_PIN, PB0_PIN, 1}, // [1] key, C
  {PH0_PIN, PG1_PIN, PB1_PIN, 3}, // [2] key, D
  {PH0_PIN, PG2_PIN, PB2_PIN, 5}, // [3] key, E
  {PH0_PIN, PG3_PIN, PB3_PIN, 6}, // [4] key, F

  {PH1_PIN, PG0_PIN, PB0_PIN, 8}, // [5] key, G
  {PH1_PIN, PG1_PIN, PB1_PIN, 10}, // [6] key, A
  {PH1_PIN, PG2_PIN, PB2_PIN, 12}, // [7] key, B
  {PH1_PIN, PG3_PIN, PB3_PIN, 13}, // [8] key, C

  {PH2_PIN, PG0_PIN, PB0_PIN}, // [9] or [DOWN]
  {PH2_PIN, PG1_PIN, PB1_PIN}, // [0] or [UP]
  {PH2_PIN, PG2_PIN, PB2_PIN}, // [100] or [ACCENT]
  {PH2_PIN, PG3_PIN, PB3_PIN}, // [200] or [SLIDE]

  {PH3_PIN, PG0_PIN, PB0_PIN, 2}, // [DEL] or C#
  {PH3_PIN, PG1_PIN, PB1_PIN, 4}, // [INS] or D#
  {PH3_PIN, PG2_PIN, PB2_PIN, 7}, // F#
  {PH3_PIN, PG3_PIN, PB3_PIN, 9}, // G#
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

struct Button {
  uint8_t state = 0; // shiftreg
  void push(bool high) {
    state = (state << 1) | high;
  }
  const bool rising() const { return state == 0x01; }
  const bool falling() const { return state == 0xfe; }
  const bool held() const { return state != 0x00; }
};

static constexpr uint16_t TEMPO = 120; // BPM
static constexpr uint16_t BEAT_MS = (60000 / TEMPO);

void loop() {
  static uint32_t ticks = 0;
  static Button buttons[32];
  static uint16_t status_state = 0;
  static uint8_t cv_out = 0;
  static uint8_t octave = 0;

  // polling loop for switched LED matrix
  // Looks like we gotta read buttons and write LEDs at the same time
  status_state = 0;
  cv_out = 0;
  for (size_t i = 0; i < ARRAY_SIZE(select_pin); ++i) {
    // open each channel
    digitalWrite(select_pin[0], (i==0)?LOW:HIGH);
    digitalWrite(select_pin[1], (i==1)?LOW:HIGH);
    digitalWrite(select_pin[2], (i==2)?LOW:HIGH);
    digitalWrite(select_pin[3], (i==3)?LOW:HIGH);
    for (int j = 0; j < 4; ++j) {
      // read buttons and status pins
      buttons[ 0 + i*4 + j].push(digitalRead(buttonLeds[i*4 + j].button));
      buttons[16 + i*4 + j].push(digitalRead(status_pins[j]));
    }
    digitalWrite(select_pin[i], HIGH);
  }

  // set LEDs last - leaves select pins low
  for (size_t i = 0; i < ARRAY_SIZE(buttonLeds); ++i) {
    if (buttons[i].rising()) {
      Serial.printf("Button index: %u", i);
    }
    if (buttons[i + 16].rising()) {
      Serial.printf("Status index: %u", i + 16);
    }

    SetLed(buttonLeds[i], buttons[i].held());
    if (buttons[i].held() && buttonLeds[i].pitch > cv_out)
      cv_out = buttonLeds[i].pitch;
  }

  if (cv_out) {
    // Accent on and off for testing
    if ((ticks & 0x20) == 0x20) SetAccent(true);
    if ((ticks & 0x2f) == 0x00) SetAccent(false);

    // DAC for CV Out
    uint8_t note = cv_out - 1;
    digitalWrite(PD0_PIN, (note >> 0) & 1);
    digitalWrite(PD1_PIN, (note >> 1) & 1);
    digitalWrite(PD2_PIN, (note >> 2) & 1);
    digitalWrite(PD3_PIN, (note >> 3) & 1);
    digitalWrite(PF0_PIN, (note >> 4) & 1);
    digitalWrite(PF1_PIN, (note >> 5) & 1);
    SendCV();

    // gate pulse
    SetGate(true);
    delay(BEAT_MS / 8);
    SetGate(false);
    delay(BEAT_MS / 8);
  }

  ++ticks;
}
