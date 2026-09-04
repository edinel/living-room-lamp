#include "LampDimmer.h"

namespace {
// ISR-context counter — see LampDimmer::zcPulses(). File-scope rather than a
// class member: rbdimmer_set_callback() takes a plain function pointer, and
// the callback must be IRAM_ATTR (no member-function trampoline through that).
volatile uint32_t g_zcPulses = 0;
void IRAM_ATTR onZeroCross(void*) { g_zcPulses++; }
}

uint32_t LampDimmer::zcPulses() const { return g_zcPulses; }

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
  if (rbdimmer_set_callback(0, onZeroCross, nullptr) != RBDIMMER_OK)
    log_w("rbdimmer_set_callback failed — no raw Z-C pulse counter");

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

  // Give the zero-cross detector time to lock on, then log what it found — the
  // channel-creation "half-cycle: 10000 us" line above is just the pre-mains
  // default guess and never updates. rbdimmerESP32 needs 50 valid half-cycle
  // measurements before it reports a frequency (~417 ms at 60 Hz, ~500 ms at
  // 50 Hz) — 700 ms covers either with margin.
  delay(700);
  uint16_t freq = rbdimmer_get_frequency(0);
  if (freq > 0) {
    log_i("Mains frequency detected: %u Hz (%u Z-C pulses so far)", freq, g_zcPulses);
  } else {
    log_w("No mains frequency locked yet (%u Z-C pulses so far)%s", g_zcPulses,
          g_zcPulses == 0 ? " — nothing reaching D2: check the Z-C wire and the "
                            "dimmer module's AC-N connection"
                          : " — pulses arriving but too irregular to lock; "
                            "check for a noisy/marginal connection");
  }

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

  // Dimming down through the floor turns the lamp off (with the normal fade)
  // instead of sticking at minLevel_ forever — the floor is a click-off point,
  // like the bottom of a rotary dimmer's travel, not a wall.
  if (dir < 0 && next < (int)minLevel_) {
    setOn(false);
    return;
  }
  setBrightness((uint8_t)constrain(next, (int)minLevel_, 100));
}
