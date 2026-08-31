# Living Room Lamp — Hardware

Wiring record for the firmware in this repo. The full build rationale (enclosure
layout, mains-safety reasoning, gesture design) lives in
[lamp-controller-build-spec.md](lamp-controller-build-spec.md); this file is the
quick reference the firmware is written against.

## Microcontroller

Seeed **XIAO ESP32** — C3 or C6 (not yet decided). Both share the same 14-pad
layout, so pins below are given by silkscreen D-number and work on either. Set
`board =` in [platformio.ini](platformio.ini) (`seeed_xiao_esp32c6` /
`seeed_xiao_esp32c3`) once the board is chosen.

## Pin map

| Signal | XIAO pad | Direction | Goes to |
|--------|----------|-----------|---------|
| I2C SDA | D4 | — | desk-box cable → MPR121 SDA |
| I2C SCL | D5 | — | desk-box cable → MPR121 SCL |
| Dimmer `Z-C` | D2 | in (GPIO ISR) | dimmer module Z-C |
| Dimmer `DIM` | D3 | out | dimmer module DIM |
| 3V3 | 3V3 | — | dimmer module VCC + desk-box cable VIN |
| GND | GND | — | dimmer module GND + desk-box cable GND |

I2C runs at **100 kHz** (`Wire.setClock(100000)`) for reliability over the 3–5 ft
inter-box cable. MPR121 address **0x5A** (ADDR tied to GND, default).

## Inter-box cable (floor box → desk box)

Single 4-conductor cable, ferrite bead clamped at each enclosure exit:

| Wire | Signal |
|------|--------|
| 1 | VIN — 3.3 V from XIAO |
| 2 | GND |
| 3 | SDA |
| 4 | SCL |

## Dimmer module (RobotDyn / rbdimmer family, BT136S TRIAC, 4 A)

Microcontroller side — galvanically isolated from mains by the onboard opto:

| Pin | Direction | To XIAO |
|-----|-----------|---------|
| GND | — | GND |
| VCC | — | 3V3 |
| Z-C | module → MCU | D2 |
| DIM | MCU → module | D3 |

Firmware uses `rbdimmerESP32` with `RBDIMMER_CURVE_LOGARITHMIC` (dimmable-LED
bulb). Mains frequency is auto-detected (`rbdimmer_register_zero_cross(pin,0,0)`).

## Touch panel (Adafruit MPR121, #1982)

Cable side: `VIN, GND, SDA, SCL` only. `3Vo`, `IRQ`, `ADDR` unused (polled, not
interrupt-driven).

| MPR121 channel | Pad | Position |
|----------------|-----|----------|
| ELE0 | A | left |
| ELE2 | B | middle |
| ELE4 | C | right |

Non-adjacent channels chosen for solder clearance at the header. Each copper pad
is a single-wire connection to its channel — no per-pad ground.

Gestures (all multi-pad AND, for TRIAC-noise rejection):

| Touch | Action |
|-------|--------|
| A + B + C | toggle on/off (fade to 0 on off, restore last level on on) |
| hold A + B | brightness up |
| hold B + C | brightness down |

`B` alone does nothing. A three-pad touch never registers as a two-pad ramp, and
releasing one finger from a three-pad touch is drained, not read as a ramp —
see `lib/GestureFsm/`.

## Mains wiring rules (floor box)

Copied verbatim from the build spec — **hot is the only conductor ever switched**:

- Hot path: wall plug → resettable breaker (3 A) → dimmer `AC-L IN` →
  (dimmer switches) → `AC-L LOAD` → outlet pigtail black lead.
- **Neutral** runs straight through, wall plug → outlet pigtail white lead,
  untouched by switching. A tap off this run (WAGO 221) feeds the dimmer module's
  `AC-N` pin as a zero-cross voltage reference only — not a load path.
- **Ground** runs straight through, wall plug → outlet pigtail green lead.
- Recommended: upstream inline GFCI adapter between wall outlet and this device.

## Tuning

MPR121 thresholds, minimum-brightness floor, and ramp step are tuned live (and
persisted to NVS) from the device's web page: `http://living-room-lamp.local.solace.org/`.
The spec is explicit that MPR121 thresholds must be set empirically once the
copper pads are mounted — bench values on the bare board do not transfer.
