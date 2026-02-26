// Copyright (c) 2026, Nicholas J. Michalek

#pragma once
#include "pins.h"

static constexpr uint16_t SWITCH_DELAY = 15; // microseconds

//
// --- 303 CPU driver functions
//

namespace DAC {
  static uint8_t pitch_ = 0;
  static uint8_t octave_bits_ = 0;
  static uint8_t slide_ = false;
  static uint8_t accent_ = false;
  static uint8_t gate_ = false;

  inline void Send() {
    // send to gate pin
    //digitalWriteFast(PI2_PIN, gate_ ? HIGH : LOW);
    // send to accent pin
    //digitalWriteFast(PE0_PIN, accent_ ? HIGH : LOW);

    // set 6-bit pitch for CV Out
    PORTC = pitch_ | (octave_bits_ << 4); // & 0x3f;

    PORTE = 0; // disable latch
    // set gate and accent pins, enable latch/slide
    PORTE = (gate_ << 1) | (accent_ << 6) | 0x1;

    if (!slide_) // turn slide bit back off
      PORTE ^= 0x1;

    // toggle the latch/slide pin
    //PORTE ^= 1;
    //PORTE ^= 1;
    // ...but this other way is... smarter? dumber? better.
    // Clock for the D/A converter chip
    /*
    if (slide) digitalWriteFast(PI1_PIN, LOW);
    digitalWriteFast(PI1_PIN, HIGH);
    if (!slide) digitalWriteFast(PI1_PIN, LOW);
    */
  }

  inline void SetPitch(uint8_t p, uint8_t oct = 0) {
    pitch_ = p;
    octave_bits_ = oct;
  }
  inline void SetGate(bool on) {
    gate_ = on;
  }
  inline void SetAccent(bool on) {
    accent_ = on;
  }
  inline void SetSlide(bool on) {
    slide_ = on;
  }
} // namespace DAC

namespace Leds {
  // like a framebuffer, each bit corresponds to an entry in the switched_leds table
  static uint8_t ledstate[3];

  void Set(OutputIndex ledidx, bool enable = true) {
    const uint8_t bit_idx = ledidx & 0x7;
    const uint8_t row = ledidx >> 3;
    ledstate[row] = (ledstate[row] & ~(1 << bit_idx)) | (enable << bit_idx);
  }
  void Set(const MatrixPin pins, bool enable = true) {
    if (enable && pins.select) {
      PORTF = 0x0f;
      digitalWriteFast(pins.select, LOW);
    }
    digitalWriteFast(pins.led, enable ? HIGH : LOW);
    //if (enable && pins.select) digitalWriteFast(pins.select, HIGH);
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

  void Send(const uint8_t tick) {
    //const uint8_t cycle = (tick >> 2) & 0x3; // scanner for select pins, bits 0-3

    // switched LEDs
    // which row depends on tick
    uint8_t mask = ledstate[(tick >> 1) & 1] >> (4 * ((tick >> 0) & 1));
    SetLedSelection(switched_leds[(tick & 0x3) << 2].select, mask);

    // direct LEDs
    for (uint8_t i = 16; i < 20; ++i) {
      digitalWriteFast(switched_leds[i].led, (ledstate[2] & (1 << (i-16))) ? HIGH : LOW);
    }

    // blank slate for next time
    ledstate[0] = 0;
    ledstate[1] = 0;
    ledstate[2] = 0;
  }

} // namespace Leds

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
