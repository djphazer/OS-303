// Copyright (c) 2026, Nicholas J. Michalek
/* 
 * Sequencer engine logic and data model for TB-303 CPU
 */

#pragma once
#include <Arduino.h>
#include <EEPROM.h>

static constexpr int MAX_STEPS = 32;
static constexpr int NUM_PATTERNS = 16;

enum SequencerMode {
  NORMAL_MODE,
  PITCH_MODE,
  TIME_MODE,
};

struct Sequence {
  // data
                                 // TODO: octave up/down flags?
  uint8_t pitch[MAX_STEPS]; // 6-bit Pitch, Accent, and Slide
  uint8_t time_data[MAX_STEPS/2];  // 0=rest, 1=note, 2=tie, 3=triplets?
  // time is stored as nibbles, so there's actually a lot of padding
  uint8_t reserved[MAX_STEPS/2 - 1];
  uint8_t length = 16;

  const uint8_t time(uint8_t idx) const {
    return (time_data[idx >> 1] >> (idx & 1)) & 0xf;
  }

  // state
  uint8_t pitch_pos, time_pos;

  const uint8_t get_pitch() const {
    return pitch[pitch_pos] & 0x3f;
  }
  const uint8_t get_accent() const {
    return pitch[pitch_pos] & (1<<6);
  }
  const bool get_slide(uint8_t step) const {
    return pitch[step] & (1<<7);
  }
  const bool get_slide() const {
    return get_slide(pitch_pos);
  }
  const bool is_tied() const {
    return time_pos < length && time(time_pos+1) == 2;
  }
  const uint8_t get_time() const {
    return time(time_pos);
  }

  void SetTime(uint8_t t) {
    const uint8_t upper = time_pos & 1;
    uint8_t &data = time_data[time_pos >> 1];
    data = (~(0x0f << 4*upper) & data) | (t << 4*upper);
  }
  void SetPitch(uint8_t p) {
    // easier to just let the pitch include the accent/slide bits
    pitch[pitch_pos] = p;
    // (p & 0x3f) | (pitch[pitch_pos] & 0xc0);
  }
  void SetLength(uint8_t len) { length = constrain(len, 1, MAX_STEPS); }

  void ToggleSlide() {
    pitch[pitch_pos] ^= (1<<7);
  }
  void ToggleAccent() {
    pitch[pitch_pos] ^= (1<<6);
  }

  bool BumpLength() {
    if (++length == MAX_STEPS) return false;
    return true;
  }

  void RegenTime() {
    time_data[time_pos] = random();
  }
  void RegenPitch() {
    pitch[pitch_pos] = random();
  }

  void Reset() {
    pitch_pos = 0;
    time_pos = 0;
  }

  // returns false for rests
  bool Advance() {
    ++time_pos %= length;
    if (time_pos == 0)
      pitch_pos = 0;
    else if (time(time_pos) == 1)
      ++pitch_pos;
    return time(time_pos);
  }
};

// --- EEPROM data layout
static constexpr int SETTINGS_SIZE = 128;
static constexpr int PATTERN_SIZE = MAX_STEPS * 2;
const char* const sig_pew = "PewPewPew!!!";

extern EEPROMClass storage;

struct PersistentSettings {
  char signature[16];

  void Load() {
    storage.get(0, signature);
  }
  void Save() {
    storage.put(0, signature);
  }
  bool Validate() const {
    if (0 == strncmp(signature, sig_pew, 12))
      return true;

    strcpy((char*)signature, sig_pew);
    return false;
  }
};

extern PersistentSettings GlobalSettings;

void WritePattern(Sequence &seq, int idx) {
  uint8_t *src = seq.pitch;
  idx *= PATTERN_SIZE;
  for (uint8_t i = 0; i < PATTERN_SIZE; ++i) {
    storage.update(SETTINGS_SIZE + idx + i, src[i]);
  }
}
void ReadPattern(Sequence &seq, int idx) {
  uint8_t *dst = seq.pitch;
  idx *= PATTERN_SIZE;
  for (uint8_t i = 0; i < PATTERN_SIZE; ++i) {
    dst[i] = storage.read(SETTINGS_SIZE + idx + i);
  }
}

struct Engine {
  elapsedMillis delay_timer = 0;

  // pattern storage
  Sequence pattern[NUM_PATTERNS]; // 32 steps each
  uint8_t p_select = 0;
  uint8_t next_p = 0; // queued pattern
                      // TODO: start & end for chains

  SequencerMode mode_ = NORMAL_MODE;
  //uint8_t chains[16][7]; // 7 tracks, up to 16 chained patterns

  uint8_t clk_count = 0;

  uint8_t cv_out_ = 0; // semitone
  int8_t octave_ = 0;

  bool slide_on = false; // flag to keep raised
  bool gate_on = false;
  bool stale = false;

  // actions
  void Load() {
    Serial.println("Loading from EEPROM...");

    // TODO: settings and calibration
    GlobalSettings.Load();
    if (GlobalSettings.Validate()) {
      for (uint8_t i = 0; i < NUM_PATTERNS; ++i) {
        ReadPattern(pattern[i], i);
        if (0 == pattern[i].length) pattern[i].SetLength(16);
      }
    } else {
      // initialize memory with defaults or zeroes
      GlobalSettings.Save();
      //Save();
    }

#if DEBUG
    Serial.println("First pattern:");
    for (uint8_t i = 0; i < 64; ++i) {
      Serial.printf("%2x ", pattern[0].pitch[i]);
    }
    Serial.print("\n");
#endif
  }
  void Save(int pidx = -1) {
    if (!stale) return;
    Serial.print("Saving to EEPROM... ");
    // TODO: only update patterns that have changed
    if (pidx < 0) {
      pidx = p_select;
      /*
      for (uint8_t i = 0; i < NUM_PATTERNS; ++i) {
        Serial.print(".");
        WritePattern(pattern[i], i);
      }
      */
    }

    WritePattern(pattern[pidx], pidx);

    stale = false;
    Serial.println("DONE!");
  }

  void Tick(uint8_t &state) {
    if (gate_on != get_gate()) {
      // rising or falling
    }
    gate_on = get_gate();
  }

  bool Advance() {
    const bool result = pattern[p_select].Advance();
    // jump to next pattern at end of current one
    if (0 == pattern[p_select].time_pos && next_p != p_select)
      p_select = next_p;
    // but don't bother updating result to step 0 on new pattern, pfft
    return result;
  }

  bool Clock() {
    bool send_note = false;
    ++clk_count %= 6;

    if (clk_count == 1) { // sixteenth note advance
      send_note = Advance();
      delay_timer = 0;
    }

    // hmmmm
    slide_on = get_slide() || pattern[p_select].is_tied();

    return send_note;
  }

  void Reset() {
    pattern[p_select].Reset();
    clk_count = 0;
    gate_on = false;
    slide_on = false;
  }

  void Generate() {
    if (mode_ == PITCH_MODE)
      pattern[p_select].RegenPitch();
    else if (mode_ == TIME_MODE)
      pattern[p_select].RegenTime();
  }

  // handle input - semitone from 0-11
  void NoteOn(int pitch) {
    cv_out_ = pitch + octave_ * 12 + 36;
    gate_on = true;
  }
  void NoteOff(uint8_t pitch) {
    if (cv_out_ == pitch + octave_ * 12 + 36)
      gate_on = false;
  }

  // getters
  SequencerMode get_mode() const { return mode_; }

  bool get_gate() const {
    return //delay_timer > 0 && 
      (clk_count < 4 || slide_on);
  }
  bool get_accent() const {
    return pattern[p_select].get_accent() && clk_count < 3;
  }
  uint8_t get_pitch() const {
    return pattern[p_select].get_pitch();
  }
  bool get_slide() const {
    return pattern[p_select].get_slide();
  }
  uint8_t get_time_pos() const {
    return pattern[p_select].time_pos;
  }
  uint8_t get_patsel() const {
    return p_select;
  }
  uint8_t get_next() const {
    return next_p;
  }
  const uint8_t get_time() const {
    return pattern[p_select].get_time();
  }
  const uint8_t get_length() const {
    return pattern[p_select].length;
  }

  // setters
  void SetPattern(uint8_t p_, bool override = false) {
    next_p = p_ & 0xf; // p_ % 16;
    if (override) p_select = next_p;
  }
  void SetLength(uint8_t len) {
    pattern[p_select].SetLength(len);
    stale = true;
  }
  bool BumpLength() {
    return pattern[p_select].BumpLength();
    stale = true;
  }
  void SetMode(SequencerMode m) {
    mode_ = m;
  }
  void NudgeOctave(int dir) {
    octave_ = constrain(octave_ + dir, -2, 2);
  }
  void SetPitch(uint8_t p, uint8_t flags) {
    pattern[p_select].SetPitch((24 + p + (octave_*12)) | flags);
    stale = true;
  }
  void SetTime(uint8_t t) {
    pattern[p_select].SetTime(t);
    stale = true;
  }

  void ToggleSlide() {
    if (mode_ == PITCH_MODE)
      pattern[p_select].ToggleSlide();
    stale = true;
  }
  void ToggleAccent() {
    if (mode_ == PITCH_MODE)
      pattern[p_select].ToggleAccent();
    stale = true;
  }

};
