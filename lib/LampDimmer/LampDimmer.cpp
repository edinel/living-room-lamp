#include "LampDimmer.h"

bool LampDimmer::begin(uint8_t zeroCrossPin, uint8_t dimPin) {
  if (rbdimmer_init() != RBDIMMER_OK) {
    log_e("rbdimmer_init failed");
    return false;
  }
  // phase 0, frequency 0 = auto-detect mains frequency
  if (rbdimmer_register_zero_cross(zeroCrossPin, 0, 0) != RBDIMMER_OK) {
    log_e("rbdimmer zero-cross register failed (pin %u)", zeroCrossPin);
    return false;
  }

  rbdimmer_config_t config = {};
  config.gpio_pin      = dimPin;
  config.phase         = 0;
  config.initial_level = 0;
  config.curve_type    = RBDIMMER_CURVE_LOGARITHMIC;  // dimmable LED bulb

  if (rbdimmer_create_channel(&config, &channel_) != RBDIMMER_OK) {
    log_e("rbdimmer channel create failed (pin %u)", dimPin);
    return false;
  }

  log_i("LampDimmer ready: ZC=%u DIM=%u", zeroCrossPin, dimPin);
  return true;
}

void LampDimmer::setConfig(uint8_t minLevel, uint8_t rampStep) {
  minLevel_ = constrain(minLevel, (uint8_t)1, (uint8_t)90);
  rampStep_ = constrain(rampStep, (uint8_t)1, (uint8_t)25);
  log_i("LampDimmer config: minLevel=%u rampStep=%u", minLevel_, rampStep_);
}

void LampDimmer::apply(uint8_t level, uint16_t fadeMs) {
  level_ = level;
  if (channel_) rbdimmer_set_level_transition(channel_, level, fadeMs);
}

void LampDimmer::setOn(bool on) {
  if (on == on_) return;
  on_ = on;
  if (on) {
    apply(lastLevel_, kFadeOnMs);
  } else {
    apply(0, kFadeOffMs);
  }
  log_i("Lamp %s (level %u)", on ? "on" : "off", level_);
}

void LampDimmer::setBrightness(uint8_t pct) {
  pct = constrain(pct, minLevel_, (uint8_t)100);
  lastLevel_ = pct;
  if (on_) apply(pct, kSetMs);
  else     level_ = pct;   // takes effect on next toggle-on
  log_i("Lamp brightness %u", pct);
}

void LampDimmer::nudge(int8_t dir) {
  int next = (int)level_ + dir * (int)rampStep_;
  setBrightness((uint8_t)constrain(next, (int)minLevel_, 100));
}
