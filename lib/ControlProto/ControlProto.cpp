#include "ControlProto.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ControlProto {
namespace {

// Returns true if `line` starts with `prefix`; if so, advances `*rest` past it.
bool startsWith(const char *line, const char *prefix, const char **rest) {
  const size_t n = strlen(prefix);
  if (strncmp(line, prefix, n) != 0) {
    return false;
  }
  if (rest) {
    *rest = line + n;
  }
  return true;
}

// Scans the remaining `key=value` tokens of a line for `key`. On a match writes
// the integer value to *outVal and returns true. Tolerant: missing/garbage keys
// simply aren't matched. Token values have no spaces (contract §1.1).
bool findIntKey(const char *rest, const char *key, long *outVal) {
  const size_t keyLen = strlen(key);
  const char *p = rest;
  while (*p) {
    while (*p == ' ' || *p == '\t') {
      p++;
    }
    if (!*p) {
      break;
    }
    const char *tokStart = p;
    while (*p && *p != ' ' && *p != '\t') {
      p++;
    }
    const size_t tokLen = (size_t)(p - tokStart);
    // Match "key=" at the token start.
    if (tokLen > keyLen + 1 && strncmp(tokStart, key, keyLen) == 0 &&
        tokStart[keyLen] == '=') {
      *outVal = strtol(tokStart + keyLen + 1, nullptr, 10);
      return true;
    }
  }
  return false;
}

} // namespace

// --- TagEvent ---------------------------------------------------------------

TagEvent tagEventFromVatosShot(int team, int damage) {
  TagEvent ev;
  ev.protocolId = "vatos";
  ev.team = team;
  ev.damage = damage;
  ev.playerId = -1;
  ev.rawCode = 0;
  ev.flags = 0;
  return ev;
}

// --- Encoders ----------------------------------------------------------------

int formatHeartbeat(char *out, size_t outSize, const char *id, const char *ip,
                    const char *fw, int team, const char *mode, int hp) {
  return snprintf(out, outSize,
                  "HB id=%s ip=%s fw=%s team=%d mode=%s hp=%d online=1", id, ip,
                  fw, team, mode, hp);
}

int formatHitEvent(char *out, size_t outSize, const char *victim,
                   int shooterTeam, int dmg, const char *proto, int hp,
                   uint32_t ts) {
  return snprintf(
      out, outSize,
      "EVT hit victim=%s shooterTeam=%d dmg=%d proto=%s hp=%d ts=%lu", victim,
      shooterTeam, dmg, proto, hp, (unsigned long)ts);
}

int formatStateEvent(char *out, size_t outSize, const char *state, int hp,
                     uint32_t ts) {
  // `hp` is optional in the grammar: emit it only when non-negative so
  // `s=dead` carries no hp while `s=respawn hp=100` does.
  if (hp >= 0) {
    return snprintf(out, outSize, "EVT state s=%s hp=%d ts=%lu", state, hp,
                    (unsigned long)ts);
  }
  return snprintf(out, outSize, "EVT state s=%s ts=%lu", state,
                  (unsigned long)ts);
}

// --- CTL parser --------------------------------------------------------------

bool parseControl(const char *line, Control &out) {
  out = Control{};
  if (!line) {
    return false;
  }
  const char *rest = nullptr;
  // Require "CTL " as the leading token (host CTL broadcasts have no hostname
  // prefix). Anything else — including our own HB/EVT broadcasts, which are
  // hostname-prefixed — drops to None.
  if (!startsWith(line, "CTL ", &rest)) {
    return false;
  }

  long v = 0;
  if (startsWith(rest, "start", nullptr)) {
    out.kind = ControlKind::Start;
    if (findIntKey(rest, "ts", &v)) {
      out.hasTs = true;
      out.ts = (uint32_t)v;
    }
    return true;
  }
  if (startsWith(rest, "stop", nullptr)) {
    out.kind = ControlKind::Stop;
    return true;
  }
  if (startsWith(rest, "reset", nullptr)) {
    out.kind = ControlKind::Reset;
    if (findIntKey(rest, "hp", &v)) {
      out.hasHp = true;
      out.hp = (int)v;
    }
    return true;
  }
  // "CTL wat ..." — recognised class, unknown subtype: drop.
  out = Control{};
  return false;
}

// --- Config JSON -------------------------------------------------------------

size_t serializeConfig(const ConfigDoc &cfg, char *out, size_t outSize) {
  JsonDocument doc;
  doc["deviceId"] = cfg.deviceId;
  doc["hostname"] = cfg.hostname;
  doc["ownTeam"] = cfg.ownTeam;
  JsonArray teams = doc["enabledTeams"].to<JsonArray>();
  for (size_t i = 0; i < cfg.enabledTeamsCount; i++) {
    teams.add(cfg.enabledTeams[i]);
  }
  doc["protocolId"] = cfg.protocolId;
  doc["brightness"] = cfg.brightness;
  JsonObject colours = doc["teamColours"].to<JsonObject>();
  for (size_t i = 0; i < TeamColourCount; i++) {
    char key[4];
    snprintf(key, sizeof(key), "%d", cfg.teamIndex[i]);
    colours[key] = cfg.teamColour[i];
  }
  // teamSfx is keyed by team index (like teamColours) so it survives reordering.
  JsonObject sfx = doc["teamSfx"].to<JsonObject>();
  for (size_t i = 0; i < TeamColourCount; i++) {
    char key[4];
    snprintf(key, sizeof(key), "%d", cfg.teamIndex[i]);
    sfx[key] = cfg.teamSfx[i];
  }
  doc["deathSfx"] = cfg.deathSfx;
  doc["startHp"] = cfg.startHp;
  return serializeJson(doc, out, outSize);
}

PatchResult applyConfigPatch(const char *json, ConfigDoc &cfg) {
  PatchResult res;
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok ||
      !doc.is<JsonObject>()) {
    snprintf(res.error, sizeof(res.error), "malformed json");
    return res;
  }

  // Stage onto a copy so an unknown field leaves cfg unchanged (contract §8).
  ConfigDoc staged = cfg;
  for (JsonPair kv : doc.as<JsonObject>()) {
    const char *key = kv.key().c_str();
    if (strcmp(key, "deviceId") == 0) {
      if (!kv.value().is<const char *>()) {
        snprintf(res.error, sizeof(res.error), "bad type: deviceId");
        return res;
      }
      strncpy(staged.deviceId, kv.value().as<const char *>(),
              sizeof(staged.deviceId) - 1);
      staged.deviceId[sizeof(staged.deviceId) - 1] = '\0';
    } else if (strcmp(key, "hostname") == 0) {
      if (!kv.value().is<const char *>()) {
        snprintf(res.error, sizeof(res.error), "bad type: hostname");
        return res;
      }
      strncpy(staged.hostname, kv.value().as<const char *>(),
              sizeof(staged.hostname) - 1);
      staged.hostname[sizeof(staged.hostname) - 1] = '\0';
    } else if (strcmp(key, "ownTeam") == 0) {
      staged.ownTeam = kv.value().as<int>();
    } else if (strcmp(key, "protocolId") == 0) {
      if (!kv.value().is<const char *>()) {
        snprintf(res.error, sizeof(res.error), "bad type: protocolId");
        return res;
      }
      strncpy(staged.protocolId, kv.value().as<const char *>(),
              sizeof(staged.protocolId) - 1);
      staged.protocolId[sizeof(staged.protocolId) - 1] = '\0';
    } else if (strcmp(key, "brightness") == 0) {
      staged.brightness = kv.value().as<int>();
    } else if (strcmp(key, "enabledTeams") == 0) {
      JsonArray arr = kv.value().as<JsonArray>();
      size_t i = 0;
      for (JsonVariant t : arr) {
        if (i >= sizeof(staged.enabledTeams) / sizeof(staged.enabledTeams[0])) {
          break;
        }
        staged.enabledTeams[i++] = t.as<int>();
      }
      staged.enabledTeamsCount = i;
    } else if (strcmp(key, "teamColours") == 0) {
      // Replace any matching team index's colour; keys are team indices as
      // JSON strings, values "#RRGGBB".
      for (JsonPair c : kv.value().as<JsonObject>()) {
        const int idx = atoi(c.key().c_str());
        for (size_t i = 0; i < TeamColourCount; i++) {
          if (staged.teamIndex[i] == idx) {
            strncpy(staged.teamColour[i], c.value().as<const char *>(),
                    sizeof(staged.teamColour[i]) - 1);
            staged.teamColour[i][sizeof(staged.teamColour[i]) - 1] = '\0';
          }
        }
      }
    } else if (strcmp(key, "teamSfx") == 0) {
      // Keys are team indices as JSON strings; values are SFX bank indices.
      for (JsonPair c : kv.value().as<JsonObject>()) {
        const int idx = atoi(c.key().c_str());
        for (size_t i = 0; i < TeamColourCount; i++) {
          if (staged.teamIndex[i] == idx) {
            staged.teamSfx[i] = c.value().as<int>();
          }
        }
      }
    } else if (strcmp(key, "deathSfx") == 0) {
      staged.deathSfx = kv.value().as<int>();
    } else if (strcmp(key, "startHp") == 0) {
      const int v = kv.value().as<int>();
      if (v != 4 && v != 8 && v != 16 && v != 32) {
        snprintf(res.error, sizeof(res.error), "startHp must be 4/8/16/32");
        return res;
      }
      staged.startHp = v;
    } else {
      // Unknown field → reject the whole patch (HTTP 400), cfg unchanged.
      snprintf(res.error, sizeof(res.error), "unknown field: %s", key);
      return res;
    }
  }

  cfg = staged;
  res.ok = true;
  return res;
}

// --- Status / mode / command JSON -------------------------------------------

size_t serializeStatus(const StatusDoc &st, char *out, size_t outSize) {
  JsonDocument doc;
  doc["deviceId"] = st.deviceId;
  doc["hostname"] = st.hostname;
  doc["fw"] = st.fw;
  doc["mode"] = st.mode;
  doc["ownTeam"] = st.ownTeam;
  doc["hp"] = st.hp;
  doc["online"] = st.online;
  doc["uptimeMs"] = st.uptimeMs;
  doc["rssi"] = st.rssi;
  return serializeJson(doc, out, outSize);
}

bool parseMode(const char *json, ModeDoc &out) {
  out = ModeDoc{};
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok ||
      !doc.is<JsonObject>()) {
    return false;
  }
  JsonVariant m = doc["mode"];
  if (m.isNull() || !m.is<const char *>()) {
    return false;
  }
  strncpy(out.mode, m.as<const char *>(), sizeof(out.mode) - 1);
  out.mode[sizeof(out.mode) - 1] = '\0';

  JsonVariant timings = doc["timings"];
  if (timings.is<JsonObject>()) {
    if (!timings["darkMinMs"].isNull()) {
      out.hasDarkMin = true;
      out.darkMinMs = timings["darkMinMs"].as<int>();
    }
    if (!timings["darkMaxMs"].isNull()) {
      out.hasDarkMax = true;
      out.darkMaxMs = timings["darkMaxMs"].as<int>();
    }
  }
  out.ok = true;
  return true;
}

size_t serializeMode(const ModeDoc &mode, char *out, size_t outSize) {
  JsonDocument doc;
  doc["mode"] = mode.mode;
  JsonObject timings = doc["timings"].to<JsonObject>();
  if (mode.hasDarkMin) {
    timings["darkMinMs"] = mode.darkMinMs;
  }
  if (mode.hasDarkMax) {
    timings["darkMaxMs"] = mode.darkMaxMs;
  }
  return serializeJson(doc, out, outSize);
}

bool parseCommand(const char *json, CommandDoc &out) {
  out = CommandDoc{};
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok ||
      !doc.is<JsonObject>()) {
    return false;
  }
  JsonVariant c = doc["cmd"];
  if (c.isNull() || !c.is<const char *>()) {
    return false;
  }
  const char *cmd = c.as<const char *>();
  if (strcmp(cmd, "identify") == 0) {
    out.kind = CommandKind::Identify;
    return true;
  }
  if (strcmp(cmd, "bright") == 0) {
    out.kind = CommandKind::Bright;
    out.value = doc["value"].as<int>();
    return true;
  }
  if (strcmp(cmd, "hit") == 0) {
    out.kind = CommandKind::Hit;
    out.team = doc["team"].as<int>();
    out.damage = doc["damage"].as<int>();
    return true;
  }
  if (strcmp(cmd, "debug") == 0) {
    out.kind = CommandKind::Debug;
    out.value = doc["value"].as<int>();
    return true;
  }
  if (strcmp(cmd, "reset") == 0) {
    out.kind = CommandKind::Reset;
    return true;
  }
  return false;
}

} // namespace ControlProto
