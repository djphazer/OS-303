// Copyright (c) 2026, Nicholas J. Michalek

#pragma once
#include "pins.h"

static constexpr uint16_t SWITCH_DELAY = 15; // microseconds

//
// --- 303 CPU driver functions
//
void SendCV(bool slide = false) {
  // Clock for the D/A converter chip
  if (slide) digitalWriteFast(PI1_PIN, LOW);
  digitalWriteFast(PI1_PIN, HIGH);
  if (!slide) digitalWriteFast(PI1_PIN, LOW);
}

void SetGate(bool on) {
  digitalWriteFast(PI2_PIN, on ? HIGH : LOW);
  PORTE ^= 1;
  PORTE ^= 1;
}
void SetAccent(bool on) {
  digitalWriteFast(PE0_PIN, on ? HIGH : LOW);
}

namespace Leds {
  // like a framebuffer, each bit corresponds to an entry in the switched_leds table
  uint8_t ledstate[3] = {0};

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

  void FlashLed(const OutputIndex led) {
    static elapsedMillis timer = 0;
    static bool onoff = false;
    if (timer > 100) {
      // blah blah
      onoff = !onoff;
      timer = 0;
    }
    Set(led, onoff);
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
