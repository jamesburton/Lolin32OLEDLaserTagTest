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
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <stdarg.h>

#include <BoardProfile.h>
#include <BoardNvs.h>
#include <HitDisplay.h>
#include <SdCard.h>
#include <SdPath.h>
#include <Sound.h>
#include <WavFile.h>

#include <ControlProto.h>
#include <IrFramer.h>
#include <IrTx.h>
#include <TagNet.h>
#include <Vatos.h>

namespace cp = ControlProto;

// Firmware version reported on the wire (contract §1.3 fw=). BUMP THIS on
// every behavioural firmware change — the host's fleet updater compares it
// against the built image to decide who needs an OTA (fleet-ota spec).
#define LT_FW_VERSION "2.3.0"
static const char *kFirmwareVersion = LT_FW_VERSION;

// Embedded marker so the host can read a firmware.bin's version by scanning
// the raw image for "LTFW:" (fleet-ota spec — toolchain-proof alternative to
// the app descriptor). Printed once at boot so the linker keeps it.
static const char kFwMarker[] = "LTFW:" LT_FW_VERSION;

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

// Starting health is selectable (4/8/16/32) via config.startHp; the health bar
// scales to the chosen max, so full health fills the central columns regardless.

// Visual state machine (drives the LEDs). The Countdown/GameOver* cues and the
// Chase* states come from CTL v2.1 (chase-mode spec §3); they are orthogonal to
// hp, which only the Rainbow/Flash/Dark/Dead path accounts for.
enum class Vis {
  Rainbow,
  Flash,
  Dark,
  Dead,
  Countdown,
  GameOverScore,
  GameOverFlood,
  ChaseDormant,
  ChaseActive
};
Vis vis = Vis::Rainbow;

// Persisted config (the NVS ConfigDoc, contract §3 / §2.2).
cp::ConfigDoc config;

// Runtime, host-re-pushed state (design §3 — never persisted).
char activeMode[24] = "idle"; // neutral idle on fresh boot; HB mode=
uint32_t darkMinMs = DefaultDarkMinMs;
uint32_t darkMaxMs = DefaultDarkMaxMs;

// Device-authoritative runtime health (design §6). Initialised from
// config.startHp in setup() once NVS config is loaded.
int hp = 32;

int hitTeam = 0;
uint8_t flashesLeft = 0;
bool flashOn = false;
uint32_t nextEventMs = 0; // next flash toggle, or end of the dark period
uint32_t ledOffAtMs = 0;  // activity LED off time (0 = off)
uint32_t hitCount = 0;
bool debugFrames = false;  // when on, broadcast every raw frame over UDP
uint32_t lastHeartbeatMs = 0;
uint32_t identifyUntilMs = 0; // white "identify" flash end time (0 = off)

// --- Chase / cue runtime state (CTL v2.1; never persisted) ------------------
bool chaseOn = false;          // between `chase on` and `chase off`/stop/gameover
bool chasePenaltyFb = false;   // penalty feedback (red blink + tone) enabled
bool chaseDisplayScore = true; // dormant boards: dim scoreboard vs dark
int chaseScores[4] = {0, 0, 0, 0}; // last host-pushed team scores (teams 1..4)
bool haveScores = false;       // true once the host has pushed a CTL score
uint32_t chaseWindowEndMs = 0; // active self-timeout deadline (0 = none)
uint8_t spinPhase = 0;         // chase active spin animation phase (0..27)
uint32_t lastAnimMs = 0;       // spin/scoreboard frame pacing
uint32_t penaltyBlinkUntilMs = 0; // dormant-hit penalty / timeout wipe end
uint32_t cueUntilMs = 0;       // countdown end / gameover phase end
int cueCount = 0;              // countdown seconds remaining tracker
int gameoverWinner = 0;        // winning team for the gameover flood (0 = draw)

Preferences nvs;

// The EFFECTIVE board profile: compile-time defaults with the NVS overrides
// applied. Board::active() returns the defaults only, so anything reading pins
// at runtime must use this — otherwise an overridden SD/audio pin is honoured
// by Sound::begin (which is handed the merged copy) but ignored everywhere
// else. Populated in setup() before any consumer runs.
Board::BoardProfile activeProfile;

// Mounts the card on demand; defined with the SD REST handlers below.
bool sdReady();

// --- Remote log forwarding (SD diagnosis) -----------------------------------
//
// When set, ESP-IDF log output is broadcast over UDP as well as Serial. These
// boards run headless, so a driver-level failure is otherwise invisible.
// Enabled only around an explicit `sdtest`, never continuously.
bool sdLogForward = false;

// esp_log hook. Must be reentrancy-guarded: broadcasting a line goes through
// the WiFi stack, which itself logs at verbose level — without the guard the
// first forwarded line would recurse until the stack blew.
int udpLogVprintf(const char *fmt, va_list args) {
  static bool inHook = false;
  const int written = vprintf(fmt, args); // always keep Serial behaviour
  if (!sdLogForward || inHook) {
    return written;
  }
  inHook = true;
  char buf[192];
  va_list copy;
  va_copy(copy, args);
  vsnprintf(buf, sizeof(buf), fmt, copy);
  va_end(copy);
  // Trim the trailing newline: TagNet::event sends one line per datagram.
  for (char *p = buf; *p; p++) {
    if (*p == '\n' || *p == '\r') {
      *p = '\0';
      break;
    }
  }
  if (buf[0] != '\0') {
    TagNet::event(buf);
  }
  inHook = false;
  return written;
}

// Plays a WAV from the microSD by path. Shared by the `play` command and the
// boot startup cue so both validate, parse and free identically.
bool playSdClip(const char *path);

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
    snprintf(key, sizeof(key), "sfx%d", config.teamIndex[i]);
    nvs.putInt(key, config.teamSfx[i]);
  }
  nvs.putInt("deathSfx", config.deathSfx);
  nvs.putInt("startHp", config.startHp);
  nvs.putInt("dmgMult", config.damageMultiplier);
  for (size_t i = 0; i < cp::TeamColourCount; i++) {
    char key[16];
    snprintf(key, sizeof(key), "teamDmg%d", config.teamIndex[i]);
    nvs.putInt(key, config.teamDamageMult[i]);
  }
  nvs.putString("hostname", config.hostname);
  nvs.putString("chaseCol", config.chaseColour); // NVS keys are max 15 chars
  nvs.putString("startSfx", config.startupSfx);
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

  // Default NEUTRAL (cp::TeamNone), not team 2: an unprovisioned board should
  // be a target everyone may shoot, never a silent member of a side. Boards
  // provisioned before 2.2.0 still have their stored value and keep it.
  config.ownTeam = nvs.getInt("ownTeam", cp::TeamNone);
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

  // teamColours: per-index override, default B/R/G/W. teamSfx: SFX bank index
  // per team, default {wail, rise, fall, twotone}; deathSfx: the death cue.
  static const char *defaults[cp::TeamColourCount] = {"#0000FF", "#FF0000",
                                                      "#00FF00", "#FFFFFF"};
  static const int sfxDefaults[cp::TeamColourCount] = {0, 2, 3, 5};
  for (size_t i = 0; i < cp::TeamColourCount; i++) {
    config.teamIndex[i] = (int)i + 1;
    char key[12];
    snprintf(key, sizeof(key), "colour%d", config.teamIndex[i]);
    String c = nvs.getString(key, defaults[i]);
    strncpy(config.teamColour[i], c.c_str(), sizeof(config.teamColour[i]) - 1);
    config.teamColour[i][sizeof(config.teamColour[i]) - 1] = '\0';
    snprintf(key, sizeof(key), "sfx%d", config.teamIndex[i]);
    config.teamSfx[i] = nvs.getInt(key, sfxDefaults[i]);
  }
  config.deathSfx = nvs.getInt("deathSfx", 6);
  config.startHp = nvs.getInt("startHp", 32);
  config.damageMultiplier = nvs.getInt("dmgMult", 1);
  for (size_t i = 0; i < cp::TeamColourCount; i++) {
    char key[16];
    snprintf(key, sizeof(key), "teamDmg%d", config.teamIndex[i]);
    config.teamDamageMult[i] = nvs.getInt(key, 0);
  }

  // Chase active-spin colour (default amber); validated on PATCH, so anything
  // stored here is already "#RRGGBB".
  String chc = nvs.getString("chaseCol", "#FFA500");
  strncpy(config.chaseColour, chc.c_str(), sizeof(config.chaseColour) - 1);
  config.chaseColour[sizeof(config.chaseColour) - 1] = '\0';

  // Startup clip path. Default "" = silent at boot, so adding this feature
  // changes no existing board's behaviour until one is configured.
  String startSfx = nvs.getString("startSfx", "");
  strncpy(config.startupSfx, startSfx.c_str(), sizeof(config.startupSfx) - 1);
  config.startupSfx[sizeof(config.startupSfx) - 1] = '\0';
}

// --- LEDs -------------------------------------------------------------------

// Adapter passed to HitDisplay::begin; returns the configured "#RRGGBB" for a
// team index using the persisted teamIndex[]/teamColour[] config arrays.
static const char *teamColourHex(int team) {
  for (size_t i = 0; i < cp::TeamColourCount; i++) {
    if (config.teamIndex[i] == team) return config.teamColour[i];
  }
  return "#FF00FF";
}

// SFX bank index for a firing team, from the persisted teamSfx[] config (mirror
// of teamColourHex). Falls back to the first slot for an unknown team.
static int teamSfxIndex(int team) {
  for (size_t i = 0; i < cp::TeamColourCount; i++) {
    if (config.teamIndex[i] == team) return config.teamSfx[i];
  }
  return config.teamSfx[0];
}

// --- Telemetry helpers ------------------------------------------------------

// Broadcast an EVT state line. Pass hp<0 to omit the hp token (e.g. s=dead).
void emitState(const char *s, int stateHp) {
  char buf[96];
  cp::formatStateEvent(buf, sizeof(buf), s, stateHp, millis());
  TagNet::event(buf);
}

// --- Hit / damage policy ----------------------------------------------------

// Effective damage multiplier for a firing team: the per-shooter-team handicap
// override when set (>0), else the global damageMultiplier (mirror of
// teamSfxIndex over config.teamDamageMult).
static int damageMultForTeam(int team) {
  for (size_t i = 0; i < cp::TeamColourCount; i++) {
    if (config.teamIndex[i] == team && config.teamDamageMult[i] > 0) {
      return config.teamDamageMult[i];
    }
  }
  return config.damageMultiplier;
}

// Apply a decoded shot: take damage locally (authoritative), emit EVT hit, then
// drive the visual response. At 0 hp hold Dead until respawn/reset.
// The EVT reports the EFFECTIVE damage (after the multiplier) so host-side hp
// mirrors stay truthful.
void applyHit(const cp::TagEvent &ev) {
  const int dmg = (ev.damage > 0 ? ev.damage : 1) * damageMultForTeam(ev.team);
  hp -= dmg;
  if (hp < 0) {
    hp = 0;
  }
  hitCount++;

  char buf[128];
  cp::formatHitEvent(buf, sizeof(buf), config.deviceId, ev.team, dmg,
                     ev.protocolId, hp, millis());
  TagNet::event(buf);

  hitTeam = ev.team;
  if (hp == 0) {
    // Dead: play the death cue only (no hit siren on the fatal shot) and hold
    // dark until respawn / CTL reset.
    Sound::playIndex(config.deathSfx);
    vis = Vis::Dead;
    HitDisplay::dark();
    emitState("dead", -1);
  } else {
    Sound::playIndex(teamSfxIndex(ev.team)); // siren for the firing team
    flashesLeft = FlashCount;
    flashOn = true;
    vis = Vis::Flash;
    HitDisplay::flashTeam(hitTeam);
    nextEventMs = millis() + FlashOnMs;
  }
}

// --- Inbound CTL + command handling -----------------------------------------

// Handle an inbound control message (CTL start/stop/reset — contract §1.4).
void handleControl(const cp::Control &c) {
  switch (c.kind) {
  case cp::ControlKind::Start:
    // Arm for play: full health, idling ready.
    hp = config.startHp;
    vis = Vis::Rainbow;
    Sound::cue(Sound::Cue::Start);
    emitState("ready", hp);
    break;
  case cp::ControlKind::Stop:
    // Leave play: neutral idle. A stop always drops out of chase mode and
    // forgets the pushed scores (the next match pushes its own).
    chaseOn = false;
    haveScores = false;
    vis = Vis::Rainbow;
    emitState("idle", -1);
    break;
  case cp::ControlKind::Reset:
    // Force a state. With no hp (or hp>0) this is a respawn to that health;
    // hp=0 forces Dead.
    if (c.hasHp && c.hp <= 0) {
      hp = 0;
      vis = Vis::Dead;
      Sound::playIndex(config.deathSfx);
      HitDisplay::dark();
      emitState("dead", -1);
    } else {
      hp = c.hasHp ? c.hp : config.startHp;
      vis = Vis::Rainbow;
      Sound::cue(Sound::Cue::Respawn);
      emitState("respawn", hp);
    }
    break;
  case cp::ControlKind::Countdown:
    // Pre-match cue: flash + beep each second, dark between. Ends into
    // Rainbow (the host's start CTL follows and re-arms hp anyway).
    cueCount = c.hasN ? c.n : 5;
    cueUntilMs = millis() + (uint32_t)cueCount * 1000;
    vis = Vis::Countdown;
    break;
  case cp::ControlKind::GameOver:
    gameoverWinner = c.hasWinner ? c.winner : 0;
    chaseOn = false; // gameover always exits chase mode
    if (haveScores) {
      vis = Vis::GameOverScore; // 5 s scoreboard hold, then the winner flood
      cueUntilMs = millis() + 5000;
    } else {
      vis = Vis::GameOverFlood;
      cueUntilMs = millis() + 3000;
    }
    break;
  case cp::ControlKind::Score:
    // Display-only: the host stays authoritative for scores.
    for (int i = 0; i < 4; i++) {
      chaseScores[i] = c.scores[i];
    }
    haveScores = true;
    break;
  case cp::ControlKind::ChaseOn:
    // Enter chase: every board goes dormant (dim scoreboard or dark) until the
    // host activates it. hp is untouched for the whole mode.
    chaseOn = true;
    chasePenaltyFb = c.penalty != 0;
    chaseDisplayScore = c.displayScore;
    chaseWindowEndMs = 0;
    vis = Vis::ChaseDormant;
    emitState("dormant", -1);
    break;
  case cp::ControlKind::ChaseOff:
    chaseOn = false;
    haveScores = false;
    vis = Vis::Rainbow;
    emitState("idle", -1);
    break;
  case cp::ControlKind::Activate:
    // Standalone-scoreboard boards never join the chase pool.
    if (strcmp(activeMode, "scoreboard") == 0) {
      break;
    }
    vis = Vis::ChaseActive;
    spinPhase = 0;
    // The window timer lives here (spec §2.1): a lost deactivate can never
    // leave this board lit.
    chaseWindowEndMs = c.hasT ? millis() + c.t : 0;
    emitState("active", -1);
    break;
  case cp::ControlKind::Deactivate:
    chaseWindowEndMs = 0;
    vis = chaseOn ? Vis::ChaseDormant : Vis::Rainbow;
    emitState("dormant", -1);
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
    HitDisplay::solid({255, 255, 255});
    return true;
  case cp::CommandKind::Bright:
    config.brightness = constrain(cmd.value, 0, 255);
    HitDisplay::setBrightness(config.brightness);
    return true;
  case cp::CommandKind::Hit:
    // Test hit without the gun.
    applyHit(cp::tagEventFromVatosShot(cmd.team, cmd.damage));
    return true;
  case cp::CommandKind::Debug:
    debugFrames = cmd.value != 0;
    return true;
  case cp::CommandKind::Reset: {
    // Revive to full health. Routes through the CTL reset logic so the REST and
    // UDP/serial reset paths can't diverge.
    cp::Control c;
    c.kind = cp::ControlKind::Reset;
    handleControl(c);
    return true;
  }
  case cp::CommandKind::Fire:
    // Emit a Vatos IR shot of the given team (1-4) + damage (1-4). Manual
    // trigger only (message/API) — the device never fires on its own.
    if (!IrTx::present() || cmd.team < 1 || cmd.team > 4 || cmd.damage < 1 ||
        cmd.damage > 4) {
      return false;
    }
    IrTx::fire(cp::tagEventFromVatosShot(cmd.team, cmd.damage));
    return true;
  case cp::CommandKind::Play:
    // Play a WAV straight off the card. Together with the /api/sd surface this
    // is what makes sound content manageable remotely: upload a clip, play it,
    // no cable and no reflash.
    return playSdClip(cmd.path);
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
    // v2.1 addressing: an id-addressed CTL is for one device only. Absent id=
    // means "everyone" (the historic broadcast behaviour).
    if (ctl.hasId && strcmp(ctl.id, config.deviceId) != 0) {
      return;
    }
    handleControl(ctl);
    return;
  }

  // Legacy serial verbs (deprecated; kept for bench use without the host).
  if (strncmp(line, "bright ", 7) == 0) {
    config.brightness = (uint8_t)constrain(atoi(line + 7), 0, 255);
    HitDisplay::setBrightness(config.brightness);
    Serial.printf("brightness=%u\n", config.brightness);
  } else if (strncmp(line, "hit ", 4) == 0) {
    int t = 0, d = 0;
    if (sscanf(line + 4, "%d %d", &t, &d) == 2 && t >= 1 && t <= 4 && d >= 1 &&
        d <= 4 && vis == Vis::Rainbow) {
      applyHit(cp::tagEventFromVatosShot(t, d));
    }
  } else if (strncmp(line, "debug ", 6) == 0) {
    debugFrames = atoi(line + 6) != 0;
  } else if (strncmp(line, "fire ", 5) == 0) {
    // Bench/host helper: transmit a Vatos shot (team 1-4, damage 1-4) over IR.
    // Manual trigger only; the device has no automatic fire.
    int t = 0, d = 0;
    if (sscanf(line + 5, "%d %d", &t, &d) == 2 && t >= 1 && t <= 4 && d >= 1 &&
        d <= 4) {
      if (IrTx::present()) {
        IrTx::fire(cp::tagEventFromVatosShot(t, d));
        Serial.printf("fired team=%d damage=%d\n", t, d);
      } else {
        Serial.println("fire: no IR transmitter on this board");
      }
    } else {
      Serial.println("usage: fire <team 1-4> <damage 1-4>");
    }
  } else if (strncmp(line, "sfx ", 4) == 0) {
    // Bench helper: play a bank entry on demand (bypasses game state) so any
    // sound — including the death cue — can be auditioned without a full game.
    Sound::playIndex(atoi(line + 4));
  } else if (strncmp(line, "lives ", 6) == 0) {
    // Select starting health (4/8/16/32), persist it, and revive to it.
    int n = atoi(line + 6);
    if (n == 4 || n == 8 || n == 16 || n == 32) {
      config.startHp = n;
      saveConfig();
      hp = config.startHp;
      vis = Vis::Rainbow;
      emitState("ready", hp);
      Serial.printf("lives=%d\n", config.startHp);
    } else {
      Serial.println("lives must be 4, 8, 16 or 32");
    }
  } else if (strncmp(line, "mult", 4) == 0 &&
             (line[4] == '\0' || line[4] == ' ')) {
    // Damage multiplier: `mult` prints, `mult <n>` sets the global (1-32),
    // `mult <team> <n>` sets a per-shooter-team handicap (0 = inherit global).
    int a = -1, b = -1;
    const int argc = sscanf(line + 4, "%d %d", &a, &b);
    if (argc <= 0) {
      Serial.printf("mult=%d team[", config.damageMultiplier);
      for (size_t i = 0; i < cp::TeamColourCount; i++) {
        Serial.printf("%s%d:%d", i ? " " : "", config.teamIndex[i],
                      config.teamDamageMult[i]);
      }
      Serial.println("] (0=inherit)");
    } else if (argc == 1 && a >= 1 && a <= 32) {
      config.damageMultiplier = a;
      saveConfig();
      Serial.printf("mult=%d\n", config.damageMultiplier);
    } else if (argc == 2 && a >= 1 && a <= 4 && b >= 0 && b <= 32) {
      for (size_t i = 0; i < cp::TeamColourCount; i++) {
        if (config.teamIndex[i] == a) {
          config.teamDamageMult[i] = b;
        }
      }
      saveConfig();
      Serial.printf("mult team %d = %d%s\n", a, b,
                    b == 0 ? " (inherit global)" : "");
    } else {
      Serial.println("usage: mult | mult <1-32> | mult <team 1-4> <0-32>");
    }
  } else if (strcmp(line, "sdtest") == 0) {
    // Remote SD diagnosis: forward the ESP-IDF driver's own log output over
    // UDP for the duration of one mount attempt, then switch it off again.
    // These boards have no serial attached in normal use, so without this the
    // only visible symptom is "not mounted" with no reason. Scoping the
    // forwarding to the single call keeps the WiFi driver's own verbose
    // chatter off the wire.
    sdLogForward = true;
    TagNet::event("SDTEST begin");
    const bool ok =
        Storage::sdBegin(activeProfile.sdCsPin, activeProfile.sdMosiPin,
                         activeProfile.sdMisoPin, activeProfile.sdSckPin);
    char msg[96];
    snprintf(msg, sizeof(msg), "SDTEST result=%s hz=%lu", ok ? "MOUNTED" : "FAILED",
             (unsigned long)Storage::sdMountHz());
    TagNet::event(msg);
    sdLogForward = false;
  } else if (strcmp(line, "sdprobe") == 0) {
    // Raw SPI handshake with the card, bypassing the SD library. Reports the
    // bytes over UDP so a headless board can be diagnosed.
    Storage::SdProbe p =
        Storage::sdProbeRaw(activeProfile.sdCsPin, activeProfile.sdMosiPin,
                            activeProfile.sdMisoPin, activeProfile.sdSckPin);
    char msg[160];
    snprintf(msg, sizeof(msg),
             "SDPROBE responded=%d r1=0x%02X cmd8=%02X%02X%02X%02X%02X v2=%d "
             "pins=cs%d,mosi%d,miso%d,sck%d",
             p.responded ? 1 : 0, p.r1, p.cmd8[0], p.cmd8[1], p.cmd8[2],
             p.cmd8[3], p.cmd8[4], p.cmd8Ok ? 1 : 0, (int)activeProfile.sdCsPin,
             (int)activeProfile.sdMosiPin, (int)activeProfile.sdMisoPin,
             (int)activeProfile.sdSckPin);
    TagNet::event(msg);
  } else if (strcmp(line, "sdplay") == 0) {
    // Spike bench helper: mount the card, list /sfx/, and play one .wav
    // through the existing I2S path. Bypasses game state entirely.
    const Board::BoardProfile &prof = activeProfile;
    if (!prof.hasSdCard()) {
      Serial.println("[sd] sdplay: no SD card configured on this board");
    } else if (!Storage::sdBegin(prof.sdCsPin, prof.sdMosiPin, prof.sdMisoPin,
                                  prof.sdSckPin)) {
      Serial.println("[sd] sdplay: mount failed");
    } else {
      Storage::sdList("/sfx", [](const char *name) {
        Serial.printf("[sd] /sfx/%s\n", name);
      });
      size_t fileLen = 0;
      uint8_t *buf = Storage::sdReadFile("/sfx/test.wav", fileLen);
      if (buf == nullptr) {
        Serial.println("[sd] sdplay: could not read /sfx/test.wav");
      } else {
        Storage::WavView view;
        const char *err = nullptr;
        if (!Storage::parseWav(buf, fileLen, view, err)) {
          Serial.printf("[sd] sdplay: WAV rejected (%s)\n", err);
        } else {
          Serial.printf("[sd] sdplay: playing %u samples @ %uHz\n",
                        (unsigned)view.sampleCount, (unsigned)view.sampleRate);
          Sound::playRaw(view.pcm, view.sampleCount);
        }
        free(buf);
      }
    }
  }
}

// --- HTTP "/" status page (kept) --------------------------------------------

String matrixStatus() {
  const char *m = vis == Vis::Rainbow ? "rainbow"
                  : vis == Vis::Flash ? "flash"
                  : vis == Vis::Dead  ? "dead"
                                      : "dark";
  char buf[320];
  // The audio/SD lines are diagnostics: without a serial cable there is
  // otherwise no way to tell "no amp configured" from "amp fine, clip
  // rejected", or "no card inserted" from "card wired to different pins".
  snprintf(buf, sizeof(buf),
           "vis=%s mode=%s hp=%d brightness=%d hits=%lu debug=%d\n"
           "audio=%s sfxBank=%u sfxPlays=%lu sfxLast=%s\n"
           "sdWired=%d sdMounted=%d sdHz=%lu sdPins=cs%d,mosi%d,miso%d,sck%d "
           "startupSfx=%s\n",
           m, activeMode, hp, config.brightness, (unsigned long)hitCount,
           debugFrames ? 1 : 0,
           Sound::present() ? "yes" : "NO-AMP-CONFIGURED",
           (unsigned)Sound::sfxCount(), (unsigned long)Sound::sfxPlays(),
           Sound::sfxLastName(), activeProfile.hasSdCard() ? 1 : 0,
           Storage::sdMounted() ? 1 : 0,
           (unsigned long)Storage::sdMountHz(), (int)activeProfile.sdCsPin,
           (int)activeProfile.sdMosiPin, (int)activeProfile.sdMisoPin,
           (int)activeProfile.sdSckPin,
           config.startupSfx[0] ? config.startupSfx : "(none)");
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
    // Path safety lives here rather than in ControlProto: that library is
    // filesystem-agnostic, and this is the boundary where a LAN caller's
    // string first becomes something we would open.
    if (r.ok && strlen(staged.startupSfx) > 0 &&
        !Storage::isSafeSdPath(staged.startupSfx)) {
      sendJson(400, "{\"error\":\"startupSfx must be a safe absolute path "
                    "(or \\\"\\\" for none)\"}");
      return;
    }
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
    HitDisplay::setBrightness(config.brightness);
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

// --- HTTP OTA (fleet-ota spec) ----------------------------------------------

// Streamed firmware upload for POST /api/update. Update.h only commits a
// fully written, verified image, so a failed/aborted upload leaves the
// running firmware untouched.
void handleUpdateUpload() {
  HTTPUpload &up = TagNet::httpServer().upload();
  if (up.status == UPLOAD_FILE_START) {
    Serial.printf("[ota] http update start: %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.isRunning() &&
        Update.write(up.buf, up.currentSize) != up.currentSize) {
      Update.printError(Serial);
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("[ota] http update ok: %u bytes\n", (unsigned)up.totalSize);
    } else {
      Update.printError(Serial);
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    Serial.println("[ota] http update aborted");
  }
}

// Completion handler for POST /api/update: report the outcome, and reboot
// into the new image on success (short delay so the response flushes first).
void handleUpdateDone() {
  if (Update.hasError()) {
    char err[96];
    snprintf(err, sizeof(err), "{\"error\":\"update failed: %s\"}",
             Update.errorString());
    sendJson(500, err);
    return;
  }
  sendJson(200, "{\"ok\":true,\"version\":\"" LT_FW_VERSION "\"}");
  delay(1000);
  ESP.restart();
}

// --- microSD REST surface ---------------------------------------------------
//
// Remote management of the card's contents, so sound clips can be listed,
// uploaded, fetched and removed without pulling the card or attaching USB.
// Every caller-supplied path goes through Storage::isSafeSdPath first — it is
// the only gate between a LAN request and the filesystem.

// Pulls the ?path= query arg, validates it, and answers 4xx itself when it is
// missing or unsafe. Returns false when the caller should stop.
bool requireSdPath(WebServer &s, String &pathOut, const char *fallback) {
  pathOut = s.hasArg("path") ? s.arg("path") : String(fallback);
  if (pathOut.length() == 0) {
    sendJson(400, "{\"error\":\"missing ?path=\"}");
    return false;
  }
  if (!Storage::isSafeSdPath(pathOut.c_str())) {
    sendJson(400,
             "{\"error\":\"unsafe path — must start with / and contain no .. "
             "segment\"}");
    return false;
  }
  return true;
}

// Mounts on demand. The card is also mounted at boot, but a card inserted
// afterwards (or a transient failure) should not need a reboot to recover.
bool sdReady() {
  const Board::BoardProfile &prof = activeProfile;
  if (!prof.hasSdCard()) {
    return false;
  }
  if (Storage::sdMounted()) {
    return true;
  }
  return Storage::sdBegin(prof.sdCsPin, prof.sdMosiPin, prof.sdMisoPin,
                          prof.sdSckPin);
}

bool playSdClip(const char *path) {
  if (path == nullptr || strlen(path) == 0) {
    return false; // "" means "no clip configured" — not an error
  }
  if (!Storage::isSafeSdPath(path) || !sdReady()) {
    return false;
  }
  size_t len = 0;
  uint8_t *buf = Storage::sdReadFile(path, len);
  if (buf == nullptr) {
    return false;
  }
  Storage::WavView view;
  const char *err = nullptr;
  const bool ok = Storage::parseWav(buf, len, view, err);
  if (ok) {
    Sound::playRaw(view.pcm, view.sampleCount);
  } else {
    Serial.printf("[sd] play '%s': WAV rejected (%s)\n", path, err);
  }
  // playRaw blocks until the clip has been written to I2S and the DMA has
  // drained, so the buffer is fully consumed by the time it returns and is
  // safe to free here. That also means a long clip stalls the loop — keep
  // clips within the ~5 s idle watchdog (the 3 s quack was trimmed for
  // exactly this reason).
  free(buf);
  return ok;
}

// GET /api/sd[?path=/dir] -> card status plus a directory listing.
void handleSdList() {
  const Board::BoardProfile &prof = activeProfile;
  if (!prof.hasSdCard()) {
    sendJson(404, "{\"error\":\"no SD card wired on this board\"}");
    return;
  }
  if (!sdReady()) {
    // Report the pins we tried: the commonest cause of this is a card wired to
    // different pins than the board profile assumes, and without a serial
    // cable there is no other way to see what was attempted.
    char err[200];
    snprintf(err, sizeof(err),
             "{\"present\":false,\"error\":\"card not mounted — not inserted, "
             "not FAT16/FAT32, or wired to other pins\",\"triedPins\":{\"cs\":%d,"
             "\"mosi\":%d,\"miso\":%d,\"sck\":%d}}",
             (int)prof.sdCsPin, (int)prof.sdMosiPin, (int)prof.sdMisoPin,
             (int)prof.sdSckPin);
    sendJson(503, err);
    return;
  }

  String path;
  if (!requireSdPath(TagNet::httpServer(), path, "/")) {
    return;
  }

  uint64_t total = 0;
  uint64_t used = 0;
  Storage::sdUsage(total, used);

  // Reported in kB: a 32-bit byte count would overflow on cards >4 GB, and
  // Arduino's String has no 64-bit append.
  String out = "{\"present\":true,\"totalKb\":";
  out += (uint32_t)(total / 1024);
  out += ",\"usedKb\":";
  out += (uint32_t)(used / 1024);
  out += ",\"path\":\"";
  out += path;
  out += "\",\"files\":[";

  struct Ctx {
    String *out;
    bool first;
  } ctx{&out, true};

  Storage::sdListDetailed(
      path.c_str(),
      [](const Storage::SdEntry &e, void *raw) {
        Ctx *c = (Ctx *)raw;
        if (!c->first) {
          *c->out += ',';
        }
        c->first = false;
        *c->out += "{\"name\":\"";
        *c->out += e.name;
        *c->out += "\",\"size\":";
        *c->out += e.size;
        *c->out += ",\"dir\":";
        *c->out += e.isDir ? "true" : "false";
        *c->out += '}';
      },
      &ctx);

  out += "]}";
  TagNet::httpServer().send(200, "application/json", out);
}

// GET /api/sd/file?path=... -> the raw file.
void handleSdDownload() {
  if (!sdReady()) {
    sendJson(503, "{\"error\":\"card not mounted\"}");
    return;
  }
  String path;
  if (!requireSdPath(TagNet::httpServer(), path, "")) {
    return;
  }
  size_t len = 0;
  uint8_t *buf = Storage::sdReadFile(path.c_str(), len);
  if (buf == nullptr) {
    sendJson(404, "{\"error\":\"not found or unreadable\"}");
    return;
  }
  TagNet::httpServer().setContentLength(len);
  TagNet::httpServer().send(200, "application/octet-stream", "");
  TagNet::httpServer().client().write(buf, len);
  free(buf);
}

// DELETE /api/sd/file?path=...
void handleSdDelete() {
  if (!sdReady()) {
    sendJson(503, "{\"error\":\"card not mounted\"}");
    return;
  }
  String path;
  if (!requireSdPath(TagNet::httpServer(), path, "")) {
    return;
  }
  if (!Storage::sdDelete(path.c_str())) {
    sendJson(404, "{\"error\":\"not found, or is a directory\"}");
    return;
  }
  sendJson(200, "{\"ok\":true}");
}

// Streamed upload half of POST /api/sd/file?path=... Mirrors the OTA handler:
// the body is written straight to the card rather than buffered in RAM, so
// clip size is bounded by the card, not by free heap.
void handleSdUploadData() {
  HTTPUpload &up = TagNet::httpServer().upload();
  if (up.status == UPLOAD_FILE_START) {
    String path;
    // The query arg is still readable here; validate before opening anything.
    if (!TagNet::httpServer().hasArg("path") ||
        !Storage::isSafeSdPath(TagNet::httpServer().arg("path").c_str())) {
      return; // the completion handler answers 400
    }
    path = TagNet::httpServer().arg("path");
    if (sdReady()) {
      Storage::sdWriteOpen(path.c_str());
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!Storage::sdWriteChunk(up.buf, up.currentSize)) {
      Storage::sdWriteAbort();
    }
  } else if (up.status == UPLOAD_FILE_END) {
    Storage::sdWriteClose();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Storage::sdWriteAbort();
  }
}

// Completion half of POST /api/sd/file?path=...
void handleSdUploadDone() {
  if (!sdReady()) {
    sendJson(503, "{\"error\":\"card not mounted\"}");
    return;
  }
  String path;
  if (!requireSdPath(TagNet::httpServer(), path, "")) {
    return;
  }
  bool isDir = false;
  if (!Storage::sdExists(path.c_str(), isDir) || isDir) {
    sendJson(500, "{\"error\":\"upload failed — nothing written\"}");
    return;
  }
  size_t len = 0;
  uint8_t *buf = Storage::sdReadFile(path.c_str(), len);
  const uint32_t size = (uint32_t)len;
  free(buf);

  char out[96];
  snprintf(out, sizeof(out), "{\"ok\":true,\"path\":\"%s\",\"size\":%u}",
           path.c_str(), (unsigned)size);
  sendJson(200, out);
}

// Minimal browser upload page (GET /update) posting to /api/update, so a
// single board can be flashed tool-free from any browser on the LAN.
void handleUpdatePage() {
  TagNet::httpServer().send(
      200, "text/html",
      "<!doctype html><title>lasertag OTA</title>"
      "<h3>Firmware update (running " LT_FW_VERSION ")</h3>"
      "<form method=POST action=/api/update enctype=multipart/form-data>"
      "<input type=file name=fw accept=.bin> <input type=submit value=Flash>"
      "</form><p>Board reboots automatically on success.</p>");
}

// Register the /api/* routes. Each path is HTTP_ANY so wrong-method requests
// reach the handler (which answers 405) rather than falling to onNotFound
// (which answers 404 for genuinely unknown routes — contract §8).
void registerRoutes() {
  WebServer &s = TagNet::httpServer();
  s.on("/api/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  s.on("/update", HTTP_GET, handleUpdatePage);

  // microSD management. /api/sd/file carries three methods: POST streams an
  // upload (hence the two-handler form), GET downloads, DELETE removes.
  s.on("/api/sd", HTTP_ANY, []() {
    if (TagNet::httpServer().method() == HTTP_GET) {
      handleSdList();
    } else {
      sendJson(405, "{\"error\":\"method not allowed\"}");
    }
  });
  s.on("/api/sd/file", HTTP_POST, handleSdUploadDone, handleSdUploadData);
  s.on("/api/sd/file", HTTP_GET, handleSdDownload);
  s.on("/api/sd/file", HTTP_DELETE, handleSdDelete);
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

  // Reference the embedded version marker so the linker keeps it in the image
  // (the host's fleet updater scans firmware.bin for it).
  Serial.printf("boot %s\n", kFwMarker);

  activeProfile = Board::active();
  BoardNvs::loadOverrides(activeProfile);
  Board::BoardProfile &profile = activeProfile;
  HitDisplay::begin(profile, teamColourHex);
  Sound::begin(profile);

  // NOTE: the microSD is deliberately NOT mounted here. Mounting before
  // TagNet::begin() puts SD probing ahead of WiFi, so a card or bus that hangs
  // the SPI probe takes the board off the network entirely — no heartbeat, no
  // REST, and no OTA route back, which requires physical USB recovery. That
  // happened once; never again. The mount now runs after networking is up,
  // and sdReady() mounts lazily on demand regardless.
  HitDisplay::solid({0, 0, 8}); // dim blue: starting / WiFi config

  // Resistor-less activity LED: minimum drive strength protects the pin
  pinMode(ACT_LED_PIN, OUTPUT);
  gpio_set_drive_capability(static_cast<gpio_num_t>(ACT_LED_PIN),
                            GPIO_DRIVE_CAP_0);
  digitalWrite(ACT_LED_PIN, LOW);

  // Boot with the persisted hostname so multiple boards co-exist on mDNS/OTA.
  // Only the hostname is read pre-begin: deviceId comes from TagNet::begin(),
  // so the full loadConfig() must stay after it.
  nvs.begin("matrix", false);
  String bootHost = nvs.getString("hostname", "lasertag-matrix");
  TagNet::begin(bootHost.c_str());

  loadConfig();
  hp = config.startHp; // adopt the configured starting health
  HitDisplay::setBrightness(config.brightness);

  esp_log_set_vprintf(udpLogVprintf); // enables `sdtest`'s remote log capture
  TagNet::onLine(onLine);             // CTL + legacy bright/hit/debug
  TagNet::onStatus(matrixStatus);     // HTTP "/" status
  TagNet::onHttpSetup(registerRoutes); // /api/* REST routes

  IrFramer::begin(IR_PIN);
  IrTx::begin(profile); // IR transmit (irTxPin=37 on the S3); no-op if absent
  randomSeed(esp_random());

  vis = Vis::Rainbow;

  // Mount the card only now — AFTER TagNet::begin() has WiFi, HTTP and OTA
  // running. If the SPI probe hangs or the card is faulty, the board is
  // already reachable and can be re-flashed over the air.
  if (activeProfile.hasSdCard()) {
    Storage::sdBegin(activeProfile.sdCsPin, activeProfile.sdMosiPin,
                     activeProfile.sdMisoPin, activeProfile.sdSckPin);
  }

  // Startup cue, last so the board is fully up before it blocks on playback.
  // Silent unless config.startupSfx names a clip on the card — the default is
  // "" precisely so this changes nothing for an unconfigured board.
  if (config.startupSfx[0] != '\0') {
    Serial.printf("[sfx] startup clip '%s'\n", config.startupSfx);
    if (!playSdClip(config.startupSfx)) {
      Serial.println("[sfx] startup clip failed (missing, unreadable or not "
                     "16 kHz/16-bit/mono)");
    }
  }

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

    // Recurring SFX status so the last-played sample is always in the newest
    // serial output — pick a favourite from here after auditioning by shooting.
    if (Sound::present()) {
      Serial.printf("[sfx] last=%u/%u name=%s plays=%lu\n", Sound::sfxLastIndex(),
                    Sound::sfxCount(), Sound::sfxLastName(),
                    (unsigned long)Sound::sfxPlays());
    }
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

    // Dedicated scoreboard boards ignore IR entirely (spec §3.2).
    if (ok && strcmp(activeMode, "scoreboard") != 0) {
      if (vis == Vis::Rainbow) {
        applyHit(cp::tagEventFromVatosShot(shot.team, shot.damage));
      } else if (vis == Vis::ChaseActive) {
        // Chase success: scoring is host-side; hp untouched. Team flash + siren,
        // then straight back to dormant awaiting the next activation.
        char buf[128];
        cp::formatHitEvent(buf, sizeof(buf), config.deviceId, shot.team,
                           shot.damage, "vatos", hp, millis());
        TagNet::event(buf);
        Sound::playIndex(teamSfxIndex(shot.team));
        HitDisplay::flashTeam(shot.team);
        chaseWindowEndMs = 0;
        penaltyBlinkUntilMs = 0;
        lastAnimMs = millis() + 400; // hold the team flash ~400 ms
        vis = Vis::ChaseDormant;
        emitState("dormant", -1);
      } else if (vis == Vis::ChaseDormant) {
        // Wrong target: report it (the host may penalize) with local feedback
        // only when the penalty flag is on.
        char buf[128];
        cp::formatDormantHitEvent(buf, sizeof(buf), config.deviceId, shot.team,
                                  shot.damage, "vatos", hp, millis());
        TagNet::event(buf);
        if (chasePenaltyFb) {
          Sound::cue(Sound::Cue::Hit);
          penaltyBlinkUntilMs = millis() + 300;
        }
      }
    }
  }

  // A board in REST mode=scoreboard is a dedicated wall display: paint the
  // latest scores and skip the game visual machine entirely (it also ignores
  // activate — see handleControl — and ignores IR, so it never scores).
  if (strcmp(activeMode, "scoreboard") == 0) {
    // Signed elapsed-time compare: lastAnimMs may sit in the future (the
    // post-hit flash hold), which would wrap an unsigned subtraction.
    if ((int32_t)(now - lastAnimMs) >= 250) {
      lastAnimMs = now;
      uint8_t grid[64];
      cp::scoreGrid(chaseScores, config.enabledTeams, config.enabledTeamsCount,
                    grid);
      HitDisplay::scoreboard(grid, 1, 1);
    }
    return; // end of loop() work for scoreboard boards
  }

  switch (vis) {
  case Vis::Rainbow: {
    if (identifyUntilMs == 0) {
      static uint32_t lastFrameMs = 0;
      if (now - lastFrameMs >= 20) {
        lastFrameMs = now;
        HitDisplay::idleWithHealth(hp, config.startHp);
      }
    }
    break;
  }
  case Vis::Flash: {
    if (now >= nextEventMs) {
      flashOn = !flashOn;
      if (flashOn) {
        HitDisplay::flashTeam(hitTeam);
        nextEventMs = now + FlashOnMs;
      } else {
        HitDisplay::dark();
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
  case Vis::Countdown: {
    if (now >= cueUntilMs) {
      vis = Vis::Rainbow;
      break;
    }
    // One white blink + beep per remaining second boundary.
    const int secsLeft = (int)((cueUntilMs - now + 999) / 1000);
    if (secsLeft != cueCount) {
      cueCount = secsLeft;
      Sound::cue(Sound::Cue::Hit);
    }
    const bool on = ((cueUntilMs - now) % 1000) > 700; // 300 ms blink
    if (on) {
      HitDisplay::solid({64, 64, 64});
    } else {
      HitDisplay::dark();
    }
    break;
  }
  case Vis::GameOverScore: {
    // Hold the final scoreboard at full brightness before the winner flood.
    if (now - lastAnimMs >= 100) {
      lastAnimMs = now;
      uint8_t grid[64];
      cp::scoreGrid(chaseScores, config.enabledTeams, config.enabledTeamsCount,
                    grid);
      HitDisplay::scoreboard(grid, 1, 1);
    }
    if (now >= cueUntilMs) {
      vis = Vis::GameOverFlood;
      cueUntilMs = now + 3000;
    }
    break;
  }
  case Vis::GameOverFlood: {
    if (gameoverWinner > 0) {
      HitDisplay::flashTeam(gameoverWinner);
    } else {
      HitDisplay::solid({96, 96, 96}); // draw: white
    }
    if (now >= cueUntilMs) {
      haveScores = false;
      vis = Vis::Rainbow;
      emitState("idle", -1);
    }
    break;
  }
  case Vis::ChaseDormant: {
    // The blink timer doubles as the post-timeout red wipe (see ChaseActive).
    if (penaltyBlinkUntilMs != 0) {
      if (now < penaltyBlinkUntilMs) {
        HitDisplay::solid({48, 0, 0}); // dim red penalty blink
        break;
      }
      penaltyBlinkUntilMs = 0;
    }

    // Signed compare: the post-hit flash hold parks lastAnimMs in the future.
    if ((int32_t)(now - lastAnimMs) >= 250) {
      lastAnimMs = now;
      if (chaseDisplayScore && haveScores) {
        uint8_t grid[64];
        cp::scoreGrid(chaseScores, config.enabledTeams,
                      config.enabledTeamsCount, grid);
        HitDisplay::scoreboard(grid, 1, 4); // dim: 25 % channel scale
      } else {
        HitDisplay::dark();
      }
    }
    break;
  }
  case Vis::ChaseActive: {
    if (chaseWindowEndMs != 0 && now >= chaseWindowEndMs) {
      // Unhit inside the window: brief red "lost it" wipe, then dormant.
      chaseWindowEndMs = 0;
      HitDisplay::solid({128, 0, 0});
      penaltyBlinkUntilMs = now + 300; // reuse the blink timer for the wipe
      vis = Vis::ChaseDormant;
      emitState("timeout", -1);
      break;
    }
    if (now - lastAnimMs >= 60) {
      lastAnimMs = now;
      Board::Rgb c{255, 165, 0}; // amber fallback if chaseColour is malformed
      Board::parseHexColour(config.chaseColour, c);
      HitDisplay::spinFrame(c, spinPhase);
      spinPhase = (uint8_t)((spinPhase + 1) % 28);
    }
    break;
  }
  }
}
