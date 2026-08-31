# TODO

## Firmware

* Bench-test the dimmer chain: XIAO-C6 → RobotDyn module → real bulb. Confirm
  `rbdimmerESP32` drives the DIM pin and phase-cuts (scope or visible dimming)
  **before** anything gets encapsulated.
* First flash over USB with mains disconnected (the `5V` pad ties to USB VBUS).
  Everything after that is OTA (`pio run -e xiao_ota -t upload`).
* Tune MPR121 `setThresholds()` on the mounted copper pads via the web page
  (`http://living-room-lamp.local.solace.org/`) — bench values on the bare
  board do not transfer.
* Feel-test and set the minimum-brightness floor and ramp step from the same
  page; they persist to NVS.
* Merge `firmware-initial` → `main` once the above checks out.

## Floor box

* Solder the RobotDyn dimmer module to the perfboard.
* Solder the XIAO-C6 to the perfboard.
* Confirm parts: 5-port WAGO 221-415 for the neutral node (neutral-in,
  neutral-out, dimmer `AC-N`, HDR-15-5 `N`).
* Breaker (3A resettable) in series with incoming hot.
* HDR-15-5: power it with the DC output unloaded, meter `+V`/`-V` for ~5.0 V,
  leave the trimmer alone, then wire `+V`→XIAO `5V`, `-V`→XIAO `GND`.
* HDR-15-5 AC in: `L` from hot after the breaker but before the dimmer's
  `AC-L IN` (un-switched); `N` from the neutral run; `⏚` to the ground run.
* Hot path: plug → breaker → dimmer `AC-L IN` → `AC-L LOAD` → pigtail black.
* Neutral and ground straight through to the pigtail.
* No heatsink on the dimmer (one LED bulb ≈ 0.2 W in the TRIAC).
* Encapsulate the dimmer module: hot-glue the back — insulation coat first
  (cover every AC terminal joint), then a mounting coat onto scuffed acrylic.
  Barrier or coat the component side if it ends up facing the perfboard.
* Mount XIAO/perfboard and the HDR-15-5 (adhesive / zip-tie — no DIN rail).
* Wire `Wire` I2C (D4/D5), dimmer `Z-C` (D2) / `DIM` (D3), 3V3 + GND to the
  desk-box cable.

## Desk box

* Solder ~50 mm wires to the back of each copper pad, then mount pads over the
  pass-through holes (joint hidden once assembled).
* Pads A/B/C → MPR121 ELE0 / ELE2 / ELE4.
* MPR121: `VIN, GND, SDA, SCL` to the 4-conductor cable. `ADDR` to GND (0x5A).

## Inter-box

* 4-conductor cable, floor → desk, ~3–5 ft: VIN / GND / SDA / SCL.
* Ferrite bead clamped at each enclosure exit.

## Enclosures

* Design + cut the floor box and desk box (no boxes.py URLs yet).

## Before first power-on

* Upstream inline GFCI adapter between wall outlet and this device.
* Visual check: no exposed mains metal touching anything; module back fully
  covered; strain relief on the input cord and the pigtail.
