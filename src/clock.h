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
  uint32_t prev_cycle; // last measurement from external sync

  // settings
  uint8_t swing = 0; // 0 to 100

  // maybe, maybe not
  void midi_start();
  void midi_stop();
  void start();
  void stop();

  void reset() {
    beat_counter = 0;
    trig_q = true;
    int_timer_ = 0;
    ext_timer_ = 0;
  }

  void resync() {
    // what if it was ALMOST there?
    //if (int_timer_ > int_tempo/4)
    if (beat_counter & 1) {
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

  inline void check_trig(const uint8_t ppqn = 6) {
    // output trigger math, using ppqn and swing
    const uint32_t beat_interval = (int_tempo * ppqn);
    const int32_t swing_offset =
        ((beat_counter & 1) ? -1 : 1) * ((beat_interval >> 1) * swing / 100);
    if (int_timer_ > (beat_interval + swing_offset)) {
      ++beat_counter;
      int_timer_ -= (beat_interval + swing_offset);
      trig_q = true;
    }
  }

  // ppqn @ 6 yields 16th notes, aka step advance 4x per beat
  void tick(bool rising_edge, const uint8_t ppqn = 6) {
    if (rising_edge) {
      uint32_t interval = ext_timer_;
      if (interval < SYNC_WINDOW) return;

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

    check_trig(ppqn);
  }
};
