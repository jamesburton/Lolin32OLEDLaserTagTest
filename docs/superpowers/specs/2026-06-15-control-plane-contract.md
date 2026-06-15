# Control-Plane Wire Contract (authoritative)

**Date:** 2026-06-15
**Status:** Authoritative source of truth for BOTH firmware and .NET host.
**Companion to:** `2026-06-15-control-plane-design.md` (the design rationale).

> This document defines the exact wire surfaces shared by the device firmware
> and the .NET host. Both implementations MUST conform to the field names,
> types, message grammar, JSON shapes, and **golden vectors** below. The golden
> vectors are test fixtures: each side writes unit tests asserting against these
> exact strings/objects so the two surfaces cannot drift.

---

## 1. UDP line-protocol (port 4210)

### 1.1 Framing

- One message per UDP packet, ASCII, no trailing newline required by parsers
  (accept with or without a trailing `\n`).
- **Every packet is prefixed by the sending device's hostname and a space**,
  because `TagNet::event()` prepends `deviceName + ' '` to the payload. So the
  on-wire form is:

  ```
  <hostname> <CLASS> [subtype] key=value key=value ...
  ```

- The host parser MUST take the **first whitespace-delimited token as the source
  hostname**, then parse `<CLASS>` and the rest. Host→device messages (`CTL`)
  are broadcast by the host WITHOUT a hostname prefix (they begin at `CTL`); the
  firmware parser MUST accept `CTL` as the first token.
- Tokens are space-separated. After the class/subtype, every remaining token is
  `key=value` (value has no spaces). Unknown keys MUST be ignored, not rejected.
- Parsers MUST tolerate loss and garbage: an unparseable line is dropped and
  counted, never fatal.

### 1.2 Message classes

| Class | Direction | Subtypes |
|-------|-----------|----------|
| `HB`  | device → broadcast | (none) |
| `EVT` | device → broadcast | `hit`, `state` |
| `CTL` | host → broadcast (device-inbound) | `start`, `stop`, `reset` |

### 1.3 Field types

| Key | Type | Notes |
|-----|------|-------|
| `id` | string (hex) | deviceId, MAC-derived, stable |
| `ip` | string (dotted-quad) | current IP |
| `fw` | string (semver) | firmware version, e.g. `2.0.0` |
| `team` | int | team index (0..N); device `ownTeam` |
| `mode` | string | active mode id, e.g. `team-colours`, `idle` |
| `hp` | int | current health 0..100 |
| `online` | int | `1` (heartbeats are only sent while online) |
| `victim` | string | deviceId of the hit device |
| `shooterTeam` | int | firing team index |
| `dmg` | int | damage 1..4 |
| `proto` | string | protocolId that decoded the shot, e.g. `vatos` |
| `s` | string | state value: one of `ready`, `idle`, `stunned`, `dead`, `respawn`. `stunned` = transient post-hit cooldown while still alive (hp>0); `dead` = health depleted (hp==0), held until respawn/reset |
| `ts` | uint (ms) | device `millis()` timestamp |

### 1.4 Message grammar

```
HB  id=<hex> ip=<ip> fw=<semver> team=<int> mode=<str> hp=<int> online=1
EVT hit victim=<hex> shooterTeam=<int> dmg=<int> proto=<str> hp=<int> ts=<ms>
EVT state s=<ready|idle|stunned|dead|respawn> [hp=<int>] ts=<ms>
CTL start [ts=<ms>]
CTL stop
CTL reset [hp=<int>]
```

### 1.5 Golden vectors (UDP)

Each row is the **exact on-wire string** and the fields a parser MUST extract.
(`source` = the leading hostname token; absent for `CTL`.)

| On-wire string | Parsed |
|----------------|--------|
| `lasertag-matrix HB id=a1b2c3 ip=192.168.1.24 fw=2.0.0 team=2 mode=team-colours hp=100 online=1` | Heartbeat{ source=`lasertag-matrix`, id=`a1b2c3`, ip=`192.168.1.24`, fw=`2.0.0`, team=2, mode=`team-colours`, hp=100, online=true } |
| `lasertag-matrix EVT hit victim=a1b2c3 shooterTeam=2 dmg=2 proto=vatos hp=80 ts=12345` | HitEvent{ source=`lasertag-matrix`, victim=`a1b2c3`, shooterTeam=2, dmg=2, proto=`vatos`, hp=80, ts=12345 } |
| `lasertag-matrix EVT state s=dead ts=12500` | StateEvent{ source=`lasertag-matrix`, s=`dead`, hp=null, ts=12500 } |
| `lasertag-matrix EVT state s=respawn hp=100 ts=20000` | StateEvent{ source=`lasertag-matrix`, s=`respawn`, hp=100, ts=20000 } |
| `CTL start ts=30000` | Control{ kind=`start`, ts=30000 } |
| `CTL stop` | Control{ kind=`stop` } |
| `CTL reset hp=100` | Control{ kind=`reset`, hp=100 } |

Garbage/partial lines that MUST parse to "drop, no throw":
`lasertag-matrix EVT` · `HB id=` · `random noise 123` · `` (empty) · `EVT wat foo=bar`

---

## 2. REST contract (JSON over HTTP, served by the device)

- Content-Type `application/json`. Bodies are UTF-8 JSON objects.
- **Write requests (`PATCH`/`POST`) MUST send a JSON (or other non-form)
  `Content-Type`.** The device's ESP32 `WebServer` only exposes a request body
  for non-form content types; an `application/x-www-form-urlencoded` body (the
  default of `curl -d`) is discarded, yielding `400 {"error":"empty body —
  send Content-Type: application/json"}`. The .NET client sets the header
  automatically; with raw `curl` pass `-H "Content-Type: application/json"`.
- `PATCH /api/config` applies only the provided keys; unknown keys → `400`.
- Status codes: `200` success, `400` malformed/unknown field, `404` unknown
  route, `405` wrong method, `500` NVS write failure (config unchanged).

### 2.1 Routes

| Method | Route | Body | Success |
|--------|-------|------|---------|
| `GET`  | `/api/status` | — | `200` StatusDoc |
| `GET`  | `/api/config` | — | `200` ConfigDoc |
| `PATCH`| `/api/config` | partial ConfigDoc | `200` full ConfigDoc |
| `POST` | `/api/mode`   | ModeDoc | `200` ModeDoc |
| `POST` | `/api/command`| CommandDoc | `200` `{"ok":true}` |

### 2.2 Golden vectors (REST)

**GET /api/status → 200**
```json
{
  "deviceId": "a1b2c3",
  "hostname": "lasertag-matrix",
  "fw": "2.0.0",
  "mode": "team-colours",
  "ownTeam": 2,
  "hp": 100,
  "online": true,
  "uptimeMs": 123456,
  "rssi": -67
}
```

**GET /api/config → 200** (ConfigDoc — the persisted NVS fields)
```json
{
  "deviceId": "a1b2c3",
  "hostname": "lasertag-matrix",
  "ownTeam": 2,
  "enabledTeams": [1, 2, 3, 4],
  "protocolId": "vatos",
  "brightness": 13,
  "teamColours": { "1": "#0000FF", "2": "#FF0000", "3": "#00FF00", "4": "#FFFFFF" }
}
```

**PATCH /api/config** request `{ "ownTeam": 3, "brightness": 20 }` → **200** returns
the full ConfigDoc with `ownTeam:3, brightness:20` and other fields unchanged.

**PATCH /api/config** request `{ "foo": 1 }` → **400**
```json
{ "error": "unknown field: foo" }
```

**POST /api/mode** request → **200** (echoes back; NOT persisted)
```json
{ "mode": "team-colours", "timings": { "darkMinMs": 5000, "darkMaxMs": 15000 } }
```

**POST /api/command** requests (each → **200** `{"ok":true}`):
```json
{ "cmd": "identify" }
{ "cmd": "bright", "value": 20 }
{ "cmd": "hit", "team": 2, "damage": 2 }
{ "cmd": "debug", "value": 1 }
```

`deviceId` derives from the lower 3 bytes of the WiFi MAC, lowercase hex, 6
chars. `teamColours` keys are team indices as JSON strings; values are
`#RRGGBB`. `enabledTeams` is an array of ints.

---

## 3. TagEvent (firmware-internal, the source of `EVT hit`)

The firmware introduces a generic, protocol-agnostic value type. The full
polymorphic `IrProtocol` interface is **deferred (YAGNI)** to the mode-framework
spec — for now a single Vatos adapter maps `Vatos::Shot` → `TagEvent`.

```cpp
struct TagEvent {
  const char *protocolId; // e.g. "vatos"
  int team;               // team index, -1 if unknown
  int damage;             // 1..4, -1 if unknown
  int playerId;           // -1 if none
  uint32_t rawCode;       // raw decoded payload (0 if n/a)
  uint32_t flags;         // reserved, 0 for now
};
```

The control plane only ever serializes `TagEvent` fields into `EVT hit`; it
never references Vatos directly.

---

## 4. Timing constants

| Constant | Value | Where |
|----------|-------|-------|
| Heartbeat interval | 2000 ms | device |
| Offline timeout | 6000 ms (3 missed beats) | host roster |
| UDP port | 4210 | both (existing `TagNet::UdpPort`) |
