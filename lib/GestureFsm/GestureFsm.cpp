#include "GestureFsm.h"

namespace {
constexpr uint16_t kMaskA = 1 << 0;  // ELE0
constexpr uint16_t kMaskB = 1 << 2;  // ELE2
constexpr uint16_t kMaskC = 1 << 4;  // ELE4
}

Combo classifyTouch(uint16_t bits) {
  const bool a = bits & kMaskA;
  const bool b = bits & kMaskB;
  const bool c = bits & kMaskC;

  if (a && b && c) return Combo::All3;
  if (a && b)      return Combo::AB;
  if (b && c)      return Combo::BC;
  if (!a && !b && !c) return Combo::None;
  return Combo::Other;   // e.g. B alone, or A+C — no action
}

Gesture GestureFsm::update(uint16_t touchedBits) {
  const Combo combo = classifyTouch(touchedBits);

  if (combo == lastCombo_) {
    if (stable_ < kStablePolls) stable_++;
  } else {
    lastCombo_ = combo;
    stable_ = 1;
  }
  const bool settled = stable_ >= kStablePolls;

  switch (state_) {
    case TouchState::Idle:
      if (!settled) return Gesture::None;
      switch (combo) {
        case Combo::All3: state_ = TouchState::Draining;    return Gesture::Toggle;
        case Combo::AB:   state_ = TouchState::RampingUp;    return Gesture::RampUp;
        case Combo::BC:   state_ = TouchState::RampingDown;  return Gesture::RampDown;
        default:                                            return Gesture::None;
      }

    case TouchState::RampingUp:
      if (combo == Combo::AB) return Gesture::RampUp;
      state_ = TouchState::Draining;
      return Gesture::None;

    case TouchState::RampingDown:
      if (combo == Combo::BC) return Gesture::RampDown;
      state_ = TouchState::Draining;
      return Gesture::None;

    case TouchState::Draining:
      if (combo == Combo::None && settled) state_ = TouchState::Idle;
      return Gesture::None;
  }
  return Gesture::None;
}
