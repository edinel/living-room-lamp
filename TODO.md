# TODO

## Firmware

* ~~Bench: firmware boots, WiFi/MQTT/HA/web all up, MPR121 reads (bit-bang I2C —
  hardware I2C is broken on the C6, see hardware.md), gestures work.~~ ✓
* ~~Self-powered from the HDR-15-5 on real mains: boots, Z-C locks at 60 Hz.~~ ✓
  (Got here via a real wiring bug — HDR-15-5 `L` was tapped off the dimmer's
  *switched* output instead of the line side; see hardware.md's "Build
  pitfall.")
* Bulb in the socket: confirm `rbdimmerESP32` actually phase-cuts and dims —
  incandescent first if available (cleanest test), then the real dimmable LED.
  Watch for flicker/buzz/warmth. This is the one thing still unverified.
* Tune MPR121 thresholds on the *mounted* copper pads via the web page
  (`http://living-room-lamp.local.solace.org/`) — bench values on the bare
  board do not transfer.
* Feel-test and set the minimum-brightness floor and ramp step from the same
  page; they persist to NVS.
* Drop `-DARDUINO_USB_CDC_ON_BOOT=1` / the wait-for-Serial once bring-up is done
  (optional — harmless to keep).
* Merge `firmware-initial` → `main` once the dimmer is verified.

## Floor box

* ~~Solder the RobotDyn dimmer module to the perfboard.~~ ✓
* ~~Solder the XIAO-C6 to the perfboard.~~ ✓
* ~~HDR-15-5 wired in, powering the XIAO from mains.~~ ✓ — `L` must splice off
  the **line side** (breaker → dimmer `AC-L IN`), not the dimmer's switched
  output; see hardware.md if rebuilding this.
* Hot path: plug → breaker → dimmer `AC-L IN` → `AC-L LOAD` → pigtail black.
* Neutral and ground straight through to the pigtail.
* No heatsink on the dimmer (one LED bulb ≈ 0.2 W in the TRIAC).
* If I2C is flaky over the desk cable: add 4.7 kΩ SDA/SCL pull-ups to 3V3 at the
  floor-box end (MPR121's onboard 10 kΩ is marginal for a 3–5 ft run).
* Encapsulate the dimmer module: hot-glue the back — insulation coat first
  (cover every AC terminal joint), then a mounting coat onto scuffed acrylic.
  Barrier or coat the component side if it ends up facing the perfboard.
* Mount XIAO/perfboard and the HDR-15-5 (adhesive / zip-tie — no DIN rail).
* ~~Wire the dimmer logic header: `VCC`→3V3, `GND`→GND, `Z-C`→D2, `DIM`→D3.~~ ✓
* Solder short tails from the perfboard (3V3 / GND / D0=SDA / D1=SCL) into a
  labelled 4-circuit inline lever splice (SPL-4): channel 1 = 3V3/red,
  2 = GND/black, 3 = SDA/white, 4 = SCL/yellow. Write the map on the connector
  body — it isn't keyed, and red↔black swapped kills the MPR121 (white↔yellow
  is harmless).
* Hold off on the desk cable itself until the run is measured with the boxes in
  their final spots, then lever it into the other side of the splice. Cable
  colours: red=3V3, black=GND, white=SDA, yellow=SCL (house I2C convention).

## Desk box

* Solder ~50 mm wires to the back of each copper pad, then mount pads over the
  pass-through holes (joint hidden once assembled).
* Pads A/B/C → MPR121 ELE0 / ELE2 / ELE4.
* MPR121: `VIN, GND, SDA, SCL` to the 4-conductor cable. `ADDR` to GND (0x5A).

## Inter-box

* 4-conductor cable, floor → desk: VIN / GND / SDA / SCL. Length TBD — measure
  with both boxes placed before cutting; spec assumes ~3–5 ft.
* Ferrite bead clamped at each enclosure exit.

## Enclosures

* Design + cut the floor box and desk box (no boxes.py URLs yet).

## Before first power-on

* Upstream inline GFCI adapter between wall outlet and this device.
* Visual check: no exposed mains metal touching anything; module back fully
  covered; strain relief on the input cord and the pigtail.
