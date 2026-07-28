#include <unity.h>

#include "engine.h"

EEPROMClass storage;
Sequence pattern[NUM_PATTERNS];
PersistentSettings GlobalSettings;

static Engine engine;

void setUp() {
  memset(&storage, 0, sizeof(storage));
  for (auto &sequence : pattern) sequence.Clear();
  engine = Engine{};
}

void tearDown() {}

void test_random_generation_marks_patterns_stale() {
  engine.Generate(true, false);
  TEST_ASSERT_TRUE(engine.stale);

  engine.stale = false;
  engine.Generate(false, true);
  TEST_ASSERT_TRUE(engine.stale);
}

void test_bulk_pattern_edits_mark_patterns_stale() {
  engine.ClearAccents();
  TEST_ASSERT_TRUE(engine.stale);

  engine.stale = false;
  engine.ClearSlides();
  TEST_ASSERT_TRUE(engine.stale);

  engine.stale = false;
  engine.ClearRatchets();
  TEST_ASSERT_TRUE(engine.stale);

  engine.stale = false;
  engine.ClearTies();
  TEST_ASSERT_TRUE(engine.stale);

  engine.stale = false;
  engine.ClearRests();
  TEST_ASSERT_TRUE(engine.stale);

  engine.stale = false;
  engine.ClearNotes();
  TEST_ASSERT_TRUE(engine.stale);
}

void test_clear_chain_marks_track_stale() {
  engine.stale = false;
  engine.ClearChain();
  TEST_ASSERT_TRUE(engine.stale);
}

void test_save_persists_changed_pattern_and_clears_stale() {
  constexpr uint8_t expected_pitch = 0x2a;
  pattern[0].pitch[0] = expected_pitch;
  engine.stale = true;

  TEST_ASSERT_TRUE(engine.Save(0));

  TEST_ASSERT_EQUAL_HEX8(expected_pitch, storage.read(PITCH_DATA_OFFSET));
  TEST_ASSERT_FALSE(engine.stale);
}

void test_save_skips_clean_patterns() {
  storage.update(PITCH_DATA_OFFSET, 0x55);
  pattern[0].pitch[0] = 0x2a;
  engine.stale = false;

  TEST_ASSERT_FALSE(engine.Save(0));

  TEST_ASSERT_EQUAL_HEX8(0x55, storage.read(PITCH_DATA_OFFSET));
}

void test_force_save_persists_even_when_stale_was_not_set() {
  storage.update(PITCH_DATA_OFFSET, 0x55);
  pattern[0].pitch[0] = 0x2a;
  engine.stale = false;

  TEST_ASSERT_TRUE(engine.Save(0, true));

  TEST_ASSERT_EQUAL_HEX8(0x2a, storage.read(PITCH_DATA_OFFSET));
  TEST_ASSERT_FALSE(engine.stale);
}

void test_clear_generate_bulk_edit_save_and_reload_workflow() {
  constexpr uint8_t pattern_index = 4;
  engine.ClearPattern(pattern_index);
  engine.SetPattern(pattern_index, true);
  engine.Generate(true, true);
  engine.ClearRatchets();

  uint8_t expected_pitch[PITCH_SIZE];
  uint8_t expected_time[TIME_SIZE];
  memcpy(expected_pitch, pattern[pattern_index].pitch, sizeof(expected_pitch));
  memcpy(expected_time, pattern[pattern_index].time_data, sizeof(expected_time));

  TEST_ASSERT_TRUE(engine.Save(0));
  pattern[pattern_index].Clear();
  engine.Load(0);

  TEST_ASSERT_EQUAL_UINT8_ARRAY(
    expected_pitch, pattern[pattern_index].pitch, PITCH_SIZE);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(
    expected_time, pattern[pattern_index].time_data, TIME_SIZE);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_random_generation_marks_patterns_stale);
  RUN_TEST(test_bulk_pattern_edits_mark_patterns_stale);
  RUN_TEST(test_clear_chain_marks_track_stale);
  RUN_TEST(test_save_persists_changed_pattern_and_clears_stale);
  RUN_TEST(test_save_skips_clean_patterns);
  RUN_TEST(test_force_save_persists_even_when_stale_was_not_set);
  RUN_TEST(test_clear_generate_bulk_edit_save_and_reload_workflow);
  return UNITY_END();
}
