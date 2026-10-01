#include <unity.h>
#include "RampRepeater.h"

using Step = RampRepeater::Step;

void setUp() {}
void tearDown() {}

void test_no_ramp_held_yields_nothing() {
  RampRepeater r;
  TEST_ASSERT_EQUAL(int(Step::None), int(r.update(0, 0)));
  TEST_ASSERT_EQUAL(int(Step::None), int(r.update(0, 1000)));
}

void test_press_steps_immediately() {
  RampRepeater r;
  TEST_ASSERT_EQUAL(int(Step::First), int(r.update(+1, 100)));
}

void test_holding_waits_out_the_hold_delay() {
  RampRepeater r;
  r.update(+1, 0);
  TEST_ASSERT_EQUAL(int(Step::None), int(r.update(+1, 50)));
  TEST_ASSERT_EQUAL(int(Step::None), int(r.update(+1, RampRepeater::kHoldDelayMs - 1)));
  TEST_ASSERT_EQUAL(int(Step::Repeat), int(r.update(+1, RampRepeater::kHoldDelayMs)));
}

void test_repeats_are_spaced_by_the_interval() {
  RampRepeater r;
  r.setIntervalMs(250);
  const uint32_t t0 = RampRepeater::kHoldDelayMs;
  r.update(+1, 0);
  TEST_ASSERT_EQUAL(int(Step::Repeat), int(r.update(+1, t0)));
  TEST_ASSERT_EQUAL(int(Step::None),   int(r.update(+1, t0 + 249)));
  TEST_ASSERT_EQUAL(int(Step::Repeat), int(r.update(+1, t0 + 250)));
  TEST_ASSERT_EQUAL(int(Step::None),   int(r.update(+1, t0 + 400)));
  TEST_ASSERT_EQUAL(int(Step::Repeat), int(r.update(+1, t0 + 500)));
}

void test_release_then_press_is_a_fresh_tap() {
  RampRepeater r;
  r.update(+1, 0);
  r.update(+1, 100);
  TEST_ASSERT_EQUAL(int(Step::None),  int(r.update(0, 150)));
  TEST_ASSERT_EQUAL(int(Step::First), int(r.update(+1, 200)));
  TEST_ASSERT_EQUAL(int(Step::None),  int(r.update(+1, 200 + RampRepeater::kHoldDelayMs - 1)));
}

void test_direction_change_is_a_fresh_tap() {
  RampRepeater r;
  r.update(+1, 0);
  TEST_ASSERT_EQUAL(int(Step::First), int(r.update(-1, 100)));
}

void test_interval_change_applies_to_next_repeat() {
  RampRepeater r;
  r.setIntervalMs(1000);
  const uint32_t t0 = RampRepeater::kHoldDelayMs;
  r.update(-1, 0);
  TEST_ASSERT_EQUAL(int(Step::Repeat), int(r.update(-1, t0)));
  TEST_ASSERT_EQUAL(int(Step::None),   int(r.update(-1, t0 + 999)));
  TEST_ASSERT_EQUAL(int(Step::Repeat), int(r.update(-1, t0 + 1000)));
}

void test_survives_millis_wraparound() {
  RampRepeater r;
  const uint32_t start = 0xFFFFFF00u;   // 256 ms before uint32 wrap
  r.update(+1, start);
  TEST_ASSERT_EQUAL(int(Step::None),   int(r.update(+1, start + 100)));
  TEST_ASSERT_EQUAL(int(Step::Repeat), int(r.update(+1, start + RampRepeater::kHoldDelayMs)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_no_ramp_held_yields_nothing);
  RUN_TEST(test_press_steps_immediately);
  RUN_TEST(test_holding_waits_out_the_hold_delay);
  RUN_TEST(test_repeats_are_spaced_by_the_interval);
  RUN_TEST(test_release_then_press_is_a_fresh_tap);
  RUN_TEST(test_direction_change_is_a_fresh_tap);
  RUN_TEST(test_interval_change_applies_to_next_repeat);
  RUN_TEST(test_survives_millis_wraparound);
  return UNITY_END();
}
