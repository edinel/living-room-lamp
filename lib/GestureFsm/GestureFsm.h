#pragma once
#include <cstdint>

// Gesture recognition for the three-pad capacitive panel.
//
// Pads map to MPR121 electrodes ELE0 / ELE2 / ELE4 (spec picks non-adjacent
// channels for solder clearance):
//     Pad A -> bit 0    Pad B -> bit 2    Pad C -> bit 4
// of the raw "touched" bitmask from Adafruit_MPR121::touched().
//
// Control is by multi-pad AND combination, never a single pad. The FSM below is
// what makes a release safe: lifting one finger off a three-pad touch passes
// briefly through A+B or B+C before all pads read clear, and that transient must
// not be read as a ramp gesture.

enum class Gesture : uint8_t { None, Toggle, RampUp, RampDown };

enum class TouchState : uint8_t { Idle, RampingUp, RampingDown, Draining };

// Which recognised pad combination a raw bitmask represents.
enum class Combo : uint8_t { None, AB, BC, All3, Other };

Combo classifyTouch(uint16_t touchedBits);

// Human-readable name for logging / the web tuning page's JSON.
const char* toString(TouchState state);

// Poll-driven gesture state machine. Feed it the raw MPR121 touched bitmask once
// per poll; it returns the gesture (if any) recognised on that poll.
//
//  - Toggle fires exactly once, on entry into a stable all-three touch.
//  - RampUp / RampDown are returned on every poll the A+B / B+C combo is held
//    (the caller rate-limits the actual brightness step).
//  - After any gesture ends the FSM drains: it ignores every combination until it
//    sees a stable no-pads-touched reading, then returns to Idle.
class GestureFsm {
public:
  Gesture update(uint16_t touchedBits);
  TouchState state() const { return state_; }

  // Polls a combination must persist before the FSM acts on it (debounce).
  static constexpr uint8_t kStablePolls = 2;

private:
  TouchState state_ = TouchState::Idle;
  Combo      lastCombo_ = Combo::None;
  uint8_t    stable_ = 0;
};
