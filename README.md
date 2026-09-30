# living-room-lamp

Arduino firmware for a pluggable, inline touch-controlled dimmable lamp
controller. A XIAO ESP32-S3 (originally a C6) in a floor box phase-cuts mains power to the lamp; a
remote desk box with three copper capacitive pads (Adafruit MPR121) is the
interface. Presents to Home Assistant as an MQTT `light` with brightness; touch
works with or without the network.

## Documentation

- [lamp-controller-build-spec.md](lamp-controller-build-spec.md) — full build
  spec: enclosures, mains-safety wiring, parts, gesture design, open items.
- [hardware.md](hardware.md) — wiring quick reference the firmware targets: pin
  map, inter-box cable, dimmer and MPR121 pinouts, mains rules.

### Dimming

- [docs/dimming/led-flicker.md](docs/dimming/led-flicker.md) — why LED bulbs
  flickered (late TRIAC fires under WiFi load on the single-core C6), the
  interrupt-driven firing + S3 core-1 fix and its bench proof, and the
  hypotheses/options ruled out along the way.

## Layout

| Path | What |
| --- | --- |
| `lib/GestureFsm/` | Pure gesture state machine (multi-pad AND, release-safe). Host-unit-tested. |
| `lib/TouchPanel/` | MPR121 wrapper + poll loop around `GestureFsm`. |
| `lib/LampDimmer/` | Interrupt-driven TRIAC firing (ZC GPIO ISR + gptimer, core 1 on S3), trim window, fades, ramp, min-level floor, fire-timing stats. |
| `src/main.cpp` | Glue: WiFi + MQTT/HA discovery + OTA + web tuning page + watchdogs. |
| `test/test_gestures/` | `pio test -e native` — FSM behaviour incl. partial-release. |

## Build

```sh
pio run -e xiao_s3 -t upload        # flash the S3 over USB (mains disconnected!)
pio run -e xiao_s3_ota -t upload    # flash over WiFi (living-room-lamp.local.solace.org)
pio run -e xiao_s3_bench -t upload  # bench timing test: bare S3, jumper D4→D2, host lamp-bench
pio run -e xiao -t upload           # original C6 board (xiao_ota for WiFi)
pio test -e native                  # gesture FSM unit tests
```

Copy `include/arduino_secrets.h.example` → `include/arduino_secrets.h` (gitignored)
and fill in WiFi + MQTT credentials.

## Tuning

MPR121 thresholds, minimum brightness, ramp step, and the dimmer trim window are set live and persisted
to NVS from `http://living-room-lamp.local.solace.org/` — no re-flash needed. Per the build
spec, MPR121 thresholds must be tuned on the mounted copper pads.
