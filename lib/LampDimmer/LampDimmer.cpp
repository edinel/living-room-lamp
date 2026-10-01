#include "LampDimmer.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_intr_alloc.h"
#include "esp_timer.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"
#include "soc/interrupts.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {
constexpr uint32_t kDebounceUs  = 3000;    // reject opto bounce / TRIAC-induced spikes
constexpr uint32_t kMinDelayUs  = 150;     // earliest fire after the zero-cross edge
constexpr uint32_t kEndGuardUs  = 500;     // latest fire = half-cycle minus this
constexpr uint32_t kZcLossUs    = 50000;   // no zero-cross this long -> force gate off
constexpr uint32_t kLockSamples = 50;

// ISR-shared state. File-scope because the ISR/callback APIs take plain
// function pointers.
gpio_num_t        g_dimPin = GPIO_NUM_NC;
gptimer_handle_t  g_timer  = nullptr;
intr_handle_t     g_zcIntr = nullptr;
uint32_t          g_zcMask = 0;        // zero-cross pin bit in the low GPIO status word
uint32_t          g_zcCore = 0;        // core the GPIO interrupt is allocated on
volatile uint8_t  g_conduction  = 0;       // 0-100 %, 0 = don't fire
volatile bool     g_paused      = false;
volatile uint32_t g_halfCycleUs = 0;       // 0 until mains frequency is locked
volatile uint16_t g_freq        = 0;
volatile uint32_t g_lastZcUs    = 0;
volatile uint32_t g_zcPulses    = 0;
volatile uint32_t g_lockSum     = 0;
volatile uint32_t g_lockCount   = 0;

portMUX_TYPE      g_statMux   = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t g_fireMin   = UINT32_MAX;
volatile uint32_t g_fireMax   = 0;
volatile uint32_t g_fireSum   = 0;
volatile uint32_t g_fireCount = 0;
volatile uint32_t g_zcMin     = UINT32_MAX;
volatile uint32_t g_zcMax     = 0;

inline uint32_t computeDelay(uint8_t conduction, uint32_t halfCycle) {
  if (!halfCycle || !conduction) return 0;
  uint32_t d = halfCycle * (100u - (conduction > 100 ? 100 : conduction)) / 100u;
  if (d < kMinDelayUs) d = kMinDelayUs;
  if (d > halfCycle - kEndGuardUs) d = halfCycle - kEndGuardUs;
  return d;
}

void IRAM_ATTR lockFrequency(uint32_t period) {
  if (period <= 5000 || period >= 15000) return;   // 33..100 Hz half-cycles only
  g_lockSum   = g_lockSum + period;
  g_lockCount = g_lockCount + 1;
  if (g_lockCount < kLockSamples) return;
  const uint32_t avg = g_lockSum / g_lockCount;
  if (avg >= 9000 && avg <= 11000)     { g_freq = 50; g_halfCycleUs = 10000; }
  else if (avg >= 7500 && avg <= 9166) { g_freq = 60; g_halfCycleUs = 8333; }
  g_lockSum = 0;
  g_lockCount = 0;
}

// Zero-cross: drop the gate (TRIAC commutates off at the zero anyway) and arm
// the one-shot fire alarm for this half-cycle.
void IRAM_ATTR zcIsr(void*) {
  const uint32_t now    = (uint32_t)esp_timer_get_time();
  const uint32_t period = now - g_lastZcUs;
  if (g_lastZcUs != 0 && period < kDebounceUs) return;
  g_lastZcUs = now;
  g_zcPulses = g_zcPulses + 1;

  gpio_set_level(g_dimPin, 0);
  gptimer_set_alarm_action(g_timer, nullptr);
  gptimer_set_raw_count(g_timer, 0);

  if (!g_halfCycleUs) {
    lockFrequency(period);
  } else if (period < 20000) {
    portENTER_CRITICAL_ISR(&g_statMux);
    if (period < g_zcMin) g_zcMin = period;
    if (period > g_zcMax) g_zcMax = period;
    portEXIT_CRITICAL_ISR(&g_statMux);
  }

  const uint32_t d = g_paused ? 0 : computeDelay(g_conduction, g_halfCycleUs);
  if (d) {
    gptimer_alarm_config_t alarm = {};
    alarm.alarm_count = d;
    gptimer_set_alarm_action(g_timer, &alarm);
  }
}

// Raw GPIO-peripheral interrupt (we own ETS_GPIO_INTR_SOURCE; nothing else in
// this firmware uses GPIO interrupts).
void IRAM_ATTR gpioIsr(void*) {
  uint32_t status = 0;
  gpio_ll_get_intr_status(&GPIO, g_zcCore, &status);
  if (!(status & g_zcMask)) return;
  gpio_ll_clear_intr_status(&GPIO, g_zcMask);
  zcIsr(nullptr);
}

// Fire: raise the gate and hold it until the next zero-cross drops it.
bool IRAM_ATTR alarmCb(gptimer_handle_t, const gptimer_alarm_event_data_t*, void*) {
  if (g_paused) return false;
  gpio_set_level(g_dimPin, 1);
  const uint32_t d = (uint32_t)esp_timer_get_time() - g_lastZcUs;
  portENTER_CRITICAL_ISR(&g_statMux);
  if (d < g_fireMin) g_fireMin = d;
  if (d > g_fireMax) g_fireMax = d;
  g_fireSum   = g_fireSum + d;
  g_fireCount = g_fireCount + 1;
  portEXIT_CRITICAL_ISR(&g_statMux);
  return false;
}

struct InitArgs {
  LampDimmer*       self;
  bool              ok;
  SemaphoreHandle_t done;
};
}  // namespace

// Interrupts are allocated on the core that registers them, so run the
// registration from a task pinned to core 1 (WiFi lives on core 0).
void lampDimmerInitTask(void* arg) {
  auto* a = static_cast<InitArgs*>(arg);
  a->ok = a->self->initHw();
  xSemaphoreGive(a->done);
  vTaskDelete(nullptr);
}

bool LampDimmer::initHw() {
  g_dimPin = (gpio_num_t)dimPin_;

  gpio_config_t out = {};
  out.pin_bit_mask = 1ULL << dimPin_;
  out.mode         = GPIO_MODE_OUTPUT;
  if (gpio_config(&out) != ESP_OK) return false;
  gpio_set_level(g_dimPin, 0);

  gpio_config_t in = {};
  in.pin_bit_mask = 1ULL << zcPin_;
  in.mode         = GPIO_MODE_INPUT;
  in.intr_type    = GPIO_INTR_DISABLE;   // enabled below, on our core only
  if (gpio_config(&in) != ESP_OK) return false;

  gptimer_config_t tc = {};
  tc.clk_src       = GPTIMER_CLK_SRC_DEFAULT;
  tc.direction     = GPTIMER_COUNT_UP;
  tc.resolution_hz = 1000000;   // 1 tick = 1 µs
  tc.intr_priority = 3;
  if (gptimer_new_timer(&tc, &g_timer) != ESP_OK) return false;
  gptimer_event_callbacks_t cbs = {};
  cbs.on_alarm = alarmCb;
  if (gptimer_register_event_callbacks(g_timer, &cbs, nullptr) != ESP_OK) return false;
  if (gptimer_enable(g_timer) != ESP_OK || gptimer_start(g_timer) != ESP_OK) return false;

  // Allocate the GPIO interrupt directly, in this task. gpio_install_isr_service()
  // does it via the IPC task instead, whose 1 KB stack overflowed when another
  // interrupt landed mid-allocation (boot-loop panic: "Stack canary watchpoint
  // triggered (ipc1)"). No ESP_INTR_FLAG_IRAM on purpose — see LampDimmer.h.
  if (zcPin_ >= 32) return false;   // gpioIsr reads only the low status word
  g_zcCore = xPortGetCoreID();
  g_zcMask = 1UL << zcPin_;
  if (esp_intr_alloc(ETS_GPIO_INTR_SOURCE, ESP_INTR_FLAG_LEVEL3, gpioIsr, nullptr,
                     &g_zcIntr) != ESP_OK) {
    log_e("GPIO interrupt allocation failed (source already in use?)");
    return false;
  }
  gpio_ll_clear_intr_status(&GPIO, g_zcMask);
  gpio_ll_set_intr_type(&GPIO, zcPin_, GPIO_INTR_POSEDGE);
  gpio_ll_intr_enable_on_core(&GPIO, g_zcCore, zcPin_);

  log_i("dimmer interrupts registered on core %d", xPortGetCoreID());
  return true;
}

bool LampDimmer::begin(uint8_t zeroCrossPin, uint8_t dimPin) {
  zcPin_  = zeroCrossPin;
  dimPin_ = dimPin;

#if CONFIG_FREERTOS_UNICORE
  ready_ = initHw();
#else
  InitArgs a{this, false, xSemaphoreCreateBinary()};
  xTaskCreatePinnedToCore(lampDimmerInitTask, "dimmer_init", 4096, &a, 5, nullptr, 1);
  xSemaphoreTake(a.done, portMAX_DELAY);
  vSemaphoreDelete(a.done);
  ready_ = a.ok;
#endif
  if (!ready_) {
    log_e("dimmer hardware init failed (ZC=%u DIM=%u)", zeroCrossPin, dimPin);
    return false;
  }
  log_i("LampDimmer ready: ZC=%u DIM=%u", zeroCrossPin, dimPin);

  // 50 clean half-cycles to lock (~417 ms at 60 Hz, ~500 ms at 50 Hz).
  delay(700);
  if (g_freq) {
    log_i("Mains frequency detected: %u Hz (%u Z-C pulses so far)", g_freq, g_zcPulses);
  } else {
    log_w("No mains frequency locked yet (%u Z-C pulses so far)%s", g_zcPulses,
          g_zcPulses == 0 ? " — nothing reaching the Z-C pin: check the Z-C wire and the "
                            "dimmer module's AC-N connection"
                          : " — pulses arriving but too irregular to lock; "
                            "check for a noisy/marginal connection");
  }
  return true;
}

void LampDimmer::tick() {
  if (!ready_) return;
  if (fading_) {
    const uint32_t el = millis() - fadeStart_;
    if (el >= fadeMs_) {
      fading_ = false;
      output(fadeTo_);
    } else {
      output(fadeFrom_ + ((int)fadeTo_ - (int)fadeFrom_) * (int)el / (int)fadeMs_);
    }
  }
  // Without zero-crosses nothing ever drops the held gate.
  if ((uint32_t)esp_timer_get_time() - g_lastZcUs > kZcLossUs) gpio_set_level(g_dimPin, 0);
}

void LampDimmer::setConfig(uint8_t minLevel, uint8_t rampStep, uint8_t trimLo, uint8_t trimHi) {
  minLevel_ = constrain(minLevel, (uint8_t)1, (uint8_t)90);
  rampStep_ = constrain(rampStep, (uint8_t)1, (uint8_t)25);
  trimLo_   = constrain(trimLo, (uint8_t)1, (uint8_t)95);
  trimHi_   = constrain(trimHi, (uint8_t)(trimLo_ + 1), (uint8_t)100);
  if (!fading_ && raw_ < 0) output(outLevel_);
  log_i("LampDimmer config: minLevel=%u rampStep=%u trim=%u..%u",
        minLevel_, rampStep_, trimLo_, trimHi_);
}

void LampDimmer::output(uint8_t level) {
  outLevel_ = level;
  g_conduction = level ? trimLo_ + (uint32_t)(trimHi_ - trimLo_) * (level - 1) / 99 : 0;
}

void LampDimmer::apply(uint8_t level, uint16_t fadeMs) {
  raw_   = -1;
  level_ = level;
  if (fadeMs == 0 || outLevel_ == level) {
    fading_ = false;
    output(level);
    return;
  }
  fadeFrom_  = outLevel_;
  fadeTo_    = level;
  fadeStart_ = millis();
  fadeMs_    = fadeMs;
  fading_    = true;
}

void LampDimmer::setRaw(uint8_t conductionPct) {
  if (!ready_) return;
  raw_ = constrain(conductionPct, (uint8_t)0, (uint8_t)100);
  fading_ = false;
  g_conduction = (uint8_t)raw_;
  log_i("Raw conduction %d%% -> delay %u us", raw_, delayUs());
}

void LampDimmer::pause() {
  g_paused = true;
  if (!ready_) return;
  gptimer_set_alarm_action(g_timer, nullptr);
  gpio_set_level(g_dimPin, 0);
  // An alarm already in flight on the other core can still raise the gate once;
  // the next zero-cross drops it and won't re-arm. Wait out one half-cycle.
  delay(12);
  gpio_set_level(g_dimPin, 0);
}

void LampDimmer::resume() { g_paused = false; }

uint32_t LampDimmer::delayUs() const {
  return g_paused ? 0 : computeDelay(g_conduction, g_halfCycleUs);
}

uint16_t LampDimmer::mainsHz() const { return g_freq; }
uint32_t LampDimmer::zcPulses() const { return g_zcPulses; }

LampDimmer::FireStats LampDimmer::takeFireStats() {
  FireStats s;
  portENTER_CRITICAL(&g_statMux);
  s.count   = g_fireCount;
  s.minUs   = g_fireCount ? g_fireMin : 0;
  s.maxUs   = g_fireMax;
  s.avgUs   = g_fireCount ? g_fireSum / g_fireCount : 0;
  s.zcMinUs = g_zcMax ? g_zcMin : 0;
  s.zcMaxUs = g_zcMax;
  g_fireMin   = UINT32_MAX;
  g_zcMin     = UINT32_MAX;
  g_fireMax   = 0;
  g_fireSum   = 0;
  g_fireCount = 0;
  g_zcMax     = 0;
  portEXIT_CRITICAL(&g_statMux);
  return s;
}

void LampDimmer::setOn(bool on) {
  if (on == on_) return;
  on_ = on;
  apply(on ? lastLevel_ : 0, on ? kFadeOnMs : kFadeOffMs);
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
