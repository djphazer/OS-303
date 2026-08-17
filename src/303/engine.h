// Copyright (c) 2026, Nicholas J. Michalek
/*
 * engine.h — TB-303 pattern model + EEPROM; Engine handles patterns, clock, and gate.
 */

#pragma once
#include <Arduino.h>
#include <EEPROM.h>
#include "drivers.h"
#include "../clock.h"

static constexpr int MAX_STEPS = 32;
static constexpr int MAX_CHAIN = 16;
static constexpr int NUM_PATTERNS = 16; // per bank; 4 banks in eeprom

enum OctaveState : uint8_t {
  OCTAVE_DOWN = 0x0,
  OCTAVE_ZERO = 0x1,
  OCTAVE_UP   = 0x2,
  OCTAVE_DOUBLE_UP = 0x3,
};

static constexpr uint8_t PITCH_EMPTY = 0xFF; // unwritten step sentinel
static constexpr uint8_t PITCH_DEFAULT = (OCTAVE_ZERO << 4); // clean default: C, octave zero, no flags

static constexpr size_t PITCH_SIZE = MAX_STEPS;
static constexpr size_t TIME_SIZE = MAX_STEPS / 4;
static constexpr size_t METADATA_SIZE = 8;

// --- EEPROM data layout ( 4K total ) ---
static constexpr size_t SETTINGS_SIZE = 128; // 16-byte sig + flags + maybe quantizer scales?
static constexpr size_t SETTINGS_OFFSET = 0;
static constexpr size_t PITCH_DATA_SIZE = 2048; // 8-bit pitch steps
static constexpr size_t PITCH_DATA_OFFSET = SETTINGS_OFFSET + SETTINGS_SIZE;
static constexpr size_t TIME_DATA_SIZE = 512; // 2-bit time steps
static constexpr size_t TIME_DATA_OFFSET = PITCH_DATA_OFFSET + PITCH_DATA_SIZE;
static constexpr size_t PATTERN_DATA_SIZE = 512; // 64 total patterns * 8 bytes
static constexpr size_t PATTERN_DATA_OFFSET = TIME_DATA_OFFSET + TIME_DATA_SIZE;
static constexpr size_t TRACK_DATA_SIZE = 8 * MAX_CHAIN * 2; // 7 tracks accessible... ;)
static constexpr size_t TRACK_DATA_OFFSET = PATTERN_DATA_OFFSET + PATTERN_DATA_SIZE;

// and this is what's left...
static constexpr size_t AUX_DATA_OFFSET = TRACK_DATA_OFFSET + TRACK_DATA_SIZE;
static constexpr int AUX_DATA_SIZE = 4096 - AUX_DATA_OFFSET;
static_assert(AUX_DATA_SIZE >= 0, "EEPROM OVERFLOW!");

// Signature - max 15 characters (+ null terminator)
// Shorter strings will match longer ones as a prefix, so appending characters
// retains compatibility with upstream and old versions, etc.
const char* const sig_pew = "OS-303-v0.8";

static constexpr uint8_t unpack_pitch(uint8_t p) {
  return (p & 0x0f) + 12*((p >> 4) & 0x3);
}

struct Sequence {
  //Sequence(uint8_t *p, uint8_t *t) : pitch(p), time_data(t) {}

  // --- sequence data = 32 + 16 + 8 = 56 bytes
  uint8_t pitch[PITCH_SIZE]; // 4-bit Pitch, 2-bit Octave, Accent, and Slide
  uint8_t time_data[TIME_SIZE]; // 0=rest, 1=note, 2=tie, 3=ratchet
  // time is stored as 2-bit cells, 4 steps per byte

  // this also doubles as the entry point for the metadata
  uint8_t reserved[METADATA_SIZE - 3];
  uint8_t transpose = 0;
  uint8_t engine_select = 0;
  uint8_t length = 16;
  // --- end sequence data

  // state
  int pitch_pos, time_pos;
  bool reset;
  bool reset_pitch;

  Sequence& operator=(const Sequence& src) {
    memcpy(pitch, src.pitch, sizeof(pitch) + sizeof(time_data) + METADATA_SIZE);
    return *this;
  }

  // 6-bit pitch, 0 == low C
  const uint8_t get_pitch(uint8_t pos) const {
    return get_octave(pos) * 12 + get_semitone(pos);
  }
  const uint8_t get_pitch() const {
    if (step_is_empty()) return PITCH_DEFAULT; // silent default, gate will be off
    return get_pitch(pitch_pos);
  }
  const uint8_t get_octave(uint8_t pos) const {
    return (pitch[pos] >> 4) & 0x03;
  }
  const uint8_t get_octave() const {
    if (step_is_empty()) return OCTAVE_ZERO;
    return get_octave(pitch_pos);
  }
  const uint8_t get_semitone(uint8_t pos) const {
    return pitch[pos] & 0x0f;
  }
  const uint8_t get_semitone() const {
    if (step_is_empty()) return PITCH_EMPTY;
    return get_semitone(pitch_pos);
  }
  const bool get_accent() const {
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
  const bool next_is_slide(int8_t direction = 1) const {
    return next_is_note() && get_slide((pitch_pos + length + direction) % length);
  }
  /// Current time step is a tie.
  bool is_tie() const {
    return (time_pos < length) && (time(time_pos) == 2);
  }
  /// Next time step (wrapped) is a tie.
  bool next_is_tie(int8_t direction = 1) const {
    const uint8_t n = (time_pos + length + direction) % length;
    return time(n) == 2;
  }
  bool next_is_note(int8_t direction = 1) const {
    const uint8_t n = (time_pos + length + direction) % length;
    return time(n) & 1;
  }
  /// Last tie in a run: on a tie step whose next step is not a tie.
  bool tie_chain_ending() const {
    return is_tie() && !next_is_tie();
  }

  inline uint8_t time(uint8_t idx) const {
    return (time_data[idx >> 2] >> (2 * (idx & 0x3))) & 0x3;
  }

  const uint8_t get_time() const { return time(time_pos); }

  void SetTime(uint8_t t, bool next = 0) {
    const uint8_t pos = (time_pos + next) % length;
    const uint8_t upper = pos & 0x3;
    uint8_t &data = time_data[pos >> 2];
    data = (~(0x03 << (2 * upper)) & data) | ((t & 0x3) << (2 * upper));
  }
  void SetPitch(uint8_t p, uint8_t flags, bool next = 0) {
    const uint8_t pos = (pitch_pos + next) % length;
    pitch[pos] = (p & 0x3f) | (flags & 0xc0);
  }
  void SetPitchSemitone(uint8_t p, bool next = 0) {
    init_if_empty(); 
    const uint8_t pos = (pitch_pos + next) % length;
    pitch[pos] = (p & 0x0f) | (pitch[pos] & 0xf0);
  }
  void SetLength(uint8_t len) {
    length = constrain(len, 1, MAX_STEPS);
    pitch_pos %= length;
    time_pos %= length;
  }
  void NudgeOctave(int dir, bool next = 0) {
    const uint8_t pos = (pitch_pos + next) % length;
    int oct = get_octave(pos) + dir;
    if (oct < 0) oct = 1;
    if (oct > 3) oct = 2;
    pitch[pos] = ((uint8_t)oct << 4) | (pitch[pos] & 0xcf);
  }
  void SetOctave(int oct, bool next = 0) {
    init_if_empty();
    const uint8_t pos = (pitch_pos + next) % length;
    if (oct < 0) oct = 1;
    if (oct > 3) oct = 2;
    pitch[pos] = ((uint8_t)oct << 4) | (pitch[pos] & 0xcf);
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

  void RegenTimes() {
    for (auto &t : time_data)
      t = random(0xff);
  }
  void RegenPitches() {
    for (auto &p : pitch)
      p = random(0xff);
  }

  void Reset() {
    pitch_pos = 0;
    time_pos = 0;
    reset = true;
    reset_pitch = true;
  }

  bool pitch_is_empty(uint8_t pos) const { return pitch[pos] == PITCH_EMPTY; }
  bool step_is_empty() const { return pitch_is_empty(pitch_pos); }

  void init_if_empty() {
    if (step_is_empty()) pitch[pitch_pos] = PITCH_DEFAULT;
  }

  void Clear() {
    for (uint8_t i = 0; i < MAX_STEPS; ++i) {
      pitch[i] = PITCH_DEFAULT;
      time_data[i>>2] = 0; // all rests
    }
    length = 8;
  }

  // returns false for rests
  bool Advance(int8_t direction = 1) {
    if (reset) {
      reset = false;
      pitch_pos = 0;
      reset_pitch = true;
    } else {
      time_pos = (time_pos + length + direction) % length;
    }

    if (time_pos == 0) {
      pitch_pos = 0;
      reset_pitch = true;
    }
    if (time(time_pos) & 1)
      AdvancePitch(direction);

    return time(time_pos);
  }

  // used in write mode
  void AdvancePitch(int8_t direction = 1) {
    if (reset_pitch)
      reset_pitch = false;
    else
      pitch_pos = (pitch_pos + length + direction) % length;
  }
};

extern EEPROMClass storage;
extern Sequence pattern[NUM_PATTERNS]; // enough to hold one bank in RAM

// bitmask
enum SettingsFlags : uint8_t {
  SETTING_MIDI_RX       = (1 << 0),
  SETTING_MIDI_CLOCK_RX = (1 << 1),
  SETTING_MIDI_TX       = (1 << 2),
  SETTING_MIDI_CLOCK_TX = (1 << 3),
  SETTING_MIDI_PC_RX    = (1 << 4),
  SETTING_MIDI_PC_TX    = (1 << 5),
  SETTING_RESERVED0     = (1 << 6),
  SETTING_PATTERN_SYNC  = (1 << 7),
};

// SETTINGS_SIZE
struct PersistentSettings {
  char signature[16];
  uint8_t flags;
  uint16_t qmask;
  // TODO: settings? calibration? plenty of room here... like 111 bytes

  void Load() {
    size_t pos = 0;

    storage.get(pos, signature);
    pos += sizeof(signature);

    flags = storage.read(pos++);

    storage.get(pos, qmask);
    pos += sizeof(qmask);
  }
  void Save() {
    size_t pos = 0;

    storage.put(pos, signature);
    pos += sizeof(signature);

    storage.update(pos++, flags);

    storage.put(pos, qmask);
    pos += sizeof(qmask);
  }
  bool Validate() {
    int sigdiff = strncmp(signature, sig_pew, strlen(sig_pew));

    // exact match
    if (0 == sigdiff) return true;

    // older version or shorter signature string
    if (sigdiff < 0 && signature[10] == '6') {
      // -= Upgrade Migration! =-

      // turn some lights on, this might take a while
      Leds::ledstate[0] = 0xff;
      Leds::ledstate[1] = 0xff;
      Leds::ledstate[2] = 0xff;
      Leds::Send(false); // only sends 1/4 of the matrix

      // OS-303-v0.6 -> OS-303-v0.8
      // Time Data gets packed tighter now: 1024 -> 512 bytes
      // Everything else gets shifted up by 512 bytes.
      const size_t chunksize = 32;
      size_t writepos = TIME_DATA_OFFSET;
      size_t readpos = TIME_DATA_OFFSET;
      uint8_t buf[chunksize];
      uint8_t packed[chunksize/2];

      while (readpos < (TIME_DATA_OFFSET + TIME_DATA_SIZE * 2)) {
        // get a chunk of the old data
        storage.get(readpos, buf);

        // repack the bytes
        for (uint8_t pi = 0; pi < chunksize / 2; ++pi) {
          packed[pi] = (buf[pi * 2] & 0x3)
                     | ((buf[pi * 2] >> 2) & 0xc)
                     | ((buf[pi * 2 + 1] << 4) & 0x30)
                     | ((buf[pi * 2 + 1] << 2) & 0xc0);
        }

        // write the new chunk and step forward
        storage.put(writepos, packed);
        writepos += chunksize / 2;
        readpos += chunksize;

        Leds::Send(false);
      }
      // shift all remaining bytes of EEPROM
      while (readpos < 4096) {
        storage.get(readpos, buf);
        storage.put(writepos, buf);
        readpos += chunksize;
        writepos += chunksize;

        Leds::Send(false);
      }

      // set new signature and save
      strcpy((char*)signature, sig_pew);
      Save();
      return true; // all good!
    }

    Clear();
    return false;
  }
  // clear and reset to defaults
  void Clear() {
    strcpy((char*)signature, sig_pew);
    flags = SETTING_MIDI_RX | SETTING_MIDI_CLOCK_RX | SETTING_MIDI_CLOCK_TX;
  }
  const bool Get(SettingsFlags opt) const {
    return flags & opt;
  }
  void Toggle(SettingsFlags opt) {
    flags ^= opt;
  }
};

extern PersistentSettings GlobalSettings;

inline void WritePattern(Sequence &seq, int idx) {
  storage.put(PITCH_DATA_OFFSET + (idx * PITCH_SIZE), seq.pitch);
  storage.put(TIME_DATA_OFFSET + (idx * TIME_SIZE), seq.time_data);

  // metadata is spread over a few different variables - handled here as simple bytes
  uint8_t *src = seq.reserved;
  for (uint8_t i = 0; i < METADATA_SIZE; ++i) {
    storage.update(PATTERN_DATA_OFFSET + (idx * METADATA_SIZE) + i, src[i]);
  }
}
inline void ReadPattern(Sequence &seq, int idx) {
  storage.get(PITCH_DATA_OFFSET + (idx * PITCH_SIZE), seq.pitch);
  storage.get(TIME_DATA_OFFSET + (idx * TIME_SIZE), seq.time_data);

  uint8_t *dst = seq.reserved;
  for (uint8_t i = 0; i < METADATA_SIZE; ++i) {
    dst[i] = storage.read(PATTERN_DATA_OFFSET + (idx * METADATA_SIZE) + i);
  }
}

enum PlaybackDirection : int8_t {
  PLAY_BACKWARD = -1,
  PLAY_STOPPED = 0,
  PLAY_FORWARD = 1
};

struct Engine {
  // pattern-chains, aka TRACKS
  uint8_t p_chain[MAX_CHAIN]; // 4-bit p_select | 4-bit repeats
  uint8_t t_chain[MAX_CHAIN]; // 6-bit transpose progression
  uint8_t p_chain_len = 0;
  uint8_t p_chain_pos = 0;
  int8_t p_repeats = -1;
  uint8_t t_chain_pos = 0;
  bool t_chain_active = false;

  uint8_t p_select = 0;
  uint8_t next_p = 0; // queued pattern

  int8_t clk_count = -1; // TODO: maybe get rid of this in favor of ClockEngine
  int8_t play_dir = PLAY_FORWARD;

  bool gate_hold = false; // tie/slide: hold gate across 16ths (firstpr.com 303 slide / gate)
  bool slide_on = false;
  bool resting = false; // hey shutup
  bool track_mode_ = false;

  Sequence* copy_src = nullptr;

  uint16_t qmask = 0x0fff; // quantizer scale mask

  ClockEngine &clock_;

  Engine(ClockEngine &cl) : clock_(cl) {}

  inline bool Init(const bool reset_memory) {
#if DEBUG
    Serial.println("Loading from EEPROM...");
#endif

    GlobalSettings.Load();
    if (GlobalSettings.Validate() && !reset_memory) {
      Load(0);
      return true;
    }

#if DEBUG
    Serial.println("EEPROM data invalid, initializing...");
#endif
    // TODO: migration from old signatures could happen here instead

    // shits getting serious, we need to level this place and start fresh
    Leds::ledstate[0] = 0xff;
    Leds::ledstate[1] = 0xff;
    Leds::ledstate[2] = 0xff;
    Leds::Send(false); // only sends 1/4 of the matrix

    // --- initialize RAM with defaults or zeroes
    GlobalSettings.Clear();
    for (uint8_t i = 0; i < NUM_PATTERNS; ++i) {
      ClearPattern(i, i == 0); // demo in pattern 1
    }
    ClearChain();

    // store cleared RAM to EEPROM in every hole, i mean all 7 tracks
    // this might take a while, so the LED matrix will reflect progress...
    for (uint8_t i = 1; i < 7; ++i) {
      Save(i);
      if (i == 1) ClearPattern(0); // no demo after Track 2

      Leds::Send(false);
    }

    GlobalSettings.Save(); // update signature after migrations

    return false;
  }

  // actions
  inline void Load(uint8_t track) {
    const uint8_t bank = track >> 1;
    for (uint8_t i = 0; i < NUM_PATTERNS; ++i) {
      ReadPattern(pattern[i], i + bank * NUM_PATTERNS);
      if (0 == pattern[i].length || 0xff == pattern[i].length)
        pattern[i].Clear();
    }

    p_chain_len = 0;
    storage.get(TRACK_DATA_OFFSET + (track * MAX_CHAIN * 2), p_chain);
    storage.get(TRACK_DATA_OFFSET + (track * MAX_CHAIN * 2) + MAX_CHAIN, t_chain);

    if (0xff == t_chain[0]) {
      // invalid / uninitialized data
      memset(p_chain, 0, sizeof(p_chain));
      memset(t_chain, 0, sizeof(t_chain));
      return;
    }

    // length is simply the first t_chain step with the top bit set
    for (uint8_t i = 0; i < MAX_CHAIN; ++i) {
      if (t_chain[i] & 0x80) {
        p_chain_len = i + 1;
        break;
      }
    }

#if DEBUG
    Serial.println("First pattern:");
    for (uint8_t i = 0; i < 64; ++i) {
      Serial.printf("%2x ", pattern[0].pitch[i]);
    }
    Serial.print("\n");
#endif
  }
  inline void Save(uint8_t track) {
    const uint8_t bank = track >> 1;
#if DEBUG
    Serial.print("Saving to EEPROM... ");
#endif
    // save all
    for (uint8_t i = 0; i < NUM_PATTERNS; ++i) {
      WritePattern(pattern[i], i + bank * NUM_PATTERNS);
    }

    for (uint8_t i = 0; i < MAX_CHAIN; ++i) {
      if (p_chain_len == i + 1) {
        t_chain[i] |= 0x80;
        break;
      } else {
        t_chain[i] &= 0x7f;
      }
    }
    storage.put(TRACK_DATA_OFFSET + (track * MAX_CHAIN * 2), p_chain);
    storage.put(TRACK_DATA_OFFSET + (track * MAX_CHAIN * 2) + MAX_CHAIN, t_chain);

#if DEBUG
    Serial.println("DONE!");
#endif
  }

  void Tick() { }

  bool Advance(int direction = 1) {
    // start sliding before advancing
    // Slide: goes high when leaving a slide, stays high when arriving at a tie, otherwise, cancel
    slide_on = get_sequence().get_slide() || (slide_on && get_sequence().next_is_tie(direction));

    bool result = get_sequence().Advance(direction);
    // jump to next pattern at end of current one
    if (0 == get_sequence().time_pos) {
      if (track_mode_ && p_chain_len) {
        if (++p_repeats > (p_chain[p_chain_pos] >> 4)) {
          ++p_chain_pos %= p_chain_len;
          p_repeats = 0;
        }
        next_p = p_chain[p_chain_pos] & 0x0f;
      }

      if (next_p != p_select) {
        p_select = next_p;
        get_sequence().Reset();
        result = get_sequence().Advance(direction);
      }
    }

    // ignore Tie after a Rest
    if (resting && result && get_sequence().is_tie()) result = false;

    if (result) { // -- state transition for new step
      // Gate: held high only when THIS step extends into the next (slide out or tie).
      gate_hold = get_sequence().get_slide() || get_sequence().next_is_tie(direction);
    } else { // rest
      slide_on = false;
      gate_hold = false;
    }
    resting = !result;
    return result;
  }

  // only for editing pitch steps
  void AdvancePitch(int direction = 1) {
    slide_on = get_sequence().get_slide();
    get_sequence().AdvancePitch(direction);
    resting = false;
  }

  void trig_check(const bool rising) {
    clock_.tick(rising);
    if (clock_.trig_pop()) {
      Advance();
    }
  }

  // returns true on 1/16th notes (clock divide by 6)
  bool Clock(const bool &track_mode) {
    track_mode_ = track_mode;
    trig_check(true);
    ++clk_count %= 6;
    return (clk_count == 0);
  }

  void Reset() {
    get_sequence().Reset();
    clock_.reset();
    clk_count = -1;
    gate_hold = false;
    slide_on = false;
    resting = true;
    p_chain_pos = 0;
    p_repeats = -1;
  }

  void Generate(bool pitch, bool time) {
    if (pitch)
      get_sequence().RegenPitches();
    if (time)
      get_sequence().RegenTimes();
  }

  void Copy() {
    copy_src = &get_sequence();
  }
  void Paste() {
    if (copy_src && (copy_src != &get_sequence())) {
      get_sequence() = *copy_src;
    }
  }

  void ClearPattern(uint8_t idx, bool demo = false) {
    pattern[idx].Clear();

    if (demo) {
      // A DEMO PATTERN for testing
      // -------------------------
      pattern[idx].SetLength(8);
      pattern[idx].time_data[0] = 0x55; // 4 notes
      pattern[idx].time_data[1] = 0x55; // 4 notes
      pattern[idx].pitch[4] = 0x07; // G, oct down
      pattern[idx].pitch[5] = 0x07; // G, oct down
      pattern[idx].pitch[6] = 0x0a; // A#, oct down
      pattern[idx].pitch[7] = 0x0a; // A#, oct down
      // -------------------------
    }

    if (idx == p_select) {
      Reset();
    }
  }

  void ClearAccents() {
    for (uint8_t &p : get_sequence().pitch) p &= ~(1 << 6);
  }
  void ClearSlides() {
    for (uint8_t &p : get_sequence().pitch) p &= ~(1 << 7);
  }
  void ClearRatchets() {
    // all Ratchets become regular Notes
    for (uint8_t &t : get_sequence().time_data) {
      for (uint8_t i = 0; i < 4; ++i) {
        if ((t >> (2 * i) & 0x3) == 0x3)
          t ^= (0x2 << (2 * i));
      }
    }
  }
  void ClearTies() {
    // all Ties become Rests
    for (uint8_t &t : get_sequence().time_data) {
      for (uint8_t i = 0; i < 4; ++i) {
        if ((t >> (2 * i) & 0x3) == 0x2)
          t ^= (0x2 << (2 * i));
      }
    }
  }
  void ClearRests() {
    // all Rests become Notes
    for (uint8_t &t : get_sequence().time_data) {
      for (uint8_t i = 0; i < 4; ++i) {
        if ((t >> (2 * i) & 0x3) == 0)
          t |= (0x1 << (2 * i));
      }
    }
  }
  void ClearNotes() {
    // reset to all Rests
    for (uint8_t &t : get_sequence().time_data) {
      t = 0;
    }
  }

  void ToggleMaskBit(uint8_t ix) {
    GlobalSettings.qmask ^= (1ul << ix);
    if (!GlobalSettings.qmask) GlobalSettings.qmask = 1;
  }
  void RotateMask(bool right) {
    if (right) {
      GlobalSettings.qmask = ((GlobalSettings.qmask >> 1) | (GlobalSettings.qmask << 11)) & 0x0fff;
    } else {
      GlobalSettings.qmask = ((GlobalSettings.qmask << 1) | (GlobalSettings.qmask >> 11)) & 0x0fff;
    }
  }

  // getters
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
    return clock_.check_swung_gate(4);
  }
  bool get_accent() const {
    if (resting) return false;
    return get_sequence().get_accent();
  }
  uint8_t get_octave() const {
    return get_sequence().get_octave();
  }
  uint8_t get_semitone() const {
    return get_sequence().get_semitone();
  }
  uint8_t get_pitch() const {
    uint8_t p = get_sequence().get_pitch();
    uint8_t pp = p;
    if (GlobalSettings.qmask) { // eliminates the 0 case
      // check current pitch against quantizer mask
      while (p) {
        if (GlobalSettings.qmask & (1ul << (p % 12))) return p;
        if (GlobalSettings.qmask & (1ul << (pp % 12))) return pp;
        --p; // round down
        ++pp; // round up
      }
    }
    return p;
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
  bool get_slide_dac() const {
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
  const uint16_t get_qmask() const {
    return GlobalSettings.qmask;
  }

  // setters
  void SetPattern(uint8_t p_, bool override = false) {
    next_p = p_ & 0xf; // p_ % 16;
    if (override) p_select = next_p;
  }
  uint8_t AddToChain(int8_t p_) {
    static uint8_t edit_idx = 0;
    if (p_ < 0) {
      // delete one
      if (p_chain_len) {
        edit_idx = p_chain_len - 1;
        if (p_chain[edit_idx] >> 4)
          p_chain[edit_idx] -= (1 << 4);
        else {
          --p_chain_len;
          if (edit_idx) --edit_idx;
        }
      }
      return edit_idx;
    }
    if (edit_idx >= MAX_CHAIN) return MAX_CHAIN - 1;
    if (!p_chain_len) {
      edit_idx = 0;
      p_chain_pos = 0;
      p_chain_len = 1;
      p_repeats = 0;
      p_chain[0] = p_ & 0x0f;
      return 0;
    }

    edit_idx = p_chain_len - 1;
    if ((p_chain[edit_idx] & 0x0f) == p_ && (p_chain[edit_idx] & 0xf0) != 0xf0) {
      // increment repeat count for same pitch
      p_chain[edit_idx] += (1 << 4);
    } else if (++edit_idx < MAX_CHAIN) {
      // set up new step
      p_chain[edit_idx] = p_ & 0x0f;
      ++p_chain_len;
    }

    return edit_idx;
  }
  void ClearChain() {
    p_chain_len = 0;
    memset(p_chain, 0, MAX_CHAIN);
    memset(t_chain, 0, MAX_CHAIN);
  }
  void SetLength(uint8_t len) {
    get_sequence().SetLength(len);
  }
  bool BumpLength() {
    return get_sequence().BumpLength();
  }
  void NudgeOctave(int dir) {
    get_sequence().NudgeOctave(dir, clk_count > 3);
  }
  // change pitch, preserving flags
  void SetPitchSemitone(uint8_t p) {
    get_sequence().SetPitchSemitone(p, clk_count > 3 && get_sequence().next_is_note());
  }
  // p is expected to be already packed as 2-bit octave | 4-bit semitone
  void SetPitch(uint8_t p, uint8_t flags) {
    get_sequence().SetPitch(p, flags, clk_count > 3 && get_sequence().next_is_note());
  }
  void SetTime(uint8_t t) {
    get_sequence().SetTime(t, clk_count > 3);
  }

  void ToggleSlide() {
    get_sequence().ToggleSlide(clk_count > 3 && get_sequence().next_is_note());
    slide_on = get_sequence().get_slide();
  }
  void ToggleAccent() {
    get_sequence().ToggleAccent(clk_count > 3 && get_sequence().next_is_note());
  }

  void ToggleTriplets() {
    // TODO: 8 clocks instead of 6, right?
  }

};
