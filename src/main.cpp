#include <Arduino.h>
#include "pins.h"
#include "engine.h"

#define DEBUG 0

//
// *** Utilities ***
//
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define CONSTRAIN(x, lb, ub) do { if (x < (lb)) x = lb; else if (x > (ub)) x = ub; } while (0)

static constexpr uint16_t SWITCH_DELAY = 15; // microseconds

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
void SetLed(const MatrixPin pins, bool enable = true) {
  if (enable && pins.select) digitalWriteFast(pins.select, LOW);
  digitalWriteFast(pins.led, enable ? HIGH : LOW);
  if (enable && pins.select) digitalWriteFast(pins.select, HIGH);
}

void FlashLed(const MatrixPin led) {
  static elapsedMillis timer = 0;
  static bool onoff = false;
  if (timer > 100) {
    // blah blah
    SetLed(led, onoff);
    onoff = !onoff;
  }
}
void SetLedSelection(uint8_t select_pin, uint8_t enable_mask) {
  const uint8_t switched_pins[4] = {
    PG0_PIN, PG1_PIN, PG2_PIN, PG3_PIN,
  };

  PORTF = 0x0f;
  delayMicroseconds(SWITCH_DELAY);
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

  // TODO:
  //const uint8_t ledseq[] = { PITCHMODE_LED, CSHARP_LED, DSHARP_KEY };
  //for (uint8_t i = 0; i < 16; ++i) { }
}

void PollInputs(PinState *inputs) {
  //PORTF = 0x00;
  PORTF = 0x0f;
  delayMicroseconds(SWITCH_DELAY);
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

// -=- globals -=-
static uint8_t ticks = 0;

static PinState inputs[INPUT_COUNT];

static uint8_t tracknum = 0;
static bool gate_on = false;
static bool send_note = true;
static bool step_counter = false;

// this is where the magic happens
static Engine engine;

//static elapsedMillis timer = 0;
//static uint16_t beat_ms = 200;


void loop() {
  // Poll all inputs... every single tick
  //if ((ticks & 0x03) == 0)
  PollInputs(inputs);

  const bool clk_run = inputs[RUN].held();

#if DEBUG
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
#endif

  // --- turn LEDs on every 4 ticks
  if ((ticks & 0x03) == 1) {
    const uint8_t cycle = (ticks >> 2) & 0x3; // scanner for select pins, bits 0-3
    uint8_t mask = 0;

    // TODO: setting the LEDs needs a lot of work

    // chasing light for pattern step
    if (clk_run && (engine.get_time_pos() >> 2) == cycle)
      mask = led_bytes[engine.get_time_pos() % 16] >> 4;

    if (gate_on) {
      for (uint8_t i = 0; i < 4; ++i) {
        // one row per tick, cycling through the 4 rows
        const uint8_t idx = cycle*4 + i;

        // show the pressed button for testing
        //mask |= inputs[switched_leds[idx].button].held() << i;
      }
    }

    SetLedSelection(switched_leds[cycle*4].select, mask);
    // extra non-switched LEDs
    SetLed(TIMEMODE_LED, engine.get_mode() == TIME_MODE);
    SetLed(PITCHMODE_LED, engine.get_mode() == PITCH_MODE);
    SetLed(FUNCTION_LED, engine.get_mode() == NORMAL_MODE);
    SetLed(ASHARP_LED, gate_on && inputs[ASHARP_KEY].held());
  }

  const bool track_mode = inputs[TRACK_SEL].held();
  const bool write_mode = inputs[WRITE_MODE].held();
  const bool fn_mod = inputs[FUNCTION_KEY].held();
  const bool clear_mod = inputs[CLEAR_KEY].held();
  const bool edit_mode = inputs[TAP_NEXT].held();

  // process all inputs
  tracknum = uint8_t(inputs[TRACK_BIT0].held()
           | (inputs[TRACK_BIT1].held() << 1)
           | (inputs[TRACK_BIT2].held() << 2));

  if (inputs[TIME_KEY].rising()) engine.SetMode(TIME_MODE);
  if (inputs[PITCH_KEY].rising()) engine.SetMode(PITCH_MODE);
  if (inputs[FUNCTION_KEY].rising()) engine.SetMode(NORMAL_MODE);

  if (inputs[BACK_KEY].rising()) engine.Reset();

  if (fn_mod && write_mode) {
    if (inputs[DOWN_KEY].rising()) {
      // TODO: .....
      /*
      if (step_counter)
        step_counter = pattern[p_select].BumpLength();
      else {
        pattern[p_select].length = 1;
        step_counter = true;
      }
      */
    }
  } else {
    if (inputs[UP_KEY].rising()) engine.NudgeOctave(1);
    if (inputs[DOWN_KEY].rising()) engine.NudgeOctave(-1);
  }

  bool gate_off = false;

  // catch falling edge of RUN
  if (inputs[RUN].falling()) {
    gate_off = true;
  }

  // DIN sync clock @ 24ppqn
  if (inputs[CLOCK].rising() && clk_run) {
    send_note = engine.Clock(); // true is like a rising edge

    if (send_note) {
      // hold CLEAR + BACK in write mode to generate random stuff
      if (!track_mode && write_mode && clear_mod && inputs[BACK_KEY].held()) {
        // TODO: * GENERATE! *
        engine.Generate();
      }
    }
  }

  // check all pitch keys...
  uint8_t notes_on = 0;
  for (uint8_t i = 0; i < ARRAY_SIZE(pitched_keys); ++i) {
    // any keypress sends a note
    if (inputs[pitched_keys[i]].rising()) {
      engine.NoteOn(i);
      send_note = true;
      //timer = 0;

      if (write_mode && !track_mode && engine.get_mode() == PITCH_MODE) {
        engine.SetPitch(i, (inputs[ACCENT_KEY].held() << 6) | (inputs[SLIDE_KEY].held() << 7));
      }
    }
    if (inputs[pitched_keys[i]].held()) {
      ++notes_on;
    }

    if (inputs[pitched_keys[i]].falling()) {
      engine.NoteOff(i);
      gate_off = true;
    }
  }
  gate_off = gate_off && (0 == notes_on);

  if (inputs[TAP_NEXT].rising()) {
    send_note = engine.Advance();
  }
  if (edit_mode) {
    gate_off = false;
    // TODO: check a buncha button actions here
    if (inputs[ACCENT_KEY].rising()) engine.ToggleAccent();
    if (inputs[SLIDE_KEY].rising()) engine.ToggleSlide();
    // etc.
  }
  if (inputs[TAP_NEXT].falling()) {
    gate_off = true;
  }

  // pattern write mode
  if (write_mode && !track_mode && engine.get_mode() == TIME_MODE) {
    if (inputs[ACCENT_KEY].rising())
      engine.SetTime(0);

    if (inputs[DOWN_KEY].rising())
      engine.SetTime(1);

    if (inputs[UP_KEY].rising())
      engine.SetTime(2);
  }

  if (clk_run) {
    // send sequence step
    if (send_note && engine.get_gate()) {
      bool slide_on = engine.get_slide();

      // DAC for CV Out
      PORTC = engine.get_pitch();

      // turn  bits off first
      PORTE = 0x00;
      PORTE = 0b11 | engine.get_accent();
      if (!slide_on) // turn slide bit back off
        PORTE ^= 1;

      gate_on = true;
      send_note = false;
    }
    if (gate_on && !engine.get_gate()) {
      // turn just the gate bit off
      PORTE &= ~0b10;
      //SetGate(false);
      gate_on = false;
    }
  } else {
    // not run mode - send notes from keys
    if (send_note) {

      // DAC for CV Out
      bool slide_on = inputs[SLIDE_KEY].held();

      PORTC = engine.get_pitch();

      PORTE = 0x00; // turn gate/slide/accent bits off first
      PORTE = 0b11 | (inputs[ACCENT_KEY].held() << 6);
      if (!slide_on) // turn slide bit back off
        PORTE ^= 1;

      gate_on = true;
      send_note = false;
    }

    if (gate_on && gate_off) {
      SetGate(false);
      gate_on = false;
      gate_off = false;
    }
  }

  ++ticks;
}
