// Copyright (c) 2026, Nicholas J. Michalek
//

#define DEBUG 0

#include <Arduino.h>
#include "pins.h"
#include "drivers.h"
#include "engine.h"
#include "MIDI.h"

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);

EEPROMClass storage;
PersistentSettings GlobalSettings;

// -=-=- Globals -=-=-
static uint8_t ticks = 0;
static uint8_t clk_count = 0;

static PinState inputs[INPUT_COUNT];

static uint8_t tracknum = 0;
static bool step_counter = false;

// this is where the magic happens
static Engine engine;

// crucial bits tying together the inputs + engine

uint8_t check_pitch_inputs() {
  uint8_t notes = 0;
  for (uint8_t i = 0; i < ARRAY_SIZE(pitched_keys); ++i) {
    if (inputs[pitched_keys[i]].held()) {
      ++notes;
    }
  }
  return notes;
}
bool check_time_inputs() {
  if (inputs[DOWN_KEY].held()) { return true; }
  if (inputs[UP_KEY].held()) { return true; }
  if (inputs[ACCENT_KEY].held()) { return true; }
  //if (inputs[SLIDE_KEY].held()) return true; // TODO: spicy time option, ratchets maybe
  return false;
}
void input_pitch(bool mod = false) {
  if (mod) {
    if (inputs[ACCENT_KEY].rising()) engine.ToggleAccent();
    if (inputs[SLIDE_KEY].rising()) engine.ToggleSlide();
    if (inputs[UP_KEY].rising()) engine.NudgeOctave(1);
    if (inputs[DOWN_KEY].rising()) engine.NudgeOctave(-1);
  }
  for (uint8_t i = 0; i < ARRAY_SIZE(pitched_keys); ++i) {
    if (inputs[pitched_keys[i]].rising()) {
      if (mod)
        engine.SetPitch(i);
      else {
        engine.get_sequence().AdvancePitch();
        const uint8_t oct = 1 - inputs[DOWN_KEY].held() + inputs[UP_KEY].held();
        const uint8_t flags = (inputs[ACCENT_KEY].held() << 6) |
                              (inputs[SLIDE_KEY].held() << 7) | (oct << 4);
        engine.SetPitch(i, flags);
      }
    }
  }
}
void input_time(bool mod = false) {
  if (inputs[DOWN_KEY].rising()) {
    if (!mod) engine.Advance();
    engine.SetTime(1); // note
  }
  if (inputs[UP_KEY].rising()) {
    if (!mod) engine.Advance();
    engine.SetTime(2); // tie
  }
  if (inputs[ACCENT_KEY].rising()) {
    if (!mod) engine.Advance();
    engine.SetTime(0); // rest
  }
}


// ===== MAIN CODE LOGIC =====

const MatrixPin note_led[] = {
  switched_leds[0],
  switched_leds[12],
  switched_leds[1],
  switched_leds[13],
  switched_leds[2],
  switched_leds[3],
  switched_leds[14],
  switched_leds[4],
  switched_leds[15],
  switched_leds[5],
  switched_leds[16 + 1],
  switched_leds[6],
  switched_leds[7],
};
void PewPew(uint8_t note, const bool accent = false) {
  Leds::Set(note_led[note], true);
  for (uint8_t oct = 0; oct < 4; ++oct) {
    DAC::SetPitch(note, accent ? 4 - oct : oct);
    DAC::SetGate(true);
    DAC::SetSlide(oct == 0); // PEW!
    DAC::SetAccent(accent);
    // DAC::SetAccent(random(2));
    DAC::Send();
    delay(40);
    DAC::SetGate(false);
    DAC::Send();
    delay(10);
  }
  Leds::Set(note_led[note], false);
  DAC::SetSlide(false);
  DAC::SetAccent(false);
}
void PewPewPew() {
  for (uint8_t note = 0; note <= 12; ++note) {
    // ascending
    PewPew(note, false); // accent off
  }
  for (uint8_t note = 0; note <= 12; ++note) {
    // descending
    PewPew(12 - note, true); // accent on
  }
}
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

  const OutputIndex loadingbar[] = {
    PITCH_MODE_LED, FUNCTION_MODE_LED,
    C_KEY_LED, CSHARP_KEY_LED,
    D_KEY_LED, DSHARP_KEY_LED,
    E_KEY_LED, F_KEY_LED, FSHARP_KEY_LED,
    G_KEY_LED, GSHARP_KEY_LED,
    A_KEY_LED, ASHARP_KEY_LED,
    B_KEY_LED, C_KEY2_LED, DOWN_KEY_LED, UP_KEY_LED,
    TIME_MODE_LED, ACCENT_KEY_LED, SLIDE_KEY_LED
  };


  // once backward
  /*
  const MatrixPin ledseq[] = {
    switched_leds[16 + 2],
    switched_leds[12],
    switched_leds[13],
    switched_leds[14],
    switched_leds[15],
    switched_leds[16 + 1],
    switched_leds[16 + 0],
    switched_leds[11],
    switched_leds[10],
    switched_leds[9],
    switched_leds[8],
    switched_leds[7],
    switched_leds[6],
    switched_leds[5],
    switched_leds[4],
    switched_leds[3],
    switched_leds[2],
    switched_leds[1],
    switched_leds[0],
  };
  const int len = ARRAY_SIZE(ledseq);
  for (uint8_t i = 0; i < len; ++i) {
    Leds::Set(ledseq[len - i], true);
    delay(50);
    Leds::Set(ledseq[len - i], false);
    delay(10);
  }
  */

  // making progress
  elapsedMillis timer = 0;
  const uint8_t len = ARRAY_SIZE(loadingbar);
  for (uint8_t i = 0; i < len; ++i) {
    Leds::Set(loadingbar[i], true);
    for (int tail = i; tail > 0 && tail > i-4; --tail) {
      Leds::Set(loadingbar[tail-1], true);
    }
    while (timer < 50) {
      Leds::Send(timer, false); // don't clear
      delay(1);
    }
    timer = 0;
    // clear
    Leds::Send(0, true);
    Leds::Send(1, true);
    Leds::Send(2, true);
    Leds::Send(3, true);
  }
  // backwards progress
  for (uint8_t i = 0; i < len; ++i) {
    Leds::Set(loadingbar[len - i], true);
    for (int tail = len - i; tail < len; ++tail) {
      Leds::Set(loadingbar[tail-1], true);
    }
    while (timer < 50) {
      Leds::Send(timer, false); // don't clear
      delay(1);
    }
    timer = 0;
    // clear
    Leds::Send(0, true);
    Leds::Send(1, true);
    Leds::Send(2, true);
    Leds::Send(3, true);
  }

  // 4-octave pewpew test for all 13 semitones
  PewPewPew();

  engine.Load();
}

void PrintPitch() {
  const uint8_t pitch = engine.get_pitch() & 0x0f;
  Leds::Set(pitch_leds[pitch % 13], true);

  Leds::Set(ACCENT_KEY_LED, engine.get_accent());
  Leds::Set(SLIDE_KEY_LED, engine.get_slide());
  Leds::Set(DOWN_KEY_LED,
            engine.get_sequence().get_octave() == OCTAVE_DOWN ||
                engine.get_sequence().get_octave() == OCTAVE_DOUBLE_UP);
  Leds::Set(UP_KEY_LED, engine.get_sequence().get_octave() > OCTAVE_ZERO);
}
void PrintTime() {
  Leds::Set(DOWN_KEY_LED, engine.get_time() == 1);
  Leds::Set(UP_KEY_LED, engine.get_time() == 2);
  Leds::Set(ACCENT_KEY_LED, engine.get_time() == 0);
}

void loop() {
  // Poll all inputs... every single tick
  //if ((ticks & 0x03) == 0)
  PollInputs(inputs);

  const bool track_mode = inputs[TRACK_SEL].held();
  const bool write_mode = inputs[WRITE_MODE].held();
  const bool fn_mod = inputs[FUNCTION_KEY].held();
  const bool clear_mod = inputs[CLEAR_KEY].held();
  const bool edit_mode = inputs[TAP_NEXT].held();

  // todo: transpose, performance stuff, config menus
  const bool pitch_mod = inputs[PITCH_KEY].held();
  const bool time_mod = inputs[TIME_KEY].held();

  if (inputs[WRITE_MODE].falling()) engine.Save();

  bool clocked = false;
  static bool midi_clk = false;
  const bool clk_run = inputs[RUN].held() || midi_clk;

  // process all MIDI here
  while (MIDI.read()) {
    if (MIDI.getType() == midi::MidiType::Clock) {
      clocked = true;
    }
    if (MIDI.getType() == midi::MidiType::Start) {
      midi_clk = true;
      engine.Reset();
    }
    if (MIDI.getType() == midi::MidiType::Stop) {
      midi_clk = false;
      DAC::SetGate(false);
      engine.Reset();
    }
    if (MIDI.getType() == midi::MidiType::ProgramChange) {
      engine.SetPattern(MIDI.getData1(), !clk_run);
    }
  }

  // DIN sync clock @ 24ppqn
  if (!midi_clk) {
    clocked = inputs[CLOCK].rising();
  }

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

  if (edit_mode) {
    switch (engine.get_mode()) {
      case PITCH_MODE: {
        if (write_mode) {
          input_pitch(true); // modify pitch
        }

        PrintPitch();
        break;
      }
      case TIME_MODE:
        if (write_mode) {
          input_time(true);
        }

        PrintTime();
      case NORMAL_MODE:
        break;
    }
  } else { // not holding a modifier
    switch (engine.get_mode()) {
      case PITCH_MODE:
        PrintPitch();
        if (!write_mode) engine.SetMode(NORMAL_MODE); // you're not supposed to be in here
        break;

      case TIME_MODE:
        PrintTime();
        if (!write_mode) engine.SetMode(NORMAL_MODE); // you're not supposed to be in here
        break;

      case NORMAL_MODE:
        // flash LED for current pattern
        Leds::Set(OutputIndex(engine.get_patsel() & 0x7), clk_count < 12);
        // solid LED for queued pattern
        if (engine.get_patsel() != engine.get_next())
          Leds::Set(OutputIndex(engine.get_next() & 0x7), true);
        Leds::Set(ACCENT_KEY_LED, !(engine.get_patsel() >> 3)); // A
        Leds::Set(SLIDE_KEY_LED, (engine.get_patsel() >> 3));   // B

        if (clk_run && write_mode) {
          // chasing light for pattern step
          Leds::Set(OutputIndex(engine.get_time_pos() & 0x7), true);
          Leds::Set(OutputIndex(CSHARP_KEY_LED + (engine.get_time_pos() >> 3)), true);
        } 
        // Inputs for Pattern Select
        for (uint8_t i = 0; i < 8; ++i) {
          if (inputs[i].rising()) {
            const uint8_t patsel = (engine.get_patsel() >> 3) * 8 + i;
            if (clear_mod)
              engine.ClearPattern(patsel);
            else
              engine.SetPattern(patsel, !clk_run);
          }
        }
        if (inputs[ACCENT_KEY].rising()) engine.SetPattern(engine.get_patsel() % 8, !clk_run);    // A
        if (inputs[SLIDE_KEY].rising()) engine.SetPattern(engine.get_patsel() % 8 + 8, !clk_run); // B
        break;
    }
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
  // hmmm
  //Leds::Set(ASHARP_KEY_LED, inputs[ASHARP_KEY].held() || (engine.get_pitch() % 12 == 10));

  Leds::Send(ticks); // hardware output, framebuffer reset

  // -=-=- process all inputs -=-=-
  //
  tracknum = uint8_t(inputs[TRACK_BIT0].held()
           | (inputs[TRACK_BIT1].held() << 1)
           | (inputs[TRACK_BIT2].held() << 2));

  if (inputs[TIME_KEY].rising() && write_mode) engine.SetMode(TIME_MODE, !clk_run);
  if (inputs[PITCH_KEY].rising() && write_mode) engine.SetMode(PITCH_MODE, !clk_run);
  if (inputs[FUNCTION_KEY].rising()) engine.SetMode(NORMAL_MODE, !clk_run);

  if (inputs[CLEAR_KEY].rising()) engine.Reset();

  if (fn_mod && write_mode) {
    if (step_counter) {
      Leds::Set(OutputIndex((engine.get_length() - 1) & 0x7), true);
      Leds::Set(CSHARP_KEY_LED, true);
      Leds::Set(DSHARP_KEY_LED, (engine.get_length() - 1) >> 3);
    }
    if (inputs[DOWN_KEY].rising()) {
      if (step_counter)
        step_counter = engine.BumpLength();
      else {
        engine.SetLength(1);
        step_counter = true;
      }
    }
  }

  if (inputs[FUNCTION_KEY].falling()) step_counter = false;

  if (clocked) {
    ++clk_count %= 24;
  }

  if (clocked && clk_run) {
    engine.Clock();

    // hold CLEAR + BACK in write mode to generate random stuff
    if (!track_mode && write_mode && clear_mod && inputs[BACK_KEY].held()) {
      // TODO: * GENERATE! *
      engine.Generate();
    }
  }

  if (inputs[TAP_NEXT].rising()) {
    DAC::SetGate(engine.Advance());
  }
  if (inputs[TAP_NEXT].falling()) {
    DAC::SetGate(false);
    if (!clk_run && engine.get_time_pos() >= engine.get_length() - 1)
      engine.SetMode(NORMAL_MODE, true);
  }

  // regular pattern write mode
  if (!edit_mode && write_mode && !track_mode) {

    // ok, dilemma... we want to advance first, then record the step.

    if (engine.get_mode() == TIME_MODE) {
      if (clk_run || check_time_inputs()) { // record time
        input_time(clk_run);
      } else if (!clk_run && engine.get_time_pos() >= engine.get_length() - 1)
        engine.SetMode(NORMAL_MODE, true);
    }

    if (engine.get_mode() == PITCH_MODE) {
      const bool check = check_pitch_inputs();
      DAC::SetGate(check);
      if (clk_run || check) { // record new pitch
        input_pitch(clk_run);
      } else if (!clk_run && engine.get_sequence().pitch_pos >= engine.get_length() - 1)
        engine.SetMode(NORMAL_MODE, true);
    }

  }

  if (clk_run) {
    // send sequence step
    DAC::SetPitch(engine.get_pitch());
    DAC::SetSlide(engine.get_slide());
    DAC::SetAccent(engine.get_accent());
    DAC::SetGate(engine.get_gate());
  } else {
    // not run mode - send notes from keys
    DAC::SetPitch(engine.get_pitch());
    DAC::SetSlide(inputs[SLIDE_KEY].held());
    DAC::SetAccent(inputs[ACCENT_KEY].held());
  }

  // catch falling edge of RUN
  if (inputs[RUN].falling() && !midi_clk) {
    DAC::SetGate(false);
    engine.Reset();
  }

  ++ticks;

  // send DAC every other tick...
  //if (0 == (ticks & 0x1))
  DAC::Send();
}
