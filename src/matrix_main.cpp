/*
 * ESP32-S3-Matrix — laser-tag target (V2 control plane).
 *
 * Idles showing a flowing rainbow on the on-board 8x8 WS2812 matrix. When a
 * Vatos shot is received it applies damage locally (the device is authoritative
 * for its own health — design §6), flashes the firing team's colour, then goes
 * dark for a random interval ("stunned"), then resumes. Reaching 0 hp holds the
 * device "dead" until a respawn / CTL reset restores it.
 *
 * Control plane (contract 2026-06-15):
 *   - Heartbeat (HB) broadcast every 2 s with id/ip/fw/team/mode/hp.
 *   - Structured EVT hit / EVT state telemetry replaces the ad-hoc hit/dark.
 *   - Inbound CTL start/stop/reset handled via TagNet's onLine (UDP/serial/HTTP).
 *   - REST /api/* surface served over HTTP; ConfigDoc persisted in NVS.
 *
 * All wire formatting/parsing lives in the pure ControlProto library (unit
 * tested natively); this file owns policy: hp accounting, the visual<->state
 * mapping, NVS, and the device's IR/LED behaviour.
 *
 * Wiring: IR receiver (VS1838B) OUT -> GPIO1; matrix data is GPIO14 (on-board).
 */

#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>
#include <WiFi.h>
#include <driver/gpio.h>

#include <ControlProto.h>
#include <IrFramer.h>
#include <TagNet.h>
#include <Vatos.h>

namespace cp = ControlProto;

// Firmware version reported on the wire (contract §1.3 fw=).
static const char *kFirmwareVersion = "2.0.0";

// On-board 8x8 WS2812 matrix data pin
#define MATRIX_PIN 14
#define NUM_LEDS 64

// IR receiver output (free header pin; GPIO10-14 are taken by IMU/matrix)
#define IR_PIN 1

// Activity LED: pulses on every received IR frame. Active-high, resistor-less,
// so driven at minimum drive strength (~5mA) to protect the pin.
#define ACT_LED_PIN 7
constexpr uint32_t LedPulseMs = 80;

// Hit response timing
constexpr uint8_t FlashCount = 4;    // colour blinks on a hit
constexpr uint32_t FlashOnMs = 150;
constexpr uint32_t FlashOffMs = 150;
constexpr uint32_t DefaultDarkMinMs = 1000; // "dead" time after a hit (testing)
constexpr uint32_t DefaultDarkMaxMs = 5000;

// Heartbeat cadence (contract §4).
constexpr uint32_t HeartbeatMs = 2000;

// Damage taken per hit when the shot doesn't carry usable damage.
constexpr int StartHp = 100;

CRGB leds[NUM_LEDS];

// Visual state machine (drives the LEDs).
enum class Vis { Rainbow, Flash, Dark, Dead };
Vis vis = Vis::Rainbow;

// Persisted config (the NVS ConfigDoc, contract §3 / §2.2).
cp::ConfigDoc config;

// Runtime, host-re-pushed state (design §3 — never persisted).
char activeMode[24] = "idle"; // neutral idle on fresh boot; HB mode=
uint32_t darkMinMs = DefaultDarkMinMs;
uint32_t darkMaxMs = DefaultDarkMaxMs;

// Device-authoritative runtime health (design §6).
int hp = StartHp;

uint8_t rainbowHue = 0;
CRGB hitColour = CRGB::White;
uint8_t flashesLeft = 0;
bool flashOn = false;
uint32_t nextEventMs = 0; // next flash toggle, or end of the dark period
uint32_t ledOffAtMs = 0;  // activity LED off time (0 = off)
uint32_t hitCount = 0;
bool debugFrames = false;  // when on, broadcast every raw frame over UDP
uint32_t lastHeartbeatMs = 0;
uint32_t identifyUntilMs = 0; // white "identify" flash end time (0 = off)

Preferences nvs;

// --- NVS persistence --------------------------------------------------------

// Persist the ConfigDoc NVS fields (contract §3). Returns false on any write
// failure so the REST layer can answer 500 (write-then-confirm — design §8).
bool saveConfig() {
  nvs.putInt("ownTeam", config.ownTeam);
  nvs.putString("protocolId", config.protocolId);
  nvs.putInt("brightness", config.brightness);
  // enabledTeams as a compact CSV; teamColours as a CSV of "#RRGGBB".
  char teams[40] = "";
  for (size_t i = 0; i < config.enabledTeamsCount; i++) {
    char n[8];
    snprintf(n, sizeof(n), "%s%d", i ? "," : "", config.enabledTeams[i]);
    strncat(teams, n, sizeof(teams) - strlen(teams) - 1);
  }
  nvs.putString("enabledTeams", teams);
  for (size_t i = 0; i < cp::TeamColourCount; i++) {
    char key[12];
    snprintf(key, sizeof(key), "colour%d", config.teamIndex[i]);
    nvs.putString(key, config.teamColour[i]);
  }
  nvs.putString("hostname", config.hostname);
  return true;
}

// Load persisted config, falling back to defaults. deviceId/hostname come from
// TagNet so a fresh device returns as "itself".
void loadConfig() {
  strncpy(config.deviceId, TagNet::deviceId(), sizeof(config.deviceId) - 1);
  config.deviceId[sizeof(config.deviceId) - 1] = '\0';

  String host = nvs.getString("hostname", TagNet::hostname());
  strncpy(config.hostname, host.c_str(), sizeof(config.hostname) - 1);
  config.hostname[sizeof(config.hostname) - 1] = '\0';

  config.ownTeam = nvs.getInt("ownTeam", 2);
  String proto = nvs.getString("protocolId", "vatos");
  strncpy(config.protocolId, proto.c_str(), sizeof(config.protocolId) - 1);
  config.protocolId[sizeof(config.protocolId) - 1] = '\0';
  config.brightness = nvs.getInt("brightness", 13);

  // enabledTeams: parse CSV, default to all four Vatos teams.
  String teams = nvs.getString("enabledTeams", "1,2,3,4");
  config.enabledTeamsCount = 0;
  int start = 0;
  while (start < (int)teams.length() &&
         config.enabledTeamsCount <
             sizeof(config.enabledTeams) / sizeof(config.enabledTeams[0])) {
    int comma = teams.indexOf(',', start);
    if (comma < 0) {
      comma = teams.length();
    }
    config.enabledTeams[config.enabledTeamsCount++] =
        teams.substring(start, comma).toInt();
    start = comma + 1;
  }

  // teamColours: per-index override, default B/R/G/W.
  static const char *defaults[cp::TeamColourCount] = {"#0000FF", "#FF0000",
                                                      "#00FF00", "#FFFFFF"};
  for (size_t i = 0; i < cp::TeamColourCount; i++) {
    config.teamIndex[i] = (int)i + 1;
    char key[12];
    snprintf(key, sizeof(key), "colour%d", config.teamIndex[i]);
    String c = nvs.getString(key, defaults[i]);
    strncpy(config.teamColour[i], c.c_str(), sizeof(config.teamColour[i]) - 1);
    config.teamColour[i][sizeof(config.teamColour[i]) - 1] = '\0';
  }
}

// --- LEDs -------------------------------------------------------------------

// Map a team index to its configured colour (parsed from "#RRGGBB").
CRGB teamColour(int team) {
  for (size_t i = 0; i < cp::TeamColourCount; i++) {
    if (config.teamIndex[i] == team) {
      long v = strtol(config.teamColour[i] + 1, nullptr, 16); // skip '#'
      return CRGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    }
  }
  return CRGB::Magenta; // unknown team
}

void showSolid(const CRGB &colour) {
  fill_solid(leds, NUM_LEDS, colour);
  FastLED.show();
}

// --- Telemetry helpers ------------------------------------------------------

// Broadcast an EVT state line. Pass hp<0 to omit the hp token (e.g. s=dead).
void emitState(const char *s, int stateHp) {
  char buf[96];
  cp::formatStateEvent(buf, sizeof(buf), s, stateHp, millis());
  TagNet::event(buf);
}

// --- Hit / damage policy ----------------------------------------------------

// Apply a decoded shot: take damage locally (authoritative), emit EVT hit, then
// drive the visual response. At 0 hp hold Dead until respawn/reset.
void applyHit(const cp::TagEvent &ev) {
  const int dmg = ev.damage > 0 ? ev.damage : 1;
  hp -= dmg;
  if (hp < 0) {
    hp = 0;
  }
  hitCount++;

  char buf[128];
  cp::formatHitEvent(buf, sizeof(buf), config.deviceId, ev.team, dmg,
                     ev.protocolId, hp, millis());
  TagNet::event(buf);

  hitColour = teamColour(ev.team);
  if (hp == 0) {
    // Dead: hold dark until respawn / CTL reset.
    vis = Vis::Dead;
    showSolid(CRGB::Black);
    emitState("dead", -1);
  } else {
    flashesLeft = FlashCount;
    flashOn = true;
    vis = Vis::Flash;
    showSolid(hitColour);
    nextEventMs = millis() + FlashOnMs;
  }
}

// --- Inbound CTL + command handling -----------------------------------------

// Handle an inbound control message (CTL start/stop/reset — contract §1.4).
void handleControl(const cp::Control &c) {
  switch (c.kind) {
  case cp::ControlKind::Start:
    // Arm for play: full health, idling ready.
    hp = StartHp;
    vis = Vis::Rainbow;
    emitState("ready", hp);
    break;
  case cp::ControlKind::Stop:
    // Leave play: neutral idle.
    vis = Vis::Rainbow;
    emitState("idle", -1);
    break;
  case cp::ControlKind::Reset:
    // Force a state. With no hp (or hp>0) this is a respawn to that health;
    // hp=0 forces Dead.
    if (c.hasHp && c.hp <= 0) {
      hp = 0;
      vis = Vis::Dead;
      showSolid(CRGB::Black);
      emitState("dead", -1);
    } else {
      hp = c.hasHp ? c.hp : StartHp;
      vis = Vis::Rainbow;
      emitState("respawn", hp);
    }
    break;
  case cp::ControlKind::None:
    break;
  }
}

// Execute a structured command (POST /api/command, contract §2.2). Returns true
// if the command was recognised and applied.
bool runCommand(const cp::CommandDoc &cmd) {
  switch (cmd.kind) {
  case cp::CommandKind::Identify:
    identifyUntilMs = millis() + 1500;
    showSolid(CRGB::White);
    return true;
  case cp::CommandKind::Bright:
    config.brightness = constrain(cmd.value, 0, 255);
    FastLED.setBrightness(config.brightness);
    FastLED.show();
    return true;
  case cp::CommandKind::Hit:
    // Test hit without the gun.
    applyHit(cp::tagEventFromVatosShot(cmd.team, cmd.damage));
    return true;
  case cp::CommandKind::Debug:
    debugFrames = cmd.value != 0;
    return true;
  case cp::CommandKind::None:
    return false;
  }
  return false;
}

// --- Line handler (serial / inbound UDP / GET /cmd) -------------------------
//
// TagNet forwards every non-WiFi command line here: serial input, inbound UDP
// packets (where CTL arrives), and the deprecated GET /cmd?c= alias. We route
// CTL through ControlProto and keep the legacy bright/hit/debug serial verbs.
void onLine(const char *line) {
  // Inbound control (CTL start/stop/reset). parseControl tolerantly drops
  // anything else — including our own/peers' hostname-prefixed HB/EVT echoes.
  cp::Control ctl;
  if (cp::parseControl(line, ctl)) {
    handleControl(ctl);
    return;
  }

  // Legacy serial verbs (deprecated; kept for bench use without the host).
  if (strncmp(line, "bright ", 7) == 0) {
    config.brightness = (uint8_t)constrain(atoi(line + 7), 0, 255);
    FastLED.setBrightness(config.brightness);
    FastLED.show();
    Serial.printf("brightness=%u\n", config.brightness);
  } else if (strncmp(line, "hit ", 4) == 0) {
    int t = 0, d = 0;
    if (sscanf(line + 4, "%d %d", &t, &d) == 2 && t >= 1 && t <= 4 && d >= 1 &&
        d <= 4 && vis == Vis::Rainbow) {
      applyHit(cp::tagEventFromVatosShot(t, d));
    }
  } else if (strncmp(line, "debug ", 6) == 0) {
    debugFrames = atoi(line + 6) != 0;
  }
}

// --- HTTP "/" status page (kept) --------------------------------------------

String matrixStatus() {
  const char *m = vis == Vis::Rainbow ? "rainbow"
                  : vis == Vis::Flash ? "flash"
                  : vis == Vis::Dead  ? "dead"
                                      : "dark";
  char buf[128];
  snprintf(buf, sizeof(buf),
           "vis=%s mode=%s hp=%d brightness=%d hits=%lu debug=%d\n", m,
           activeMode, hp, config.brightness, (unsigned long)hitCount,
           debugFrames ? 1 : 0);
  return String(buf);
}

// --- REST surface (contract §2.1) -------------------------------------------

// The state value reported in StatusDoc/HB mode reflects activeMode. The state
// string for /api/status's hp/online is derived from runtime health.

void sendJson(int code, const char *json) {
  TagNet::httpServer().send(code, "application/json", json);
}

// Read a write request's JSON body into bodyOut. Writes MUST be sent with a
// JSON (or other non-form) Content-Type: the ESP32 WebServer only exposes the
// body as the "plain" arg for non-form types, while an
// application/x-www-form-urlencoded body (curl's default with -d) is consumed
// by form parsing and is not recoverable. On a missing body this answers 400
// with an actionable message and returns false, so callers should
// `if (!requireBody(s, body)) return;`.
bool requireBody(WebServer &s, String &bodyOut) {
  bodyOut = s.arg("plain");
  if (bodyOut.length() == 0) {
    sendJson(400,
             "{\"error\":\"empty body — send Content-Type: application/json\"}");
    return false;
  }
  return true;
}

// GET /api/status -> 200 StatusDoc
void handleStatus() {
  cp::StatusDoc st;
  strncpy(st.deviceId, config.deviceId, sizeof(st.deviceId) - 1);
  strncpy(st.hostname, config.hostname, sizeof(st.hostname) - 1);
  strncpy(st.fw, kFirmwareVersion, sizeof(st.fw) - 1);
  strncpy(st.mode, activeMode, sizeof(st.mode) - 1);
  st.ownTeam = config.ownTeam;
  st.hp = hp;
  st.online = TagNet::online();
  st.uptimeMs = millis();
  st.rssi = (int)WiFi.RSSI();
  char buf[320];
  cp::serializeStatus(st, buf, sizeof(buf));
  sendJson(200, buf);
}

// GET /api/config -> 200 ConfigDoc ; PATCH /api/config -> 200 full ConfigDoc
void handleConfig() {
  WebServer &s = TagNet::httpServer();
  if (s.method() == HTTP_GET) {
    char buf[512];
    cp::serializeConfig(config, buf, sizeof(buf));
    sendJson(200, buf);
    return;
  }
  if (s.method() == HTTP_PATCH) {
    String body;
    if (!requireBody(s, body)) {
      return;
    }
    cp::ConfigDoc staged = config; // apply onto a copy; commit only if NVS ok
    cp::PatchResult r = cp::applyConfigPatch(body.c_str(), staged);
    if (!r.ok) {
      char err[80];
      snprintf(err, sizeof(err), "{\"error\":\"%s\"}", r.error);
      sendJson(400, err);
      return;
    }
    // Write-then-confirm: persist, and only adopt staged on success (§8).
    cp::ConfigDoc prev = config;
    config = staged;
    if (!saveConfig()) {
      config = prev;
      sendJson(500, "{\"error\":\"nvs write failed\"}");
      return;
    }
    FastLED.setBrightness(config.brightness);
    char buf[512];
    cp::serializeConfig(config, buf, sizeof(buf));
    sendJson(200, buf);
    return;
  }
  sendJson(405, "{\"error\":\"method not allowed\"}");
}

// POST /api/mode -> 200 ModeDoc (runtime only; not persisted)
void handleMode() {
  WebServer &s = TagNet::httpServer();
  if (s.method() != HTTP_POST) {
    sendJson(405, "{\"error\":\"method not allowed\"}");
    return;
  }
  String body;
  if (!requireBody(s, body)) {
    return;
  }
  cp::ModeDoc m;
  if (!cp::parseMode(body.c_str(), m)) {
    sendJson(400, "{\"error\":\"malformed mode\"}");
    return;
  }
  strncpy(activeMode, m.mode, sizeof(activeMode) - 1);
  activeMode[sizeof(activeMode) - 1] = '\0';
  if (m.hasDarkMin) {
    darkMinMs = m.darkMinMs;
  }
  if (m.hasDarkMax) {
    darkMaxMs = m.darkMaxMs;
  }
  char buf[256];
  cp::serializeMode(m, buf, sizeof(buf));
  sendJson(200, buf);
}

// POST /api/command -> 200 {"ok":true}
void handleCommand() {
  WebServer &s = TagNet::httpServer();
  if (s.method() != HTTP_POST) {
    sendJson(405, "{\"error\":\"method not allowed\"}");
    return;
  }
  String body;
  if (!requireBody(s, body)) {
    return;
  }
  cp::CommandDoc cmd;
  if (!cp::parseCommand(body.c_str(), cmd) || !runCommand(cmd)) {
    sendJson(400, "{\"error\":\"bad command\"}");
    return;
  }
  sendJson(200, "{\"ok\":true}");
}

// Register the /api/* routes. Each path is HTTP_ANY so wrong-method requests
// reach the handler (which answers 405) rather than falling to onNotFound
// (which answers 404 for genuinely unknown routes — contract §8).
void registerRoutes() {
  WebServer &s = TagNet::httpServer();
  s.on("/api/status", HTTP_ANY, []() {
    if (TagNet::httpServer().method() == HTTP_GET) {
      handleStatus();
    } else {
      sendJson(405, "{\"error\":\"method not allowed\"}");
    }
  });
  s.on("/api/config", HTTP_ANY, handleConfig);
  s.on("/api/mode", HTTP_ANY, handleMode);
  s.on("/api/command", HTTP_ANY, handleCommand);
  s.onNotFound([]() { sendJson(404, "{\"error\":\"not found\"}"); });
}

// --- Setup / loop -----------------------------------------------------------

void setup() {
  Serial.begin(115200);

  // This matrix is RGB-ordered (not the usual GRB) — with GRB, Red showed green.
  FastLED.addLeds<WS2812B, MATRIX_PIN, RGB>(leds, NUM_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 500); // keep within USB current
  showSolid(CRGB(0, 0, 8)); // dim blue: starting / WiFi config

  // Resistor-less activity LED: minimum drive strength protects the pin
  pinMode(ACT_LED_PIN, OUTPUT);
  gpio_set_drive_capability(static_cast<gpio_num_t>(ACT_LED_PIN),
                            GPIO_DRIVE_CAP_0);
  digitalWrite(ACT_LED_PIN, LOW);

  TagNet::begin("lasertag-matrix");

  nvs.begin("matrix", false);
  loadConfig();
  FastLED.setBrightness(config.brightness);

  TagNet::onLine(onLine);             // CTL + legacy bright/hit/debug
  TagNet::onStatus(matrixStatus);     // HTTP "/" status
  TagNet::onHttpSetup(registerRoutes); // /api/* REST routes

  IrFramer::begin(IR_PIN);
  randomSeed(esp_random());

  vis = Vis::Rainbow;
  emitState("ready", hp);
}

void loop() {
  TagNet::handle();

  const uint32_t now = millis();

  // Heartbeat broadcast every HeartbeatMs (contract §4).
  if (now - lastHeartbeatMs >= HeartbeatMs) {
    lastHeartbeatMs = now;
    char buf[160];
    cp::formatHeartbeat(buf, sizeof(buf), config.deviceId,
                        WiFi.localIP().toString().c_str(), kFirmwareVersion,
                        config.ownTeam, activeMode, hp);
    TagNet::event(buf);
  }

  // Switch the activity LED off once its pulse has elapsed
  if (ledOffAtMs != 0 && now >= ledOffAtMs) {
    digitalWrite(ACT_LED_PIN, LOW);
    ledOffAtMs = 0;
  }

  // Identify flash: hold white briefly, then resume.
  if (identifyUntilMs != 0 && now >= identifyUntilMs) {
    identifyUntilMs = 0;
  }

  // Register hits only while idling in rainbow mode (ignore while flashing/dead)
  const IrFramer::Edge *edges;
  size_t n;
  while (IrFramer::poll(&edges, &n)) {
    // Pulse the activity LED on every received frame
    digitalWrite(ACT_LED_PIN, HIGH);
    ledOffAtMs = now + LedPulseMs;

    // Attempt to decode every frame (for diagnostics); act only in rainbow mode
    Vatos::Shot shot;
    bool ok = false;
    if (n == Vatos::FrameEdges) {
      uint32_t durations[Vatos::FrameEdges];
      for (size_t i = 0; i < n; i++) {
        durations[i] = edges[i].durationUs;
      }
      ok = Vatos::decode(durations, n, shot);
    }

    if (debugFrames) {
      String line = "frame n=" + String((unsigned)n);
      line += ok ? " dec=" + String(shot.team) + ":" + String(shot.damage)
                 : String(" dec=none");
      line += " data=";
      for (size_t i = 0; i < n; i++) {
        line += (edges[i].level ? 'H' : 'L');
        line += edges[i].durationUs;
        if (i + 1 < n) {
          line += ',';
        }
      }
      TagNet::event(line.c_str());
    }

    if (ok && vis == Vis::Rainbow) {
      applyHit(cp::tagEventFromVatosShot(shot.team, shot.damage));
    }
  }

  switch (vis) {
  case Vis::Rainbow: {
    if (identifyUntilMs == 0) {
      static uint32_t lastFrameMs = 0;
      if (now - lastFrameMs >= 20) {
        lastFrameMs = now;
        fill_rainbow(leds, NUM_LEDS, rainbowHue++, 4);
        FastLED.show();
      }
    }
    break;
  }
  case Vis::Flash: {
    if (now >= nextEventMs) {
      flashOn = !flashOn;
      if (flashOn) {
        showSolid(hitColour);
        nextEventMs = now + FlashOnMs;
      } else {
        showSolid(CRGB::Black);
        nextEventMs = now + FlashOffMs;
        if (--flashesLeft == 0) {
          vis = Vis::Dark;
          nextEventMs = now + random(darkMinMs, darkMaxMs + 1);
          // Post-hit cooldown while still alive: "stunned", not "dead". s=dead
          // is reserved for hp==0 (handled in applyHit / CTL reset).
          emitState("stunned", hp);
        }
      }
    }
    break;
  }
  case Vis::Dark: {
    if (now >= nextEventMs) {
      // Dark period over: resume idling but KEEP hp so damage accumulates
      // across hits. Full-health respawn happens only on CTL reset/start.
      vis = Vis::Rainbow;
      emitState("ready", hp);
    }
    break;
  }
  case Vis::Dead: {
    // Hold dark at 0 hp until a respawn / CTL reset; nothing to animate.
    break;
  }
  }
}
