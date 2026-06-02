// Copyright (c) 2026, Nicholas J. Michalek
//

#ifndef DEBUG
#define DEBUG 0
#endif

#include <Arduino.h>
#include "pins.h"
#include "drivers.h"
#include "engine.h"
#include "MIDI.h"

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);

EEPROMClass storage;
PersistentSettings GlobalSettings;
Sequence pattern[NUM_PATTERNS];

// -=-=- Globals -=-=-
static uint8_t ticks = 0;
static uint8_t clk_count = 0;
// shortcuts for tempo-synced flashers
#define BEAT_FLASH (clk_count < 12)
#define DOUBLE_BEAT_FLASH (clk_count % 12 < 6)
#define SLOW_FLASH (clk_count & (1<<3))
#define MED_FLASH (clk_count & (1<<2))
#define FAST_FLASH (clk_count & (1<<1))
#define SUPER_FLASH (clk_count & 1)

static uint8_t transpose = 0 | (OCTAVE_ZERO << 4); // range is 0 to 47
static uint8_t transpose_next = 0 | (OCTAVE_ZERO << 4);

static PinState inputs[INPUT_COUNT];

static uint8_t tracknum = 0;
static uint8_t track_loaded = 0;
static bool step_counter = false;
static bool midi_clk = false;
static bool wrap_edit = false;
static bool clk_run = false;
static bool track_mode;
static bool write_mode;
static bool perform_mode;
static bool beat_reset;

static bool dac_stale = false;
static elapsedMicros dac_timer;
// static elapsedMillis led_timer;
static elapsedMillis pattern_cleared_flash_timer;
static constexpr uint16_t PATTERN_CLEARED_FLASH_MS = 400;

static Engine engine;

// TODO: separate generalized UI class?
enum UIMode {
  NORMAL_MODE,
  PITCH_MODE,
  TIME_MODE,
  MENU_CONFIG,
  MENU_PITCH,
  MENU_TIME,
};
UIMode mode_ = NORMAL_MODE;
UIMode get_mode() { return mode_; }
void SetMode(UIMode m, bool reset = false) {
  if (reset && m != mode_) engine.Reset();
  mode_ = m;
}

// ----- MIDI live note input -----
static constexpr uint8_t MIDI_STACK_SIZE = 8;
static uint8_t midi_note_stack[MIDI_STACK_SIZE];
static uint8_t midi_note_vel[MIDI_STACK_SIZE];
static uint8_t midi_note_depth = 0;
static uint8_t midi_live_note = 0;
static bool    midi_live_gate = false;
static bool    midi_live_accent = false;
static bool    midi_live_slide = false;

static void midi_stack_push(uint8_t note, uint8_t vel) {
  for (uint8_t i = 0; i < midi_note_depth; ++i) {
    if (midi_note_stack[i] == note) {
      for (uint8_t j = i; j + 1 < midi_note_depth; ++j) {
        midi_note_stack[j] = midi_note_stack[j + 1];
        midi_note_vel[j]   = midi_note_vel[j + 1];
      }
      --midi_note_depth;
      break;
    }
  }
  if (midi_note_depth < MIDI_STACK_SIZE) {
    midi_note_stack[midi_note_depth] = note;
    midi_note_vel[midi_note_depth]   = vel;
    ++midi_note_depth;
  }
}
static void midi_stack_remove(uint8_t note) {
  for (uint8_t i = 0; i < midi_note_depth; ++i) {
    if (midi_note_stack[i] == note) {
      for (uint8_t j = i; j + 1 < midi_note_depth; ++j) {
        midi_note_stack[j] = midi_note_stack[j + 1];
        midi_note_vel[j]   = midi_note_vel[j + 1];
      }
      --midi_note_depth;
      return;
    }
  }
}

static void midi_note_off(byte channel, byte pitch, byte velocity) {
  if (!GlobalSettings.Get(SETTING_MIDI_RX)) return;
  // TODO: channel filter
  if (channel > 1) return;

  (void)channel; (void)velocity;
  midi_stack_remove(static_cast<uint8_t>(pitch));

  if (midi_note_depth > 0) {
    midi_live_note   = midi_note_stack[midi_note_depth - 1];
    midi_live_accent = (midi_note_vel[midi_note_depth - 1] >= 100);
    midi_live_slide  = true;
  } else {
    midi_live_gate  = false;
    midi_live_slide = false;
    DAC::SetGate(false);
  }
  dac_stale = true;
}
static void midi_note_on(byte channel, byte pitch, byte velocity) {
  if (!GlobalSettings.Get(SETTING_MIDI_RX)) return;
  // TODO: channel filter
  if (channel > 1) return;

  if (velocity == 0) { midi_note_off(channel, pitch, 0); return; }

  const uint8_t prev = midi_live_note;
  const bool was_on  = midi_live_gate;

  midi_stack_push(static_cast<uint8_t>(pitch), static_cast<uint8_t>(velocity));

  midi_live_slide  = was_on && (prev != static_cast<uint8_t>(pitch));
  midi_live_accent = (velocity >= 100);
  midi_live_gate   = true;
  midi_live_note   = static_cast<uint8_t>(pitch);
  dac_stale = true;
}

// crucial bits tying together the inputs + engine

uint8_t check_pitch_held() {
  for (uint8_t i = 0; i < ARRAY_SIZE(pitched_keys); ++i) {
    if (!inputs[pitched_keys[i]].off()) {
      return i + 1; // <-- watch out for that +1
    }
  }
  return 0;
}
uint8_t check_pitch_inputs() {
  for (uint8_t i = 0; i < ARRAY_SIZE(pitched_keys); ++i) {
    // pitched_keys vs. pitch_leds
    // FIGHT!
    if (inputs[pitched_keys[i]].rising()) {
      return i + 1; // <-- watch out for that +1
    }
  }
  return 0;
}
bool check_time_inputs() {
  if (!inputs[DOWN_KEY].off()) { return true; }
  if (!inputs[UP_KEY].off()) { return true; }
  if (!inputs[ACCENT_KEY].off()) { return true; }
  if (!inputs[SLIDE_KEY].off()) return true;
  if (!inputs[TAP_NEXT].off() || !inputs[BACK_KEY].off()) return true;
  return false;
}
bool input_pitch(bool mod = false) {
  bool result = false;
  if (mod) {
    if (inputs[ACCENT_KEY].rising()) engine.ToggleAccent();
    if (inputs[SLIDE_KEY].rising()) engine.ToggleSlide();
    if (inputs[UP_KEY].rising()) engine.NudgeOctave(1);
    if (inputs[DOWN_KEY].rising()) engine.NudgeOctave(-1);
  }
  for (uint8_t i = 0; i < ARRAY_SIZE(pitched_keys); ++i) {
    if (inputs[pitched_keys[i]].rising()) {
      if (mod)
        engine.SetPitchSemitone(i);
      else {
        engine.AdvancePitch();
        const uint8_t oct = 1 - inputs[DOWN_KEY].held() + inputs[UP_KEY].held();
        const uint8_t flags = (inputs[ACCENT_KEY].held() << 6) |
                              (inputs[SLIDE_KEY].held() << 7); // | (oct << 4);
        engine.SetPitch(i | (oct << 4), flags);
      }
      result = true;
    }
  }
  return result;
}
bool input_time(bool mod = false) {
  bool result = false;
  if (inputs[DOWN_KEY].rising()) {
    if (!mod) engine.Advance(track_mode);
    engine.SetTime(1); // note
    result = true;
  }
  if (inputs[UP_KEY].rising()) {
    if (!mod) engine.Advance(track_mode);
    engine.SetTime(2); // tie
    result = true;
  }
  if (inputs[ACCENT_KEY].rising()) {
    if (!mod) engine.Advance(track_mode);
    engine.SetTime(0); // rest
    result = true;
  }
  if (inputs[SLIDE_KEY].rising()) {
    if (!mod) engine.Advance(track_mode);
    engine.SetTime(3); // ????
    result = true;
  }
  return result;
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
    DAC::SetPitch(note + 12*(accent ? 4 - oct : oct));
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

void SplashAnim(bool reverse = false) {
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

  // making progress
  elapsedMillis timer = 0;
  const uint8_t len = ARRAY_SIZE(loadingbar);

  if (!reverse) {
    for (uint8_t i = 0; i < len; ++i) {
      // clear
      Leds::Send();
      Leds::Send();

      Leds::Set(loadingbar[i], true);
      for (int tail = i; tail > 0 && tail > i-4; --tail) {
        Leds::Set(loadingbar[tail-1], true);
      }
      while (timer < 32) {
        Leds::Send(false); // don't clear
        delay(1);
      }
      timer = 0;
    }
  } else {
    // backwards progress
    for (uint8_t i = 0; i < len; ++i) {
      Leds::Set(loadingbar[len - i], true);
      for (int tail = len - i; tail < len; ++tail) {
        Leds::Set(loadingbar[tail-1], true);
      }
      while (timer < 50) {
        Leds::Send(false); // don't clear
        delay(1);
      }
      timer = 0;
      // clear
      Leds::Send();
      Leds::Send();
    }
  }
}

#if DEBUG
extern "C" {
  static void jumptoboot(void) {
    // call bootloader to test
    ((int (*)(void))0x1F000)();
  }
}
#endif

void setup() {
  Serial1.begin(31250);
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleNoteOn(midi_note_on);
  MIDI.setHandleNoteOff(midi_note_off);

  for (uint8_t i = 0; i < ARRAY_SIZE(INPUTS); ++i) {
    pinMode(INPUTS[i], INPUT); // pullup?
  }
  for (uint8_t i = 0; i < ARRAY_SIZE(OUTPUTS); ++i) {
    pinMode(OUTPUTS[i], OUTPUT);
  }
  // Timer3 fast PWM 8-bit on OC3A (PC6 / PF2_PIN) for filter CV, no prescaler → ~15.6kHz
  TCCR3A = (1 << COM3A1) | (1 << WGM30);
  TCCR3B = (1 << WGM32) | (1 << CS30);
  OCR3A = 0;

  for (uint8_t i = 0; i < 4; ++i) {
    digitalWriteFast(select_pin[i], HIGH);
  }

  PollInputs(inputs);
  PollInputs(inputs);
  PollInputs(inputs);
  PollInputs(inputs);
#if DEBUG
  /* This won't be necessary in production, bootloader runs first */
  if (inputs[TAP_NEXT].held()) {
    jumptoboot();
  }

  Serial.begin(9600);
#endif

  SplashAnim();

#if DEBUG
  // 4-octave pewpew test for all 13 semitones
  PewPewPew();
#endif

  const bool reset_memory = inputs[FUNCTION_KEY].held() && inputs[CLEAR_KEY].held();

  // this might take a while on first boot
  if (!engine.Init(reset_memory)) {
    SplashAnim(true); // wax off
    SplashAnim(false); // wax on
  }
}

// --- LED helpers ---
void PrintPosition(const uint8_t pos) {
  // chasing light for pattern step
  Leds::Set(OutputIndex(pos & 0x7), true);
  Leds::Set(OutputIndex(CSHARP_KEY_LED + ((pos >> 3) & 0x3)), true);
  Leds::Set(ASHARP_KEY_LED, pos >> 5);
}
void PrintPitch(const uint8_t semi, const uint8_t octave, const bool acc, const bool slide) {
  if (semi < 13)
    Leds::Set(pitch_leds[semi], true);

  Leds::Set(ACCENT_KEY_LED, acc);
  Leds::Set(SLIDE_KEY_LED, slide);
  Leds::Set(DOWN_KEY_LED, octave == OCTAVE_DOWN || octave == OCTAVE_DOUBLE_UP);
  Leds::Set(UP_KEY_LED, octave > OCTAVE_ZERO);
}
void PrintTime() {
  Leds::Set(DOWN_KEY_LED, engine.get_time() == 1);
  Leds::Set(UP_KEY_LED, engine.get_time() == 2);
  Leds::Set(ACCENT_KEY_LED, engine.get_time() == 0);
  Leds::Set(SLIDE_KEY_LED, engine.get_time() == 3); // ???

  PrintPosition(engine.get_time_pos());
}
void PrintChain(uint8_t stepidx) {
  if (0 == engine.p_chain_len) return;

  const uint8_t data = engine.p_chain[stepidx];
  Leds::Set(OutputIndex(data & 0x7), true);

  Leds::Set(CSHARP_KEY_LED, (data >> 4) & 1);
  Leds::Set(DSHARP_KEY_LED, (data >> 5) & 1);
  Leds::Set(FSHARP_KEY_LED, (data >> 6) & 1);
  Leds::Set(GSHARP_KEY_LED, (data >> 7) & 1);
}

// --- UI context/mode helpers ---
void ProcessEdit() {
  switch (get_mode()) {
  case PITCH_MODE: {
    if (write_mode) {
      bool result = input_pitch(true); // modify pitch
      if (result) DAC::SetPitch(engine.get_pitch() + unpack_pitch(transpose));
    }

    PrintPitch(engine.get_semitone(), engine.get_octave(), engine.get_accent(), engine.get_slide());
    break;
  }
  case TIME_MODE:
    if (write_mode) {
      input_time(true);
    }

    PrintTime();
  case NORMAL_MODE:
    PrintPosition(engine.get_time_pos());
    break;
  default: break;
  }

  // Manual Reset to Step 0
  if (inputs[BACK_KEY].rising()) {
    engine.Reset();
    clk_count = 0;
    midi_clk = false; // reset midi sync
  }
}
void ProcessDefault(const bool &clear_mod) {
  // record inputs for regular pattern write mode
  const bool pattern_write = (write_mode && !track_mode);

  if (!clk_run) {
    // Handle TAP/BACK button advance for step editing when stopped.
    // With clock running, TAP does nothing - Clock() drives advance

    if (inputs[TAP_NEXT].rising() || inputs[BACK_KEY].rising()) {
      // step forward or back?
      int dir = inputs[TAP_NEXT].rising() ? 1 : -1;
      if (get_mode() == PITCH_MODE) {
        engine.AdvancePitch(dir);
        DAC::SetGate(true);
      } else
        DAC::SetGate(engine.Advance(track_mode, dir));
      DAC::SetAccent(engine.get_accent());
      DAC::SetSlide(engine.get_slide_dac());
      DAC::SetPitch(engine.get_pitch() + unpack_pitch(transpose));
    }

    if (inputs[TAP_NEXT].falling() || inputs[BACK_KEY].falling()) {
      DAC::SetGate(false);
      if (!wrap_edit && engine.get_time_pos() >= engine.get_length() - 1)
        SetMode(NORMAL_MODE, true);
    }
  }

  if (pattern_write && clear_mod && inputs[PITCH_KEY].rising())
    engine.Generate(true, false);

  if (pattern_write && clear_mod && inputs[TIME_KEY].rising())
    engine.Generate(false, true);

  switch (get_mode()) {
  case PITCH_MODE:
    if (pattern_write) {
      if (clear_mod) {
        if (inputs[ACCENT_KEY].rising()) engine.ClearAccents();
        if (inputs[SLIDE_KEY].rising()) engine.ClearSlides();
        break;
      }

      static bool keyhold = false;
      const bool check = check_pitch_held() || !inputs[TAP_NEXT].off() || !inputs[BACK_KEY].off();
      if (check != keyhold) {
        DAC::SetGate(check);
        keyhold = check;
        dac_stale = true;
      }

      // record new pitch
      bool result = input_pitch(clk_run);
      if (!clk_run && result) {
        DAC::SetPitch(engine.get_pitch() + unpack_pitch(transpose));
        dac_stale = true;
      }
      if (!result && !check) {
        // kick out after recording last step - only if no input held or rising
        if (!clk_run && engine.get_sequence().pitch_pos >= engine.get_length() - 1)
          SetMode(NORMAL_MODE, true);
      }
    }

    PrintPitch(engine.get_semitone(), engine.get_octave(), engine.get_accent(), engine.get_slide());
    if (!write_mode)
      SetMode(NORMAL_MODE); // you're not supposed to be in here
    break;

  case TIME_MODE:
    if (pattern_write) {
      if (clear_mod) {
        if (inputs[DOWN_KEY].rising()) engine.ClearNotes();
        if (inputs[UP_KEY].rising()) engine.ClearTies();
        if (inputs[ACCENT_KEY].rising()) engine.ClearRests();
        if (inputs[SLIDE_KEY].rising()) engine.ClearRatchets();
        break;
      }

      if (!input_time(clk_run) && !check_time_inputs()) {
        // kick out after recording last step - only if no input held or rising
        if (!clk_run && engine.get_time_pos() >= engine.get_length() - 1)
          SetMode(NORMAL_MODE, true);
      }
    }

    PrintTime();
    if (!write_mode)
      SetMode(NORMAL_MODE); // you're not supposed to be in here
    break;

  case NORMAL_MODE:
    // flash LED for current pattern
    Leds::Set(OutputIndex(engine.get_patsel() & 0x7), BEAT_FLASH);
    // solid LED for queued pattern
    if (engine.get_patsel() != engine.get_next())
      Leds::Set(OutputIndex(engine.get_next() & 0x7), true);
    Leds::Set(ACCENT_KEY_LED, !(engine.get_patsel() >> 3) || (!(engine.get_next() >> 3) && SUPER_FLASH)); // A
    Leds::Set(SLIDE_KEY_LED, (engine.get_patsel() >> 3) || ((engine.get_next() >> 3) && SUPER_FLASH));   // B

    if (track_mode) {
      Leds::Set(ASHARP_KEY_LED, true); // pattern-chaining enabled indicator
      if (clear_mod && inputs[BACK_KEY].rising()) {
        engine.ClearChain();
      }

      PrintChain(engine.p_chain_pos);
    }

    if (pattern_write && clear_mod) {
      if (inputs[CSHARP_KEY].rising())
        engine.Copy();
      if (inputs[DSHARP_KEY].rising())
        engine.Paste();
    }

    if (clk_run && write_mode) {
      PrintPosition(engine.get_time_pos());
    }
    // Inputs for Pattern Select
    {
      bool performing = false;
      for (uint8_t i = 0; i < 8; ++i) {
        if (inputs[i].rising()) {
          const uint8_t patsel = (engine.get_next() >> 3) * 8 + i;
          if (clear_mod) {
            engine.ClearPattern(patsel);
            pattern_cleared_flash_timer = 0;
          } else {
            engine.SetPattern(patsel, !clk_run);
            if (perform_mode) beat_reset = true;
          }
        }
        if (!inputs[i].off()) performing = true;
      }
      if (perform_mode && !performing) engine.resting = true; // hey, take a break
    }

    if (inputs[ACCENT_KEY].rising())
      engine.SetPattern(engine.get_next() % 8, !clk_run); // A
    if (inputs[SLIDE_KEY].rising())
      engine.SetPattern(engine.get_next() % 8 + 8, !clk_run); // B

    break;
  default: break;
  }

  // no modifier - show current mode; flash for pattern clear
  if (pattern_cleared_flash_timer < PATTERN_CLEARED_FLASH_MS) {
    Leds::Set(TIME_MODE_LED, true);
    Leds::Set(PITCH_MODE_LED, true);
    Leds::Set(ASHARP_KEY_LED, true);
  } else {
    Leds::Set(TIME_MODE_LED, get_mode() == TIME_MODE);
    Leds::Set(PITCH_MODE_LED, get_mode() == PITCH_MODE);
    Leds::Set(FUNCTION_MODE_LED, get_mode() == NORMAL_MODE && (!clk_run || BEAT_FLASH));
  }
}
void SetTranspose(const uint8_t tr) {
  transpose_next = tr;
  if (!clk_run) transpose = transpose_next;
}
void ProcessPitchMod() {
  Leds::Set(PITCH_MODE_LED, MED_FLASH);
  PrintPitch(transpose & 0x0f, (transpose >> 4) & 0x3, false, false);
  if (FAST_FLASH)
    PrintPitch(transpose_next & 0x0f, (transpose_next >> 4) & 0x3, false, false);

  // TODO: beat-synced transpose change?

  // check pitch keys to set new root note
  for (uint8_t i = 0; i < ARRAY_SIZE(pitched_keys); ++i) {
    if (inputs[pitched_keys[i]].rising()) {
      SetTranspose((transpose_next & 0xf0) + i);
    }
  }
  // check octave keys to jump by 12
  if (inputs[DOWN_KEY].rising() && (transpose_next >> 4)) {
    SetTranspose(transpose_next - (1 << 4));
  }
  if (inputs[UP_KEY].rising() && ((transpose_next >> 4) ^ 0x3)) {
    SetTranspose(transpose_next + (1 << 4));
  }
  // TODO: other pitch effects?
}

void ProcessChainEdit() {
  static uint8_t step_edit = 0;
  static bool b_section = false;

  // check keys for quick-chaining patterns
  if (inputs[ACCENT_KEY].rising()) b_section = false;
  if (inputs[SLIDE_KEY].rising()) b_section = true;

  uint8_t patsel = 0;
  for (uint8_t i = 0; i < 8; ++i) {
    if (inputs[i].rising()) {
      patsel = i + 1;
    }
  }

  if (patsel) {
    step_edit = engine.AddToChain(patsel - 1 + b_section * 8);
  }

  if (inputs[BACK_KEY].rising()) {
    // undo, step backward
    step_edit = engine.AddToChain(-1);
  }

  Leds::Set(ACCENT_KEY_LED, !b_section); // A
  Leds::Set(SLIDE_KEY_LED, b_section);   // B
  PrintChain(step_edit);
}
void ProcessFunctionMod() {
  Leds::Set(FUNCTION_MODE_LED, MED_FLASH);

  if (write_mode) {
    if (track_mode) {
      ProcessChainEdit();
    } else { // pattern write mode
      // show step length on LEDs
      PrintPosition(engine.get_length() - 1);

      // tap in number of steps
      if (inputs[DOWN_KEY].rising()) {
        if (step_counter)
          step_counter = engine.BumpLength();
        else {
          engine.SetLength(1);
          step_counter = true;
        }
      }

      // modify length with pitch keys
      // pitched_keys vs. pitch_leds
      uint8_t pitch = check_pitch_inputs();
      if (pitch--) {
        uint8_t keyidx = pitch_leds[pitch];
        if (keyidx < 8)
          engine.SetLength(1 + (engine.get_length()-1) / 8 * 8 + keyidx);
        else if (keyidx < 16 && keyidx > 11) {
          const uint8_t huge = (engine.get_length() - 1) & (1 << 5); // either 32 or 0
          engine.SetLength(1 + (engine.get_length()-1) % 8 + 8 * (keyidx - 12) + huge);
        }

        if (inputs[ASHARP_KEY].rising())
          engine.SetLength(1 + (engine.get_length() - 1 + 32) % MAX_STEPS);
      }

      if (inputs[UP_KEY].rising()) {
        // TODO: triplets mode?
        // I also want variable swing, which is a bit different ...
        engine.ToggleTriplets();
      }

      // half
      if (inputs[ACCENT_KEY].rising()) {
        engine.SetLength(engine.get_length() / 2);
      }
      // double
      if (inputs[SLIDE_KEY].rising()) {
        engine.SetLength(engine.get_length() * 2);
      }
    }
  } else {
    // Play Mode
    ProcessChainEdit();
    // you can still edit here, but it's only temporary...
  }

  if (inputs[CLEAR_KEY].rising()) // enter system config
    mode_ = MENU_CONFIG;

  if (inputs[PITCH_KEY].rising())
    mode_ = MENU_PITCH;

  if (inputs[TIME_KEY].rising())
    mode_ = MENU_TIME;
}

void ProcessTimeMenu() {
  Leds::Set(PITCH_MODE_LED, true);
  Leds::Set(TIME_MODE_LED, MED_FLASH);
  Leds::Set(FUNCTION_MODE_LED, true);

  if (inputs[FUNCTION_KEY].rising()) mode_ = NORMAL_MODE;

  // TODO: swing amount, clock division, etc.
}
void ProcessPitchMenu() {
  Leds::Set(PITCH_MODE_LED, MED_FLASH);
  Leds::Set(TIME_MODE_LED, true);
  Leds::Set(FUNCTION_MODE_LED, true);

  if (inputs[FUNCTION_KEY].rising()) mode_ = NORMAL_MODE;

  const uint16_t mask = engine.get_qmask();
  for (uint8_t i = 0; i < 13; ++i) {
    Leds::Set(pitch_leds[i], mask & (1ul << i));
  }

  uint8_t pitch = check_pitch_inputs();
  if (pitch) engine.ToggleMaskBit(pitch - 1);

  if (inputs[DOWN_KEY].rising()) engine.RotateMask(true);
  if (inputs[UP_KEY].rising()) engine.RotateMask(false);

  // TODO: arpeggiator direction
}
void ProcessConfigMenu() {
  static bool stale = false;
  Leds::Set(PITCH_MODE_LED, true);
  Leds::Set(TIME_MODE_LED, true);
  Leds::Set(FUNCTION_MODE_LED, MED_FLASH);

  for (uint8_t i = 0; i < 8; ++i) {
    if (inputs[i].rising()) {
      GlobalSettings.flags ^= (1 << i);
      stale = true;

      if (!GlobalSettings.Get(SETTING_MIDI_CLOCK_RX)) midi_clk = false;
    }

    Leds::Set(OutputIndex(i), GlobalSettings.flags & (1 << i));
  }

  if (inputs[FUNCTION_KEY].rising()) {
    mode_ = NORMAL_MODE;
    if (stale) {
      GlobalSettings.Save();
      stale = false;
    }
  }
}

void SwitchToTrack() {
  // this means nothing gets saved if you switch banks in Play Mode...
  // could enable live-edits that do not persist
  // Also, this could stutter if you switch while playing in write mode
  if (write_mode)
    engine.Save(track_loaded);

  engine.Load(tracknum);
  track_loaded = tracknum;
}

// TODO: ISR for polling & LEDs

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
void loop() {
  // Poll all inputs... every single tick
  //if ((ticks & 0x03) == 0)
  PollInputs(inputs);
  engine.Tick();

#if DEBUG
  if (Serial.available() && Serial.read()) {
    for (uint8_t i = 0; i < INPUT_COUNT/2; ++i) {
      Serial.printf("Input #%2u = %x   |  Input #%2u = %x\n", i, inputs[i].state, i + INPUT_COUNT/2, inputs[i + INPUT_COUNT/2].state);
    }
  }
#endif

  tracknum = uint8_t(inputs[TRACK_BIT0].held()
           | (inputs[TRACK_BIT1].held() << 1)
           | (inputs[TRACK_BIT2].held() << 2));

  track_mode = inputs[TRACK_SEL].held();
  write_mode = inputs[WRITE_MODE].held();
  const bool clear_mod = inputs[CLEAR_KEY].held();
  const bool edit_mode = inputs[TAP_NEXT].held() || inputs[BACK_KEY].held();

  // transpose, performance stuff, config menus
  const bool fn_mod = inputs[FUNCTION_KEY].held();
  const bool pitch_mod = inputs[PITCH_KEY].held();
  const bool time_mod = inputs[TIME_KEY].held();

  bool clocked = false;

  // process all MIDI here
  while (MIDI.read()) {
    const midi::MidiType type = MIDI.getType();

    if (GlobalSettings.Get(SETTING_MIDI_CLOCK_RX)) {
      switch (type) {
        case midi::MidiType::Clock:
          midi_clk = true;
          clocked = true;
          break;
        case midi::MidiType::Continue:
        case midi::MidiType::Start:
          midi_clk = true;
          clk_run = true;
          if (GlobalSettings.Get(SETTING_MIDI_CLOCK_TX)) MIDI.sendRealTime(type);
          if (inputs[CLEAR_KEY].held()) perform_mode = true;
          engine.Reset();
          clk_count = 0;
          break;
        case midi::MidiType::Stop:
          midi_clk = false;
          clk_run = false;
          if (GlobalSettings.Get(SETTING_MIDI_CLOCK_TX)) MIDI.sendRealTime(type);
          DAC::SetGate(false);
          engine.Reset();
          midi_note_depth = 0;
          midi_live_gate  = false;
          midi_live_slide = false;
          dac_stale = true;
          break;
        default: break;
      }
    }

    if (GlobalSettings.Get(SETTING_MIDI_PC_RX) && type == midi::MidiType::ProgramChange) {
      engine.SetPattern(MIDI.getData1(), !clk_run);
    }

    if (type == midi::MidiType::ControlChange) {
      if (MIDI.getData1() == 1) { // CC 1 (mod wheel) → filter CV
        DAC::SetFilter( (MIDI.getData2() << 1) | (MIDI.getData2() >> 6) );
        dac_stale = true;
      }
    }
  }

  // DIN sync clock @ 24ppqn
  if (!midi_clk) {
    clocked = inputs[CLOCK].rising();
  }

  if (GlobalSettings.Get(SETTING_MIDI_CLOCK_TX)) {
    if (clocked) {
      MIDI.sendRealTime(midi::Clock);
    }
    if (inputs[RUN].rising()) {
      MIDI.sendRealTime(midi::Start);
    }
    if (inputs[RUN].falling()) {
      MIDI.sendRealTime(midi::Stop);
    }
  }

  // Save pattern data - only if clock isn't running, to prevent stuttering
  // - when exiting write mode
  // - when stopping the clock in write mode
  if ((inputs[WRITE_MODE].falling() && !clk_run) ||
      (write_mode && inputs[RUN].falling() && !midi_clk)) {
    engine.Save(track_loaded);
  }

  // --- Analog Clock Start
  if (inputs[RUN].rising()) {
    if (inputs[CLEAR_KEY].held()) perform_mode = true;
    clk_run = true;
    if (!midi_clk) clk_count = 0;
    else beat_reset = true;
    //Serial.println("CLOCK RUN STARTED");
    engine.Reset();
  }

  // -=-=- Process inputs and set LEDs -=-=-

  switch (get_mode()) {
    default:
      if (edit_mode) { // holding WRITE/NEXT/TAP
        ProcessEdit();
      } else {
        // Flash lights for modifiers
        if (pitch_mod) {
          ProcessPitchMod();
        } else if (time_mod) {
          Leds::Set(TIME_MODE_LED, MED_FLASH);
          // TODO: performance time effects
        } else if (fn_mod) {
          ProcessFunctionMod();
        } else {
          ProcessDefault(clear_mod);
        }
      }
      break;

    case MENU_CONFIG:
      ProcessConfigMenu();
      break;
    case MENU_PITCH:
      ProcessPitchMenu();
      break;
    case MENU_TIME:
      ProcessTimeMenu();
      break;
  }

  // --- Sync'd Reset (step or quarter note)
  // const bool sync_ready = true; // full 24ppqn resolution
  const bool sync_ready = GlobalSettings.Get(SETTING_PATTERN_SYNC)
                        ? (clk_count == 0) // quarter-note beat sync
                        : (clk_count % 6 == 0); // step sync
  if (sync_ready && beat_reset) {
    beat_reset = false;
    engine.Reset();
  }

  // actual engine Clock
  const bool performing = !perform_mode || (check_pitch_held() && !beat_reset);
  if (clocked && clk_run && performing) {
    if (engine.Clock(track_mode)) {
      const bool sync_to_pattern = GlobalSettings.Get(SETTING_PATTERN_SYNC);
      const bool ready = (sync_to_pattern && engine.get_time_pos() == 0) || sync_ready;
      if (ready) {
        // pattern-synced changes here
        // todo: consider moving this into the engine?
        transpose = transpose_next;
        if (tracknum != track_loaded)
          SwitchToTrack();
      }
    }
    dac_stale = true;
  }

  // increment clock counter after everything else
  if (clocked) {
    ++clk_count %= 24;
  }

  // show all pressed buttons
  for (uint8_t i = 0; i < 16; ++i) {
    if (inputs[switched_leds[i].button].held())
      Leds::Set(OutputIndex(i), true);
  }

  // a way to throttle LED update for dimming
  // if (led_timer > 1) {
  Leds::Send(); // hardware output, framebuffer reset
    // led_timer = 0;
  // }

  // ----- <TODO> this stuff probably belongs elsewhere ------
  if (!clk_run && (tracknum != track_loaded)) {
    SwitchToTrack();
  }

  // --- other input handling
  if (inputs[TIME_KEY].rising() && write_mode) SetMode(TIME_MODE, !clk_run);
  if (inputs[PITCH_KEY].rising() && write_mode) SetMode(PITCH_MODE, !clk_run);
  if (inputs[FUNCTION_KEY].rising()) SetMode(NORMAL_MODE, !clk_run);
  if (inputs[CLEAR_KEY].rising()) {
    // press CLEAR while holding one or more pattern slots
    for (uint8_t i = 0; i < 8; ++i) {
      if (inputs[i].held()) {
        engine.ClearPattern((engine.get_patsel() >> 3) * 8 + i);
        pattern_cleared_flash_timer = 0;
      }
    }
  }
  if (inputs[FUNCTION_KEY].falling()) step_counter = false;
  // ----- </TODO> -------------

  if (midi_live_gate) {
    // Live MIDI note overrides pattern output (stopped or running)
    const int16_t live_pitch = constrain(int16_t(midi_live_note) - 36, 0, 48);
    DAC::SetPitch(static_cast<uint8_t>(live_pitch) + unpack_pitch(transpose));
    DAC::SetSlide(midi_live_slide);
    DAC::SetAccent(midi_live_accent);
    DAC::SetGate(true);
  } else {
    if (clk_run) {
      // send sequence step
      DAC::SetSlide(engine.get_slide_dac());
      DAC::SetAccent(engine.get_accent());
      DAC::SetGate(engine.get_gate());
      DAC::SetPitch(engine.get_pitch() + unpack_pitch(transpose));
    }
  }

  // catch falling edge of RUN
  if (inputs[RUN].falling()) {
    //Serial.println("CLOCK STOPPED");
    DAC::SetGate(false);
    engine.Reset();
    clk_run = false;
    perform_mode = false;
  }

  ++ticks;

  // simulate original interrupt timing for DAC update,
  // which will naturally beat against the clock pulses and hopefully evoke the same kind of jitter
  if (dac_timer > 1800 && (dac_stale || !clk_run)) {
    DAC::Send();
    dac_timer = 0;
    dac_stale = 0;
  }
}
