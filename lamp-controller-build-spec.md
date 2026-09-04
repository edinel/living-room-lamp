# Touch-Controlled Dimmable Lamp Controller — Build Spec

## Overview

A pluggable, inline lamp controller: plugs into the wall, lamp plugs into it.
Touch-based on/off + dimming via a remote capacitive touch panel. Built around
an ESP32, an AC phase-cut dimmer module, and an Adafruit MPR121 capacitive
touch breakout. Two physical enclosures connected by one 4-conductor cable.

**Firmware target: Arduino (not ESPHome).**

---

## Physical layout — two enclosures

### Floor box (mains-adjacent, on the floor near the outlet)
Contains:
- Seeed XIAO ESP32-C6 (developer has prior ESP32 experience). Powered at its
  `5V` pad from the AC-DC supply below.
- AC-DC power supply: Mean Well **HDR-15-5** (5V 2.4A 12W, 85-264VAC in),
  ultra-slim DIN-rail brick, all connections on screw terminals. Fastened to
  the enclosure floor (short DIN-rail stub, or bonded/zip-tied directly — no
  rail required). Factory-set 5.0V; verify with a meter before connecting the
  XIAO, don't adjust the trimmer.
- AC Dimmer Module, 4A, RobotDyn/rbdimmer-family (phase-cut TRIAC dimmer,
  TRIAC part BT136S), with heatsink adhered via non-conductive thermally
  conductive epoxy
- Push-button resettable circuit breaker (3A, 125-250VAC) in place of a fuse,
  in series with incoming hot
- WAGO lever connector on the neutral node (221 series, 5-port / 221-415 —
  carries neutral-in, neutral-out to the pigtail, the dimmer module's AC-N
  zero-cross reference, and the HDR-15-5's N; one spare port). A 3-port 221 on
  the ground node (in, out, HDR-15-5 `⏚`).
- Panel-mount female receptacle pigtail (15A/125VAC, pre-wired black/white/
  green leads) — this is what the lamp plugs into
- Input: cut-down cord with molded male plug (this is what plugs into the wall)

Wiring rules (mains side):
- **Hot only** ever gets switched/dimmed. Hot path: wall plug → breaker →
  dimmer module `AC-L IN` → (dimmer switches) → `AC-L LOAD` → outlet pigtail
  black lead.
- **Neutral** runs straight through from wall plug to outlet pigtail white
  lead, untouched by switching. A tap off this same neutral run (via the
  WAGO connector) feeds the dimmer module's `AC-N` pin — this is a zero-cross
  reference only, not a switched/load path.
- **Ground** runs straight through from wall plug to outlet pigtail green
  lead, untouched.
- **5V supply (HDR-15-5)** taps the *un-switched* side: its `L` from the hot
  after the breaker but before the dimmer's `AC-L IN`; its `N` from the
  straight-through neutral run (via the same WAGO 221 as the dimmer's `AC-N`);
  its `⏚` to the ground run. It must stay powered whether the lamp is on or
  off. The 5V/GND output is isolated SELV — ordinary hookup wire to the XIAO
  `5V` pad; the XIAO's onboard regulator makes 3.3V for the MPR121 and the
  dimmer module's logic side.
- Recommended: use with an upstream inline GFCI adapter (plugs between wall
  outlet and this device's input plug).

### Desk box (remote, sits on the desk near the user)
Contains:
- Adafruit MPR121 12-Key Capacitive Touch Sensor Breakout (product #1982)
- Three copper touch pads, mounted flush on the *exterior* of the enclosure.
  Wires are soldered to the back of each pad before mounting, then the pad is
  affixed over a wood pass-through hole — joint is fully hidden/protected
  once assembled. ~50mm of wire from each pad back to the MPR121 board.

---

## Inter-box wiring: single 4-conductor cable

Carries I2C + power from the floor box (ESP32) up to the desk box (MPR121),
approx. 3-5 feet:

| Wire | Signal |
|------|--------|
| 1 | VIN (3.3V from ESP32) |
| 2 | GND |
| 3 | SDA |
| 4 | SCL |

Notes for firmware/reliability:
- Run I2C at standard-mode speed for reliability over this distance:
  `Wire.setClock(100000);`
- Ferrite beads are clamped on the cable at both enclosure exit points
  (physical mitigation, no firmware implication).
- Default MPR121 I2C address: **0x5A** (ADDR pin tied to GND on the board,
  unmodified default).

---

## Dimmer module pinout (microcontroller side)

4 pins, galvanically isolated from mains side via onboard optocoupler:

| Pin | Direction | Purpose |
|-----|-----------|---------|
| GND | — | ground, shared with ESP32 |
| VCC | — | power, 3.3V from ESP32 |
| Z-C | Module → ESP32 | zero-cross detection signal |
| DIM | ESP32 → Module | TRIAC gate/trigger signal |

Library: **rbdimmerESP32** (Arduino library for this dimmer family).
Curve type: `RBDIMMER_CURVE_RMS` if final bulb is incandescent,
`RBDIMMER_CURVE_LOGARITHMIC` if final bulb is a dimmable-rated LED.

---

## Touch sensor pinout (MPR121)

Wires needed from MPR121 to ESP32 (over the 4-conductor cable above):
`VIN, GND, SDA, SCL`. (`3Vo` and `IRQ` and `ADDR` are unused — polling
`cap.touched()` in the main loop, not interrupt-driven.)

Electrode channels used — chosen non-adjacent (0, 2, 4) deliberately, to
leave physical soldering clearance between pad wires on the header. Pads are
physically arranged in a row (A, B, C) on the desk box, so that adjacent
pairs are natural to touch together with one or two fingers:

| Channel | Pad label |
|---------|-----------|
| ELE0 | Pad A (left) |
| ELE2 | Pad B (middle) |
| ELE4 | Pad C (right) |

Control is via **multi-pad AND combinations**, not single-pad tap/hold. This
is a deliberate noise-rejection strategy: requiring two or three channels to
report "touched" at the same instant makes it far less likely that a
spurious noise glitch (e.g. coupled in from the TRIAC's switching) falsely
triggers an action, at the cost of requiring a slightly more deliberate touch
from the user. All three channels are read in one I2C transaction per poll,
so there's no timing-window concern combining them.

| Gesture | Channels required (AND) | Action |
|---------|--------------------------|--------|
| Touch all three pads (A+B+C) | ELE0 & ELE2 & ELE4 | Toggle power on/off |
| Touch A+B | ELE0 & ELE2 | Brightness up (hold to ramp) |
| Touch B+C | ELE2 & ELE4 | Brightness down (hold to ramp) |

Note pad B (ELE2) participates in every gesture — that's expected and fine,
since the gestures are distinguished by *which combination* is active, not
by any single pad in isolation. A touch on B alone (no A or C) should map to
no action.

No ground connection is needed on the individual copper pads — each pad is a
single-wire connection to its electrode channel. (There is a spare GND pin on
the MPR121 board adjacent to the electrode header; it is electrically the
same net as the board's main GND and is not needed for this build.)

`setThresholds()` values must be empirically tuned once the physical copper
pads are wired and mounted — do not trust placeholder values from testing on
the bare onboard pads.

---

## Desired firmware behavior

- **Quick touch of all three pads (A+B+C)** → toggle lamp on/off. When
  turning on, restore last remembered brightness level. When turning off,
  fade to 0 (short transition, e.g. ~300ms) rather than an instant cut.
- **Hold A+B** → brightness ramps up smoothly while both are held, stops at
  100.
- **Hold B+C** → brightness ramps down smoothly while both are held. Reaching
  a reasonable minimum and continuing to hold turns the lamp off (via the same
  fade-to-0 path as the A+B+C toggle) — like the click-off at the bottom of a
  rotary dimmer's travel. (Revised from an earlier "ramp never goes below the
  floor, only the toggle turns it off" — rejected because it left the lamp
  logically "on" at a floor brightness with no way to get to off except a
  different, three-finger gesture; the click-off matches how a physical
  dimmer actually behaves and needs no path-of-its-own for turning off.)
- Because B participates in both ramp gestures, use the *current* combined
  touch state each poll to decide which gesture (if any) is active — e.g.
  check "all three" first, then "A+B only", then "B+C only", so a
  three-pad touch never gets misread as also satisfying a two-pad gesture.
- Debounce the toggle so a held all-three-pad touch doesn't repeatedly fire
  — toggle should fire once per distinct touch event (on the transition into
  the "all three touched" state), not continuously while held.
- Persist last brightness level across on/off toggles for the session (RAM is
  fine — does not need to survive power loss unless that turns out to be
  desired later).

---

## Parts/components already on hand (for reference, not purchasing)

- Seeed XIAO ESP32-C6
- Mean Well HDR-15-5 DIN-rail AC-DC supply (5V 2.4A 12W)
- AC Dimmer Module 4A (rbdimmer/RobotDyn-compatible family)
- Adafruit MPR121 breakout (#1982)
- Push-button resettable breaker, 3A 125-250VAC
- WAGO 221-series lever connectors (incl. a 5-port 221-415 for the neutral node)
- Panel-mount outlet pigtail (15A/125VAC)
- Copper hexagon touch pads (solid copper stock)
- SSR-25DA — purchased but **not used in this build**; reserved for a future
  project (it's zero-cross on/off switching only, not phase-cut capable, so
  it can't do dimming)

---

## Open items / things to verify during build, not yet resolved in code

- Actual `setThresholds()` values for the three copper pads (tune empirically)
- Actual GPIO pin numbers on the specific ESP32 board in use for I2C
  (SDA/SCL) and for the dimmer's Z-C/DIM pins — not yet assigned
- Bulb type for the final build (incandescent vs. dimmable LED) — determines
  dimmer curve type
- Minimum brightness floor value for the "dim down" ramp
