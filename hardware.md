# Living Room Lamp — Hardware

Wiring record for the firmware in this repo. The full build rationale (enclosure
layout, mains-safety reasoning, gesture design) lives in
[lamp-controller-build-spec.md](lamp-controller-build-spec.md); this file is the
quick reference the firmware is written against.

## Microcontroller

Seeed **XIAO ESP32-C6** (`board = seeed_xiao_esp32c6`). Pins below are given by
silkscreen D-number.

## Pin map

| Signal | XIAO pad | Direction | Goes to |
|--------|----------|-----------|---------|
| I2C SDA | D0 | — | desk-box cable → MPR121 SDA |
| I2C SCL | D1 | — | desk-box cable → MPR121 SCL |
| Dimmer `Z-C` | D2 | in (GPIO ISR) | dimmer module Z-C |
| Dimmer `DIM` | D3 | out | dimmer module DIM |
| 5V | 5V | in | HDR-15-5 `+V` (5.0 V) |
| 3V3 | 3V3 | out | dimmer module VCC + desk-box cable VIN |
| GND | GND | — | HDR-15-5 `−V` + dimmer module GND + desk-box cable GND |

The XIAO is powered at its `5V` pad from the HDR-15-5; its onboard regulator
supplies the `3V3` pad that feeds the MPR121 (over the cable) and the dimmer
module's logic side. Do **not** connect USB while the HDR-15-5 is live — the `5V`
pad ties straight to USB VBUS. First flash on the bench with mains disconnected;
everything after is OTA.

I2C runs at **100 kHz** (`Wire.setClock(100000)`) for reliability over the 3–5 ft
inter-box cable. MPR121 address **0x5A** (ADDR tied to GND, default).

## Power supply (Mean Well HDR-15-5)

5V 2.4A DIN-rail AC-DC brick, all screw terminals, ~90 × 17.5 × 55 mm. Fasten to
the enclosure floor — DIN-rail stub or bonded/zip-tied directly, no rail needed.

| HDR-15-5 terminal | Connects to |
|-------------------|-------------|
| `L` | hot, after the 3A breaker, before the dimmer's `AC-L IN` — a new splice on the **line (un-switched) side**, not a tap off the dimmer's output |
| `N` | neutral straight-through run (same WAGO 221 as the dimmer's `AC-N`) |
| `⏚` | ground straight-through run |
| `+V` | XIAO `5V` pad |
| `−V` | XIAO `GND` |

Ships factory-set at 5.0 V. Power it with the DC output unloaded, meter `+V`/`−V`
for ~5.0 V, then connect the XIAO. Leave the trimmer alone. Idle draw <0.1 W; the
XIAO peaks well under 0.5 A, so the 2.4 A rating is large margin.

**Build pitfall (hit during bring-up, cost a debugging session — get this right):**
`L` must splice off hot **between the breaker and the dimmer's `AC-L IN`**, i.e. a
new junction in parallel with the wire that continues to the dimmer — not off the
dimmer's *output* WAGO (the one feeding the pigtail). The dimmer's output is
switched hot: it's only live while the TRIAC is conducting, and firmware boots
with the lamp off, so a supply fed from there gets no power at boot and the
system can never bootstrap (XIAO needs power before it can tell the dimmer to
turn on). The failure signature was: HDR-15-5 never powers up standalone, and
metering `L`↔`N` at its input reads a small non-mains "ghost" voltage (a few
volts, not ~0 or ~120) — the tell that one leg is floating on the dimmer's
switched side rather than genuinely connected to line.

## Inter-box cable (floor box → desk box)

Single 4-conductor cable, ferrite bead clamped at each enclosure exit.
Wire colours follow the house I2C convention (as in `edinel/AirSensor`):

| Colour | Signal | Floor-box end | Desk-box end |
| --- | --- | --- | --- |
| Red | VIN — 3.3 V from XIAO | XIAO `3V3` node | MPR121 `VIN` |
| Black | GND | XIAO `GND` node | MPR121 `GND` |
| White | SDA | XIAO `D0` | MPR121 `SDA` |
| Yellow | SCL | XIAO `D1` | MPR121 `SCL` |

The two ends join at a labelled 4-circuit inline lever splice (SPL-4) in the
floor box (channel 1 = 3V3/red, 2 = GND/black, 3 = SDA/white, 4 = SCL/yellow) so
the desk run can be cut and terminated after the boxes are placed. No
polarisation on the splice — swapping red/black destroys the MPR121, so label
the connector body. (Swapping white/yellow is harmless — the bus just fails to
enumerate until corrected.)

## Dimmer module (RobotDyn / rbdimmer family, BT136S TRIAC, 4 A)

Microcontroller side — galvanically isolated from mains by the onboard opto:

| Pin | Direction | To XIAO |
|-----|-----------|---------|
| GND | — | GND |
| VCC | — | 3V3 |
| Z-C | module → MCU | D2 |
| DIM | MCU → module | D3 |

**All four of these pins are on the optically isolated low-voltage side — none
connect to any AC conductor.** The module senses the mains zero-crossing on its
isolated side and outputs it as a logic pulse on `Z-C`; `DIM` is the gate trigger
back to the module. VCC is taken from the XIAO `3V3` (not `5V`) so the `Z-C`
output swings 0–3.3 V and stays within the C6's GPIO limit. The only neutral
connection anywhere near the dimmer is its mains-side `AC-N` screw terminal.

Firmware uses `rbdimmerESP32` with `RBDIMMER_CURVE_LOGARITHMIC` (dimmable-LED
bulb). Mains frequency is auto-detected (`rbdimmer_register_zero_cross(pin,0,0)`).

## Touch panel (Adafruit MPR121, #1982)

Cable side: `VIN` (red), `GND` (black), `SDA` (white), `SCL` (yellow) only.
`3Vo`, `IRQ`, `ADDR` unused (polled, not interrupt-driven).

| MPR121 channel | Pad | Position |
|----------------|-----|----------|
| ELE0 | A | left |
| ELE2 | B | middle |
| ELE4 | C | right |

Non-adjacent channels chosen for solder clearance at the header. Each copper pad
is a single-wire connection to its channel — no per-pad ground.

### I2C is bit-banged, not hardware

`lib/TouchPanel` drives the MPR121 with a software (GPIO-toggled) I2C
implementation on `D0`/`D1`, **not** `Wire`. The ESP32-C6 hardware I2C (Arduino
core ≥ 3.2, "i2c-ng" driver) returns zeros / `ESP_ERR_INVALID_STATE` on every
register *read* — a known upstream regression,
[espressif/arduino-esp32 #11374](https://github.com/espressif/arduino-esp32/issues/11374).
Address-ACK and writes work; reads do not. Confirmed on this board with a boot
probe (writes ACKed, `CONFIG1`/`CONFIG2` reads came back `0x00`). The only fix
from Espressif's side is core 3.1.3, which would drag the whole platform (and the
rbdimmer / PsychicHttp builds) backwards, so the workaround lives in `TouchPanel`
instead. Revisit if the driver is ever fixed — `git log` for `bit-bang`.

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
