// Copyright (c) 2026, Nicholas J. Michalek
/* 
 * Sequencer engine logic and data model for TB-303 CPU
 */

#pragma once
#include <Arduino.h>
#include <EEPROM.h>

//
// *** Utilities ***
//
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define CONSTRAIN(x, lb, ub) do { if (x < (lb)) x = lb; else if (x > (ub)) x = ub; } while (0)

static constexpr int MAX_STEPS = 64;
static constexpr int NUM_PATTERNS = 16;

enum SequencerMode {
  NORMAL_MODE,
  PITCH_MODE,
  TIME_MODE,
};

enum OctaveState {
  OCTAVE_DOWN,
  OCTAVE_ZERO,
  OCTAVE_UP,
  OCTAVE_DOUBLE_UP,
};

static constexpr uint8_t PITCH_EMPTY = 0xFF; // unwritten step sentinel
static constexpr uint8_t PITCH_DEFAULT = (OCTAVE_ZERO*12); // clean default: C, octave zero, no flags

struct Sequence {
  // --- sequence data - 128 bytes
  // for DAC pitch, 0 is a low G#; 4 is lowest C; and middle C is 28
  // We're gonna store pitch as if the lowest C is 0, so it needs +4 when sent to DAC
  uint8_t pitch[MAX_STEPS]; // 6-bit Pitch, Accent, and Slide
  uint8_t time_data[MAX_STEPS/2]; // 0=rest, 1=note, 2=tie, 3=??
  // time is stored as nibbles, so there's actually a lot of padding
  uint8_t reserved[MAX_STEPS/2 - 1];
  uint8_t length = 16;
  // --- end sequence data

  // state
  int pitch_pos, time_pos;
  bool reset; // hold plz

  // --- functions
  // 6-bit pitch, 0 == low C
  const uint8_t get_pitch() const {
    if (step_is_empty()) return PITCH_DEFAULT; // silent default, gate will be off
    return pitch[pitch_pos] & 0x3f;
  }
  const uint8_t get_octave() const {
    if (step_is_empty()) return OCTAVE_ZERO;
    return get_pitch() / 12;
  }
  // semitone index (0–11) for LED display. Returns 0xFF if step is unwritten.
  const uint8_t get_semitone() const {
    if (step_is_empty()) return PITCH_EMPTY;
    return get_pitch() % 12;
  }
  const uint8_t get_accent() const {
    if (step_is_empty()) return 0;
    return pitch[pitch_pos] & (1<<6);
  }
  const bool get_slide(uint8_t step) const {
    if (pitch_is_empty(step)) return false;
    return pitch[step] & (1<<7);
  }
  const bool get_slide() const {
    return get_slide(pitch_pos);
  }
  const bool is_sliding() const {
    return (pitch_pos < length) && get_slide(pitch_pos+1);
  }
  const bool is_tied() const {
    return (time_pos < length) && (time(time_pos+1) == 2);
  }
  inline uint8_t time(uint8_t idx) const {
    return (time_data[idx >> 1] >> 4*(idx & 1)) & 0xf;
  }
  const uint8_t get_time() const {
    return time(time_pos);
  }

  void SetTime(uint8_t t) {
    const uint8_t upper = time_pos & 1;
    uint8_t &data = time_data[time_pos >> 1];
    data = (~(0x0f << 4*upper) & data) | (t << 4*upper);
  }
  void SetPitch(uint8_t p, uint8_t flags) {
    pitch[pitch_pos] = (p & 0x3f) | (flags & 0xc0);
  }
  void SetPitchSemitone(uint8_t p) {
    init_if_empty(); 
    pitch[pitch_pos] =
        ((get_octave() * 12 + p) & 0x3f) | (pitch[pitch_pos] & 0xc0);
  }
  void SetLength(uint8_t len) { length = constrain(len, 1, MAX_STEPS); }
  void SetOctave(int oct) {
    init_if_empty();
    CONSTRAIN(oct, 0, 3);
    pitch[pitch_pos] =
        ((uint8_t)oct * 12 + get_semitone()) | (pitch[pitch_pos] & 0xc0);
  }

  void ToggleSlide() { init_if_empty(); pitch[pitch_pos] ^= (1 << 7); }
  void ToggleAccent() { init_if_empty(); pitch[pitch_pos] ^= (1 << 6); }
  void SetSlide(bool on) {
    init_if_empty();
    pitch[pitch_pos] = (pitch[pitch_pos] & ~(1 << 7)) | (on << 7);
  }
  void SetAccent(bool on) {
    init_if_empty();
    pitch[pitch_pos] = (pitch[pitch_pos] & ~(1 << 6)) | (on << 6);
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
    reset = true;
  }

  bool pitch_is_empty(uint8_t pos) const { return pitch[pos] == PITCH_EMPTY; }
  bool step_is_empty() const { return pitch_is_empty(pitch_pos); }

  void init_if_empty() {
    if (step_is_empty()) pitch[pitch_pos] = PITCH_DEFAULT;
  }

  void Clear() {
    for (uint8_t i = 0; i < MAX_STEPS; ++i) {
      pitch[i] = PITCH_DEFAULT;
      time_data[i>>1] = 0; // all rests
    }
    length = 8;
  }

  // returns false for rests
  bool Advance() {
    if (reset) {
      reset = false;
      return time(0);
    }
    ++time_pos %= length;
    if (time_pos == 0)
      pitch_pos = 0;
    else if (time(time_pos) == 1)
      ++pitch_pos;
    return time(time_pos);
  }

  // used in write mode
  void AdvancePitch() {
    if (reset) reset = false;
    else ++pitch_pos %= length;
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
  //elapsedMillis delay_timer = 0;

  // pattern storage
  Sequence pattern[NUM_PATTERNS]; // 32 steps each
  uint8_t p_select = 0;
  uint8_t next_p = 0; // queued pattern
                      // TODO: start & end for chains

  SequencerMode mode_ = NORMAL_MODE;
  //uint8_t chains[16][7]; // 7 tracks, up to 16 chained patterns

  int8_t clk_count = -1;

  bool slide_gate = false; // flag to keep the gate raised before a slid step
  bool stale = false;
  bool resting = false; // hey shutup

  // actions
  void Load() {
    Serial.println("Loading from EEPROM...");

    // TODO: settings and calibration
    GlobalSettings.Load();
    if (GlobalSettings.Validate()) {
      for (uint8_t i = 0; i < NUM_PATTERNS; ++i) {
        ReadPattern(pattern[i], i);
        if (0 == pattern[i].length) pattern[i].SetLength(8);
      }
    } else {
      Serial.println("EEPROM data invalid, initializing...");
      // initialize memory with defaults or zeroes
      for (uint8_t i = 0; i < NUM_PATTERNS; ++i) {
        pattern[i].Clear();
      }
      GlobalSettings.Save();
      stale = true;
      Save();
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
    if (pidx < 0) {
      // save all
      for (uint8_t i = 0; i < NUM_PATTERNS; ++i) {
        Serial.print(".");
        WritePattern(pattern[i], i);
      }
    } else
      WritePattern(pattern[pidx], pidx);

    stale = false;
    Serial.println("DONE!");
  }

  void Tick(uint8_t &state) {
    // static bool gate_on = 0;
    // if (gate_on != get_gate()) {
      // rising or falling
    // }
    // gate_on = get_gate();
  }

  // returns false for rests
  bool Advance() {
    bool result = get_sequence().Advance();
    // jump to next pattern at end of current one
    if (0 == get_sequence().time_pos && next_p != p_select) {
      p_select = next_p;
      get_sequence().Reset();
      result = get_sequence().Advance();
    }
    if (result) {
       slide_gate = get_slide() || get_sequence().is_tied() || get_sequence().is_sliding();
    }
    resting = !result;
    return result;
  }

  // returns true for new pitch step
  bool Clock() {
    bool send_note = false;
    ++clk_count %= 6;

    if (clk_count == 0) { // sixteenth note advance
      send_note = Advance();
      //delay_timer = 0;
    }

    return send_note;
  }

  void Reset() {
    get_sequence().Reset();
    clk_count = -1;
    slide_gate = false;
    resting = true;
  }

  void Generate() {
    if (mode_ == PITCH_MODE)
      get_sequence().RegenPitch();
    else if (mode_ == TIME_MODE)
      get_sequence().RegenTime();
  }

  void ClearPattern(uint8_t idx) {
    pattern[idx].Clear();
    stale = true;
    if (idx == p_select) {
      Reset();
      mode_ = NORMAL_MODE;
    }
  }

  // getters
  SequencerMode get_mode() const { return mode_; }

  Sequence &get_sequence() { return pattern[p_select]; }
  const Sequence &get_sequence() const { return pattern[p_select]; }
  const Sequence &get_pattern(uint8_t idx) const { return pattern[idx & 0xf]; }

  bool get_gate() const {
    // TODO: fancy stuff for shuffle, ratchets, etc.
    // also for emulated jitter
    //delay_timer > 0 && 
    return ((clk_count < 3) || slide_gate) && !resting;
  }
  bool get_accent() const {
    return !resting && get_sequence().get_accent() && (clk_count < 2 || get_sequence().is_tied());
  }
  uint8_t get_semitone() const {
    return get_sequence().get_semitone();
  }
  uint8_t get_pitch() const {
    return get_sequence().get_pitch();
  }
  // MIDI note number: OCTAVE_DOWN C = 36 (C2), OCTAVE_ZERO C = 48 (C3)
  uint8_t get_midi_note() const {
    if (get_sequence().step_is_empty()) return 48;
    const uint8_t semitone = get_sequence().get_semitone();
    const uint8_t oct = get_sequence().get_octave();
    return 36 + semitone + (oct * 12);
  }
  bool get_slide() const {
    return get_sequence().get_slide();
  }
  uint8_t get_time_pos() const {
    return get_sequence().time_pos;
  }
  uint8_t get_patsel() const {
    return p_select;
  }
  uint8_t get_next() const {
    return next_p;
  }
  const uint8_t get_time() const {
    return get_sequence().get_time();
  }
  const uint8_t get_length() const {
    return get_sequence().length;
  }

  // setters
  void SetPattern(uint8_t p_, bool override = false) {
    next_p = p_ & 0xf; // p_ % 16;
    if (override) p_select = next_p;
  }
  void SetLength(uint8_t len) {
    get_sequence().SetLength(len);
    stale = true;
  }
  bool BumpLength() {
    stale = true;
    return get_sequence().BumpLength();
  }
  void SetMode(SequencerMode m, bool reset = false) {
    if (reset && m != mode_) Reset();
    mode_ = m;
  }
  void NudgeOctave(int dir) {
    get_sequence().SetOctave(int(get_sequence().get_octave()) + dir);
  }
  // change pitch, preserving flags
  void SetPitchSemitone(uint8_t p) {
    get_sequence().SetPitchSemitone(p);
    stale = true;
  }
  void SetPitch(uint8_t p, uint8_t flags) {
    get_sequence().SetPitch(p, flags);
    stale = true;
  }
  void SetTime(uint8_t t) {
    get_sequence().SetTime(t);
    stale = true;
  }

  void ToggleSlide() {
    if (mode_ == PITCH_MODE)
      get_sequence().ToggleSlide();
    stale = true;
  }
  void ToggleAccent() {
    if (mode_ == PITCH_MODE)
      get_sequence().ToggleAccent();
    stale = true;
  }

};
