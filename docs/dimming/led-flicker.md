# LED flicker: diagnosis and fix

Record of the bring-up problem where a dimmable LED bulb would not dim cleanly,
what it turned out to be, and the options that were tried or rejected along the
way. Current wiring and firmware facts live in [hardware.md](../../hardware.md).

## Symptom

On the original build (XIAO ESP32-C6, `rbdimmerESP32`, logarithmic curve), with
a TRIAC-rated Philips BA11 E12 LED bulb and a PCS LDS-120V bleeder installed,
a raw sweep of conduction % (linear, 60 Hz) gave:

| Conduction % | Fire angle | Observed |
|---|---|---|
| 20 | ~144° | on, very dim, steady |
| 25–35 | ~117°–135° | wavering brightness |
| 40–50 | ~90°–108° | steady |
| 55–65 | ~63°–81° | flicker; intermittent drop-out at 65 |
| 70–100 | ≤ ~54° | steady, little or no further brightening |

The flicker was *wavering brightness*, not on/off, and it came and went over
time at a fixed setting.

## Root cause: late TRIAC fires

Firmware instrumentation timestamped each zero-cross and each actual gate-high
edge in GPIO interrupts and reported the per-window spread on the tuning page.
At a steady raw level of 60 (set delay 3333 µs):

- Mains half-cycle period: 8324–8346 µs (spread ≤ 36 µs), so mains and
  zero-cross detection were stable.
- Fire delay: normally 3347 µs (a fixed +14 µs), but individual fires landed up
  to **~850 µs late**. A visible flicker coincided with the worst-case spread
  jumping to 859 µs.

Around 850 µs is ~18° of the half-cycle, enough to visibly change the power
delivered in that half-cycle. The cause: `rbdimmerESP32` fires the gate from an
`esp_timer` callback, and the Arduino core builds don't enable esp_timer ISR
dispatch (`CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD` is unset), so
callbacks run in the esp_timer *task*. The C6 has a single application core
(`CONFIG_FREERTOS_UNICORE=1`) that WiFi shares, so WiFi activity delays that task.

## Fix

1. **Own interrupt-driven firing path** (`lib/LampDimmer`, replacing
   `rbdimmerESP32`). The zero-cross GPIO interrupt drops the gate and arms a
   one-shot gptimer alarm; the alarm interrupt raises the gate and holds it
   until the next zero-cross. No task or esp_timer is in the firing path.
2. **XIAO ESP32-S3**, dual core. Both dimmer interrupts are registered from a
   task pinned to core 1 at level 3. WiFi, TCP/IP and esp_timer are pinned to
   core 0 in the S3 Arduino build (`ESP_WIFI_TASK_PINNED_TO_CORE_0`,
   `LWIP_TCPIP_TASK_AFFINITY_CPU0`, `ESP_TIMER_TASK_AFFINITY_CPU0`). ESP-IDF
   allocates an interrupt on the core that registers it.
3. **Flash-write safety.** The dimmer interrupts are deliberately not
   IRAM-flagged, so ESP-IDF postpones them during flash writes instead of
   crashing. The same class of bug caused the earlier OTA panic, where
   rbdimmer's IRAM ISR called non-IRAM `gpio_set_level`. Settings saves and OTA
   pause the dimmer so a postponed zero-cross can't leave the gate held on, and
   `WiFi.persistent(false)` avoids credential writes on every reconnect.
4. **Trim window.** Brightness 1–100 maps linearly onto conduction %
   `[trimLo, trimHi]`, default 20..70 from the sweep above, tunable and persisted
   from the tuning page. It replaces the log curve, which spent more than half
   the slider in the range where the LED driver is already at full output.

**Bench proof** on a bare XIAO S3 over USB (env `xiao_s3_bench`: fake 120 Hz
zero-cross generated on D4 and jumpered to the Z-C pad, which was D2 then and
is D3 on the S3 since), with the tuning page polling and
heavy page reloads in other tabs:

- Worst fire spread: **57 µs** over ~1 minute, **60 µs** over 10 minutes
  (against ~850 µs before).
- Worst half-cycle spread: 9 µs.
- `/api/intr` interrupt map: `GPIO` and `TG0_T0_LEVEL` at level 3 on CPU 1.
  CPU 1 otherwise holds only level-1 tick, IPC and USB-serial interrupts.

## Hypotheses tested and ruled out

- **TRIAC holding current (bleeder).** The PCS LDS-120V (120 VAC, 1.8 W) was
  installed in parallel across the pigtail load and neutral. The flicker band
  didn't change, and flicker was worst mid-range rather than at the low end,
  where holding current would matter most. The bleeder is still installed; its
  effect at the very low end, and on "off" glow, is untested. It runs warm,
  which is within its 1.8 W rating.
- **Bulb compatibility.** The first bulb's dimmer type was unknown. The
  replacement is explicitly TRIAC-rated (Philips BA11), and the flicker
  persisted.
- **Gate pulse too short (inrush ringing).** The rbdimmer gate pulse was raised
  from 100 µs to 1000 µs (`CONFIG_RBDIMMER_DEFAULT_PULSE_WIDTH_US`). The flicker
  band stayed at the same fire angles.
- **Mains distortion / zero-cross wobble.** The half-cycle spread stayed at
  ≤ 36 µs while fires were landing ~850 µs late.

## Options rejected

- **Aeotec ZW150 "Bypass" as the bleeder.** Despite the "fix TRIAC flicker"
  marketing, it's a capacitive parasitic-power accessory for Aeotec's Z-Wave Nano
  Dimmer smart switch (≤ 4 W), not a resistive bleeder.
- **Enabling esp_timer ISR dispatch via pioarduino `custom_sdkconfig`.** This
  would have been a one-line fix, since rbdimmer switches to `ESP_TIMER_ISR`
  automatically. But it triggers hybrid compile, which rebuilds the whole
  framework from source, raced the VS Code PlatformIO extension's own
  `project init`, needed the partition-table path spelled out in full, removed
  the installed `framework-arduinoespressif32-libs` package, and finally failed
  to link (`undefined reference to __wrap_log_printf` from rbdimmer) even after
  a clean build.
- **Hardware-timed firing with MCPWM** (zero-cross as a sync input, gate
  generated entirely in hardware). Unverified on the C6, and more unfamiliar
  code than the interrupt approach.
- **RobotDyn DimmerLink** (a separate Cortex-M board that handles zero-cross and
  firing, controlled over I2C/UART). It would mean new hardware, and its docs
  don't say whether it works with the existing dimmer module, or whether it
  supports LED trim.
- **The C6's low-power core.** It isn't usable from Arduino/FreeRTOS, needs
  ESP-IDF LP-core tooling (the same framework-rebuild path that failed), and can
  only drive LP GPIO 0–7, while D3 is GPIO21 on the C6.
- **Staying on the C6 with the new interrupt-driven firing.** Untested: the
  `xiao` env builds it, but on one core WiFi's interrupts and critical sections
  still share the CPU with firing, so the S3's core isolation was chosen.
