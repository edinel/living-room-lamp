#include "RampRepeater.h"

RampRepeater::Step RampRepeater::update(int8_t dir, uint32_t nowMs) {
  if (dir == 0) {
    dir_ = 0;
    return Step::None;
  }
  if (dir != dir_) {
    dir_    = dir;
    nextMs_ = nowMs + kHoldDelayMs;
    return Step::First;
  }
  if ((int32_t)(nowMs - nextMs_) < 0) return Step::None;
  nextMs_ = nowMs + intervalMs_;
  return Step::Repeat;
}
