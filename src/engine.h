// Copyright (c) 2026, Nicholas J. Michalek
/*
 * engine.h — TB-303 pattern model + EEPROM; Engine handles patterns, clock, and gate.
 */

#pragma once
#include <Arduino.h>
#include <EEPROM.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define CONSTRAIN(x, lb, ub) do { if (x < (lb)) x = lb; else if (x > (ub)) x = ub; } while (0)

static constexpr int MAX_STEPS = 32;
static constexpr int MAX_CHAIN = 32;
static constexpr int NUM_PATTERNS = 16; // per bank; 4 banks in eeprom

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

static constexpr size_t PITCH_SIZE = MAX_STEPS;
static constexpr size_t TIME_SIZE = MAX_STEPS / 2;
static constexpr size_t METADATA_SIZE = 8;

// --- EEPROM data layout ( 4K total ) ---
static constexpr size_t SETTINGS_SIZE = 128; // 16-byte sig + flags + maybe quantizer scales?
static constexpr size_t SETTINGS_OFFSET = 0;
static constexpr size_t PITCH_DATA_SIZE = 2048; // 8-bit pitch steps
static constexpr size_t PITCH_DATA_OFFSET = SETTINGS_OFFSET + SETTINGS_SIZE;
static constexpr size_t TIME_DATA_SIZE = 1024; // 4-bit time steps
static constexpr size_t TIME_DATA_OFFSET = PITCH_DATA_OFFSET + PITCH_DATA_SIZE;
static constexpr size_t PATTERN_DATA_SIZE = 512; // 64 total patterns * 8 bytes
static constexpr size_t PATTERN_DATA_OFFSET = TIME_DATA_OFFSET + TIME_DATA_SIZE;
static constexpr size_t TRACK_DATA_SIZE = 6 * 32 * 2;
static constexpr size_t TRACK_DATA_OFFSET = PATTERN_DATA_OFFSET + PATTERN_DATA_SIZE;

// and this is what's left...
static constexpr int AUX_DATA_SIZE = 4096 - (TRACK_DATA_OFFSET + TRACK_DATA_SIZE);
static_assert(AUX_DATA_SIZE >= 0, "EEPROM OVERFLOW!");

// Signature - max 15 characters (+ null terminator)
// Shorter strings will match longer ones as a prefix, so appending characters
// retains compatibility with upstream and old versions, etc.
const char* const sig_pew = "OS-303-v0.5";

struct Sequence {
  //Sequence(uint8_t *p, uint8_t *t) : pitch(p), time_data(t) {}

  // --- sequence data - 128 bytes
  // for DAC pitch, 0 is a low G#; 4 is lowest C; and middle C is 28
  // We're gonna store pitch as if the lowest C is 0, so it needs +4 when sent to DAC
  uint8_t pitch[MAX_STEPS]; // 6-bit Pitch, Accent, and Slide
  uint8_t time_data[MAX_STEPS/2]; // 0=rest, 1=note, 2=tie, 3=??
  // time is stored as nibbles, so there's actually a lot of padding

  // this also doubles as the entry point for the metadata
  uint8_t reserved[METADATA_SIZE - 1]; // compiler won't like this
  uint8_t length = 16;
  // --- end sequence data

  // state
  int pitch_pos, time_pos;
  bool reset; // hold plz

  // void Init(uint8_t *p, uint8_t *t) {
  //   pitch = p;
  //   time_data = t;
  // }

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
    return pitch[step] & (1 << 7);
  }
  const bool get_slide() const {
    return get_slide(pitch_pos);
  }
  /// Next pitch step (wrapped) is a slide
  const bool next_is_slide() const {
    return get_slide((pitch_pos + 1) % length);
  }
  /// Current time step is a tie.
  bool is_tie() const {
    return (time_pos < length) && (time(time_pos) == 2);
  }
  /// Next time step (wrapped) is a tie.
  bool next_is_tie() const {
    const uint8_t n = (time_pos + 1) % length;
    return time(n) == 2;
  }
  bool next_is_note() const {
    const uint8_t n = (time_pos + 1) % length;
    return time(n) & 1;
  }
  /// Last tie in a run: on a tie step whose next step is not a tie.
  bool tie_chain_ending() const {
    return is_tie() && !next_is_tie();
  }

  inline uint8_t time(uint8_t idx) const {
    return (time_data[idx >> 1] >> (4 * (idx & 1))) & 0xf;
  }

  const uint8_t get_time() const { return time(time_pos); }

  void SetTime(uint8_t t, bool next = 0) {
    const uint8_t pos = (time_pos + next) % length;
    const uint8_t upper = pos & 1;
    uint8_t &data = time_data[pos >> 1];
    data = (~(0x0f << (4 * upper)) & data) | ((t & 0xf) << (4 * upper));
  }
  void SetPitch(uint8_t p, uint8_t flags, bool next = 0) {
    const uint8_t pos = (pitch_pos + next) % length;
    pitch[pos] = (p & 0x3f) | (flags & 0xc0);
  }
  void SetPitchSemitone(uint8_t p, bool next = 0) {
    init_if_empty(); 
    const uint8_t pos = (pitch_pos + next) % length;
    pitch[pos] =
        ((get_octave() * 12 + p) & 0x3f) | (pitch[pos] & 0xc0);
  }
  void SetLength(uint8_t len) {
    length = constrain(len, 1, MAX_STEPS);
    pitch_pos %= length;
    time_pos %= length;
  }
  void SetOctave(int oct, bool next = 0) {
    init_if_empty();
    CONSTRAIN(oct, 0, 3);
    const uint8_t pos = (pitch_pos + next) % length;
    pitch[pos] =
        ((uint8_t)oct * 12 + get_semitone()) | (pitch[pos] & 0xc0);
  }

  void ToggleSlide(bool next = 0) {
    init_if_empty();
    pitch[(pitch_pos + next) % length] ^= (1 << 7);
  }
  void ToggleAccent(bool next = 0) {
    init_if_empty();
    pitch[(pitch_pos + next) % length] ^= (1 << 6);
  }
  /* unused
  void SetSlide(bool on) {
    init_if_empty();
    pitch[pitch_pos] = (pitch[pitch_pos] & ~(1 << 7)) | (on << 7);
  }
  void SetAccent(bool on) {
    init_if_empty();
    pitch[pitch_pos] = (pitch[pitch_pos] & ~(1 << 6)) | (on << 6);
  }
  */

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
    else if (time(time_pos) & 1)
      ++pitch_pos;
    return time(time_pos);
  }

  // used in write mode
  void AdvancePitch() {
    if (reset) reset = false;
    else ++pitch_pos %= length;
  }
};

extern EEPROMClass storage;
extern Sequence pattern[NUM_PATTERNS]; // enough to hold one bank in RAM

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

inline void WritePattern(Sequence &seq, int idx) {
  uint8_t *src = seq.pitch;
  for (uint8_t i = 0; i < PITCH_SIZE; ++i) {
    storage.update(PITCH_DATA_OFFSET + (idx * PITCH_SIZE) + i, src[i]);
  }
  src = seq.time_data;
  for (uint8_t i = 0; i < TIME_SIZE; ++i) {
    storage.update(TIME_DATA_OFFSET + (idx * TIME_SIZE) + i, src[i]);
  }
  src = seq.reserved;
  for (uint8_t i = 0; i < METADATA_SIZE; ++i) {
    storage.update(PATTERN_DATA_OFFSET + (idx * METADATA_SIZE) + i, src[i]);
  }
}
inline void ReadPattern(Sequence &seq, int idx) {
  uint8_t *dst = seq.pitch;
  for (uint8_t i = 0; i < PITCH_SIZE; ++i) {
    dst[i] = storage.read(PITCH_DATA_OFFSET + (idx * PITCH_SIZE) + i);
  }
  dst = seq.time_data;
  for (uint8_t i = 0; i < TIME_SIZE; ++i) {
    dst[i] = storage.read(TIME_DATA_OFFSET + (idx * TIME_SIZE) + i);
  }
  dst = seq.reserved;
  for (uint8_t i = 0; i < METADATA_SIZE; ++i) {
    dst[i] = storage.read(PATTERN_DATA_OFFSET + (idx * METADATA_SIZE) + i);
  }
}

struct Engine {
  uint8_t p_chain[MAX_CHAIN]; // 4-bit p_select | 4-bit repeats
  uint8_t p_chain_pos = 0;
  int8_t p_repeats = -1;
  uint8_t p_chain_len = 0;

  uint8_t p_select = 0;
  uint8_t next_p = 0; // queued pattern

  uint8_t t_chain[MAX_CHAIN]; // 6-bit transpose progression
  uint8_t t_chain_idx = 0;
  bool t_chain_active = false;

  SequencerMode mode_ = NORMAL_MODE;

  int8_t clk_count = -1;

  bool gate_hold = false; // tie/slide: hold gate across 16ths (firstpr.com 303 slide / gate)
  bool slide_on = false;
  bool stale = false;
  bool resting = false; // hey shutup

  void Init() {
#if DEBUG
    Serial.println("Loading from EEPROM...");
#endif

    // TODO: settings and calibration
    GlobalSettings.Load();
    if (GlobalSettings.Validate()) {
      Load(0);
    } else {
#if DEBUG
      Serial.println("EEPROM data invalid, initializing...");
#endif
      // TODO: migration from old signatures could happen here instead

      // initialize memory with defaults or zeroes
      for (uint8_t i = 0; i < NUM_PATTERNS; ++i) {
        pattern[i].Clear();
      }
      GlobalSettings.Save();
      stale = true;
      Save(0);
      // stale = true;
      // Save(1);
      // stale = true;
      // Save(2);
      // stale = true;
      // Save(3);
    }
  }

  // actions
  void Load(uint8_t bank) {
    for (uint8_t i = 0; i < NUM_PATTERNS; ++i) {
      ReadPattern(pattern[i], i + bank * NUM_PATTERNS);
      if (0 == pattern[i].length || 0xff == pattern[i].length)
        pattern[i].Clear();
    }

#if DEBUG
    Serial.println("First pattern:");
    for (uint8_t i = 0; i < 64; ++i) {
      Serial.printf("%2x ", pattern[0].pitch[i]);
    }
    Serial.print("\n");
#endif
  }
  void Save(uint8_t bank) {
    if (!stale) return;
#if DEBUG
    Serial.print("Saving to EEPROM... ");
#endif
    // save all
    for (uint8_t i = 0; i < NUM_PATTERNS; ++i) {
      WritePattern(pattern[i], i + bank * NUM_PATTERNS);
    }

    stale = false;
#if DEBUG
    Serial.println("DONE!");
#endif
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
    if (0 == get_sequence().time_pos) {
      // pattern-chaining active?
      if (p_chain_len) {
        if (++p_repeats > (p_chain[p_chain_pos] >> 4)) {
          ++p_chain_pos %= p_chain_len;
          p_repeats = 0;
        }
        next_p = p_chain[p_chain_pos] & 0x0f;
      }

      if (next_p != p_select) {
        p_select = next_p;
        get_sequence().Reset();
        result = get_sequence().Advance();
      }
    }

    if (result) { // -- state transition for new step
      // Gate: held high only when THIS step extends into the next (slide out or tie).
      // Slide: stays high when arriving at a tie, or goes high when arriving at a slide, otherwise, cancel
      gate_hold = get_sequence().next_is_slide() || get_sequence().next_is_tie();
      slide_on = (slide_on && get_sequence().is_tie()) || get_sequence().get_slide();
    } else { // rest
      slide_on = false;
      gate_hold = false;
    }
    resting = !result;
    return result;
  }

  // returns true on step advance (clock divide by 6)
  bool Clock() {
    ++clk_count %= 6;

    if (clk_count == 0) { // sixteenth note advance
      Advance();
      return true;
    }

    return false;
  }

  void Reset() {
    get_sequence().Reset();
    clk_count = -1;
    gate_hold = false;
    slide_on = false;
    resting = true;
    p_chain_pos = 0;
    p_repeats = -1;
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
    if (resting) return false;
    if (get_time() == 3) { // ratchet overrides slide and tie stuff
      return !(clk_count & 1); // 3x
      //return !(clk_count == 2 || clk_count == 5); // 2x
    }
    if (gate_hold) return true; // tie/slide: hold through the 16th (cf. full clk_count span)
    // First 3 of 6 DIN clocks per 16th — matches reference OS-303. A ~1.3ms micros() window
    // here is easy to miss if loop() is slower than that (MIDI/LEDs/etc.), so non-slide
    // notes go silent while slide/tie still work (slide_gate holds high all 6 clocks).
    return clk_count < 3;
  }
  bool get_accent() const {
    if (resting) return false;
    return get_sequence().get_accent();
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
    return slide_on;
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
    p_chain_len = 0;
  }
  uint8_t AddToChain(uint8_t p_) {
    static uint8_t idx = 0;
    if (idx >= MAX_CHAIN) return 0;
    if (!p_chain_len) {
      idx = 0;
      p_chain_pos = 0;
      p_chain_len = 1;
      p_repeats = 0;
      p_chain[0] = p_ & 0x0f;
      return 0;
    }

    if ((p_chain[idx] & 0x0f) == p_ && (p_chain[idx] & 0xf0) != 0xf0) {
      p_chain[idx] += (1 << 4);
    } else {
      if (++idx < MAX_CHAIN) {
        p_chain[idx] = p_ & 0x0f;
        ++p_chain_len;
      }
    }

    return idx;
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
    get_sequence().SetOctave(int(get_sequence().get_octave()) + dir, clk_count > 3);
    stale = true;
  }
  // change pitch, preserving flags
  void SetPitchSemitone(uint8_t p) {
    get_sequence().SetPitchSemitone(p, clk_count > 3 && get_sequence().next_is_note());
    stale = true;
  }
  void SetPitch(uint8_t p, uint8_t flags) {
    get_sequence().SetPitch(p, flags, clk_count > 3 && get_sequence().next_is_note());
    stale = true;
  }
  void SetTime(uint8_t t) {
    get_sequence().SetTime(t, clk_count > 3);
    stale = true;
  }

  void ToggleSlide() {
    if (mode_ == PITCH_MODE) {
      get_sequence().ToggleSlide(clk_count > 3 && get_sequence().next_is_note());
      slide_on = get_sequence().get_slide();
    }
    stale = true;
  }
  void ToggleAccent() {
    if (mode_ == PITCH_MODE)
      get_sequence().ToggleAccent(clk_count > 3 && get_sequence().next_is_note());
    stale = true;
  }

  void ToggleTriplets() {
    // TODO: 8 clocks instead of 6, right?
  }

};
