#include "TouchPanel.h"

bool TouchPanel::begin(TwoWire& wire, uint8_t i2cAddr) {
  ok_ = mpr_.begin(i2cAddr, &wire);
  if (!ok_) {
    log_e("MPR121 not found at 0x%02X", i2cAddr);
    return false;
  }
  mpr_.setThresholds(touchThr_, releaseThr_);
  log_i("MPR121 ready at 0x%02X (touch=%u release=%u)", i2cAddr, touchThr_, releaseThr_);
  return true;
}

void TouchPanel::setThresholds(uint8_t touch, uint8_t release) {
  touchThr_   = touch;
  releaseThr_ = release;
  if (ok_) mpr_.setThresholds(touchThr_, releaseThr_);
  log_i("MPR121 thresholds set: touch=%u release=%u", touchThr_, releaseThr_);
}

uint16_t TouchPanel::touchedMask() const {
  return ok_ ? const_cast<Adafruit_MPR121&>(mpr_).touched() : 0;
}

Gesture TouchPanel::poll() {
  if (!ok_) return Gesture::None;

  unsigned long now = millis();
  if (now - lastPoll_ < kPollIntervalMs) return Gesture::None;
  lastPoll_ = now;

  return fsm_.update(mpr_.touched());
}
