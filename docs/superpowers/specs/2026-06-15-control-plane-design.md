# Control-Plane Design (V2 foundation)

**Date:** 2026-06-15
**Status:** Approved (brainstorming), pending implementation plan
**Depends on:** V1 firmware (`TagNet`, `IrFramer`, `Vatos`); commit `58b755a`, tag `v1.0`

## 1. Scope & boundary

This spec defines the **control plane** for the laser-tag platform: device
discovery, the config schema, the REST contract, the UDP message protocol, and
the telemetry/event model.

It defines **what events devices emit and how config moves** — **not** scoring
rules or match orchestration. Those belong to later specs (host/mode-framework).
This spec **extends the existing `TagNet` library**; it does not replace it.

The protocol abstraction (`IrProtocol` / generic `TagEvent`) and the generic
indexed team model are pre-approved decisions from earlier brainstorming; §7
records only how they touch this layer.

## 2. Architecture overview

```
┌─────────────┐   REST/JSON (config, CRUD, status)    ┌──────────────┐
│   Host /    │ ◄───────────────────────────────────► │   Device     │
│ .NET client │   UDP line-protocol (port 4210):       │ (ESP32 +     │
│  + CLI      │   ◄── heartbeat (discovery/roster)     │  TagNet)     │
│             │   ◄── telemetry (hit/state events)     │              │
│             │   ──► low-latency broadcast control     │              │
└─────────────┘                                         └──────────────┘
```

- **REST (TCP/JSON)** — reliable request/response: read/write config, query
  status, set mode/team. **OpenAPI describes this surface only.**
- **UDP (line-protocol, port 4210)** — discovery heartbeat, telemetry events,
  and time-critical broadcast control (e.g. simultaneous "game start"). The
  `.NET` client needs a **hand-written UDP parser**; the UDP grammar is
  documented separately from the OpenAPI doc. (OpenAPI cannot express a UDP
  line-protocol, so "clean OpenAPI" applies to REST, not the whole contract.)
- **mDNS** — best-effort, **non-load-bearing**. Provides human-friendly
  `.local` names *where it resolves*; the UDP heartbeat is the authoritative
  roster. Retained at user request (re-added after being rejected last session,
  where it did not resolve on the Windows host) purely for friendly display
  names — nothing depends on it.

### Wire format

JSON for REST; a compact, defined **line-protocol** for UDP (one line per
message). Rationale: REST JSON is clean for the `.NET` client + OpenAPI
generation, while the line-protocol matches what the firmware already does well
(cheap parsing, no per-packet JSON allocation) and keeps `TagMonitor` output
human-readable. Trade-off accepted: two formats to document.

## 3. Config schema & persistence

A rebooted device returns as "itself" (identity + prefs) in **neutral idle**;
the host re-pushes mode/timings on connect. This minimizes NVS wear and avoids a
crashed device rejoining mid-game in stale state.

| Field          | Persist (NVS) | Notes                                          |
|----------------|:-------------:|------------------------------------------------|
| `deviceId`     | ✅            | stable unique id (derive from MAC)             |
| `hostname`     | ✅            | OTA / mDNS name                                |
| `ownTeam`      | ✅            | index into the team set                        |
| `enabledTeams` | ✅            | active subset of `0..N`                        |
| `protocolId`   | ✅            | which `IrProtocol` (e.g. `vatos`)              |
| `brightness`   | ✅            | LED brightness                                 |
| `teamColours`  | ✅            | optional override map (default Vatos B/R/G/W)  |
| WiFi creds     | ✅            | already in NVS today                           |
| `activeMode`   | ⛔ runtime    | host re-pushes on connect                      |
| mode timings   | ⛔ runtime    | e.g. dark / diamond delays                     |
| health / lives | ⛔ runtime    | device-owned (see §6)                          |
| scores / hit state | ⛔ runtime | host-owned match state                        |

## 4. Discovery & heartbeat (UDP)

Each device broadcasts a heartbeat to the LAN broadcast address on port 4210
every **~2 s** (tunable). The host listens passively and builds a live roster; a
device is marked **offline** after ~3 missed beats (~6 s).

```
HB id=a1b2c3 ip=192.168.1.24 fw=2.0.0 team=2 mode=team-colours hp=100 online=1
```

A roster entry is the last-seen timestamp plus the parsed fields. A new `id`
auto-registers. The mDNS name, where it resolves, is attached for display only.

## 5. REST contract (config/CRUD + status)

JSON over HTTP, served by the device, OpenAPI-described.

| Method  | Route          | Purpose                                                          |
|---------|----------------|------------------------------------------------------------------|
| `GET`   | `/api/status`  | live runtime status (mode, hp, team, online, fw, uptime, RSSI)   |
| `GET`   | `/api/config`  | full persisted config (the §3 NVS fields)                        |
| `PATCH` | `/api/config`  | partial update; persists changed NVS fields, returns new config  |
| `POST`  | `/api/mode`    | set runtime `activeMode` + timings (not persisted)               |
| `POST`  | `/api/command` | structured one-shot actions (`identify`, `bright`, test `hit`, `debug`) |

- Existing `GET /` remains the human status page.
- Existing `GET /cmd?c=…` is kept as a **deprecated alias** during transition.
- `PATCH` semantics: only provided fields change; unknown fields rejected `400`.

## 6. UDP message protocol & the hybrid event model

One line per message: `KEY type k=v k=v …`. Three message classes.

**Heartbeat** (device → broadcast): see §4.

**Telemetry / events** (device → broadcast):

```
EVT hit victim=a1b2c3 shooterTeam=2 dmg=2 proto=vatos hp=80 ts=12345
EVT state s=dead ts=12500
EVT state s=respawn hp=100 ts=20000
```

**Low-latency control** (host → broadcast):

```
CTL start ts=…        # simultaneous game start
CTL stop
CTL reset hp=100      # force all devices to a state
```

### Hybrid authority model (stated decision)

- The **device is authoritative for its own health/lives.** It applies damage
  locally for instant feedback (death animation) the moment it decodes a valid
  `TagEvent`, then emits the `EVT hit`.
- The **host's match state is a mirror derived from the event stream.** During
  play the host never pushes authoritative hp; it tallies match-level scoring
  from `EVT` lines. One scorekeeper per concern — no drift.
- **Reboot mid-game:** the device rejoins **fresh at full health in neutral
  idle** (casual-friendly; no persisted game state). The host detects the
  re-register via a new/reset heartbeat and **flags the device "rejoined"** in
  the roster so the operator or mode can decide what to do — it does **not**
  silently restore old hp. `CTL reset` / `CTL start` can re-arm it.
- `shotFired` is **out of scope**: `EVT hit` already carries `shooterTeam`, so
  shot/hit correlation is unnecessary here. It can be added later if a mode
  requires it.

## 7. How the protocol & team abstractions thread through

The control plane stays **protocol- and mode-agnostic**:

- Devices decode IR via an `IrProtocol` and produce a generic **`TagEvent`**
  (`protocolId, team?, damage?, playerId?, rawCode, flags`). The control plane
  only serializes `TagEvent` fields into `EVT` lines — it never hard-codes
  Vatos. Adding/swapping a protocol changes `protocolId` + decode, not the wire
  contract.
- Teams are a generic indexed set `0..N`; `team=` on the wire is just the index.
  The colour map is config (`teamColours`), defaulting to Vatos
  Blue/Red/Green/White.

## 8. Error handling

- **REST:** `400` malformed / unknown field on `PATCH`; `404` unknown route;
  `405` wrong method; success returns the resulting resource. NVS write failure
  → `500` with config unchanged (write-then-confirm).
- **UDP:** fire-and-forget, no acks. The receiver tolerates loss and garbage —
  unparseable lines are dropped and counted, never fatal (matches reality: weak
  RSSI makes telemetry lossy, not broken). Heartbeat loss is the liveness
  signal, so loss is *information*, not an error.
- **Offline device:** `TagNet` network calls are already no-ops with no creds,
  so game logic stays network-agnostic; REST is simply unreachable and the host
  marks the device offline.
- **Config/firmware mismatch:** the heartbeat carries `fw=`; the host can warn
  on version skew but does not block.

## 9. Testing

- **Host-side (.NET):** unit-test the UDP line parser (well-formed, partial,
  garbage, unknown keys); the REST client against the OpenAPI contract; roster
  liveness/timeout logic with simulated heartbeats.
- **Firmware:** the line grammar is simple enough to unit-test the encode/parse
  helpers natively (host-compiled, no board). Manual integration via
  `TagMonitor` (readable UDP) and `curl`/CLI against `/api/*`.
- **End-to-end:** the existing matrix at `192.168.1.24` over OTA — `PATCH` team
  and brightness, `POST /api/mode`, fire a Vatos shot, confirm `EVT hit` + local
  death animation + host roster/score update.

## 10. Build order (within this spec)

1. Firmware: line-protocol encode/parse helpers + `TagEvent` serialization
   (unit-tested natively).
2. Firmware: heartbeat broadcaster + structured `EVT` / `CTL` over the existing
   `TagNet` UDP socket.
3. Firmware: `/api/*` REST routes + JSON config in NVS (extend `onLine` /
   `onStatus`).
4. Host: UDP parser + roster + REST client + OpenAPI doc.
5. Wire it end-to-end on the matrix; **then** the mode-framework + Team-Colours
   + CLI/skill specs build on top.
