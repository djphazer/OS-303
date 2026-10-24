#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/303/eeprom_restore.h"

using namespace EepromRestoreProtocol;

struct FakeStorage {
  uint8_t data[IMAGE_SIZE];
  int fail_addr;

  FakeStorage() : fail_addr(-1) { memset(data, 0xEE, sizeof(data)); }
  uint8_t read(uint16_t addr) const { return data[addr]; }
  void update(uint16_t addr, uint8_t value) {
    if (addr != fail_addr)
      data[addr] = value;
  }
};

static void MakeImage(uint8_t *image) {
  for (uint16_t i = 0; i < IMAGE_SIZE; ++i)
    image[i] = uint8_t(i * 37 + 11);
  memset(image, 0, 16);
  memcpy(image, "OS-303-v0.8", 11);
  image[MARKER_A_ADDR] = 0xFF;
  image[MARKER_B_ADDR] = 0xFF;
}

static uint32_t ImageCrc(const uint8_t *image) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (uint16_t i = 0; i < IMAGE_SIZE; ++i)
    crc = Crc32Update(crc, image[i]);
  return ~crc;
}

static Status SendChunk(Session<FakeStorage> &session, const uint8_t *image,
                        uint8_t chunk) {
  const uint16_t addr = uint16_t(chunk) * CHUNK_SIZE;
  return session.AcceptChunk(addr, image + addr, Crc16(image + addr, CHUNK_SIZE));
}

static void SendImage(Session<FakeStorage> &session, const uint8_t *image) {
  for (uint8_t chunk = 0; chunk < CHUNK_COUNT; ++chunk)
    assert(SendChunk(session, image, chunk) == READY);
}

static void TestSuccessfulRestore() {
  uint8_t image[IMAGE_SIZE];
  MakeImage(image);
  FakeStorage storage;
  Session<FakeStorage> session(storage);

  assert(session.Begin(ImageCrc(image)) == READY);
  assert(session.MarkerPresent());
  SendImage(session, image);
  assert(session.Commit(ImageCrc(image)) == COMPLETE);
  assert(session.finished());
  assert(!session.MarkerPresent());
  assert(memcmp(storage.data, image, IMAGE_SIZE) == 0);
}

static void TestSettingsAreCommittedLast() {
  uint8_t image[IMAGE_SIZE];
  MakeImage(image);
  FakeStorage storage;
  Session<FakeStorage> session(storage);

  assert(session.Begin(ImageCrc(image)) == READY);
  assert(SendChunk(session, image, 0) == READY);
  assert(memcmp(storage.data, image, CHUNK_SIZE) != 0);
  SendImage(session, image);  // Duplicate chunk zero is harmless.
  assert(session.Commit(ImageCrc(image)) == COMPLETE);
  assert(memcmp(storage.data, image, CHUNK_SIZE) == 0);
}

static void TestInterruptedRestoreCanReplay() {
  uint8_t image[IMAGE_SIZE];
  MakeImage(image);
  FakeStorage storage;
  Session<FakeStorage> interrupted(storage);

  assert(interrupted.Begin(ImageCrc(image)) == READY);
  for (uint8_t chunk = 0; chunk < 20; ++chunk)
    assert(SendChunk(interrupted, image, chunk) == READY);
  assert(interrupted.MarkerPresent());

  Session<FakeStorage> replay(storage);  // Simulate a power cycle.
  assert(replay.MarkerPresent());
  assert(replay.Begin(ImageCrc(image)) == READY);
  SendImage(replay, image);
  assert(replay.Commit(ImageCrc(image)) == COMPLETE);
  assert(memcmp(storage.data, image, IMAGE_SIZE) == 0);
}

static void TestErrorsLatchUntilRestart() {
  uint8_t image[IMAGE_SIZE];
  MakeImage(image);
  FakeStorage storage;
  Session<FakeStorage> session(storage);

  assert(session.Begin(ImageCrc(image)) == READY);
  assert(session.AcceptChunk(CHUNK_SIZE, image + CHUNK_SIZE, 0) == ERR_CHECKSUM);
  assert(SendChunk(session, image, 1) == ERR_CHECKSUM);
  assert(session.received() == 0);
  assert(session.MarkerPresent());

  assert(session.Begin(ImageCrc(image)) == READY);
  assert(SendChunk(session, image, 0) == READY);
  assert(session.Commit(ImageCrc(image)) == ERR_INCOMPLETE);
}

static void TestRejectsWrongSignatureAndMarkerPayload() {
  uint8_t image[IMAGE_SIZE];
  MakeImage(image);
  FakeStorage storage;
  Session<FakeStorage> session(storage);

  image[0] = 'X';
  assert(session.Begin(ImageCrc(image)) == READY);
  assert(SendChunk(session, image, 0) == ERR_SIGNATURE);

  MakeImage(image);
  image[MARKER_A_ADDR] = MARKER_A;
  assert(session.Begin(ImageCrc(image)) == READY);
  assert(SendChunk(session, image, CHUNK_COUNT - 1) == ERR_DATA);
}

static void TestDetectsEepromWriteFailures() {
  uint8_t image[IMAGE_SIZE];
  MakeImage(image);

  FakeStorage marker_failure;
  marker_failure.fail_addr = MARKER_A_ADDR;
  Session<FakeStorage> cannot_arm(marker_failure);
  assert(cannot_arm.Begin(ImageCrc(image)) == ERR_CHECKSUM);
  assert(!cannot_arm.begun());

  FakeStorage header_failure;
  Session<FakeStorage> cannot_commit(header_failure);
  assert(cannot_commit.Begin(ImageCrc(image)) == READY);
  SendImage(cannot_commit, image);
  header_failure.fail_addr = 0;
  assert(cannot_commit.Commit(ImageCrc(image)) == ERR_CHECKSUM);
  assert(cannot_commit.MarkerPresent());
}

int main() {
  assert(Crc16(reinterpret_cast<const uint8_t *>("123456789"), 9) == 0x29B1);
  TestSuccessfulRestore();
  TestSettingsAreCommittedLast();
  TestInterruptedRestoreCanReplay();
  TestErrorsLatchUntilRestart();
  TestRejectsWrongSignatureAndMarkerPayload();
  TestDetectsEepromWriteFailures();
  puts("EEPROM restore tests passed");
  return 0;
}
