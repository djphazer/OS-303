#include <Arduino.h>
#include "pins.h"

//
// *** Utilities ***
//
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define CONSTRAIN(x, lb, ub) do { if (x < (lb)) x = lb; else if (x > (ub)) x = ub; } while (0)

static constexpr int MAX_STEPS = 32;

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

struct Sequence {
  uint8_t pitch[MAX_STEPS]; // 6-bit Pitch, Accent, and Slide
  uint8_t time[MAX_STEPS];  // 0=rest, 1=note, 2=tie, 3=triplets?
  uint8_t length = 16;
  uint8_t pitch_pos, time_pos;

  const uint8_t get_pitch() const {
    return pitch[pitch_pos] & 0x3f;
  }
  const uint8_t get_accent() const {
    return pitch[pitch_pos] & (1<<6);
  }
  const bool get_slide() const {
    return pitch[pitch_pos] & (1<<7);
  }
  const bool is_tied() const {
    return time_pos < length && time[time_pos+1] == 2;
  }

  void SetTime(uint8_t t) {
    time[time_pos] = t;
  }
  void SetPitch(uint8_t p) {
    // easier to just let the pitch include the accent/slide bits
    pitch[pitch_pos] = p;
    // (p & 0x3f) | (pitch[pitch_pos] & 0xc0);
  }
  void SetLength(uint8_t len) { length = constrain(len, 1, MAX_STEPS); }
  bool BumpLength() {
    if (++length == MAX_STEPS) return false;
    return true;
  }

  void RegenTime() {
    time[time_pos] = random();
  }
  void RegenPitch() {
    pitch[pitch_pos] = random();
  }

  void Reset() {
    pitch_pos = 0;
    time_pos = 0;
  }

  // returns false for rests
  bool Advance() {
    ++time_pos;
    time_pos %= length;
    if (time_pos == 0)
      pitch_pos = 0;
    else if (time[time_pos] == 1)
      ++pitch_pos;
    return time[time_pos];
  }
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
  digitalWriteFast(PI2_PIN, on ? HIGH : LOW);
}
void SetAccent(bool on) {
  digitalWriteFast(PE0_PIN, on ? HIGH : LOW);
}

void SetLed(uint8_t pin, bool enable = true) {
  digitalWriteFast(pin, enable ? HIGH : LOW);
}
void SetLed(PinPair pins, bool enable = true) {
  digitalWriteFast(pins.led, enable ? HIGH : LOW);
}
void SetLed(MatrixPin pins, bool enable = true) {
  if (enable) digitalWriteFast(pins.select, LOW);
  digitalWriteFast(pins.led, enable ? HIGH : LOW);
  if (enable) digitalWriteFast(pins.select, HIGH);
}
void SetLedSelection(uint8_t select_pin, uint8_t enable_mask) {
  const uint8_t switched_pins[4] = {
    PG0_PIN, PG1_PIN, PG2_PIN, PG3_PIN,
  };

  PORTF = 0x0f;
  delayMicroseconds(15);
  digitalWriteFast(select_pin, LOW);
  for (uint8_t i = 0; i < 4; ++i) {
    digitalWriteFast(switched_pins[i], (enable_mask & (1 << i))?HIGH:LOW);
  }
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
}

void PollInputs(PinState *inputs) {
  //PORTF = 0x00;
  PORTF = 0x0f;
  delayMicroseconds(15);
  //PORTF = 0xff;

  /*
  digitalWriteFast(select_pin[0], HIGH);
  digitalWriteFast(select_pin[1], HIGH);
  digitalWriteFast(select_pin[2], HIGH);
  digitalWriteFast(select_pin[3], HIGH);
  // all LEDs
  digitalWriteFast(PG0_PIN, HIGH);
  digitalWriteFast(PG1_PIN, HIGH);
  digitalWriteFast(PG2_PIN, HIGH);
  digitalWriteFast(PG3_PIN, HIGH);
  */

  // read PA and PB pins while select pins are high
  for (uint8_t i = 0; i < 4; ++i) {
    inputs[EXTRA_PIN_OFFSET + i].push(digitalReadFast(status_pins[i])); // PAx
    // not sure if these are actually used...
    //inputs[EXTRA_PIN_OFFSET + i+4].push(digitalReadFast(button_pins[i])); // PBx
  }

  //PORTF = 0x0f;

  /*
  digitalWriteFast(PG0_PIN, LOW);
  digitalWriteFast(PG1_PIN, LOW);
  digitalWriteFast(PG2_PIN, LOW);
  digitalWriteFast(PG3_PIN, LOW);
  */

  // open each switched channel with select pin
  for (uint8_t i = 0; i < 4; ++i) {
    digitalWriteFast(select_pin[i], LOW); // PHx
    for (uint8_t j = 0; j < 4; ++j) {
      // read pins
      inputs[ 0 + i*4 + j].push(digitalReadFast(button_pins[j])); // PBx
      inputs[16 + i*4 + j].push(digitalReadFast(status_pins[j])); // PAx
    }
    digitalWriteFast(select_pin[i], HIGH); // PHx
  }
}

enum SequencerMode {
  NORMAL_MODE,
  PITCH_MODE,
  TIME_MODE,
};

void loop() {
  //static elapsedMillis timer = 0;
  //static uint16_t beat_ms = 200;

  static PinState inputs[INPUT_COUNT];
  static uint8_t ticks = 0;
  static uint8_t cv_out = 0; // semitone
  static int8_t octave = 0;
  static uint8_t clk_count = 0;
  static uint8_t note_count = 0;
  static uint8_t tracknum = 0;
  static bool gate_on = false;
  static bool slide_on = false;
  static bool send_note = true;
  static bool step_counter = false;
  static uint8_t mode_ = NORMAL_MODE;

  // pattern storage
  static Sequence pattern[16]; // 32 steps each
  static uint8_t p_select = 0;
  static uint8_t chains[16][7]; // 7 tracks, up to 16 chained patterns

  // Poll all inputs... every single tick
  //if ((ticks & 0x03) == 0)
  PollInputs(inputs);

  const bool clk_run = inputs[RUN].held();
  if (inputs[RUN].rising()) {
    Serial.println("CLOCK RUN STARTED");
  }
  if (inputs[RUN].falling()) {
    Serial.println("CLOCK STOPPED");
  }

  if (Serial.available() && Serial.read()) {
    for (uint8_t i = 0; i < INPUT_COUNT/2; ++i) {
      Serial.printf("Input #%2u = %x   |  Input #%2u = %x\n", i, inputs[i].state, i + INPUT_COUNT/2, inputs[i + INPUT_COUNT/2].state);
    }
  }

  // --- turn LEDs on every 4 ticks
  if ((ticks & 0x03) == 1) {
    const uint8_t cycle = (ticks >> 2) & 0x3; // scanner for select pins, bits 0-3
    uint8_t mask = 0;

    // TODO: setting the LEDs needs a lot of work

    // chasing light for pattern step
    if (clk_run && (pattern[p_select].time_pos >> 2) == cycle)
      mask = led_bytes[pattern[p_select].time_pos % 16] >> 4;

    if (gate_on) {
      for (uint8_t i = 0; i < 4; ++i) {
        // one row per tick, cycling through the 4 rows
        const uint8_t idx = cycle*4 + i;
        // show the pressed button for testing
        mask |= inputs[switched_leds[idx].button].held() << i;
      }
    }

    SetLedSelection(switched_leds[cycle*4].select, mask);
    // extra non-switched LEDs
    SetLed(TIMEMODE_LED, mode_ == TIME_MODE);
    SetLed(PITCHMODE_LED, mode_ == PITCH_MODE);
    SetLed(FUNCTION_LED, mode_ == NORMAL_MODE);
    SetLed(ASHARP_LED, gate_on && inputs[ASHARP_KEY].held());
  }

  const bool track_mode = inputs[TRACK_SEL].held();
  const bool write_mode = inputs[WRITE_MODE].held();
  const bool fn_mod = inputs[FUNCTION_KEY].held();
  //const bool clear_mod = inputs[CLEAR_KEY].held();

  // process all inputs
  tracknum = uint8_t(inputs[TRACK_BIT0].held()
           | (inputs[TRACK_BIT1].held() << 1)
           | (inputs[TRACK_BIT2].held() << 2));

  if (inputs[TIME_KEY].rising()) mode_ = TIME_MODE;
  if (inputs[PITCH_KEY].rising()) mode_ = PITCH_MODE;
  if (inputs[FUNCTION_KEY].rising()) mode_ = NORMAL_MODE;

  if (inputs[CLEAR_KEY].rising()) cv_out = 0;
  if (inputs[BACK_KEY].rising()) pattern[p_select].Reset();

  if (fn_mod && write_mode) {
    if (inputs[DOWN_KEY].rising()) {
      if (step_counter)
        step_counter = pattern[p_select].BumpLength();
      else {
        pattern[p_select].length = 1;
        step_counter = true;
      }
    }
  } else {
    if (inputs[UP_KEY].rising()) octave += 1;
    if (inputs[DOWN_KEY].rising()) octave -= 1;
    CONSTRAIN(octave, -2, 2);
  }

  bool gate_off = false;

  // DIN sync clock @ 24ppqn
  if (inputs[CLOCK].rising()) {
    ++clk_count %= 6;

    if (clk_run) {
      if (clk_count == 0) { // sixteenth note advance
        send_note = pattern[p_select].Advance();
        cv_out = pattern[p_select].get_pitch();

        // hold CLEAR + BACK in write mode to generate random stuff
        if (!track_mode && write_mode && inputs[CLEAR_KEY].held() && inputs[BACK_KEY].held()) {
          // * GENERATE! *
          if (mode_ == PITCH_MODE)
            pattern[p_select].RegenPitch();
          else if (mode_ == TIME_MODE)
            pattern[p_select].RegenTime();
        }
      }

      // turn gate off halfway
      if (!slide_on && (clk_count == 3) && !pattern[p_select].is_tied()) {
        gate_off = !slide_on;
      }
    }
  }

  // check all pitch keys...
  uint8_t notes_on = 0;
  for (uint8_t i = 0; i < ARRAY_SIZE(pitched_keys); ++i) {
    // any keypress sends a note
    if (inputs[pitched_keys[i]].rising()) {
      send_note = true;
      //timer = 0;

      if (write_mode && !track_mode && mode_ == PITCH_MODE) {
        pattern[p_select].SetPitch((24 + i + 12 * octave) |
                                   (inputs[ACCENT_KEY].held() << 6) |
                                   (inputs[SLIDE_KEY].held() << 7));
      }
    }
    if (inputs[pitched_keys[i]].held()) {
      ++notes_on;
      cv_out = i+1 + 12*octave;
    }

    if (inputs[pitched_keys[i]].falling()) {
      gate_off = true;
    }
  }
  gate_off = gate_off && (0 == notes_on);

  if (inputs[TAP_NEXT].rising()) {
    send_note = pattern[p_select].Advance();
    cv_out = pattern[p_select].get_pitch();
  }
  if (inputs[TAP_NEXT].held()) {
    gate_off = false;
    // TODO: check a buncha button actions here
  }
  if (inputs[TAP_NEXT].falling()) {
    gate_off = true;
  }

  // pattern write mode
  if (write_mode && !track_mode) {
    if (mode_ == TIME_MODE) {
      if (inputs[ACCENT_KEY].rising())
        pattern[p_select].SetTime(0);

      if (inputs[DOWN_KEY].rising())
        pattern[p_select].SetTime(1);

      if (inputs[UP_KEY].rising())
        pattern[p_select].SetTime(2);
    }
  }

  if (clk_run) {
    // send sequence step
    if (send_note) {
      slide_on = pattern[p_select].get_slide();

      // DAC for CV Out
      PORTC = pattern[p_select].get_pitch();

      // turn  bits off first
      PORTE = 0x00;
      PORTE = 0b11 | pattern[p_select].get_accent();
      if (!slide_on) // turn slide bit back off
        PORTE ^= 1;

      gate_on = true;
      ++note_count;
      send_note = false;
    }
  } else {
    // not run mode - send notes from keys
    if (send_note && cv_out) {

      // DAC for CV Out
      uint8_t note = 24 + cv_out - 1 + 12*octave;
      slide_on = inputs[SLIDE_KEY].held();

      PORTC = note;

      PORTE = 0x00; // turn gate/slide/accent bits off first
      PORTE = 0b11 | (inputs[ACCENT_KEY].held() << 6);
      if (!slide_on) // turn slide bit back off
        PORTE ^= 1;

      gate_on = true;
      ++note_count;
      send_note = false;
    }
  }

  // catch falling edge of RUN
  if (inputs[RUN].falling()) {
    gate_off = true;
  }

  // simple gate-off
  if (gate_on && gate_off) {
    //delay(1); // to mimic real 303, the gate off should be 1ms late
    SetGate(false);
    PORTE ^= 1;
    PORTE ^= 1;
    gate_on = false;
  }

  ++ticks;
}
