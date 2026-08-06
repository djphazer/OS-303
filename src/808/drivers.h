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

#include "pins.h"

// my lil library of macros
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define CONSTRAIN(x, lb, ub) do { if (x < (lb)) x = lb; else if (x > (ub)) x = ub; } while (0)
#define LOOP(i, n) for(uint8_t i = 0; i < (n); ++i)

// a cute lil state machine with debounce
struct PinState {
  enum SignalState {
    // 4 bits for debounce
    STATE_OFF     = 0x00,
    STATE_RISING  = 0x07,
    STATE_FALLING = 0x08,
    STATE_ON      = 0x0F,
    STATE_MASK    = 0x0F // 4 bits only
  };
  uint8_t state = 0; // shiftreg
  void push(bool high) { state = (state << 1) | high; }
  const bool rising() const { return (state & STATE_MASK) == STATE_RISING; }
  const bool falling() const { return (state & STATE_MASK) == STATE_FALLING; }
  const bool held() const { return (state & STATE_MASK) == STATE_ON; }
  const bool off() const { return (state & STATE_MASK) == 0; }
  const bool read() const { return state & 1; }
};

namespace hw {
  static constexpr int SWITCH_DELAY = 3; // micros
  static constexpr int LED_DWELL_TIME = 25; // micros

  static PinState inputs[INPUT_COUNT];

  inline void Init() {
    for (uint8_t i = 0; i < ARRAY_SIZE(INPUTS); ++i) {
      pinMode(INPUTS[i], INPUT); // pullup?
    }
    for (uint8_t i = 0; i < ARRAY_SIZE(OUTPUTS); ++i) {
      pinMode(OUTPUTS[i], OUTPUT);
    }
  }

  inline void PollInputsAndSetLeds(uint16_t &frame) {
    // Raise all select pins and drive all LED pins LOW before reading.
    PORTF = 0x0f;
    delayMicroseconds(SWITCH_DELAY);

    // read PA pins while select pins are high
    for (uint8_t i = 0; i < 4; ++i) {
      inputs[EXTRA_PIN_OFFSET + i].push(digitalReadFast(status_pins[i])); // PAx
    }

    // open each switched channel with select pin
    for (uint8_t i = 0; i < 4; ++i) {
      // set LED segment
      const uint8_t leds = ((frame >> (4 * i)) & 0x0F);
      PORTF = (leds << 4) | 0x0F;

      // engage select pin
      digitalWriteFast(select_pin[i], LOW); // PHx
      delayMicroseconds(SWITCH_DELAY);

      for (uint8_t j = 0; j < 4; ++j) {
        // read pins
        inputs[ 0 + i*4 + j].push(digitalReadFast(button_pins[j])); // PBx
        inputs[16 + i*4 + j].push(digitalReadFast(status_pins[j])); // PAx
      }
      delayMicroseconds(LED_DWELL_TIME);

      // disengage select pin
      digitalWriteFast(select_pin[i], HIGH); // PHx
      delayMicroseconds(SWITCH_DELAY);
    }
  }
  inline void SetExtraLeds(bool a_or_b, bool part_two) {
    digitalWriteFast(AB_LED_PIN, a_or_b);
    digitalWriteFast(PART_LED_PIN, part_two);
  }

  // turn on ONE LED - only used by the startup sequence
  inline void LightCell(uint8_t s, uint8_t n) {
    PORTF |= 0x0F; // unselect
    delayMicroseconds(SWITCH_DELAY);
    PORTF = (1 << (4 + n)) | 0x0F; // set led
    PORTF &= ~(1 << s); // select
  }

  inline void Trigger(uint16_t mask) {
    // This could be optimized using PORTC, PORTD, and PORTE directly...
    // but probably easier to address individual pins anyway.
    LOOP(i, ARRAY_SIZE(inst_pins)) {
      digitalWriteFast(inst_pins[i], (mask & (1UL << i)) ? HIGH : LOW);
    }
    delayMicroseconds(10); // settling time
    digitalWriteFast(TRIG_PIN, HIGH);
    delayMicroseconds(10); // hold trig pulse high for a moment
    digitalWriteFast(TRIG_PIN, LOW);
  }

  // --- getters for switch codes - no debounce
  inline uint8_t GetPrescale() {
    return inputs[PRESCALE_BIT0].read() | inputs[PRESCALE_BIT1].read() << 1;
  }
  // Instrument select codes are in reverse order...
  inline uint8_t GetInstSelect() {
    return inputs[INST_SEL_BIT0].read() |
      (inputs[INST_SEL_BIT1].read() << 1) |
      (inputs[INST_SEL_BIT2].read() << 2) |
      (inputs[INST_SEL_BIT3].read() << 3);
  }
  inline uint8_t GetModeSwitch() {
    return inputs[MODE_BIT0].read() |
      (inputs[MODE_BIT1].read() << 1) |
      (inputs[MODE_BIT2].read() << 2);
  }
  inline uint8_t GetAutoFill() {
    return inputs[AUTOFILL_BIT0].read() |
      (inputs[AUTOFILL_BIT1].read() << 1) |
      (inputs[AUTOFILL_BIT2].read() << 2);
  }

} // namespace hw
