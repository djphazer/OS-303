// Copyright (c) 2026, Nicholas J. Michalek
//
// MIT License

/* 
 * Can we make a unified clock-sync object?
 * Handle start/stop from various sources, and syncing to external pulses,
 * with some config stuff, of course...
 *
 * written by hand, for the love of the game
 */

#pragma once
#include <elapsedMillis.h>

struct ClockEngine {
  static constexpr int SYNC_WINDOW = 50; // micros
  static constexpr uint8_t PPQN = 24;
  static constexpr uint8_t TRIGMULT = 4; // 1/16th notes, step advance 4x per beat

  // state
  elapsedMicros int_timer_, ext_timer_;
  uint16_t beat_counter;
  bool trig_q = false; // consumable
  uint32_t int_tempo; // micro-second interval @ 24ppqn
  uint32_t prev_cycle; // last measurement from external sync

  uint8_t clk_count = 0;

  // settings
  uint8_t swing = 0; // 0 to 100

  // maybe, maybe not
  void midi_start();
  void midi_stop();
  void start();
  void stop();

  void reset() {
    clk_count = 0;
    beat_counter = 0;
    trig_q = true;
    int_timer_ = 0;
    ext_timer_ = 0;
  }

  void resync() {
    // what if it was ALMOST there?
    if (beat_counter & 1) {
      // odd beats gotta jump forward
      ++beat_counter;
      trig_q = true;
    }
    int_timer_ = 0;
  }

  bool trig_pop() {
    if (trig_q) {
      trig_q = false;
      return true;
    }
    return false;
  }

  // wow, this is just a 50% pulse wave
  const bool check_gate(const uint8_t mult = 1) const {
    const int period = (PPQN / mult);
    return clk_count % period < period/2;
  }
  // okay, but we need one with swing...
  const bool check_swung_gate(const uint8_t mult = 1) const {
    const uint32_t beat_interval = (int_tempo * (PPQN / mult));
    const int32_t swing_offset =
        ((beat_counter & 1) ? -1 : 1) * ((beat_interval >> 1) * swing / 100);
    const uint32_t gate_interval = (beat_interval + swing_offset) / 2;
    return int_timer_ < gate_interval;
  }

  inline void check_trig() {
    // output trigger math, using ppqn and swing
    // 24/4 == 6 pulses, yields swung 16th notes
    const uint32_t beat_interval = (int_tempo * (PPQN / TRIGMULT));
    const int32_t swing_offset =
        ((beat_counter & 1) ? -1 : 1) * ((beat_interval >> 1) * swing / 100);
    if (int_timer_ > (beat_interval + swing_offset)) {
      ++beat_counter;
      int_timer_ -= (beat_interval + swing_offset);
      trig_q = true;
    }
  }

  void tick(bool rising_edge) {
    if (rising_edge) {
      uint32_t interval = ext_timer_;
      if (interval < SYNC_WINDOW) return;

      ++clk_count %= PPQN; // * 2;
      if (0 == clk_count) resync();

      // check diff against int_tempo to determine how far off we are
      //
      // is it exactly twice? maybe we missed a pulse
      int diff = (int32_t(interval/2) - int_tempo);

      if (diff > SYNC_WINDOW || diff < -SYNC_WINDOW) // too far
        diff = int32_t(interval) - int_tempo; // check actual

      if (diff > SYNC_WINDOW || diff < -SYNC_WINDOW) {
        // still too far, needs adjustment
        int_tempo = ((int_tempo + interval + 1) / 2); // nudge halfway
      }

      ext_timer_ = 0;
    }

    check_trig();
  }
};
