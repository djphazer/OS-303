// Copyright (c) 2026, Nicholas J. Michalek
//
// MIT License

#include <stdint.h>

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
  const bool rising_2bit() const { return (state & 0x3) == 0x1; }
  const bool rising() const { return (state & STATE_MASK) == STATE_RISING; }
  const bool falling() const { return (state & STATE_MASK) == STATE_FALLING; }
  const bool held() const { return (state & STATE_MASK) == STATE_ON; }
  const bool off() const { return (state & STATE_MASK) == 0; }
  const bool read() const { return state & 1; }
};

// TODO: complete these enums
enum EventType : uint8_t {
  EVENT_MENU,
  EVENT_BUTTON,
  EVENT_CLOCK,
  EVENT_SEQUENCER,
};
enum EventControl : uint8_t {

};
enum EventFlags : uint8_t {
  FLAG_CLOCKRUN = 0b00000001,
  FLAG_EXTERNAL = 0b10000000,
};

// -- standard events and queue
struct Event {
  uint8_t type, control, value, flags;
};
template <uint8_t Size>
struct EventQueue {
  Event dummy_ = Event{0,0,0,0};
  Event queue_[Size];
  uint8_t read_pos_ = 0, write_pos_ = 0;

  const bool available() const {
    return read_pos_ != write_pos_;
  }
  void push(Event &ev) {
    queue_[write_pos_] = ev;
    ++write_pos_ %= Size;
  }
  const Event &pop() {
    uint8_t idx = read_pos_;
    ++read_pos_ %= Size;
    return queue_[idx];
  }
};
