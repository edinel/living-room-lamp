#include "TouchPanel.h"

namespace {
// MPR121 register map (subset used here).
constexpr uint8_t REG_TOUCH_STATUS = 0x00;   // 2 bytes, LE, bits 0..11
constexpr uint8_t REG_FILT_DATA_0L = 0x04;   // ch n at 0x04 + 2n, 2 bytes LE, 10-bit
constexpr uint8_t REG_BASELINE_0   = 0x1E;   // ch n at 0x1E + n, 1 byte = value >> 2
constexpr uint8_t REG_MHDR         = 0x2B;   // baseline-filter block, 11 regs
constexpr uint8_t REG_ELE0_TOUCH   = 0x41;   // ch n: touch 0x41+2n, release 0x42+2n
constexpr uint8_t REG_DEBOUNCE     = 0x5B;
constexpr uint8_t REG_CONFIG1      = 0x5C;
constexpr uint8_t REG_CONFIG2      = 0x5D;
constexpr uint8_t REG_ECR          = 0x5E;   // 0x00 = stop, ECR_RUN = run
constexpr uint8_t REG_SOFTRESET    = 0x80;

constexpr uint8_t CONFIG2_POR      = 0x24;   // CONFIG2 power-on value — presence check
constexpr uint8_t ECR_RUN          = 0x8F;   // baseline tracking + all electrodes (Adafruit's value)
constexpr uint8_t NUM_ELECTRODES   = 12;
}

void TouchPanel::writeReg(uint8_t reg, uint8_t val) {
  wire_->beginTransmission(addr_);
  wire_->write(reg);
  wire_->write(val);
  wire_->endTransmission(true);
}

uint8_t TouchPanel::read8(uint8_t reg) {
  wire_->beginTransmission(addr_);
  wire_->write(reg);
  if (wire_->endTransmission(true) != 0) return 0;   // STOP, not repeated START
  if (wire_->requestFrom((int)addr_, 1) != 1) return 0;
  return wire_->read();
}

uint16_t TouchPanel::read16(uint8_t reg) {
  wire_->beginTransmission(addr_);
  wire_->write(reg);
  if (wire_->endTransmission(true) != 0) return 0;
  if (wire_->requestFrom((int)addr_, 2) != 2) return 0;
  uint16_t lo = wire_->read();
  uint16_t hi = wire_->read();
  return (uint16_t)((hi << 8) | lo);
}

void TouchPanel::writeThresholds(uint8_t touch, uint8_t release) {
  for (uint8_t i = 0; i < NUM_ELECTRODES; i++) {
    writeReg(REG_ELE0_TOUCH + 2 * i,     touch);
    writeReg(REG_ELE0_TOUCH + 2 * i + 1, release);
  }
}

bool TouchPanel::begin(TwoWire& wire, uint8_t i2cAddr) {
  wire_ = &wire;
  addr_ = i2cAddr;

  writeReg(REG_SOFTRESET, 0x63);
  delay(1);
  writeReg(REG_ECR, 0x00);                       // electrodes off for config

  if (read8(REG_CONFIG2) != CONFIG2_POR) {
    ok_ = false;
    log_e("MPR121 not responding at 0x%02X", addr_);
    return false;
  }

  writeThresholds(touchThr_, releaseThr_);

  // Baseline-filter tuning — Adafruit's proven defaults (MHDR..FDLT).
  static const uint8_t kFilt[] = {
    0x01, 0x01, 0x0E, 0x00,   // rising:  MHDR NHDR NCLR FDLR
    0x01, 0x05, 0x01, 0x00,   // falling: MHDF NHDF NCLF FDLF
    0x00, 0x00, 0x00,         // touched: NHDT NCLT FDLT
  };
  for (uint8_t i = 0; i < sizeof(kFilt); i++) writeReg(REG_MHDR + i, kFilt[i]);

  writeReg(REG_DEBOUNCE, 0x00);
  writeReg(REG_CONFIG1, 0x10);                   // 16 uA charge current
  writeReg(REG_CONFIG2, 0x20);                   // 0.5 us encoding, 1 ms period
  writeReg(REG_ECR, ECR_RUN);                    // run

  ok_ = true;
  log_i("MPR121 ready at 0x%02X (touch=%u release=%u)", addr_, touchThr_, releaseThr_);
  return true;
}

void TouchPanel::setThresholds(uint8_t touch, uint8_t release) {
  touchThr_   = touch;
  releaseThr_ = release;
  if (ok_) {
    writeReg(REG_ECR, 0x00);
    writeThresholds(touch, release);
    writeReg(REG_ECR, ECR_RUN);
  }
  log_i("MPR121 thresholds set: touch=%u release=%u", touchThr_, releaseThr_);
}

uint16_t TouchPanel::touchedMask() {
  return ok_ ? (read16(REG_TOUCH_STATUS) & 0x0FFF) : 0;
}

uint16_t TouchPanel::filtered(uint8_t chan) {
  return ok_ ? (read16(REG_FILT_DATA_0L + 2 * chan) & 0x03FF) : 0;
}

uint16_t TouchPanel::baseline(uint8_t chan) {
  return ok_ ? (uint16_t)(read8(REG_BASELINE_0 + chan) << 2) : 0;
}

Gesture TouchPanel::poll() {
  if (!ok_) return Gesture::None;

  unsigned long now = millis();
  if (now - lastPoll_ < kPollIntervalMs) return Gesture::None;
  lastPoll_ = now;

  return fsm_.update(touchedMask());
}
