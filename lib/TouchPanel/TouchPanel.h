#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPR121.h>
#include "GestureFsm.h"

// The remote three-pad capacitive panel (MPR121 over the 4-conductor cable).
//
// poll() does one I2C transaction, reads all electrodes, and advances the
// gesture state machine. Thresholds are tunable live from the web page and are
// applied to all 12 channels (the three copper pads are identical).
class TouchPanel {
public:
  // Electrode channels wired to the pads (spec: non-adjacent for solder room).
  static constexpr uint8_t kChanA = 0;   // ELE0, Pad A (left)
  static constexpr uint8_t kChanB = 2;   // ELE2, Pad B (middle)
  static constexpr uint8_t kChanC = 4;   // ELE4, Pad C (right)

  // Defaults for values the spec says to tune on the mounted copper pads.
  static constexpr uint8_t kDefaultTouchThreshold   = 12;
  static constexpr uint8_t kDefaultReleaseThreshold = 6;

  // Minimum gap between polls; one poll reads every channel.
  static constexpr unsigned long kPollIntervalMs = 50;

  bool begin(TwoWire& wire = Wire, uint8_t i2cAddr = 0x5A);
  void setThresholds(uint8_t touch, uint8_t release);

  // Call every loop; rate-limited internally. Returns None between poll ticks.
  Gesture poll();

  // Diagnostics for the web tuning page. All return 0 when the panel is absent
  // so a missing MPR121 can't flood the bus from the status endpoint.
  TouchState state() const     { return fsm_.state(); }
  bool       ready() const     { return ok_; }
  uint16_t   touchedMask() const;
  uint16_t   filtered(uint8_t chan) { return ok_ ? mpr_.filteredData(chan) : 0; }
  uint16_t   baseline(uint8_t chan) { return ok_ ? mpr_.baselineData(chan) : 0; }
  uint8_t    touchThreshold() const   { return touchThr_; }
  uint8_t    releaseThreshold() const { return releaseThr_; }

private:
  Adafruit_MPR121 mpr_;
  GestureFsm      fsm_;
  bool          ok_         = false;
  uint8_t       touchThr_   = kDefaultTouchThreshold;
  uint8_t       releaseThr_ = kDefaultReleaseThreshold;
  unsigned long lastPoll_   = 0;
};
