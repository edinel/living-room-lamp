// Living Room Lamp — touch-controlled dimmable AC lamp controller.
// See lamp-controller-build-spec.md and hardware.md for wiring.
//
// Floor box: XIAO ESP32 + RobotDyn phase-cut dimmer, mains side.
// Desk box:  Adafruit MPR121 + three copper touch pads, on a 4-conductor cable.
//
// Gestures (multi-pad AND, noise-rejecting):
//   A+B+C tap  -> toggle on/off  (fade to 0 on off, restore last level on on)
//   hold A+B   -> brightness up
//   hold B+C   -> brightness down
//
// Home Assistant: MQTT-discovered `light` with brightness. Touch works with or
// without the network. Sensitivity is tuned live at http://living-room-lamp.local.solace.org/

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <PsychicHttp.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "LampDimmer.h"
#include "TouchPanel.h"
#include "arduino_secrets.h"

// Git short SHA this binary was built from — set by scripts/build_id.py.
// Fallback here only so editors/native builds that skip the PIO hook still
// compile; the real firmware always gets the injected value.
#ifndef BUILD_ID
#define BUILD_ID "unknown"
#endif

// ---------------------------------------------------------------------------
// Pin map — XIAO ESP32-C6 silkscreen
// ---------------------------------------------------------------------------
#define PIN_SDA        D0   // I2C to desk box
#define PIN_SCL        D1
#define PIN_DIMMER_ZC  D2   // dimmer module Z-C (input)
#define PIN_DIMMER_DIM D3   // dimmer module DIM (output)

// ---------------------------------------------------------------------------
// Identity / MQTT topics
// ---------------------------------------------------------------------------
#define HOSTNAME              "living-room-lamp"
#define MQTT_CLIENT_ID        "living_room_lamp"

#define TOPIC_DISCOVERY       "homeassistant/light/living_room_lamp/config"
#define TOPIC_STATE           "living_room_lamp/light/state"
#define TOPIC_SET             "living_room_lamp/light/set"
#define TOPIC_BRIGHTNESS_STATE "living_room_lamp/light/brightness/state"
#define TOPIC_BRIGHTNESS_SET   "living_room_lamp/light/brightness/set"

static const unsigned long WIFI_CHECK_MS = 30000;
static const unsigned long MQTT_CHECK_MS =  5000;
static const unsigned long RAMP_STEP_MS  =    40;   // min gap between ramp ticks

// ---------------------------------------------------------------------------
// Persisted tuning config (NVS) — editable from the web page
// ---------------------------------------------------------------------------
struct TuningConfig {
  uint8_t touchThr = TouchPanel::kDefaultTouchThreshold;
  uint8_t relThr   = TouchPanel::kDefaultReleaseThreshold;
  uint8_t minLevel = LampDimmer::kDefaultMinLevel;
  uint8_t rampStep = LampDimmer::kDefaultRampStep;
};

static TuningConfig g_cfg;

static void loadConfig() {
  Preferences p;
  // Read-write so the namespace is created on first boot (no nvs_open NOT_FOUND).
  p.begin("lamp", /*readOnly=*/false);
  g_cfg.touchThr = p.getUChar("touchThr", g_cfg.touchThr);
  g_cfg.relThr   = p.getUChar("relThr",   g_cfg.relThr);
  g_cfg.minLevel = p.getUChar("minLevel", g_cfg.minLevel);
  g_cfg.rampStep = p.getUChar("rampStep", g_cfg.rampStep);
  p.end();
}

static void saveConfig() {
  Preferences p;
  p.begin("lamp", /*readOnly=*/false);
  p.putUChar("touchThr", g_cfg.touchThr);
  p.putUChar("relThr",   g_cfg.relThr);
  p.putUChar("minLevel", g_cfg.minLevel);
  p.putUChar("rampStep", g_cfg.rampStep);
  p.end();
}

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
LampDimmer        lamp;
TouchPanel        touch;
WiFiClient        wifiClient;
PubSubClient      mqtt(wifiClient);
PsychicHttpServer server;

// Set while "preparing for OTA" from the web page: touch and MQTT commands are
// ignored so a flash isn't racing a live gesture or a stray HA command. Not
// persisted — always false on boot.
static bool g_otaMode = false;

// ---------------------------------------------------------------------------
// State setters — single entry point for both touch and MQTT
// ---------------------------------------------------------------------------
static void publishState() {
  if (!mqtt.connected()) return;
  mqtt.publish(TOPIC_STATE, lamp.isOn() ? "ON" : "OFF", /*retain=*/true);
  char buf[4];
  snprintf(buf, sizeof(buf), "%u", lamp.brightness());
  mqtt.publish(TOPIC_BRIGHTNESS_STATE, buf, /*retain=*/true);
}

static void applyOn(bool on) {
  lamp.setOn(on);
  publishState();
}

static void applyBrightness(uint8_t pct) {
  lamp.setBrightness(pct);
  publishState();
}

// ---------------------------------------------------------------------------
// MQTT / Home Assistant
// ---------------------------------------------------------------------------
static void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  if (g_otaMode) return;   // frozen — see g_otaMode

  String msg((char*)payload, length);

  if (strcmp(topic, TOPIC_SET) == 0) {
    if (msg == "ON")       applyOn(true);
    else if (msg == "OFF") applyOn(false);
  } else if (strcmp(topic, TOPIC_BRIGHTNESS_SET) == 0) {
    applyBrightness((uint8_t)constrain(msg.toInt(), 0, 100));
  }
}

static void connectMQTT() {
  log_i("MQTT connecting to %s:%d", MQTT_HOST, MQTT_PORT);
  if (!mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS,
                    TOPIC_STATE, /*qos=*/0, /*retain=*/true, "OFF")) {
    log_w("MQTT connect failed, rc=%d", mqtt.state());
    return;
  }
  log_i("MQTT connected");

  const char* discovery =
    "{"
      "\"name\":\"Living Room Lamp\","
      "\"unique_id\":\"living_room_lamp_light\","
      "\"device\":{"
        "\"identifiers\":[\"living_room_lamp\"],"
        "\"name\":\"Living Room Lamp\","
        "\"model\":\"XIAO ESP32 phase-cut dimmer\","
        "\"manufacturer\":\"DIY\""
      "},"
      "\"state_topic\":\"" TOPIC_STATE "\","
      "\"command_topic\":\"" TOPIC_SET "\","
      "\"brightness_state_topic\":\"" TOPIC_BRIGHTNESS_STATE "\","
      "\"brightness_command_topic\":\"" TOPIC_BRIGHTNESS_SET "\","
      "\"brightness_scale\":100,"
      "\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
      "\"optimistic\":false"
    "}";
  mqtt.publish(TOPIC_DISCOVERY, discovery, /*retain=*/true);

  mqtt.subscribe(TOPIC_SET);
  mqtt.subscribe(TOPIC_BRIGHTNESS_SET);
  publishState();
}

// ---------------------------------------------------------------------------
// WiFi / MQTT watchdogs — rate-limited, non-blocking (global CLAUDE.md pattern)
// ---------------------------------------------------------------------------
static void checkWiFi() {
  static unsigned long last = 0;
  unsigned long now = millis();
  if (now - last < WIFI_CHECK_MS) return;
  last = now;

  if (WiFi.status() == WL_CONNECTED) return;

  log_w("WiFi disconnected — reconnecting");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

static void checkMQTT() {
  static unsigned long last = 0;
  unsigned long now = millis();
  if (now - last < MQTT_CHECK_MS) return;
  last = now;

  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  connectMQTT();
}

// ---------------------------------------------------------------------------
// Web server — live MPR121 readout + tuning form
// ---------------------------------------------------------------------------
static const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Living Room Lamp</title>
<style>body{font-family:system-ui;margin:1.5rem;max-width:34rem}
table{border-collapse:collapse;width:100%;margin:1rem 0}td,th{border:1px solid #ccc;padding:.3rem .6rem;text-align:right}
th:first-child,td:first-child{text-align:left}label{display:block;margin:.6rem 0}
input{width:5rem}.on{color:#0a0;font-weight:bold}.bad{color:#c00;font-weight:bold}
#otaBanner{display:none;background:#fee;border:1px solid #c00;border-radius:.4rem;padding:.6rem 1rem;margin:1rem 0}
</style></head><body>
<h1>Living Room Lamp</h1>
<p style="opacity:.55;font-size:.8rem;margin-top:-.6rem">build <span id="build"></span> ✓</p>
<p>Lamp: <span id="lamp"></span> &nbsp; Gesture state: <b id="fsm"></b> &nbsp; Mains: <span id="hz"></span>
&nbsp; Z-C pulses: <span id="zc"></span> &nbsp; WiFi: <span id="rssi"></span></p>
<div id="otaBanner">🛠 <b>OTA mode</b> — touch and remote control are frozen. Flash now, or
<button id="otaExit" type="button">cancel</button></div>
<table><thead><tr><th>Pad</th><th>filtered</th><th>baseline</th><th>touched</th></tr></thead>
<tbody id="pads"></tbody></table>
<form id="cfg">
<label>Touch threshold <input name="touchThr" type="number" min="1" max="255"></label>
<label>Release threshold <input name="relThr" type="number" min="1" max="255"></label>
<label>Min brightness (%) <input name="minLevel" type="number" min="1" max="90"></label>
<label>Ramp step (%) <input name="rampStep" type="number" min="1" max="25"></label>
<button>Save</button> <span id="saved"></span>
</form>
<p><button id="otaEnter" type="button">Prepare for OTA update</button></p>
<script>
const $=s=>document.querySelector(s);
let lastZc=null;
function applyCfg(cfg){
 for(const k of ['touchThr','relThr','minLevel','rampStep']) $('[name='+k+']').value=cfg[k];
}
// Status (lamp/pads/fsm) polls every 400ms. Config fields are NOT re-synced
// here — doing so fights the number-input spin buttons (a spinner click
// doesn't reliably count as "focused" before the next poll lands, so the
// field snaps back to the old value before Save can fire). They're loaded
// once at page load and once after a Save response instead.
async function tick(){
 const s=await (await fetch('/api/status')).json();
 $('#lamp').innerHTML=s.on?'<span class=on>ON '+s.brightness+'%</span>':'off';
 $('#fsm').textContent=s.fsm;
 $('#hz').innerHTML=s.mainsHz?s.mainsHz+' Hz':'<span class=bad>not detected</span>';
 const dz=lastZc==null?0:s.zcPulses-lastZc; lastZc=s.zcPulses;
 $('#zc').innerHTML=s.zcPulses+(dz>0?' <span class=on>(+'+dz+')</span>':' <span class=bad>(idle)</span>');
 $('#rssi').textContent=s.rssi+' dBm';
 $('#pads').innerHTML=s.pads.map(p=>`<tr><td>${p.name}</td><td>${p.filtered}</td><td>${p.baseline}</td><td>${p.touched?'YES':'-'}</td></tr>`).join('');
 $('#otaBanner').style.display=s.otaMode?'block':'none';
}
$('#cfg').onsubmit=async e=>{e.preventDefault();
 const b={};
 for(const [k,v] of new FormData(e.target)) b[k]=Number(v);   // FormData values are strings — send real JSON numbers
 const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});
 const j=await r.json();
 if(j.cfg) applyCfg(j.cfg);   // reflect whatever the server actually stored (incl. clamping)
 $('#saved').textContent='saved';setTimeout(()=>$('#saved').textContent='',1500);
};
async function setOtaMode(on){
 await fetch('/api/ota-mode',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({enabled:on})});
 tick();
}
$('#otaEnter').onclick=()=>setOtaMode(true);
$('#otaExit').onclick=()=>setOtaMode(false);
(async()=>{ const s=await (await fetch('/api/status')).json(); applyCfg(s.cfg); $('#build').textContent=s.buildId; })();
tick();setInterval(tick,400);
</script></body></html>)HTML";

static String cfgJson() {
  return "{\"touchThr\":" + String(g_cfg.touchThr) +
         ",\"relThr\":"   + String(g_cfg.relThr) +
         ",\"minLevel\":" + String(g_cfg.minLevel) +
         ",\"rampStep\":" + String(g_cfg.rampStep) + "}";
}

static void sendStatusJson(PsychicResponse* response) {
  const uint16_t touched = touch.touchedMask();
  const struct { const char* name; uint8_t ch; } pads[] = {
    {"A", TouchPanel::kChanA}, {"B", TouchPanel::kChanB}, {"C", TouchPanel::kChanC},
  };

  String j = "{";
  j += "\"buildId\":\"" + String(BUILD_ID) + "\"";
  j += ",\"on\":" + String(lamp.isOn() ? "true" : "false");
  j += ",\"brightness\":" + String(lamp.brightness());

  j += ",\"fsm\":\"" + String(toString(touch.state())) + "\"";
  j += ",\"mainsHz\":" + String(lamp.mainsHz());
  j += ",\"zcPulses\":" + String(lamp.zcPulses());
  j += ",\"rssi\":" + String(WiFi.RSSI());
  j += ",\"otaMode\":" + String(g_otaMode ? "true" : "false");

  j += ",\"pads\":[";
  for (size_t i = 0; i < 3; i++) {
    if (i) j += ",";
    j += "{\"name\":\"" + String(pads[i].name) + "\"";
    j += ",\"filtered\":" + String(touch.filtered(pads[i].ch));
    j += ",\"baseline\":" + String(touch.baseline(pads[i].ch));
    j += ",\"touched\":" + String((touched >> pads[i].ch) & 1);
    j += "}";
  }
  j += "]";

  j += ",\"cfg\":" + cfgJson();
  j += "}";

  response->send(200, "application/json", j.c_str());
}

static uint8_t jsonU8(const String& body, const char* key, uint8_t fallback,
                      uint8_t lo, uint8_t hi) {
  int k = body.indexOf(String("\"") + key + "\"");
  if (k < 0) return fallback;
  int colon = body.indexOf(':', k);
  if (colon < 0) return fallback;
  int start = colon + 1;
  while (start < (int)body.length() && (body[start] == ' ' || body[start] == '"')) start++;
  long v = body.substring(start).toInt();   // String::toInt() stops at the first non-digit
  return (uint8_t)constrain(v, (long)lo, (long)hi);
}

static bool jsonBool(const String& body, const char* key, bool fallback) {
  int k = body.indexOf(String("\"") + key + "\"");
  if (k < 0) return fallback;
  int colon = body.indexOf(':', k);
  if (colon < 0) return fallback;
  int start = colon + 1;
  while (start < (int)body.length() && body[start] == ' ') start++;
  return body.startsWith("true", start);
}

static void registerWebRoutes() {
  server.on("/", HTTP_GET, [](PsychicRequest* request, PsychicResponse* response) {
    return response->send(200, "text/html", PAGE_HTML);
  });

  server.on("/api/status", HTTP_GET, [](PsychicRequest* request, PsychicResponse* response) {
    sendStatusJson(response);
    return ESP_OK;
  });

  server.on("/api/config", HTTP_POST, [](PsychicRequest* request, PsychicResponse* response) {
    String body = request->body();
    g_cfg.touchThr = jsonU8(body, "touchThr", g_cfg.touchThr, 1, 255);
    g_cfg.relThr   = jsonU8(body, "relThr",   g_cfg.relThr,   1, 255);
    g_cfg.minLevel = jsonU8(body, "minLevel", g_cfg.minLevel, 1, 90);
    g_cfg.rampStep = jsonU8(body, "rampStep", g_cfg.rampStep, 1, 25);

    touch.setThresholds(g_cfg.touchThr, g_cfg.relThr);
    lamp.setConfig(g_cfg.minLevel, g_cfg.rampStep);
    saveConfig();

    String resp = "{\"ok\":true,\"cfg\":" + cfgJson() + "}";
    return response->send(200, "application/json", resp.c_str());
  });

  server.on("/api/ota-mode", HTTP_POST, [](PsychicRequest* request, PsychicResponse* response) {
    String body = request->body();
    bool wasOta = g_otaMode;
    g_otaMode = jsonBool(body, "enabled", g_otaMode);

    if (g_otaMode && !wasOta) {
      applyOn(false);           // known-off state before a flash
      lamp.shutdownForOTA();    // stop rbdimmer's zero-cross ISR — see LampDimmer.h
      log_w("OTA mode ENTERED — touch/MQTT frozen, dimmer shut down");
      return response->send(200, "application/json", "{\"ok\":true}");
    }

    if (!g_otaMode && wasOta) {
      // rbdimmer was torn down entering OTA mode and can't be safely
      // re-initialized in place — reboot for a clean restart instead.
      log_w("OTA mode cancelled — rebooting to restore the dimmer");
      esp_err_t res = response->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
      delay(200);
      ESP.restart();
      return res;
    }

    return response->send(200, "application/json", "{\"ok\":true}");
  });
}

// ---------------------------------------------------------------------------
// OTA
// ---------------------------------------------------------------------------
static void configureOTA() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.onStart([]() { log_i("OTA start"); });
  ArduinoOTA.onEnd([]()   { log_i("OTA done — rebooting"); });
  ArduinoOTA.onError([](ota_error_t e) { log_e("OTA error %u", e); });
}

// ---------------------------------------------------------------------------
// Bring up the HTTP server + OTA once WiFi has an interface; the async HTTP
// server can't bind before then. Re-arm on reconnect (Jessa-Bedside pattern).
// ---------------------------------------------------------------------------
static void ensureNetServices() {
  static bool up = false;
  const bool connected = (WiFi.status() == WL_CONNECTED);
  if (connected && !up) {
    server.begin();
    ArduinoOTA.begin();
    up = true;
    log_i("Net services up on %s (%s)", HOSTNAME, WiFi.localIP().toString().c_str());
  } else if (!connected && up) {
    up = false;   // server.begin() / ArduinoOTA.begin() re-run on reconnect
  }
}

// ---------------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) delay(10);   // let the USB-CDC host attach

  log_i("Living Room Lamp — build %s", BUILD_ID);

  loadConfig();

  if (!lamp.begin(PIN_DIMMER_ZC, PIN_DIMMER_DIM))
    log_e("dimmer init failed — lamp control unavailable");
  lamp.setConfig(g_cfg.minLevel, g_cfg.rampStep);

  // Bit-banged I2C on D0/D1 — the ESP32-C6 hardware I2C driver can't read
  // (arduino-esp32 #11374). See lib/TouchPanel.
  if (!touch.begin(PIN_SDA, PIN_SCL, 0x5A))
    log_e("touch panel init failed — touch control unavailable");
  touch.setThresholds(g_cfg.touchThr, g_cfg.relThr);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  // Modem sleep saves power between beacons at the cost of occasional latency
  // spikes — fine for MQTT/HTTP, fatal to a sustained OTA transfer (manifests
  // as a mid-flash "Connection reset by peer"). This device is always
  // mains-powered, so there's no reason to trade reliability for it.
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(768);
  mqtt.setCallback(onMqttMessage);

  registerWebRoutes();
  configureOTA();
}

static void rampTick(int8_t dir) {
  static unsigned long last = 0;
  unsigned long now = millis();
  if (now - last < RAMP_STEP_MS) return;
  last = now;
  lamp.nudge(dir);
  publishState();
}

void loop() {
  checkWiFi();
  ensureNetServices();
  ArduinoOTA.handle();
  checkMQTT();
  mqtt.loop();   // onMqttMessage() self-guards on g_otaMode

  // OTA mode: lamp is already off (see /api/ota-mode), touch is frozen so a
  // stray gesture can't do anything while a flash is imminent/in progress.
  if (g_otaMode) return;

  switch (touch.poll()) {
    case Gesture::Toggle:   applyOn(!lamp.isOn());          break;
    case Gesture::RampUp:   if (lamp.isOn()) rampTick(+1);  break;
    case Gesture::RampDown: if (lamp.isOn()) rampTick(-1);  break;
    case Gesture::None:                                     break;
  }
}
