#pragma once
#include <Arduino.h>
#include "rbdimmerESP32.h"

// Phase-cut AC dimmer control for a single lamp channel.
//
// Wraps rbdimmerESP32 (zero-cross + TRIAC gate). Owns the "last non-zero
// brightness" so a toggle-on restores the level the lamp was at before it was
// switched off. That level lives in RAM only — the spec says it need not survive
// a power cut.
class LampDimmer {
public:
  // Defaults for the values the spec leaves open ("tune during build").
  // minLevel and rampStep are overridable at runtime from the web tuning page.
  static constexpr uint8_t  kDefaultMinLevel     = 10;   // dim-down click-off point (%)
  static constexpr uint8_t  kDefaultRampStep     = 2;    // % per ramp tick
  static constexpr uint16_t kFadeOffMs           = 300;  // toggle-off fade
  static constexpr uint16_t kFadeOnMs            = 150;  // toggle-on restore
  static constexpr uint16_t kSetMs               = 80;   // discrete brightness change

  bool begin(uint8_t zeroCrossPin, uint8_t dimPin);
  void setConfig(uint8_t minLevel, uint8_t rampStep);

  void setOn(bool on);          // on -> fade to last level; off -> fade to 0
  void setBrightness(uint8_t pct);   // absolute, clamped to [minLevel, 100]

  // +1 / -1, one rampStep. Ramping down through minLevel turns the lamp off
  // (with the normal fade) rather than sticking at the floor.
  void nudge(int8_t dir);

  bool    isOn() const      { return on_; }
  uint8_t brightness() const { return level_; }   // current target level, 0-100

private:
  void apply(uint8_t level, uint16_t fadeMs);

  rbdimmer_channel_t* channel_ = nullptr;
  bool    on_        = false;
  uint8_t level_     = 0;                    // current target level
  uint8_t lastLevel_ = 60;                   // restored on toggle-on
  uint8_t minLevel_  = kDefaultMinLevel;
  uint8_t rampStep_  = kDefaultRampStep;
};
