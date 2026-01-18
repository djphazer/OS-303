#include <Arduino.h>
#include "pins.h"

//
// *** Utilities ***
//
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define CONSTRAIN(x, lb, ub) do { if (x < (lb)) x = lb; else if (x > (ub)) x = ub; } while (0)

// data + function bundles, aka structs
struct PinPair {
  uint8_t led, button, pitch;
};
struct MatrixPin {
  uint8_t select, led, pitch;
  InputIndex button;
};
struct PinState {
  uint8_t state = 0; // shiftreg
  void push(bool high) {
    state = (state << 1) | high;
  }
  // using simple 2-bit rise/fall detection
  // I don't think we need debouncing
  const bool rising() const { return (state & 0x03) == 0x01; }
  const bool falling() const { return (state & 0x03) == 0x02; }
  const bool held() const { return state & 0x03; }
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
const PinPair main_leds[4] = {
  // led,           button,     pitch
  {TIMEMODE_LED,    TIME_KEY,   0},
  {ASHARP_LED,      ASHARP_KEY, 11},
  {PITCHMODE_LED,   PITCH_KEY,  0},
  {FUNCTION_LED,    FUNCTION_KEY,   0},
};
const InputIndex pitched_keys[] = {
  C_KEY, CSHARP_KEY, D_KEY, DSHARP_KEY, E_KEY, F_KEY, FSHARP_KEY,
  G_KEY, GSHARP_KEY, A_KEY, ASHARP_KEY, B_KEY, C_KEY2
};

// util functions
void SendCV(bool slide = false) {
  // Clock for the D/A converter chip
  if (slide) digitalWriteFast(PI1_PIN, LOW);
  digitalWriteFast(PI1_PIN, HIGH);
  if (!slide) digitalWriteFast(PI1_PIN, LOW);
}

void SetGate(bool on) {
  digitalWriteFast(PI2_PIN, on? HIGH : LOW);
}

void SetAccent(bool on) {
  // Accent seems to use PE0
  digitalWriteFast(PE0_PIN, on ? HIGH : LOW);
}

void SetLed(uint8_t pin, bool enable = true) {
  digitalWriteFast(pin, enable ? HIGH : LOW);
}
void SetLed(PinPair pins, bool enable = true) {
  digitalWriteFast(pins.led, enable ? HIGH : LOW);
}
void SetLedSelection(uint8_t select_pin, uint8_t enable_mask) {
  const uint8_t switched_pins[4] = {
    PG0_PIN, PG1_PIN, PG2_PIN, PG3_PIN,
  };

  digitalWriteFast(select_pin, LOW);
  for (uint8_t i = 0; i < 4; ++i) {
    digitalWriteFast(switched_pins[i], (enable_mask & (1 << i))?HIGH:LOW);
  }
}
void SetLed(MatrixPin pins, bool enable = true) {
  if (enable) digitalWriteFast(pins.select, LOW);
  digitalWriteFast(pins.led, enable ? HIGH : LOW);
  if (enable) digitalWriteFast(pins.select, HIGH);
}


//void HandleClock() { }

void setup() {
  Serial.begin(9600);
  for (uint8_t i = 0; i < ARRAY_SIZE(INPUTS); ++i) {
    pinMode(INPUTS[i], INPUT); // pullup?
  }
  for (uint8_t i = 0; i < ARRAY_SIZE(OUTPUTS); ++i) {
    pinMode(OUTPUTS[i], OUTPUT);
  }
  for (uint8_t i = 0; i < 4; ++i) {
    digitalWriteFast(select_pin[i], HIGH);
  }
  //attachInterrupt(PA3_PIN, HandleClock, RISING);
}

void PollInputs(PinState *inputs) {
  digitalWriteFast(select_pin[0], HIGH);
  digitalWriteFast(select_pin[1], HIGH);
  digitalWriteFast(select_pin[2], HIGH);
  digitalWriteFast(select_pin[3], HIGH);
  // turn all LEDs off first
  digitalWriteFast(PG0_PIN, LOW);
  digitalWriteFast(PG1_PIN, LOW);
  digitalWriteFast(PG2_PIN, LOW);
  digitalWriteFast(PG3_PIN, LOW);

  // read PA and PB pins while select pins are high
  for (uint8_t i = 0; i < 4; ++i) {
    inputs[EXTRA_PIN_OFFSET + i].push(digitalReadFast(status_pins[i])); // PAx
    // not sure if these are actually used...
    //inputs[EXTRA_PIN_OFFSET + i+4].push(digitalReadFast(button_pins[i])); // PBx
  }

  // open each switched channel with select pin
  for (uint8_t i = 0; i < 4; ++i) {
    digitalWriteFast(select_pin[i], LOW); // PHx
    for (int j = 0; j < 4; ++j) {
      // read pins
      inputs[ 0 + i*4 + j].push(digitalReadFast(button_pins[j])); // PBx
      inputs[16 + i*4 + j].push(digitalReadFast(status_pins[j])); // PAx
    }
    digitalWriteFast(select_pin[i], HIGH); // PHx
  }
}

void loop() {
  static elapsedMillis timer = 0;
  static PinState inputs[INPUT_COUNT];
  static uint8_t ticks = 0;
  static uint8_t cv_out = 0; // semitone
  static int8_t octave = 0;
  static uint8_t clk_count = 0;
  static uint8_t note_count = 0;
  static uint8_t tracknum = 0;
  static uint16_t beat_ms = 200;
  static bool gate_on = false;
  static bool time_mode = false;
  static bool function_mode = false;
  static uint8_t step_pitch[16]; // 6-bit Pitch, Accent, and Slide
  static uint8_t step_time[16];
  static uint8_t step_idx = 0;

  // Poll all inputs...
  PollInputs(inputs);

  // --- turn LEDs on asap
  uint8_t mask = 0;
  for (uint8_t i = 0; i < 4; ++i) {
    // one row per tick, cycling through the 4 rows
    const uint8_t idx = (ticks & 0x3)*4 + i;
    // show the pressed button for testing
    mask |= inputs[switched_leds[idx].button].held() << i;
  }
  SetLedSelection(switched_leds[(ticks & 0x3)*4].select, gate_on ? mask : 0);
  // extra non-switched LEDs
  SetLed(TIMEMODE_LED, time_mode);
  SetLed(PITCHMODE_LED, !time_mode);
  SetLed(FUNCTION_LED, function_mode);
  SetLed(ASHARP_LED, gate_on && inputs[ASHARP_KEY].held());

  // process all inputs
  tracknum = uint8_t(inputs[TRACK_BIT0].held()
           | (inputs[TRACK_BIT1].held() << 1)
           | (inputs[TRACK_BIT2].held() << 2)) + 1;

  if (inputs[TIME_KEY].rising()) time_mode = true;
  if (inputs[PITCH_KEY].rising()) time_mode = false;

  if (inputs[FUNCTION_KEY].rising()) function_mode = !function_mode;
  if (inputs[CLEAR_KEY].rising()) cv_out = 0;
  if (inputs[BACK_KEY].rising()) step_idx = 0;

  if (inputs[UP_KEY].rising()) octave += 1;
  if (inputs[DOWN_KEY].rising()) octave -= 1;
  CONSTRAIN(octave, -2, 2);

  const bool clk_run = inputs[RUN].held();
  const bool track_mode = inputs[TRACK_SEL].held();
  const bool write_mode = inputs[WRITE_MODE].held();

  static bool send_note = true;
  bool gate_off = false;
  // DIN sync clock @ 24ppqn
  if (inputs[CLOCK].rising()) {
    const uint8_t clklen = (6*(step_time[step_idx]+1));
    ++clk_count %= clklen;
    if (clk_count == 0) {
      // sync and send quarter notes
      beat_ms = timer;
      timer = 0;
      send_note = true;
      ++step_idx %= 16;
    }
    if (clk_count == clklen/2) {
      gate_off = true;
    }
  }
  for (uint8_t i = 0; i < ARRAY_SIZE(pitched_keys); ++i) {
    // any keypress sends a note
    if (inputs[pitched_keys[i]].rising()) {
      send_note = true;
      timer = 0;
    }
    if (inputs[pitched_keys[i]].held()) {
      cv_out = i+1;
    }
  }

  if (clk_run) {
    // send sequence step
    if (send_note) {
      const uint8_t note = step_pitch[step_idx];

      SetAccent((note >> 6) & 1);
      // DAC for CV Out
      digitalWriteFast(PD0_PIN, (note >> 0) & 1);
      digitalWriteFast(PD1_PIN, (note >> 1) & 1);
      digitalWriteFast(PD2_PIN, (note >> 2) & 1);
      digitalWriteFast(PD3_PIN, (note >> 3) & 1);
      digitalWriteFast(PF0_PIN, (note >> 4) & 1);
      digitalWriteFast(PF1_PIN, (note >> 5) & 1);

      SendCV((note >> 7) & 1);

      SetGate(true);
      gate_on = true;
      ++note_count;
      send_note = false;
    }
  } else {
    // not run mode - send notes from keys
    if (send_note && cv_out) {
      SetAccent(inputs[ACCENT_KEY].held());

      // DAC for CV Out
      uint8_t note = 24 + cv_out - 1 + 12*(octave);
      const bool slide = inputs[SLIDE_KEY].held();

      digitalWriteFast(PD0_PIN, (note >> 0) & 1);
      digitalWriteFast(PD1_PIN, (note >> 1) & 1);
      digitalWriteFast(PD2_PIN, (note >> 2) & 1);
      digitalWriteFast(PD3_PIN, (note >> 3) & 1);
      digitalWriteFast(PF0_PIN, (note >> 4) & 1);
      digitalWriteFast(PF1_PIN, (note >> 5) & 1);
      SendCV(slide);
      SetGate(true);
      gate_on = true;
      ++note_count;
      send_note = false;
    }
  }

  // simple gate-off
  if (gate_on && gate_off) {
    //delay(1); // to mimic real 303, the gate off should be 1ms late
    SetGate(false);
    gate_on = false;
  }

  ++ticks;
}
