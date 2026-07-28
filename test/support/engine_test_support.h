#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef DEBUG
#define DEBUG 0
#endif

#define constrain(value, lower, upper) \
  ((value) < (lower) ? (lower) : ((value) > (upper) ? (upper) : (value)))

inline long random(long upper) {
  return upper ? rand() % upper : 0;
}

class EEPROMClass {
 public:
  static constexpr size_t kSize = 4096;
  uint8_t data[kSize] = {};

  uint8_t read(size_t address) const { return data[address]; }
  void update(size_t address, uint8_t value) { data[address] = value; }

  template <typename T>
  T &get(size_t address, T &value) {
    memcpy(&value, data + address, sizeof(T));
    return value;
  }

  template <typename T>
  const T &put(size_t address, const T &value) {
    const auto *bytes = reinterpret_cast<const uint8_t *>(&value);
    for (size_t i = 0; i < sizeof(T); ++i) update(address + i, bytes[i]);
    return value;
  }
};

namespace Leds {
static uint8_t ledstate[4] = {};
inline void Send(bool = true) {}
}
