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

#include <elapsedMillis.h>

struct ClockEngine {
  static constexpr int SYNC_WINDOW = 50; // micros

  // state
  elapsedMicros int_timer_, ext_timer_;
  uint16_t beat_counter;
  bool trig_q = false; // consumable
  uint32_t int_tempo; // micro-second interval @ 24ppqn

  // settings
  uint8_t swing = 0; // 0 to 100
  uint8_t ppqn = 12; // clock divider

  // maybe, maybe not
  void midi_start();
  void midi_stop();
  void start();
  void stop();

  void resync() {
    beat_counter = 0;
    // todo: what if it was ALMOST there?
    int_timer_ = 0;
    trig_q = true; // always retrigger
  }

  bool trig_pop() {
    if (trig_q) {
      trig_q = false;
      return true;
    }
    return false;
  }

  inline void check_trig() {
    // output trigger math, using ppqn and swing
    const uint32_t beat_interval = (int_tempo * ppqn);
    const int32_t swing_offset =
        ((beat_counter & 1) ? -1 : 1) * ((beat_interval >> 1) * swing / 100);
    if (int_timer_ > (beat_interval + swing_offset)) {
      ++beat_counter;
      int_timer_ = 0;
      trig_q = true;
    }
  }

  void tick(bool rising_edge) {
    if (rising_edge) {
      uint32_t interval = ext_timer_;

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
