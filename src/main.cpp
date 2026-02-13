// Copyright (c) 2026, Nicholas J. Michalek
//

#include <Arduino.h>
#include "pins.h"
#include "drivers.h"
#include "engine.h"
#include "MIDI.h"

#define DEBUG 0

//
// *** Utilities ***
//
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define CONSTRAIN(x, lb, ub) do { if (x < (lb)) x = lb; else if (x > (ub)) x = ub; } while (0)

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);


// -=-=- Globals -=-=-
static uint8_t ticks = 0;
static uint8_t clk_count = 0;

static PinState inputs[INPUT_COUNT];

static uint8_t tracknum = 0;
static bool gate_on = false;
static bool send_note = true;
static bool step_counter = false;

// this is where the magic happens
static Engine engine;


// ===== MAIN CODE LOGIC =====

void setup() {
  Serial1.begin(31250);
  MIDI.begin(MIDI_CHANNEL_OMNI);

  for (uint8_t i = 0; i < ARRAY_SIZE(INPUTS); ++i) {
    pinMode(INPUTS[i], INPUT); // pullup?
  }
  for (uint8_t i = 0; i < ARRAY_SIZE(OUTPUTS); ++i) {
    pinMode(OUTPUTS[i], OUTPUT);
  }
  for (uint8_t i = 0; i < 4; ++i) {
    digitalWriteFast(select_pin[i], HIGH);
  }

  Serial.begin(9600);

  const MatrixPin ledseq[] = {
    switched_leds[16 + 2],
    switched_leds[8],
    switched_leds[9],
    switched_leds[10],
    switched_leds[11],
    switched_leds[16 + 1],
    switched_leds[16 + 0],
    switched_leds[15],
    switched_leds[14],
    switched_leds[13],
    switched_leds[12],
    switched_leds[7],
    switched_leds[6],
    switched_leds[5],
    switched_leds[4],
    switched_leds[3],
    switched_leds[2],
    switched_leds[1],
    switched_leds[0],
  };

  int x = 3;
  do {
    for (uint8_t i = 0; i < ARRAY_SIZE(ledseq); ++i) {
      Leds::Set(ledseq[i], true);
      delay(20);
      Leds::Set(ledseq[i], false);
      delay(10);
    }
  } while (--x > 0);
}

void loop() {
  // Poll all inputs... every single tick
  //if ((ticks & 0x03) == 0)
  PollInputs(inputs);

  const bool clk_run = inputs[RUN].held();
  const bool track_mode = inputs[TRACK_SEL].held();
  const bool write_mode = inputs[WRITE_MODE].held();
  const bool fn_mod = inputs[FUNCTION_KEY].held();
  const bool clear_mod = inputs[CLEAR_KEY].held();
  const bool edit_mode = inputs[TAP_NEXT].held();

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

  // --- update LEDs on every 4 ticks
  //if ((ticks & 0x03) == 1) {
    //const uint8_t cycle = (ticks >> 2) & 0x3; // scanner for select pins, bits 0-3
    //uint8_t mask = 0;
  //}

  if (edit_mode) {
    if (engine.get_mode() == PITCH_MODE) {
      const uint8_t pitch = engine.get_pitch();
      Leds::Set(pitch_leds[pitch % 12], true);

      Leds::Set(ACCENT_KEY_LED, engine.get_accent());
      Leds::Set(SLIDE_KEY_LED, engine.get_slide());

      if (pitch < 24) Leds::Set(DOWN_KEY_LED, true);
      if (pitch >= 36) Leds::Set(UP_KEY_LED, true);
    }
  } else if (clk_run && write_mode) {
    // chasing light for pattern step
    Leds::Set(OutputIndex(engine.get_time_pos() & 0x7), true);
    Leds::Set(OutputIndex(CSHARP_KEY_LED + (engine.get_time_pos() >> 3)), true);
  } else {
    // Pattern Select
    for (uint8_t i = 0; i < 8; ++i) {
      if (inputs[i].rising()) engine.SetPattern((engine.get_patsel() >> 3) * 8 + i);
    }
    if (inputs[ACCENT_KEY].rising()) engine.SetPattern(engine.get_patsel() % 8);
    if (inputs[SLIDE_KEY].rising()) engine.SetPattern(engine.get_patsel() % 8 + 8);

    // flash LED for current pattern
    Leds::Set(OutputIndex(engine.get_patsel() & 0x7), clk_count < 12);
    // TODO: solid LED for queued pattern
  }

  for (uint8_t i = 0; i < 16; ++i) {
    // show all pressed buttons
    if (inputs[switched_leds[i].button].held())
      Leds::Set(OutputIndex(i), true);
  }

  // extra non-switched LEDs
  Leds::Set(TIME_MODE_LED, engine.get_mode() == TIME_MODE);
  Leds::Set(PITCH_MODE_LED, engine.get_mode() == PITCH_MODE);
  Leds::Set(FUNCTION_MODE_LED, engine.get_mode() == NORMAL_MODE);
  Leds::Set(ASHARP_KEY_LED, inputs[ASHARP_KEY].held() || (gate_on && (engine.get_pitch() % 12 == 10)));

  Leds::Send(ticks); // hardware output, framebuffer reset

  // -=-=- process all inputs -=-=-
  //
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

  // process all MIDI here
  bool clocked = false;
  static bool midi_clk = false;
  while (MIDI.read()) {
    if (MIDI.getType() == midi::MidiType::Clock) {
      clocked = true;
    }
    if (MIDI.getType() == midi::MidiType::Start) {
      midi_clk = true;
    }
    if (MIDI.getType() == midi::MidiType::Stop) {
      midi_clk = false;
    }
  }

  // DIN sync clock @ 24ppqn
  if (!midi_clk) {
    clocked = inputs[CLOCK].rising();
  }

  if (clocked) ++clk_count %= 24;

  if (clocked && clk_run) {
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
      if (write_mode) {
        engine.NoteOn(i);
        send_note = true;

        if (!track_mode && engine.get_mode() == PITCH_MODE) {
          engine.SetPitch(i, (inputs[ACCENT_KEY].held() << 6) | (inputs[SLIDE_KEY].held() << 7));
        }
      }
    }
    if (inputs[pitched_keys[i]].held()) {
      ++notes_on;
    }

    if (inputs[pitched_keys[i]].falling()) {
      engine.NoteOff(i);
      gate_off = true;
      --notes_on;
    }
  }
  gate_off = gate_off && (0 == notes_on);

  if (inputs[TAP_NEXT].rising()) {
    send_note = engine.Advance();
  }
  if (edit_mode) {
    gate_off = false;
    // TODO: check a buncha button actions here
    if (write_mode) {
      if (inputs[ACCENT_KEY].rising()) engine.ToggleAccent();
      if (inputs[SLIDE_KEY].rising()) engine.ToggleSlide();
      // etc.
    }
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
      //PORTE &= ~0b10;
      SetGate(false);
      gate_on = false;
      gate_off = false;
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
