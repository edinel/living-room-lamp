# living-room-lamp

Arduino firmware for a pluggable, inline touch-controlled dimmable lamp
controller. A XIAO ESP32-C6 in a floor box phase-cuts mains power to the lamp; a
remote desk box with three copper capacitive pads (Adafruit MPR121) is the
interface. Presents to Home Assistant as an MQTT `light` with brightness; touch
works with or without the network.

## Documentation

- [lamp-controller-build-spec.md](lamp-controller-build-spec.md) — full build
  spec: enclosures, mains-safety wiring, parts, gesture design, open items.
- [hardware.md](hardware.md) — wiring quick reference the firmware targets: pin
  map, inter-box cable, dimmer and MPR121 pinouts, mains rules.

## Layout

| Path | What |
| --- | --- |
| `lib/GestureFsm/` | Pure gesture state machine (multi-pad AND, release-safe). Host-unit-tested. |
| `lib/TouchPanel/` | MPR121 wrapper + poll loop around `GestureFsm`. |
| `lib/LampDimmer/` | `rbdimmerESP32` wrapper: on/off fade, brightness, ramp, min-level floor. |
| `src/main.cpp` | Glue: WiFi + MQTT/HA discovery + OTA + web tuning page + watchdogs. |
| `test/test_gestures/` | `pio test -e native` — FSM behaviour incl. partial-release. |

## Build

```sh
pio run -e xiao                # build
pio run -e xiao -t upload      # flash over USB
pio run -e xiao_ota -t upload  # flash over WiFi (living-room-lamp.local.solace.org)
pio test -e native             # gesture FSM unit tests
```

Copy `include/arduino_secrets.h.example` → `include/arduino_secrets.h` (gitignored)
and fill in WiFi + MQTT credentials.

## Tuning

MPR121 thresholds, minimum brightness, and ramp step are set live and persisted
to NVS from `http://living-room-lamp.local.solace.org/` — no re-flash needed. Per the build
spec, MPR121 thresholds must be tuned on the mounted copper pads.
