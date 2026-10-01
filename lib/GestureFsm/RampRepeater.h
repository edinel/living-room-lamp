#pragma once
#include <cstdint>

// Keyboard-style auto-repeat for the ramp gestures: a press steps once
// immediately (a tap), and holding past kHoldDelayMs repeats every interval.
// The bulb's own driver can lag a brightness change by seconds, so a slow,
// tunable repeat lets what you see keep up with where the level actually is.
//
// Feed it every loop: dir = +1 while A+B is held, -1 while B+C is held, 0
// otherwise (derive it from the GestureFsm *state*, which persists between
// touch polls). Millis wraparound safe.
class RampRepeater {
public:
  enum class Step : uint8_t { None, First, Repeat };

  static constexpr uint32_t kHoldDelayMs = 500;

  Step update(int8_t dir, uint32_t nowMs);
  void setIntervalMs(uint16_t ms) { intervalMs_ = ms; }

private:
  int8_t   dir_        = 0;
  uint32_t nextMs_     = 0;
  uint16_t intervalMs_ = 250;
};
