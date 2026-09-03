#pragma once
#include <Arduino.h>
#include "GestureFsm.h"

// Direct MPR121 driver over BIT-BANGED I2C.
//
// The ESP32-C6 hardware I2C (Arduino i2c-ng driver, core >= 3.2) returns zeros /
// ESP_ERR_INVALID_STATE on every register read — a known regression
// (espressif/arduino-esp32 #11374). Address-ACK and writes work, reads do not.
// So this talks to the MPR121 by toggling two GPIOs directly: open-drain
// emulation (drive low / release to the cable's pull-ups), ~50 kHz, which is
// plenty for a 2-byte status read every poll and immune to the driver bug.
//
// poll() reads the touch-status word and advances the gesture state machine.
// Thresholds apply to all 12 channels and are tunable live from the web page.
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

  bool begin(uint8_t sdaPin, uint8_t sclPin, uint8_t i2cAddr = 0x5A);
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
  // Bit-banged I2C primitives.
  void    sdaHigh();
  void    sdaLow();
  void    sclHigh();
  void    sclLow();
  bool    sclWaitHigh();          // release SCL, wait out any clock-stretch
  void    i2cDelay();
  void    i2cStart();
  void    i2cRestart();
  void    i2cStop();
  bool    i2cWrite(uint8_t b);    // returns true if ACKed
  uint8_t i2cRead(bool ackAfter);

  // MPR121 register access (repeated-START reads — fine when we own the lines).
  void     writeReg(uint8_t reg, uint8_t val);
  uint8_t  read8(uint8_t reg);
  uint16_t read16(uint8_t reg);   // little-endian (MPR121 order)
  void     writeThresholds(uint8_t touch, uint8_t release);

  uint8_t       sda_ = 0, scl_ = 0;
  uint8_t       addr_ = 0x5A;
  GestureFsm    fsm_;
  bool          ok_         = false;
  uint8_t       touchThr_   = kDefaultTouchThreshold;
  uint8_t       releaseThr_ = kDefaultReleaseThreshold;
  unsigned long lastPoll_   = 0;
};
