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
constexpr uint8_t REG_ECR          = 0x5E;
constexpr uint8_t REG_SOFTRESET    = 0x80;

constexpr uint8_t CONFIG2_POR      = 0x24;   // CONFIG2 power-on value — presence check
constexpr uint8_t ECR_RUN          = 0x8F;   // baseline tracking + all electrodes
constexpr uint8_t NUM_ELECTRODES   = 12;

constexpr uint8_t I2C_DELAY_US     = 5;      // ~50-100 kHz depending on GPIO speed
}

// --- bit-banged I2C -------------------------------------------------------
// Open-drain emulation: a line is either actively driven LOW or released to the
// cable's pull-ups (INPUT). Never driven HIGH.

void TouchPanel::sdaHigh() { pinMode(sda_, INPUT_PULLUP); }
void TouchPanel::sdaLow()  { pinMode(sda_, OUTPUT); digitalWrite(sda_, LOW); }
void TouchPanel::sclLow()  { pinMode(scl_, OUTPUT); digitalWrite(scl_, LOW); }

bool TouchPanel::sclWaitHigh() {
  pinMode(scl_, INPUT_PULLUP);
  for (int i = 0; i < 500; i++) {           // up to ~500 us of clock-stretch
    if (digitalRead(scl_)) return true;
    delayMicroseconds(1);
  }
  return false;
}
void TouchPanel::sclHigh()  { sclWaitHigh(); }

void TouchPanel::i2cDelay() { delayMicroseconds(I2C_DELAY_US); }

void TouchPanel::i2cStart() {
  sdaHigh(); sclHigh(); i2cDelay();
  sdaLow();  i2cDelay();
  sclLow();  i2cDelay();
}

void TouchPanel::i2cRestart() {
  sdaHigh(); i2cDelay();
  sclHigh(); i2cDelay();
  sdaLow();  i2cDelay();
  sclLow();  i2cDelay();
}

void TouchPanel::i2cStop() {
  sdaLow();  i2cDelay();
  sclHigh(); i2cDelay();
  sdaHigh(); i2cDelay();
}

bool TouchPanel::i2cWrite(uint8_t b) {
  for (uint8_t i = 0; i < 8; i++) {
    (b & 0x80) ? sdaHigh() : sdaLow();
    b <<= 1;
    i2cDelay();
    sclHigh(); i2cDelay();
    sclLow();  i2cDelay();
  }
  sdaHigh();                       // release for the ACK bit
  i2cDelay();
  sclHigh(); i2cDelay();
  bool ack = (digitalRead(sda_) == LOW);
  sclLow();  i2cDelay();
  return ack;
}

uint8_t TouchPanel::i2cRead(bool ackAfter) {
  uint8_t b = 0;
  sdaHigh();
  for (uint8_t i = 0; i < 8; i++) {
    i2cDelay();
    sclHigh(); i2cDelay();
    b = (b << 1) | (digitalRead(sda_) ? 1 : 0);
    sclLow();
  }
  ackAfter ? sdaLow() : sdaHigh();
  i2cDelay();
  sclHigh(); i2cDelay();
  sclLow();  i2cDelay();
  sdaHigh();
  return b;
}

// --- MPR121 register access ---------------------------------------------

void TouchPanel::writeReg(uint8_t reg, uint8_t val) {
  i2cStart();
  i2cWrite(addr_ << 1);
  i2cWrite(reg);
  i2cWrite(val);
  i2cStop();
}

uint8_t TouchPanel::read8(uint8_t reg) {
  i2cStart();
  if (!i2cWrite(addr_ << 1) || !i2cWrite(reg)) { i2cStop(); return 0; }
  i2cRestart();
  i2cWrite((addr_ << 1) | 1);
  uint8_t v = i2cRead(false);
  i2cStop();
  return v;
}

uint16_t TouchPanel::read16(uint8_t reg) {
  i2cStart();
  if (!i2cWrite(addr_ << 1) || !i2cWrite(reg)) { i2cStop(); return 0; }
  i2cRestart();
  i2cWrite((addr_ << 1) | 1);
  uint8_t lo = i2cRead(true);
  uint8_t hi = i2cRead(false);
  i2cStop();
  return (uint16_t)((hi << 8) | lo);
}

void TouchPanel::writeThresholds(uint8_t touch, uint8_t release) {
  for (uint8_t i = 0; i < NUM_ELECTRODES; i++) {
    writeReg(REG_ELE0_TOUCH + 2 * i,     touch);
    writeReg(REG_ELE0_TOUCH + 2 * i + 1, release);
  }
}

// --- public -----------------------------------------------------------

bool TouchPanel::begin(uint8_t sdaPin, uint8_t sclPin, uint8_t i2cAddr) {
  sda_  = sdaPin;
  scl_  = sclPin;
  addr_ = i2cAddr;
  sdaHigh();
  sclHigh();
  delay(1);

  writeReg(REG_SOFTRESET, 0x63);
  delay(1);
  writeReg(REG_ECR, 0x00);                       // electrodes off for config

  uint8_t c2 = read8(REG_CONFIG2);
  if (c2 != CONFIG2_POR) {
    ok_ = false;
    log_e("MPR121 bad CONFIG2 = 0x%02X (want 0x%02X) at 0x%02X", c2, CONFIG2_POR, addr_);
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
  writeReg(REG_ECR, ECR_RUN);

  ok_ = true;
  log_i("MPR121 ready at 0x%02X (bit-bang SDA=%u SCL=%u, touch=%u release=%u)",
        addr_, sda_, scl_, touchThr_, releaseThr_);
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

  const TouchState before = fsm_.state();
  const uint16_t   mask   = touchedMask();
  const Gesture    g      = fsm_.update(mask);
  const TouchState after  = fsm_.state();

  // Log every FSM transition, not just ones that end up changing the lamp —
  // e.g. a single-pad touch that never reaches "stable" produces no gesture
  // and is silent by design, but Idle->RampingUp etc. should be visible even
  // without the web page open.
  if (after != before) {
    log_i("Touch %03X: %s -> %s%s", mask, toString(before), toString(after),
          g == Gesture::Toggle ? " [Toggle]" : "");
  }
  return g;
}
