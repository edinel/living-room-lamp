#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "GestureFsm.h"

// Direct MPR121 driver.
//
// Deliberately NOT Adafruit_MPR121 / Adafruit_BusIO: their register reads use an
// I2C repeated-START (write_then_read), which the ESP32-C6 Arduino i2c-ng driver
// rejects with ESP_ERR_INVALID_STATE. Every read here is a register-address
// write terminated with STOP, followed by a separate read transaction.
//
// poll() does one I2C read of the touch-status word and advances the gesture
// state machine. Thresholds apply to all 12 channels (the three pads are
// identical) and are tunable live from the web page.
class TouchPanel {
public:
  // Electrode channels wired to the pads (spec: non-adjacent for solder room).
  static constexpr uint8_t kChanA = 0;   // ELE0, Pad A (left)
  static constexpr uint8_t kChanB = 2;   // ELE2, Pad B (middle)
  static constexpr uint8_t kChanC = 4;   // ELE4, Pad C (right)

  // Defaults for values the spec says to tune on the mounted copper pads.
  static constexpr uint8_t kDefaultTouchThreshold   = 12;
  static constexpr uint8_t kDefaultReleaseThreshold = 6;

  // Minimum gap between polls.
  static constexpr unsigned long kPollIntervalMs = 50;

  bool begin(TwoWire& wire = Wire, uint8_t i2cAddr = 0x5A);
  void setThresholds(uint8_t touch, uint8_t release);

  // Call every loop; rate-limited internally. Returns None between poll ticks.
  Gesture poll();

  // Diagnostics for the web tuning page. Return 0 when the panel is absent.
  TouchState state() const { return fsm_.state(); }
  bool       ready() const { return ok_; }
  uint16_t   touchedMask();
  uint16_t   filtered(uint8_t chan);
  uint16_t   baseline(uint8_t chan);
  uint8_t    touchThreshold() const   { return touchThr_; }
  uint8_t    releaseThreshold() const { return releaseThr_; }

private:
  void     writeReg(uint8_t reg, uint8_t val);
  uint8_t  read8(uint8_t reg);
  uint16_t read16(uint8_t reg);          // little-endian (MPR121 order)
  void     writeThresholds(uint8_t touch, uint8_t release);

  TwoWire*      wire_ = nullptr;
  uint8_t       addr_ = 0x5A;
  GestureFsm    fsm_;
  bool          ok_         = false;
  uint8_t       touchThr_   = kDefaultTouchThreshold;
  uint8_t       releaseThr_ = kDefaultReleaseThreshold;
  unsigned long lastPoll_   = 0;
};
