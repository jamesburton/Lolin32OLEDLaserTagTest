/*
 * ControlProto — pure, Arduino-free control-plane logic for the laser-tag
 * platform. Implements the UDP line-protocol encoders/parser, the TagEvent
 * value type, and the REST JSON (de)serialization defined in
 * docs/superpowers/specs/2026-06-15-control-plane-contract.md.
 *
 * This library contains ONLY portable C++ (no Arduino.h / FastLED / WiFi) so it
 * unit-tests natively under `platform = native`. The Arduino side (matrix_main,
 * TagNet) owns all policy (hp accounting, visual-state mapping, NVS, WiFi) and
 * calls into these dumb format/parse helpers.
 *
 * Buffers: encoders write into a caller-supplied char buffer; nothing here
 * allocates beyond ArduinoJson's documents for the REST helpers.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <ArduinoJson.h>

namespace ControlProto {

// --- §3 TagEvent ------------------------------------------------------------

/// <summary>
/// Generic, protocol-agnostic decoded IR event (contract §3). The control plane
/// only serializes these fields into an `EVT hit` line; it never references
/// Vatos directly. The full polymorphic IrProtocol interface is deferred (YAGNI).
/// </summary>
struct TagEvent {
  const char *protocolId; ///< e.g. "vatos"
  int team;               ///< team index, -1 if unknown
  int damage;             ///< 1..4, -1 if unknown
  int playerId;           ///< -1 if none
  uint32_t rawCode;       ///< raw decoded payload (0 if n/a)
  uint32_t flags;         ///< reserved, 0 for now
};

/// <summary>
/// Adapter building a TagEvent from a Vatos shot's fields. Takes plain ints so
/// this pure library need not include Vatos; the Arduino side passes
/// shot.team / shot.damage.
/// </summary>
/// <param name="team">Firing team index (1..4 for Vatos).</param>
/// <param name="damage">Shot damage (1..4).</param>
/// <returns>A TagEvent with protocolId="vatos" and the supplied fields.</returns>
TagEvent tagEventFromVatosShot(int team, int damage);

// --- §1 UDP line-protocol encoders ------------------------------------------
//
// All encoders produce the payload WITHOUT the hostname prefix (TagNet::event
// prepends `deviceName + ' '`). Field order matches contract §1.4 exactly and
// there is no trailing newline.

/// <summary>
/// Formats a heartbeat line (contract §1.4):
/// `HB id=&lt;hex&gt; ip=&lt;ip&gt; fw=&lt;semver&gt; team=&lt;int&gt; mode=&lt;str&gt; hp=&lt;int&gt; online=1`.
/// </summary>
/// <param name="out">Destination buffer.</param>
/// <param name="outSize">Size of the destination buffer.</param>
/// <param name="id">deviceId (MAC-derived lowercase hex).</param>
/// <param name="ip">Current IP as a dotted-quad string.</param>
/// <param name="fw">Firmware version string, e.g. "2.0.0".</param>
/// <param name="team">Team index (ownTeam).</param>
/// <param name="mode">Active mode id, e.g. "team-colours".</param>
/// <param name="hp">Current health 0..100.</param>
/// <returns>Number of characters written (excluding the NUL), or -1 on error.</returns>
int formatHeartbeat(char *out, size_t outSize, const char *id, const char *ip,
                    const char *fw, int team, const char *mode, int hp);

/// <summary>
/// Formats a hit-event line (contract §1.4):
/// `EVT hit victim=&lt;hex&gt; shooterTeam=&lt;int&gt; dmg=&lt;int&gt; proto=&lt;str&gt; hp=&lt;int&gt; ts=&lt;ms&gt;`.
/// </summary>
/// <param name="out">Destination buffer.</param>
/// <param name="outSize">Size of the destination buffer.</param>
/// <param name="victim">deviceId of the hit device.</param>
/// <param name="shooterTeam">Firing team index.</param>
/// <param name="dmg">Damage 1..4.</param>
/// <param name="proto">protocolId that decoded the shot, e.g. "vatos".</param>
/// <param name="hp">Resulting health after damage.</param>
/// <param name="ts">Device millis() timestamp.</param>
/// <returns>Number of characters written (excluding the NUL), or -1 on error.</returns>
int formatHitEvent(char *out, size_t outSize, const char *victim,
                   int shooterTeam, int dmg, const char *proto, int hp,
                   uint32_t ts);

/// <summary>
/// Formats a state-event line (contract §1.4):
/// `EVT state s=&lt;ready|idle|dead|respawn&gt; [hp=&lt;int&gt;] ts=&lt;ms&gt;`.
/// The optional `hp=` token is emitted only when <paramref name="hp"/> is &gt;= 0
/// (pass a negative value to omit it, matching `s=dead` which carries no hp).
/// </summary>
/// <param name="out">Destination buffer.</param>
/// <param name="outSize">Size of the destination buffer.</param>
/// <param name="state">State value: one of ready, idle, dead, respawn.</param>
/// <param name="hp">Health to include, or a negative value to omit hp.</param>
/// <param name="ts">Device millis() timestamp.</param>
/// <returns>Number of characters written (excluding the NUL), or -1 on error.</returns>
int formatStateEvent(char *out, size_t outSize, const char *state, int hp,
                     uint32_t ts);

// --- §1 inbound CTL parser --------------------------------------------------

/// <summary>Kind of an inbound control message (contract §1.2 / §1.4).</summary>
enum class ControlKind {
  None,  ///< not a CTL line / unparseable (drop)
  Start, ///< `CTL start [ts=<ms>]`
  Stop,  ///< `CTL stop`
  Reset, ///< `CTL reset [hp=<int>]`
};

/// <summary>
/// Parsed inbound control message. Optional fields use a `has*` flag because
/// `ts`/`hp` are optional in the grammar.
/// </summary>
struct Control {
  ControlKind kind = ControlKind::None;
  bool hasTs = false;
  uint32_t ts = 0;
  bool hasHp = false;
  int hp = 0;
};

/// <summary>
/// Parses an inbound line into a Control. Tolerant of garbage/partial input:
/// anything that is not a recognised `CTL start|stop|reset` line yields
/// kind=None (the caller drops it). Never throws, never crashes.
///
/// The line is the raw inbound text. Host-broadcast CTL lines have no hostname
/// prefix and begin at `CTL`; this parser requires `CTL` as the first token.
/// </summary>
/// <param name="line">NUL-terminated inbound line (may be null/empty).</param>
/// <param name="out">Receives the parsed control message.</param>
/// <returns>True if a valid CTL line was parsed (out.kind != None).</returns>
bool parseControl(const char *line, Control &out);

// --- §2.2 REST JSON config ---------------------------------------------------

/// Number of distinct team colours carried in a ConfigDoc (Vatos: 1..4).
constexpr size_t TeamColourCount = 4;

/// <summary>
/// In-memory mirror of the persisted ConfigDoc (contract §2.2). teamColours is
/// stored as a fixed map of the four Vatos team indices (1..4) to "#RRGGBB"
/// strings; enabledTeams as a small int array.
/// </summary>
struct ConfigDoc {
  char deviceId[8] = "";                 ///< 6-char lowercase hex + NUL
  char hostname[32] = "";                ///< OTA / mDNS name
  int ownTeam = 0;                       ///< index into the team set
  int enabledTeams[8] = {0};             ///< active subset (values)
  size_t enabledTeamsCount = 0;          ///< number of valid enabledTeams
  char protocolId[16] = "vatos";         ///< which IrProtocol decoded
  int brightness = 13;                   ///< LED brightness 0..255
  int teamIndex[TeamColourCount] = {1, 2, 3, 4};        ///< colour map keys
  char teamColour[TeamColourCount][8] = {"#0000FF", "#FF0000", "#00FF00",
                                         "#FFFFFF"};     ///< "#RRGGBB" values
};

/// <summary>Serializes a ConfigDoc into the contract §2.2 ConfigDoc JSON.</summary>
/// <param name="cfg">The config to serialize.</param>
/// <param name="out">Destination buffer.</param>
/// <param name="outSize">Size of the destination buffer.</param>
/// <returns>Number of bytes written (excluding the NUL).</returns>
size_t serializeConfig(const ConfigDoc &cfg, char *out, size_t outSize);

/// <summary>Result of a PATCH apply: ok, or an error message for HTTP 400.</summary>
struct PatchResult {
  bool ok = false;       ///< true if every provided key was known and applied
  char error[48] = "";   ///< e.g. "unknown field: foo" (empty when ok)
};

/// <summary>
/// Applies a partial ConfigDoc (PATCH body) onto an existing config in place.
/// Only the provided keys change. Any unknown key fails the whole patch and
/// reports `unknown field: <name>` (contract §2.2 / §8 → HTTP 400); on failure
/// <paramref name="cfg"/> is left UNCHANGED.
/// </summary>
/// <param name="json">The PATCH request body (a JSON object).</param>
/// <param name="cfg">The config to update in place on success.</param>
/// <returns>A PatchResult: ok, or the error to surface as 400.</returns>
PatchResult applyConfigPatch(const char *json, ConfigDoc &cfg);

// --- §2.2 REST JSON status / mode / command ---------------------------------

/// <summary>Runtime status for GET /api/status (contract §2.2 StatusDoc).</summary>
struct StatusDoc {
  char deviceId[8] = "";
  char hostname[32] = "";
  char fw[12] = "";
  char mode[24] = "";
  int ownTeam = 0;
  int hp = 100;
  bool online = false;
  uint32_t uptimeMs = 0;
  int rssi = 0;
};

/// <summary>Serializes a StatusDoc into the contract §2.2 StatusDoc JSON.</summary>
/// <param name="st">The status to serialize.</param>
/// <param name="out">Destination buffer.</param>
/// <param name="outSize">Size of the destination buffer.</param>
/// <returns>Number of bytes written (excluding the NUL).</returns>
size_t serializeStatus(const StatusDoc &st, char *out, size_t outSize);

/// <summary>Parsed POST /api/mode body (contract §2.2 ModeDoc).</summary>
struct ModeDoc {
  bool ok = false;        ///< true if the body parsed as an object with a mode
  char mode[24] = "";     ///< the requested mode id
  bool hasDarkMin = false;
  int darkMinMs = 0;
  bool hasDarkMax = false;
  int darkMaxMs = 0;
};

/// <summary>
/// Parses a POST /api/mode body into a ModeDoc. Recognises `mode` and the
/// `timings.darkMinMs` / `timings.darkMaxMs` fields. Unknown extras are ignored
/// (mode is runtime, not persisted). ok is false only on malformed JSON or a
/// missing mode.
/// </summary>
/// <param name="json">The request body.</param>
/// <param name="out">Receives the parsed mode + timings.</param>
/// <returns>True if a usable ModeDoc was parsed.</returns>
bool parseMode(const char *json, ModeDoc &out);

/// <summary>
/// Re-serializes a ModeDoc as the echoed §2.2 response
/// `{ "mode": ..., "timings": { "darkMinMs": ..., "darkMaxMs": ... } }`.
/// Only the timings actually present are emitted.
/// </summary>
/// <param name="mode">The mode to echo back.</param>
/// <param name="out">Destination buffer.</param>
/// <param name="outSize">Size of the destination buffer.</param>
/// <returns>Number of bytes written (excluding the NUL).</returns>
size_t serializeMode(const ModeDoc &mode, char *out, size_t outSize);

/// <summary>The structured one-shot command kinds for POST /api/command.</summary>
enum class CommandKind {
  None,     ///< malformed / unknown cmd
  Identify, ///< `{ "cmd": "identify" }`
  Bright,   ///< `{ "cmd": "bright", "value": <int> }`
  Hit,      ///< `{ "cmd": "hit", "team": <int>, "damage": <int> }`
  Debug,    ///< `{ "cmd": "debug", "value": <int> }`
};

/// <summary>Parsed POST /api/command body (contract §2.2 CommandDoc).</summary>
struct CommandDoc {
  CommandKind kind = CommandKind::None;
  int value = 0;  ///< bright/debug value
  int team = 0;   ///< hit team
  int damage = 0; ///< hit damage
};

/// <summary>
/// Parses a POST /api/command body into a CommandDoc. Recognises the four
/// commands in §2.2; an unrecognised or missing cmd yields kind=None (the
/// caller answers 400).
/// </summary>
/// <param name="json">The request body.</param>
/// <param name="out">Receives the parsed command.</param>
/// <returns>True if a recognised command was parsed.</returns>
bool parseCommand(const char *json, CommandDoc &out);

} // namespace ControlProto
