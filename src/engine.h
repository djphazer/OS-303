/* 
 * Sequencer engine logic for 303
 *   by djphazer (Nicholas J. Michalek)
 */

#include <Arduino.h>
#include <EEPROM.h>

// TODO: set up EEPROM storage here

static constexpr int MAX_STEPS = 32;

enum SequencerMode {
  NORMAL_MODE,
  PITCH_MODE,
  TIME_MODE,
};

struct Sequence {
  uint8_t pitch[MAX_STEPS]; // 6-bit Pitch, Accent, and Slide
  uint8_t time[MAX_STEPS];  // 0=rest, 1=note, 2=tie, 3=triplets?
                            // TODO: octave up/down flags?

  uint8_t dummy = 0;
  uint8_t length = 16;
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
    return time_pos < length && time[time_pos+1] == 2;
  }

  void SetTime(uint8_t t) {
    time[time_pos] = t;
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
    time[time_pos] = random();
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
    ++time_pos;
    time_pos %= length;
    if (time_pos == 0)
      pitch_pos = 0;
    else if (time[time_pos] == 1)
      ++pitch_pos;
    return time[time_pos];
  }
};

struct Engine {
  elapsedMillis delay_timer = 0;

  // pattern storage
  Sequence pattern[16]; // 32 steps each
  uint8_t p_select = 0;

  uint8_t mode_ = NORMAL_MODE;
  //uint8_t chains[16][7]; // 7 tracks, up to 16 chained patterns

  uint8_t clk_count = 0;

  uint8_t cv_out_ = 0; // semitone
  int8_t octave_ = 0;

  bool slide_on = false; // flag to keep raised
  bool gate_on = false;

  // actions
  void Tick(uint8_t &state) {
    if (gate_on != get_gate()) {
      // rising or falling
    }
    gate_on = get_gate();
  }

  bool Advance() {
    return pattern[p_select].Advance();
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
  uint8_t get_mode() const { return mode_; }

  bool get_gate() const {
    return delay_timer > 0 && (clk_count < 4 || slide_on);
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
  void SetPattern(uint8_t p_) {
    p_select = constrain(p_, 0, 15);
  }

  // setters
  void SetMode(SequencerMode m) {
    mode_ = m;
  }
  void NudgeOctave(int dir) {
    octave_ = constrain(octave_ + dir, -2, 2);
  }
  void SetPitch(uint8_t p, uint8_t flags) {
    pattern[p_select].SetPitch((24 + p + (octave_*12)) | flags);
  }
  void SetTime(uint8_t t) {
    pattern[p_select].SetTime(t);
  }

  void ToggleSlide() {
    if (mode_ == PITCH_MODE)
      pattern[p_select].ToggleSlide();
  }
  void ToggleAccent() {
    if (mode_ == PITCH_MODE)
      pattern[p_select].ToggleAccent();
  }

};
