// Checked, restartable EEPROM restore state machine.
#pragma once

#include <stdint.h>
#include <string.h>

namespace EepromRestoreProtocol {

static constexpr uint16_t IMAGE_SIZE = 4096;
static constexpr uint8_t CHUNK_SIZE = 32;
static constexpr uint8_t CHUNK_COUNT = IMAGE_SIZE / CHUNK_SIZE;
static constexpr uint16_t MARKER_A_ADDR = IMAGE_SIZE - 2;
static constexpr uint16_t MARKER_B_ADDR = IMAGE_SIZE - 1;
static constexpr uint8_t MARKER_A = 0xA5;
static constexpr uint8_t MARKER_B = 0x5A;
static constexpr uint8_t MARKER_INACTIVE = 0xFF;
static constexpr uint8_t VERSION = 1;

enum Status : uint8_t {
  READY = 0x00,
  COMPLETE = 0x01,
  ERR_BEGIN = 0x40,
  ERR_DATA = 0x41,
  ERR_INCOMPLETE = 0x43,
  ERR_CHECKSUM = 0x44,
  ERR_SIGNATURE = 0x45,
};

inline uint16_t Crc16(const uint8_t *data, uint8_t len) {
  uint16_t crc = 0xFFFF;
  while (len--) {
    crc ^= uint16_t(*data++) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  return crc;
}

inline uint32_t Crc32Update(uint32_t crc, uint8_t value) {
  crc ^= value;
  for (uint8_t bit = 0; bit < 8; ++bit)
    crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320UL : 0);
  return crc;
}

template <typename Storage>
class Session {
 public:
  explicit Session(Storage &storage) : storage_(storage) { Reset(); }

  bool MarkerPresent() const {
    return storage_.read(MARKER_A_ADDR) == MARKER_A
        && storage_.read(MARKER_B_ADDR) == MARKER_B;
  }

  Status Begin(uint32_t expected_crc32) {
    Reset();
    expected_crc32_ = expected_crc32;

    // Write the complete marker before accepting data. If power fails between
    // these two writes, no pattern data has been changed yet.
    storage_.update(MARKER_B_ADDR, MARKER_B);
    storage_.update(MARKER_A_ADDR, MARKER_A);
    if (storage_.read(MARKER_A_ADDR) != MARKER_A
        || storage_.read(MARKER_B_ADDR) != MARKER_B)
      return Fail(ERR_CHECKSUM);

    begun_ = true;
    return READY;
  }

  Status AcceptChunk(uint16_t addr, const uint8_t *raw,
                     uint16_t supplied_crc) {
    if (error_ != READY)
      return error_;
    if (!begun_ || addr >= IMAGE_SIZE || (addr % CHUNK_SIZE) != 0)
      return Fail(ERR_DATA);
    if (Crc16(raw, CHUNK_SIZE) != supplied_crc)
      return Fail(ERR_CHECKSUM);

    const uint8_t chunk = addr / CHUNK_SIZE;
    const uint8_t bit = 1 << (chunk & 7);
    if (seen_[chunk >> 3] & bit)
      return READY;  // Canonical files contain no duplicates; replay is safe.

    if (addr == 0) {
      static const char signature[] = "OS-303-v0.8";
      if (memcmp(raw, signature, sizeof(signature) - 1) != 0)
        return Fail(ERR_SIGNATURE);
      memcpy(first_chunk_, raw, CHUNK_SIZE);
    } else {
      if (addr + CHUNK_SIZE == IMAGE_SIZE
          && (raw[CHUNK_SIZE - 2] != MARKER_INACTIVE
              || raw[CHUNK_SIZE - 1] != MARKER_INACTIVE))
        return Fail(ERR_DATA);

      for (uint8_t i = 0; i < CHUNK_SIZE; ++i) {
        const uint16_t write_addr = addr + i;
        if (write_addr != MARKER_A_ADDR && write_addr != MARKER_B_ADDR)
          storage_.update(write_addr, raw[i]);
      }
    }

    seen_[chunk >> 3] |= bit;
    ++received_;
    return READY;
  }

  Status Commit(uint32_t supplied_crc32) {
    if (error_ != READY)
      return error_;
    if (!begun_ || received_ != CHUNK_COUNT)
      return Fail(ERR_INCOMPLETE);
    if (supplied_crc32 != expected_crc32_
        || ImageCrc32(true) != expected_crc32_)
      return Fail(ERR_CHECKSUM);

    // Commit settings and the format signature only after the complete staged
    // image passes CRC-32, then verify those writes from EEPROM.
    for (uint8_t i = 0; i < CHUNK_SIZE; ++i)
      storage_.update(i, first_chunk_[i]);
    if (ImageCrc32(false) != expected_crc32_)
      return Fail(ERR_CHECKSUM);

    // Clear the recovery marker last, after the complete image is verified.
    // A power failure between these writes is safe: only reserved bytes differ.
    storage_.update(MARKER_B_ADDR, MARKER_INACTIVE);
    if (storage_.read(MARKER_B_ADDR) != MARKER_INACTIVE)
      return Fail(ERR_CHECKSUM);
    storage_.update(MARKER_A_ADDR, MARKER_INACTIVE);
    if (storage_.read(MARKER_A_ADDR) != MARKER_INACTIVE)
      return Fail(ERR_CHECKSUM);

    begun_ = false;
    finished_ = true;
    return COMPLETE;
  }

  bool begun() const { return begun_; }
  bool finished() const { return finished_; }
  uint8_t received() const { return received_; }
  Status error() const { return error_; }
  Status Reject(Status status) { return Fail(status); }

 private:
  void Reset() {
    begun_ = false;
    finished_ = false;
    error_ = READY;
    received_ = 0;
    expected_crc32_ = 0;
    memset(seen_, 0, sizeof(seen_));
    memset(first_chunk_, 0xFF, sizeof(first_chunk_));
  }

  Status Fail(Status status) {
    error_ = status;
    return status;
  }

  uint32_t ImageCrc32(bool use_pending_first_chunk) const {
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint16_t addr = 0; addr < IMAGE_SIZE; ++addr) {
      uint8_t value;
      if (addr < CHUNK_SIZE && use_pending_first_chunk)
        value = first_chunk_[addr];
      else if (addr == MARKER_A_ADDR)
        value = MARKER_INACTIVE;
      else if (addr == MARKER_B_ADDR)
        value = MARKER_INACTIVE;
      else
        value = storage_.read(addr);
      crc = Crc32Update(crc, value);
    }
    return ~crc;
  }

  Storage &storage_;
  bool begun_;
  bool finished_;
  Status error_;
  uint8_t received_;
  uint8_t seen_[CHUNK_COUNT / 8];
  uint8_t first_chunk_[CHUNK_SIZE];
  uint32_t expected_crc32_;
};

}  // namespace EepromRestoreProtocol
