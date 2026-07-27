# Game Manager (Spec A): CTL grammar v2 + host match orchestration

**Date:** 2026-07-12
**Status:** Approved design, pre-implementation
**Depends on:** Control-plane contract (`2026-06-15-control-plane-contract.md`), `LaserTag.Client`
**Followed by:** Spec B (firmware behaviours: match cues, target activation, OLED-health role), Spec C (hunt + retaliation modes)

## Goal

A host-side game manager that turns the existing telemetry/control plane into
playable matches: a **CTL grammar v2** any master can speak (the .NET host now,
a firmware master later — the "hybrid" decision), a **pluggable game-mode
framework**, and an **interactive console** with a live scoreboard. Delivers
the long-planned .NET host CLI (handoff Next Steps #7) and the missing UDP CTL
sender as by-products.

## Decomposition (agreed 2026-07-12)

- **Spec A (this doc):** grammar v2 + `LaserTag.Game` framework + `LaserTag.Host`
  console + Deathmatch, Elimination, respawn waves.
- **Spec B:** firmware pass — countdown/gameover cues, activate/deactivate
  handling, `id=` CTL addressing, OLED-shows-health role on the Lolin32.
- **Spec C:** modes needing B — 1-player hunt mode (host activates targets) and
  retaliation mode.

Grammar v2 is designed once, here, so B and C plug in without wire changes.

## CTL grammar v2

Same shape as v1: `CTL <verb> [k=v ...]`, host→subnet broadcast (never
`255.255.255.255`), lossy — every send repeated 3–4×. No hostname prefix
(contract §1.4). Devices parse unknown verbs to `None` and ignore them, so the
host may speak v2 before any firmware understands it.

### Addressing

Every CTL verb gains an **optional `id=<deviceId>`**. A device applies the CTL
only if `id` is absent or equals its own `deviceId`. Per-target control over
plain broadcast; a future firmware master inherits addressing for free.

### Verbs

| Verb | Direction | Meaning |
|---|---|---|
| `CTL start [ts=]` | v1 | Match running; device goes ready/full-health |
| `CTL stop` | v1 | Back to idle |
| `CTL reset [hp=] [id=]` | v1+id | Respawn/top-up to `hp` (default `startHp`); the respawn primitive |
| `CTL countdown n=<secs>` | **new** | Pre-match count-in cue (matrix countdown + siren — firmware = Spec B) |
| `CTL gameover winner=<team\|0>` | **new** | Match end; winner-colour celebration, `0` = draw |
| `CTL activate [id=]` | **new** | Wake a dormant target |
| `CTL deactivate [id=]` | **new** | Target goes dormant: ignores hits, dims (hunt mode, Spec C) |

The grammar is mirrored in `lib/ControlProto` (C++, Spec B) and
`LaserTag.Client` (`UdpMessageParser`/`FormatControl`, this spec).

### Wire-contract limitation: team-granularity scoring

`EVT hit` carries `shooterTeam` only — the Vatos IR protocol encodes team, not
shooter identity. **All scoring is per-team** until enhanced gun electronics /
custom guns can encode a player id (future protocol work, out of scope).
`MatchState` keys scores by team but the key type is isolated so a player key
can slot in later.

## Architecture

### `dotnet/LaserTag.Game` — new class library (pure logic, no sockets)

- **`MatchEngine`** — single-threaded state machine fed by an event channel:
  consumes parsed `Heartbeat`/`HitEvent`/`StateEvent` + timer ticks, owns
  `MatchState`, delegates rules to the active `IGameMode`, emits outbound
  control intents via an injected **`IControlSender`** (interface; fake in
  tests, UDP broadcast in production).
- **`IGameMode`** — plugin contract: `OnMatchStart`, `OnHit`, `OnDeviceState`,
  `OnTick`, plus a respawn-policy hook (per-player delay / synced waves /
  never). Ships with `DeathmatchMode` and `EliminationMode`.
- **`MatchState`** — participants (per-device hp/alive/team, derived from the
  event stream), per-team scores, match clock, phase:
  `Lobby → Countdown → Running → Finished`.
- Injectable clock throughout (same pattern as `DeviceRoster`).

### `dotnet/LaserTag.Host` — new console exe (.NET Generic Host + DI)

- **`UdpTelemetryService`** (BackgroundService) — binds UDP 4210, parses via
  `UdpMessageParser`, writes to a `Channel`.
- **`MatchEngineService`** — drains the channel plus a periodic tick (~250 ms)
  into `MatchEngine`; maintains `DeviceRoster` for liveness.
- **`ConsoleUiService`** — Spectre.Console live scoreboard + event feed +
  command REPL: `devices`, `start dm <duration> [--kill N] [--waves Ns]`,
  `start elim [--timer Nm]`, `stop`, `score`, `reset [id]`,
  `activate|deactivate [id]`, `quit`.

### `LaserTag.Client` additions (shared wire layer)

- **`UdpControlSender : IControlSender`** — subnet-broadcast socket, repeats
  each CTL **3× by default** (constructor-configurable, ~20 ms apart). Closes
  the known gap ("CTL sender must use subnet broadcast").
- Grammar-v2 support in `FormatControl` + `UdpMessageParser` (new
  `ControlKind` values `Countdown`, `GameOver`, `Activate`, `Deactivate`;
  optional `id` on all).

## Match lifecycle (both modes)

`start <mode>` → **Lobby**: snapshot online devices as participants (teams
from heartbeat `team=`) → `CTL countdown n=5` → **Running**: `CTL start` +
`CTL reset hp=<startHp>` (all full-health) → win condition or `stop` →
`CTL gameover winner=<team|0>` → **Finished**: scoreboard frozen until the
next `start`.

## Mode rules & defaults

### Deathmatch — `start dm 5m`

- Score by shooter team: **+1 per hit, +5 per kill** (configurable:
  `--kill N`, `--hit N`).
- Respawn default: **per-player 10 s delay** (`CTL reset id=<victim>` when it
  elapses). `--waves 30s` switches to synced waves: every 30 s, one broadcast
  respawn for all currently-dead participants.
- Winner: highest team score at timer end; tie → `winner=0`.

### Elimination — `start elim`

- No respawns; death is permanent for the round. Display = alive count per team.
- Winner: last team with ≥1 alive participant. Optional `--timer 10m` safety
  cap: on expiry, most alive players wins; tie → draw.

## Error handling & edge cases

- **Lossy UDP:** CTL repeated 3–4×; cues (`countdown`/`gameover`) are
  fire-and-forget — the engine never requires a device ack.
- **Missed EVTs:** heartbeats carry `hp=`; `MatchState` reconciles on every HB.
  An hp drop with no matching `EVT hit` becomes an *unattributed hit* — no
  team scores, but alive/dead tracking stays correct.
- **Dropout mid-match:** offline (3 missed HBs, per `DeviceRoster`) → treated
  as absent (Elimination: excluded from alive counts; DM: team keeps its
  score). On rejoin the engine re-issues the current phase to that device
  (`CTL start` / `reset id=`).
- **Respawn races:** `reset hp=` on an alive device is an idempotent top-up;
  events time-stamped before match start are ignored.
- **Old firmware:** new verbs are ignored (parse to `None`) — matches still
  run, cues just aren't visible until Spec B.

## Testing

- **`LaserTag.Game.Tests`** (new xUnit project): scripted event sequences with
  fake clock + capturing `IControlSender` — full match walkthroughs: DM
  scoring/kill bonus/per-player respawn/waves/tie; Elimination
  last-team-standing/timer cap; dropout + rejoin; HB hp-reconciliation;
  phase transitions.
- **`LaserTag.Client.Tests`**: grammar-v2 golden strings (format + parse round
  trips, `id=` filtering semantics), `UdpControlSender` formatting/repeat
  behaviour (loopback or injected socket abstraction).
- **Manual bench**: `start dm 1m` against the live S3-Matrix, shoot it with
  the Lolin32, watch the live scoreboard; verify gameover freezes scores.

## Out of scope (deferred)

- Firmware handling of `countdown`/`gameover`/`activate`/`deactivate`/`id=`
  (Spec B), hunt + retaliation modes (Spec C), web scoreboard/daemon frontend,
  player-granularity scoring (needs gun electronics), Claude skill wrapper
  over the console commands (after the CLI exists).

## Post-implementation notes (2026-07-12)

Findings from the final-review fix wave on the `LaserTag.Game`/`LaserTag.Host`
implementation, before handoff to Spec B:

- **`id=` compat consequence — RESOLVED:** firmware now enforces `id=`
  addressing (CTL grammar v2.1), so an `id=`-addressed `reset`/`start`/
  `activate`/`deactivate` reaches only the targeted device. Boards still
  running older firmware still ignore the `id=` filter and apply addressed
  CTLs to every device on the arena, so all boards must be reflashed before a
  multi-device match. See
  `docs/superpowers/specs/2026-07-27-chase-mode-design.md` for the firmware
  work that landed the fix.
- **Device reboot mid-match revives it:** hp is device-authoritative and
  volatile (not persisted across reboot). A device that reboots mid-match
  reports hp>0 on its next heartbeat and the engine treats that as the
  documented "reboot recovery" case, sending it a plain `CTL start` — even if
  it was dead when it went offline. This is a known limitation the host
  cannot fix without a protocol change (e.g. the device persisting/reporting
  its pre-reboot death); it is intentionally out of scope here. (Contrast
  with the WiFi-blip case, which the engine does now handle correctly: a
  device that was dead before going offline and is still hp<=0 on its rejoin
  heartbeat gets `CTL reset hp=0 id=`, not `start` — see `MatchEngine.OnHeartbeat`.)
- **Stale-event guard is phase-gating, not device-timestamp comparison:** the
  engine ignores hits/state/heartbeat effects outside `Running` by gating on
  `Phase`, not by comparing the event's `ts` (device millis, resets on
  reboot) against a wall-clock cutoff. Device `ts` is monotonic-since-boot,
  not comparable across devices or to host wall-clock, so phase-gating is the
  correct mechanism here — noted so a future maintainer doesn't "fix" this
  into a device-ts comparison that would silently break on reboot.
- **v1 UI is REPL + on-demand score, not a live-refresh layout:** `score` is
  a command the operator runs, not a continuously repainted scoreboard. A
  live-refresh TUI (Spectre `Live`) was considered and deferred — the console
  event feed (`HIT`/`STATE`/`OFFLINE`/`PHASE`/`GAME OVER`) plus on-demand
  `score` is sufficient for the current bench-play scale and keeps the REPL
  simple.
