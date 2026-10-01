#pragma once
#include <Arduino.h>

// Phase-cut (leading-edge) AC dimmer control for a single lamp channel.
//
// Firing path is entirely interrupt-driven, no tasks or esp_timer:
//   zero-cross GPIO ISR -> gate LOW, arm a one-shot hardware timer (gptimer)
//   timer alarm ISR     -> gate HIGH, held until the next zero-cross
// On dual-core chips both interrupts are allocated on core 1, away from WiFi
// (pinned to core 0). The ISRs are deliberately NOT IRAM-flagged: during flash
// writes ESP-IDF postpones them instead of running them with the cache off, so
// there is no cache-access crash risk — bracket flash writes with pause()/resume()
// so a postponed zero-cross can't leave the gate held high.
//
// Brightness 1-100 maps linearly onto a conduction-% trim window [trimLo, trimHi]
// (LED drivers are dark below one angle and saturated above another). Owns the
// "last non-zero brightness" so a toggle-on restores the level the lamp was at;
// RAM only — the spec says it need not survive a power cut.
class LampDimmer {
public:
  static constexpr uint8_t  kDefaultMinLevel = 10;   // dim-down click-off point (%)
  static constexpr uint8_t  kDefaultTrimLo   = 20;   // conduction % at brightness 1
  static constexpr uint8_t  kDefaultTrimHi   = 70;   // conduction % at brightness 100
  static constexpr uint16_t kFadeOffMs       = 300;  // toggle-off fade
  static constexpr uint16_t kFadeOnMs        = 150;  // toggle-on restore
  static constexpr uint16_t kSetMs           = 80;   // discrete brightness change

  bool begin(uint8_t zeroCrossPin, uint8_t dimPin);
  void tick();   // call every loop(): runs fades and the zero-cross-loss watchdog

  void setConfig(uint8_t minLevel, uint8_t trimLo, uint8_t trimHi);

  void setOn(bool on);               // on -> fade to last level; off -> fade to 0
  void setBrightness(uint8_t pct);   // absolute, clamped to [minLevel, 100]
  void turnOnAtFloor();              // on at minLevel, ignoring the last level

  // +1 / -1 by step %. Ramping down through minLevel turns the lamp off
  // (with the normal fade) rather than sticking at the floor.
  void nudge(int8_t dir, uint8_t step);

  bool    isOn() const       { return on_; }
  uint8_t brightness() const { return level_; }   // current target level, 0-100

  // Test hook for the tuning page: drive the TRIAC at a raw conduction %,
  // bypassing on/off state and the trim window. Any normal setOn/setBrightness/
  // nudge leaves raw mode.
  void    setRaw(uint8_t conductionPct);
  int16_t rawLevel() const { return raw_; }       // -1 when not in raw mode

  // Gate off and firing suspended, e.g. around flash writes or for OTA.
  void pause();
  void resume();

  uint32_t delayUs() const;    // fire delay after zero-cross in effect, 0 = not firing
  uint16_t mainsHz() const;    // 0 until 50 clean half-cycles have been measured
  uint32_t zcPulses() const;   // debounced zero-cross edges since boot

  // Measured zero-cross -> gate-high delay of each actual fire since the last
  // call (then reset). Spread = maxUs - minUs is the fire-timing jitter.
  // zcMin/zcMaxUs: half-cycle period range over the same window (~8333 at 60 Hz).
  struct FireStats { uint32_t count, minUs, maxUs, avgUs, zcMinUs, zcMaxUs; };
  FireStats takeFireStats();

private:
  bool initHw();
  void apply(uint8_t level, uint16_t fadeMs);
  void output(uint8_t level);   // user level 0-100 -> conduction % via trim window

  uint8_t  zcPin_     = 0;
  uint8_t  dimPin_    = 0;
  bool     ready_     = false;
  bool     on_        = false;
  uint8_t  level_     = 0;     // target level
  uint8_t  outLevel_  = 0;     // level currently being output (moves during a fade)
  uint8_t  lastLevel_ = 60;    // restored on toggle-on
  uint8_t  minLevel_  = kDefaultMinLevel;
  uint8_t  trimLo_    = kDefaultTrimLo;
  uint8_t  trimHi_    = kDefaultTrimHi;
  int16_t  raw_       = -1;

  bool     fading_    = false;
  uint8_t  fadeFrom_  = 0;
  uint8_t  fadeTo_    = 0;
  uint32_t fadeStart_ = 0;
  uint16_t fadeMs_    = 0;

  friend void lampDimmerInitTask(void*);
};
