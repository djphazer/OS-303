// Copyright (c) 2026, Nicholas J. Michalek
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

/*
 * TR-808 sequencer firmware
 */

#include <Arduino.h>
#include <MIDI.h>
#include "drivers.h"
#include "pins.h"

#ifndef DEBUG
#define DEBUG 0
#endif
#define BOOT_MAGIC 0xB7

extern "C" {
  static void jumptoboot(void) {
    cli();
    GPIOR0 = BOOT_MAGIC; // flag for bootloader
    asm volatile("jmp 0xF800"); // byte address 0x1F000, using word addressing
  }
}

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);

// --- State ---
static uint16_t ledframe = 0;
static bool trig = false;
static elapsedMillis trig_timer = 0;
static uint16_t trigmask = 0;
//static elapsedMicros poll_timer = 0;
static uint8_t poll_ticks = 0;

// --clock state
static uint8_t ppqn = 12;
static bool midi_clk = false;
static bool clk_run = false;
static uint8_t clk_count = 0;
/*static uint8_t beat_count = 0;*/

// --crude sequencer model
// separate tracks for each instrument?!
static constexpr uint8_t MAX_SEQ_STEPS = 32;
static uint16_t sequence[MAX_SEQ_STEPS];
static uint8_t step[INST_COUNT];
static uint8_t length[INST_COUNT];
static bool reset = 0;

void sequencer_advance() {
  if (reset) reset = false;
  else LOOP(i, INST_COUNT) {
    ++step[i] %= length[i];
  }
  /*++beat_count;*/

  LOOP(i, INST_COUNT) {
    trigmask |= (sequence[step[i]] & (1UL << i));
  }
  trig = true;
}
void sequencer_reset() {
  reset = true;
  LOOP(i, INST_COUNT) {
    step[i] = 0;
  }
}

// --- Clock ---
static bool clock_advance() {
  ++clk_count %= 24;
  return 0 == (clk_count % ppqn);
}
static void clock_reset() {
  clk_count = 0xff;
  sequencer_reset();
}

// util
const PinState &Input(uint8_t i) { return hw::inputs[i]; }

// --- MIDI Callbacks ---
static void midi_note_off(uint8_t chan, uint8_t note, uint8_t vel) {
  // todo?
}
static void midi_note_on(uint8_t chan, uint8_t note, uint8_t vel) {
  LOOP(i, ARRAY_SIZE(INST_NOTE)) {
    if (note == INST_NOTE[i]) {
      trig = true;
      trigmask |= (1UL << i);
      if (vel > 63) trigmask |= 1; // accent
    }
  }
}
static void midi_sysex_cb(byte *data, unsigned sz) {
  // yeah, we're only looking for a very specific type of individual...
  if (sz < 4 || data[0] != 0xF0 || data[1] != 0x7D || data[sz - 1] != 0xF7)
    return;

  // special command to initiate flash update
  if (0x4A == data[2]) { jumptoboot(); }
}
static void midi_start_cb() {
  midi_clk = true;
  clock_reset();
}
static void midi_stop_cb() {
  midi_clk = false;
  clock_reset();
}
static void midi_clock_cb() {
  if (midi_clk) {
    if (clock_advance()) sequencer_advance();
  }
}

// --- INIT ---
void setup() {
  Serial1.begin(31250);
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleNoteOn(midi_note_on);
  MIDI.setHandleNoteOff(midi_note_off);
  MIDI.setHandleSystemExclusive(midi_sysex_cb);
  MIDI.setHandleClock(midi_clock_cb);
  MIDI.setHandleStart(midi_start_cb);
  MIDI.setHandleStop(midi_stop_cb);

  hw::Init();

  delay(100);

  // step LEDs & Trigger test
  LOOP(i, 4) {
    LOOP(n, 4) {
      hw::LightCell(i, n);
      hw::Trigger(1UL << (i*4 + n));
      delay(150);
    }
  }
  LOOP(i, INST_COUNT) {
    length[i] = 16;
  }
}

void loop() {
  MIDI.read(); // to trigger callback handlers

  hw::PollInputsAndSetLeds(ledframe);
  // --- input flags
  clk_run = !Input(RUN).off();
  const bool clear_mod = Input(CLEAR_KEY).held();
  const uint8_t inst_sel = 11 - hw::GetInstSelect();
  // flicker between both for 'AB' mode
  const bool ab_led = Input(ABVAR_BIT0).read() ||
                      (Input(ABVAR_BIT1).read() && (poll_ticks & 0x10));
  const bool variation = Input(IFVARIATION_B_SWITCH).read();
  hw::SetExtraLeds(ab_led, clk_run ? (step[inst_sel] >= 16) : variation);

  ++poll_ticks;
  ledframe = 0;


  // target the closest step
  const uint8_t rec_step =
      (!clk_run || (clk_count < ppqn / 2)) ? step[inst_sel] : (step[inst_sel] + 1) % MAX_SEQ_STEPS;

  bool editmode = false;

  switch (hw::GetModeSwitch()) {
    // -- edit modes
    case PATCLR_CODE:
      // todo: set a flag?
      if (clear_mod) {
        break;
      }
      // fall thru
    case PART1_CODE:
    case PART2_CODE:
      editmode = true;
      // show selected instrument hits
      LOOP(i, 16) {
        if (sequence[i + (variation * 16)] & (1UL << inst_sel))
          ledframe |= (1UL << i);
      }
      if (clear_mod) {
        // set the last step
        LOOP(i, 16) {
          if (Input(i).rising())
            length[inst_sel] = 1 + i + (variation * 16);
        }
      }
      break;

    // -- play modes
    case MANPLAY_CODE:
      break;
    case PLAY_CODE:
      break;
    case COMPOSE_CODE:
      break;
  }

  if (clear_mod) {
    // TEST: show Mode and Auto-fill switches
    /*ledframe |= (1UL << (hw::GetModeSwitch()));*/
    /*ledframe |= (1UL << (8 + hw::GetAutoFill()));*/

    if (!editmode) {
      LOOP(i, 12) {
        // --- clear a whole drum track
        if (Input(i).rising()) {
          LOOP(s, MAX_SEQ_STEPS) {
            sequence[s] &= ~(1UL << i);
          }
        }
      }
    }
  } else if (editmode) {
    // edit selected instrument hits
    LOOP(i, 16) {
      if (Input(i).rising()) {
        sequence[i + (variation * 16)] ^= (1UL << inst_sel); // toggle
      }
    }
  } else {
    // set bits in realtime
    LOOP(i, 12) { // each step is one instrument
      if (Input(i).rising()) {
        trigmask |= (1UL << i);
        sequence[rec_step] |= (1UL << i);
        trig = true;
      }
    }
  }


  if (Input(TAP_FILL_IN).rising() && !clk_run && !midi_clk) 
    sequencer_advance();
  if (Input(RUN).rising() || Input(RUN).falling()) {
    clock_reset();
  }

  if (Input(CLEAR_KEY).rising()) {
    switch (hw::GetPrescale()) {
      case PSCODE_1:
        ppqn = 8;
        break;
      case PSCODE_2:
        ppqn = 4;
        break;
      case PSCODE_3:
        ppqn = 12;
        break;
      case PSCODE_4:
        ppqn = 6;
        break;
    }
  }

  // --- Clock & Sequencer ---
  const bool clocked = Input(CLOCK).rising_2bit();
  if (!midi_clk && clocked) {
    if (clock_advance() && clk_run) {
      sequencer_advance();
    }
  }

  // current step LED flashes on beat
  if ((variation && step[inst_sel] >= 16) || (!variation && step[inst_sel] < 16)) {
    if ((clk_count % ppqn) < (ppqn/2)) ledframe ^= (1UL << (step[inst_sel] % 16));
  }
  // -------------------------

  // allows a window for simultaneous triggers to gather
  if (trig_timer > 2 && trig) {
    hw::Trigger(trigmask);
    trigmask = 0;
    trig = 0;
    trig_timer = 0;
  }
}
