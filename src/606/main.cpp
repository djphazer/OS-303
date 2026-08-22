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
 * TR-606 sequencer firmware
 */

#include <Arduino.h>
#include <MIDI.h>
#include "drivers.h"
#include "../clock.h"

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

// --- State ---
static uint16_t ledframe = 0;
static bool trig = false;
static elapsedMillis trig_timer = 0;
static uint16_t trigmask = 0;
static elapsedMicros poll_timer = 0;
static uint8_t poll_ticks = 0;

// --clock state
static bool midi_clk = false;
static bool clk_run = false;
static ClockEngine clock_;

// crude sequencer model
static constexpr uint8_t MAX_SEQ_STEPS = 32;
static uint8_t sequence[MAX_SEQ_STEPS];
static uint8_t step = 0;
static bool reset = 0;

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);

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
  clk_run = true;
  clock_.reset();
}
static void midi_stop_cb() {
  midi_clk = false;
  clk_run = false;
  clock_.reset();
  //SAVE();
}
static void midi_clock_cb() {
  if (midi_clk) {
    clock_.tick(true);
  }
}

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
      delay(100);
    }
  }
}

// util
const PinState &Input(uint8_t i) { return hw::inputs[i]; }

void loop() {
  MIDI.read(); // to trigger callback handlers

  //if (poll_timer > 100) {
    hw::PollInputsAndSetLeds(ledframe);
    hw::SetPatGroupLed(step >= 8);

    ++poll_ticks;
    ledframe = 0;
    //poll_timer = 0;
  //}

  // --- input flags
  clk_run = !Input(RUN).off();
  const bool clear_mod = Input(CLEAR_KEY).held();
  const uint8_t inst_sel = hw::GetInstSelect();

  // target the closest step
  const uint8_t rec_step =
      (!clk_run || clock_.check_gate()) ? step : (step + 1) % MAX_SEQ_STEPS;

  bool editmode = Input(WRITE_MODE).held();
  if (editmode) {
    // show selected instrument hits
    LOOP(i, 16) {
      if (sequence[i] & (1UL << inst_sel))
        ledframe |= (1UL << i);
    }
  }

  if (clear_mod) {
    LOOP(i, 8) {
      // --- clear a whole drum track
      if (Input(i).rising()) {
        LOOP(s, MAX_SEQ_STEPS) {
          sequence[s] &= ~(1UL << i);
        }
      }
    }
  } else if (editmode) {
    // edit selected instrument hits
    LOOP(i, 16) {
      if (Input(i).rising()) {
        sequence[i] ^= (1UL << inst_sel); // toggle
      }
    }
  } else {
    // set bits in realtime
    LOOP(i, 8) { // first 8 steps are one instrument
      if (Input(i).rising()) {
        trigmask |= (1UL << i);
        sequence[rec_step] |= (1UL << i);
        trig = true;
      }
    }
  }


  if (Input(TAP_WRITE).rising()) ++step %= MAX_SEQ_STEPS;
  if (Input(RUN).rising() || Input(RUN).falling()) {
    step = 0;
    reset = true;
    clock_.reset();
  }

  if (Input(CLEAR_KEY).rising()) {
    switch (hw::GetPrescale()) {
      case PSCODE_1:
        // TODO
        break;
      case PSCODE_2:
        // TODO
        break;
      case PSCODE_3:
        // TODO
        break;
      case PSCODE_4:
        // TODO
        break;
    }
  }

  // --- Clock & Sequencer ---
  const bool clocked = Input(CLOCK).rising_2bit();
  if (!midi_clk && clocked) {
    clock_.tick(true);
  } else // the magic that happens in between the clocks... (swing)
    clock_.tick(false);

  // run it (or consume triggers when RUN is off)
  if (clock_.trig_pop() && clk_run) {
    //sequencer_advance();
    // advance sequencer
    if (reset) reset = false;
    else ++step %= MAX_SEQ_STEPS;

    trigmask |= sequence[step];
    trig = true;
  }

  // current step LED flashes on beat
  if (clock_.check_gate()) ledframe |= (1UL << step);
  // -------------------------

  // allows a window for simultaneous triggers to gather
  if (trig_timer > 2 && trig) {
    hw::Trigger(trigmask);
    trigmask = 0;
    trig = 0;
    trig_timer = 0;
  }
}
