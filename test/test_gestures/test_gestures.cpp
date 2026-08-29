#include <unity.h>
#include "GestureFsm.h"

// Raw MPR121 touched-bitmask helpers (Pad A=bit0, B=bit2, C=bit4).
static constexpr uint16_t A   = 1 << 0;
static constexpr uint16_t B   = 1 << 2;
static constexpr uint16_t C   = 1 << 4;
static constexpr uint16_t NONE = 0;

// Feed the same bitmask for n polls, returning the last gesture seen.
static Gesture hold(GestureFsm& fsm, uint16_t bits, int n) {
  Gesture g = Gesture::None;
  for (int i = 0; i < n; i++) g = fsm.update(bits);
  return g;
}

// Feed n polls, asserting no poll ever returns a ramp gesture.
static void holdNoRamp(GestureFsm& fsm, uint16_t bits, int n) {
  for (int i = 0; i < n; i++) {
    Gesture g = fsm.update(bits);
    TEST_ASSERT_TRUE(g != Gesture::RampUp && g != Gesture::RampDown);
  }
}

void setUp() {}
void tearDown() {}

void test_classify_combinations() {
  TEST_ASSERT_EQUAL(int(Combo::All3), int(classifyTouch(A | B | C)));
  TEST_ASSERT_EQUAL(int(Combo::AB),   int(classifyTouch(A | B)));
  TEST_ASSERT_EQUAL(int(Combo::BC),   int(classifyTouch(B | C)));
  TEST_ASSERT_EQUAL(int(Combo::None), int(classifyTouch(NONE)));
  TEST_ASSERT_EQUAL(int(Combo::Other), int(classifyTouch(B)));       // B alone = no action
  TEST_ASSERT_EQUAL(int(Combo::Other), int(classifyTouch(A | C)));   // A+C without B
}

void test_all_three_toggles_once_on_stable_touch() {
  GestureFsm fsm;
  TEST_ASSERT_EQUAL(int(Gesture::None), int(fsm.update(A | B | C)));   // poll 1: not yet stable
  TEST_ASSERT_EQUAL(int(Gesture::Toggle), int(fsm.update(A | B | C))); // poll 2: fires
  // Held: never fires again.
  for (int i = 0; i < 10; i++)
    TEST_ASSERT_EQUAL(int(Gesture::None), int(fsm.update(A | B | C)));
}

void test_single_poll_all_three_does_not_toggle() {
  GestureFsm fsm;
  TEST_ASSERT_EQUAL(int(Gesture::None), int(fsm.update(A | B | C)));
  TEST_ASSERT_EQUAL(int(Gesture::None), int(fsm.update(NONE)));
}

void test_ab_ramps_up_while_held() {
  GestureFsm fsm;
  fsm.update(A | B);                                    // poll 1: debounce
  TEST_ASSERT_EQUAL(int(Gesture::RampUp), int(fsm.update(A | B)));   // poll 2: enters ramp
  TEST_ASSERT_EQUAL(int(Gesture::RampUp), int(fsm.update(A | B)));
  TEST_ASSERT_EQUAL(int(Gesture::RampUp), int(fsm.update(A | B)));
}

void test_bc_ramps_down_while_held() {
  GestureFsm fsm;
  fsm.update(B | C);
  TEST_ASSERT_EQUAL(int(Gesture::RampDown), int(fsm.update(B | C)));
  TEST_ASSERT_EQUAL(int(Gesture::RampDown), int(fsm.update(B | C)));
}

// Item A: releasing one finger from a three-pad touch must not fire a ramp.
void test_release_from_all_three_via_ab_emits_no_ramp() {
  GestureFsm fsm;
  hold(fsm, A | B | C, 2);            // toggle fires here
  holdNoRamp(fsm, A | B, 3);          // finger C lifts first
  holdNoRamp(fsm, NONE, 3);           // full release
}

void test_release_from_all_three_via_bc_emits_no_ramp() {
  GestureFsm fsm;
  hold(fsm, A | B | C, 2);
  holdNoRamp(fsm, B | C, 3);          // finger A lifts first
  holdNoRamp(fsm, NONE, 3);
}

// Item A: wobble between the two ramp combos must not flip to the other ramp.
void test_ramp_wobble_does_not_flip_direction() {
  GestureFsm fsm;
  hold(fsm, A | B, 5);               // ramping up
  fsm.update(B);                     // brief single-pad glitch -> drains
  holdNoRamp(fsm, B | C, 3);         // would-be ramp-down is ignored
  holdNoRamp(fsm, NONE, 2);
}

void test_new_gesture_recognised_after_drain() {
  GestureFsm fsm;
  hold(fsm, A | B | C, 2);           // toggle
  hold(fsm, NONE, 2);                // drain back to Idle
  TEST_ASSERT_EQUAL(int(TouchState::Idle), int(fsm.state()));
  fsm.update(A | B);
  TEST_ASSERT_EQUAL(int(Gesture::RampUp), int(fsm.update(A | B)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_classify_combinations);
  RUN_TEST(test_all_three_toggles_once_on_stable_touch);
  RUN_TEST(test_single_poll_all_three_does_not_toggle);
  RUN_TEST(test_ab_ramps_up_while_held);
  RUN_TEST(test_bc_ramps_down_while_held);
  RUN_TEST(test_release_from_all_three_via_ab_emits_no_ramp);
  RUN_TEST(test_release_from_all_three_via_bc_emits_no_ramp);
  RUN_TEST(test_ramp_wobble_does_not_flip_direction);
  RUN_TEST(test_new_gesture_recognised_after_drain);
  return UNITY_END();
}
