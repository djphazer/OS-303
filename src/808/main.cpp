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

static void midi_sysex_cb(byte *data, unsigned sz) {
  // yeah, we're only looking for a very specific type of individual...
  if (sz < 4 || data[0] != 0xF0 || data[1] != 0x7D || data[sz - 1] != 0xF7)
    return;

  // special command to initiate flash update
  if (0x4A == data[2]) { jumptoboot(); }
}

void setup() {
  Serial1.begin(31250);
  MIDI.begin(MIDI_CHANNEL_OMNI);
  // MIDI.setHandleNoteOn(midi_note_on);
  // MIDI.setHandleNoteOff(midi_note_off);
  MIDI.setHandleSystemExclusive(midi_sysex_cb);

  hw::Init();

  delay(100);

  // LED test
  LOOP(i, 4) {
    LOOP(n, 4) {
      hw::LightCell(i, n);
      hw::Trigger(1UL << (i*4 + n));
      delay(150);
    }
  }
}

static uint16_t ticks = 0;
static uint16_t ledframe = 0;
void loop() {
  MIDI.read(); // to trigger callback handlers

  hw::PollInputsAndSetLeds(ledframe);
  // flicker between them for 'AB' mode
  const bool ab_led = hw::inputs[ABVAR_BIT0].held() &&
                      (hw::inputs[ABVAR_BIT1].off() && (ticks & 1));
  hw::SetExtraLeds(ab_led, hw::inputs[IFVARIATION_B_SWITCH].held());
  // clear frame
  ledframe = 0;

  // Testing: show pressed button, and trigger instruments
  bool trig = false;
  uint16_t testmask = 0;
  LOOP(i, 16) {
    if (hw::inputs[i].held()) {
      // PORTF = led_bytes[i];
      testmask |= (1UL << i);
      ledframe |= (1UL << i);
    }
  }
  LOOP(i, 12) {
    if (hw::inputs[i].rising()) trig = true;
  }
  if (trig) hw::Trigger(testmask);

  ++ticks;
}
