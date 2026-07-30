# Handoff

## Goal
Open, extensible laser-tag platform around the (reverse-engineered) **Vatos** IR
protocol. Two ESP32 boards today, designed to scale. Built in layers: V1 (decode/
TX) → V2 control plane (REST+UDP, .NET host) → board-capability HAL → game modes.
Codes & features behind interfaces so other guns/protocols and boards plug in.

## Current State

### Team assignment SHIPPED + fw 2.2.0 fleet-wide (2026-07-29)
Closes the "no team assignment" gap. **Team 0 = `none` = a neutral target and
is now the default**: shootable by everyone, scores for the SHOOTER's team,
never a side that can win. That matches what the firmware always did
physically — it has **never own-team filtered**, so every decoded shot damages
whatever board receives it.
- **Firmware 2.2.0:** `ownTeam` is validated (`0-4`, with an explicit TYPE
  check — ArduinoJson reads a non-numeric as 0, which is now legal, so
  `{"ownTeam":"red"}` would otherwise silently un-team a board). NVS default
  moved 2 -> `cp::TeamNone`. Boards provisioned earlier keep their stored team.
- **Surfaces:** host `team <id|all> <0-4|none>` and `teams split <n>`
  (round-robin over a STABLE id order, so a fleet always splits the same way);
  `POST /api/team` on the web manager; team buttons on each Devices card in
  both shells. All route to `PATCH /api/config` — teams are persisted config,
  NOT control plane.
- **Engine:** neutrals excluded from elimination's last-team-standing (they
  would otherwise win or keep a round alive), and `AddScore` refuses team 0 so
  a malformed event can't open a "team none" bucket that wins.
- **`ota all` PROVEN:** flashed all four boards 2.1.0 -> 2.2.0 over HTTP in
  ~50 s total, no espota, no per-board IP chasing.
- **Verified live: 25/26** — assign-to-neutral, heartbeat mirroring, 2v2 split,
  and device-side rejection of `5`/`-1`/`"red"` with the team surviving intact.

### ✅ IR board-to-board VERIFIED — first time ever (2026-07-29)
The largest untested path in the project is now exercised end to end. The
prototype board (`eb278c`, raw-drive IR TX, no 2N2222A) fires; `eb20f8`
receives, decodes and scores.
- **Damage fidelity exact** for all four Vatos damage values: a dmg-N shot
  costs exactly N hp (1->31, 2->30, 3->29, 4->28 from startHp 32).
- **Team fidelity exact** for all four teams, confirmed in the telemetry
  itself, not just inferred from hp:
  `EVT hit victim=eb20f8 shooterTeam=1..4 dmg=1 proto=vatos hp=31`.
- **Only the prototype board can transmit.** A sweep with every board as
  shooter landed hits *only* from `eb278c`; the three PCB boards registered
  nothing in any direction. Their carrier has the 2N2222A driver footprint but
  no IR LED fitted yet, and `fire` returns `ok:true` on all of them regardless
  — `IrTx::present()` comes from the compile-time BoardProfile, so it is NOT
  evidence an emitter exists.
- **Range is very short** (raw GPIO drive, no transistor): boards must be
  aimed at close range. It took an aim adjustment to get the first hit.
- **Rapid fire is dropped BY DESIGN, not lost.** `applyHit` runs only while
  `vis == Vis::Rainbow`, so shots arriving during the post-hit flash/stun
  window are ignored — a 600 ms burst of 10 shots landed 4. Space shots ~4 s
  apart when measuring, or the miss rate looks like an RF/alignment fault.

### FLEET ON 2.1.0 + first 4-board integration test PASSED (2026-07-29)
First session with **all four S3 boards powered at once**. Boards 1 (`752b38`)
and 2 (`eb278c`) were still on the pre-2.1.0 image, so each got its one
bootstrap espota; boards 3+4 were already current. **Fleet is now uniformly
2.1.0, so `ota all` over HTTP covers every board — espota is retired** (except
for a future board's first flash, and for the Lolin32, which is offline).

**Integration test (54 assertions across two runs, all green).** Driven through
the web manager's JSON API — the same surface the phone app uses — while
reading each board's own `/api/status` back directly, so a host-vs-board
disagreement would show rather than hide. Scripts are throwaway (scratchpad),
but the method is worth repeating:
- **Verified on all four:** `identify`; `bright` (round-tripped through
  `/api/config`); `hit`; `reset`; `CTL start`/`stop`; and the telemetry path —
  every board's hp drop reached the host within ~6 s, a full kill scored team 1
  and flipped `alive=false`, and the match ran Countdown → Running → Finished.
- **`CTL start` reaching a board is observable via hp**, not via `mode`: Start
  sets `hp = config.startHp`, so damage the fleet, start a match, and watch all
  boards return to 32 with no explicit reset. This is the check to reuse.
- **Two things this did NOT prove.** All damage was synthetic `{"cmd":"hit"}`
  over HTTP, which exercises scoring but **not the IR path** — a `fire` from
  one board registered on nothing (expected; nothing was aimed). And
  **activate/deactivate remains unconfirmed**: it changes `vis` only, which is
  not exposed over REST, so it needs an eyeball on the panels.

### Web + Android managers SHIPPED (2026-07-28) — one UI, one engine, two shells
Spec: `docs/superpowers/specs/2026-07-28-managers-design.md`. Supersedes
`docs/android-controller-options.md` (both its Option B and Option C are built).
- **Architecture:** screens live ONCE in `LaserTag.Ui` (Blazor + `IGameSession`);
  the engine lives ONCE in `LaserTag.Runtime` (`GameService`, UDP listener, 4 Hz
  tick — extracted from `LaserTag.Host`, which still works unchanged). The two
  shells differ only in which machine runs the engine.
- **`LaserTag.Web`** — `dotnet run --project dotnet/LaserTag.Web` → binds
  `0.0.0.0:5080`, plain HTTP (a self-signed cert on a LAN IP just nags phones).
  Screens: Devices / Match / Live / Firmware. JSON API for scripting:
  `GET /api/devices`, `GET /api/match`, `POST /api/match/start|stop`,
  `POST /api/control`. **Verified live** by sending fake heartbeats to UDP 4210:
  roster populated, deathmatch started, simulated hit scored team 1 and dropped
  the victim to hp 30.
- **`LaserTag.App`** — MAUI Blazor Hybrid, `net10.0-android` only, phone-as-host
  (no PC at play time). `dotnet publish dotnet/LaserTag.App -f net10.0-android
  -c Release -p:AndroidPackageFormat=apk` → signed 29 MB APK.
  **RUNS on an Android 15 emulator** — installed via adb, screens render,
  navigation works, broadcast discovery found `10.0.2.255:4210`, no crashes
  (screenshots in `docs/images/`). **Still unverified: receiving real
  telemetry** — an emulator is behind NAT so host broadcasts never arrive, and
  the multicast lock could not be exercised. A real phone on the boards' Wi-Fi
  is the true test.
- **Two bugs the emulator run caught that no build could:** the Blazor error
  banner (`#blazor-error-ui`) is shown by default and was rendering permanently
  — the project templates' `app.css` had hidden it, and swapping to the shared
  stylesheet lost that rule (now in `lasertag.css`, and the duplicate copy in
  `index.html` removed). And Android 15 forces edge-to-edge, drawing the web
  view under the status bar; `env(safe-area-inset-top)` is 0 in an Android
  WebView and the manifest's `windowOptOutEdgeToEdgeEnforcement` did not take,
  so `LaserTag.App/wwwroot/app.css` applies a fixed 30px inset in that shell
  only.
- **Two Android traps handled** (both fail as a silently empty roster, never an
  error): Android drops inbound broadcast UDP without a
  `WifiManager.MulticastLock` (`AndroidMulticastGuard` + manifest
  `CHANGE_WIFI_MULTICAST_STATE`); and **MAUI builds a service provider but never
  starts `IHostedService`**, so the listener and tick are started by hand in
  `App.OnStart`.
- **Gotchas that cost time:** `maui-android` was NOT installed despite the
  exploration doc's claim (`dotnet workload install maui-android` fixed it; the
  install rewrites SDK manifests, so concurrent `dotnet` commands fail while it
  runs). Blazor components in a razor class library **404 unless BOTH**
  `Router.AdditionalAssemblies` **and**
  `MapRazorComponents<App>().AddAdditionalAssemblies(...)` list the assembly —
  there is a test guarding exactly this. A running `LaserTag.Web` locks its DLLs
  and breaks later builds; stop it first.
- **Tests 212** (Client 110, Game 55, Rf 16, **Ui 18**, **Web 13**). Runtime
  gained `listen:false` after tests showed `SO_REUSEADDR` lets two hosts bind
  4210 and silently share datagrams.
- **Next for the managers:** run the APK on a real phone (multicast lock is the
  thing to prove); consider a foreground service if backgrounding drops hits;
  the `IGameSession` seam is where a thin client (phone driving a remote host)
  would slot in without touching a screen.

### RF (2.4 GHz) sub-project CLOSED — no signal found; these units stay on IR (2026-07-28)
**Decision: integrate these guns over IR only** (`docs/gun-protocol.md`, already
working). Do not spend more time hunting RF on this kit unless a labelled or
vest-bundled unit turns up. Tool usage is documented in `tools/README.md`
("RF probe"); the probe and analysis pipeline are ready to take a capture the
same evening if the question ever reopens.

Spec: `docs/superpowers/specs/2026-07-28-rf-protocol-analysis-design.md`; plan:
`docs/superpowers/plans/2026-07-28-rf-protocol-analysis.md`; findings:
`docs/rf-protocol.md`. Commits `bc33611`, `958abc2`, `d496679`, `befdc4d`,
`9d72355`, `bd22a21`, plus the analysis library and evidence commits.
- **Outcome: the Vatos kit was NOT detected on air.** Firing, two guns firing at
  each other, pairing and power-cycling all produced only WiFi-shaped energy,
  and **2901 promiscuous captures yielded 0 CRC-valid packets** (2402/2464/2476
  MHz at 1 M and 2 M, address widths 3-5). That is 3 of 126 channels and 2 of 3
  rates — not exhaustive, but the tooling is proven, so it is real evidence.
- **Leading hypothesis: these guns have no radio.** They are the rechargeable
  gun-only units; the "2.4GHz Data SYNC" claim comes from vendor listings for
  **vest-bundled** Vatos sets. Unverifiable directly — they are a child's toys
  in use, cannot be disassembled, have no visible model number, and the
  packaging is gone. The spec's Phase 0 chip-marking gate is permanently blocked
  by this route; non-invasive alternatives are listed in `docs/rf-protocol.md`.
- **Hardware:** nRF24L01+ pulled from an LC Technology `NRF24L01-TTL_V2` adaptor
  (its own CH340T+MS51FB9AE firmware is a transparent bridge, useless for
  sniffing) onto an **ESP8266, CP210x on COM6**. Wiring CE=GPIO4 CSN=GPIO5
  SCK=GPIO14 MOSI=GPIO13 MISO=GPIO12, 3V3 + 10 µF at the module. Env
  `esp8266-rfprobe`; commands `selftest`, `scan`, `watch`, `dwell`, `sniff`.
- **`LaserTag.Rf`** (net10.0, 16 xUnit tests green): line parser, nRF24 CRC16,
  bit realignment, ESB packet validation, address recovery. Raw captures kept in
  `docs/captures/` for re-analysis. Plan Task 6 (`RfTrainer`) deferred — no
  confirmed signal to capture.
- **Product-line evidence backs the no-radio reading:** every Vatos SKU that
  advertises "2.4GHz Data Sync" ships with vests/receivers; the one confirmed
  vest-free rechargeable line (VL-BB8933B / B0CZL4NCP3, "No Vests Needed")
  advertises no RF at all. No standalone Vatos vests are sold, so "buy a vest to
  give the link a peer" is NOT an available experiment. Manufacturer Canhui
  holds FCC ID `2A6LV-BB1550F` (2407-2475 MHz, ~1 mW) — **check the unit for an
  "FCC ID: 2A…" label; that is the only non-invasive proof either way.**
- **If resumed:** confine sweeps to channels 7-75 (the 2407-2475 MHz grant band)
  and include **250 kbps**, which was never tested. Two of the three channels
  sniffed (2402, 2476) were outside the grant band entirely.
- **Gotcha that cost real time:** the first occupancy metric counted SPI polls,
  which scales with loop speed rather than airtime, so `watch` and `dwell`
  disagreed ~100x on the same idle channel and manufactured two false
  candidates (2446/2407 MHz). Fixed in `9d72355` (fixed 500 µs cadence,
  `high/samples` percentage). **A candidate found by sweeping is not a finding
  until dwelling on it reproduces the effect** — a third candidate (2464 MHz,
  37% vs 3%) died exactly that way.

### Fleet OTA over HTTP SHIPPED (2026-07-27) — fw 2.1.0
Spec: `docs/superpowers/specs/2026-07-27-fleet-ota-design.md` (commits
`1819524`, `d571cb5`, `e071754`).
- **Version discipline starts now:** `LT_FW_VERSION` (matrix_main) = `2.1.0`;
  BUMP ON EVERY behavioural firmware change. The image embeds an
  `LTFW:<semver>` marker so tooling reads a .bin's version without flashing
  it (`FirmwareImage.TryReadVersion`; app-descriptor rejected — the
  precompiled Arduino core owns it).
- **Device:** `POST /api/update` (multipart — WebServer's upload machinery
  requires it, NOT octet-stream) → Update.h verified flash → reply → reboot;
  failed uploads leave firmware untouched. `GET /update` = browser upload
  form. LAN-trusted, no auth.
- **Host:** `fw [bin]` (roster running-vs-available table) and
  `ota <id|all> [--force] [bin]` (sequential HTTP pushes; `all` = outdated
  only). `FirmwareImage`/`FirmwareUpdater` live in **LaserTag.Client** so the
  Android app reuses them. .NET tests now **165** (Client 110 + Game 55).
- **Proven live:** boards 3+4 espota'd to 2.1.0 (their last-ever espota),
  then `ota eb20f8 --force` re-flashed board 3 fully over HTTP (upload →
  flash → reboot → HB `fw=2.1.0`). Boards 1+2 (unpowered) need one espota
  flash to gain the endpoint; after that everything is `ota all`.
- Gotchas hit: a ternary between two interpolated strings defeats
  `MarkupLineInterpolated`'s FormattableString overload (CS1503, fixed
  `e071754`); board 3's WiFi power-save makes unicast ping/REST lag while
  broadcast HBs flow — HB is the authoritative liveness signal.

### Chase mode + CTL v2.1 firmware pass SHIPPED; 4-board fleet (2026-07-26/27, overnight run)
**Spec B's core is DELIVERED and the first new game mode is live.** Spec:
`docs/superpowers/specs/2026-07-27-chase-mode-design.md`; plan:
`docs/superpowers/plans/2026-07-27-chase-mode.md` (7 tasks, subagent-executed,
commits `ab2d72d..fddadb4` + review fix `edfc007`).
- **Firmware CTL v2.1** (`lib/ControlProto` + `src/matrix_main.cpp`): the
  **`id=` filter is now ENFORCED** (multi-device play unblocked); `countdown`
  (per-second blink+beep) and `gameover` (5 s scoreboard hold → winner flood)
  cues; `activate [t=<ms>]`/`deactivate` dormancy with **device-side window
  self-timeout** (`EVT state s=timeout`); `chase on penalty= display=` /
  `chase off`; `score 1..4=` display pushes; dormant hits emit
  `EVT hit … dormant=1` (no hp change). New `chaseColour` config (default
  `#FFA500`, NVS `chaseCol`, PATCH-validated).
- **Scoreboards on the 8×8** (`cp::scoreGrid`, native-tested): 2 teams =
  middle-out columns (blank centre at 0, 1 LED = 1 pt, 32/side); 3–4 teams =
  4×4 quadrants (16/team). Dormant chase boards show it dim (25 %); gameover
  holds it full; `POST /api/mode {"mode":"scoreboard"}` turns any board into a
  dedicated wall scoreboard (ignores IR + activate; host excludes it from the
  chase pool via the new `Participant.Mode`).
- **Host `ChaseMode`**: `start chase <dur and/or --first N> [--min 2s]
  [--max 5s] [--gap 1s] [--penalty N] [--dark]`; random pick (no immediate
  repeat at ≥3 boards), penalty floored at 0, slack fallback (window +1.5 s)
  for lost timeout EVTs, offline-target advance. `GameService` pushes
  `CTL score` on change + 1 s refresh (all modes — dm can use wall
  scoreboards too).
- **Bench-VERIFIED live on boards 3+4**: activate/spin → timeout → gap →
  next-target loop, scored hit → `GAME OVER — winner: team 3`, duration end.
  Known quirks for next session: 3× CTL repeat = triple `EVT state` lines
  (cosmetic); REST `{"cmd":"hit"}` bypasses chase routing (damages hp and
  bounces a dormant board via flash→rainbow until re-activated) — the
  firmware **dormant-penalty IR path is still unexercised on hardware**
  (needs a real gun or aligned IR TX); on-matrix scoreboard rendering needs
  eyeball confirmation.
- **Also this session (before the overnight run):** boards 2–4 flashed +
  provisioned (see Hardware fleet below); **hostname-from-NVS boot fix**
  (`9632b44`) so multiple boards co-exist on mDNS/OTA — board 1 briefly
  auto-renamed to `lasertag-matrix-2.local` during the conflict window, fixed
  by reflash+reboot; **Android controller exploration**
  `docs/android-controller-options.md` (recommendation: MAUI app that IS the
  host, reusing LaserTag.Game/Client; `maui-android` workload already
  installed; gotcha: Android needs a `MulticastLock` to receive broadcast
  UDP). A **vibration motor** was considered and parked: not for this
  pin-constrained carrier — future wearable-target board, driven by the same
  cue abstraction as sound (spec §5 table).

### PCB ORDERED (2026-07-12) — carrier rev1
**10× lasertag-carrier rev1 (100×80mm) ordered from PCBWay:**
https://member.pcbway.com/Order/GroupDetail?GroupId=1768070 *(temporary
tracking link — remove once boards arrive and are verified).*
Ordered artifact: `hardware/lasertag-carrier/fab/lasertag-carrier-rev1-gerbers.zip`
(also attached to the `pcb-carrier-rev1` GitHub release). Board was DRC-clean
(0 unconnected / 0 electrical; one documented kicad-cli phantom silk warning).
Journey + gotchas: `PCB_FROM_PLATFORMIO.md` (repo root, linked from README).
The audio work is now **committed to `main`** (`90f003a` "Add MAX98357A I2S
sound: siren bank, per-team/death cues, selectable lives"), as is the microSD
spike implementation and all PCB work — the working tree is clean. All native
tests pass (see Tests below), the `esp32-s3-matrix` env builds, and the
S3 board is flashed with it over USB (COM6).

### Game manager (Spec A) SHIPPED + damage multiplier (2026-07-12/13)
The **host-side game manager is built, reviewed, and on `main`** (22 commits,
`fe7246a..1c258fe`): spec `docs/superpowers/specs/2026-07-12-game-manager-design.md`
(see its **Post-implementation notes**), plan `docs/superpowers/plans/2026-07-12-game-manager.md`,
execution ledger `.superpowers/sdd/progress.md` (gitignored).
- **`dotnet/LaserTag.Host`** — Generic Host console REPL: `devices`,
  `start dm 5m [--kill N --hit N --waves 30s]`, `start elim [--timer 10m]`,
  `score`, `stop`, `reset|activate|deactivate [id]`, `quit`. Subnet-broadcast
  auto-discovery (RFC1918 + gateway-preferred; `--broadcast <ip>` override —
  this box's virtual adapters made naive discovery flap between subnets).
- **`dotnet/LaserTag.Game`** — `MatchEngine` (Lobby→Countdown→Running→Finished,
  HB hp-reconciliation, rejoin handling incl. dead-stay-dead) + `IGameMode`
  plugins: `DeathmatchMode` (hit +1/kill +5, per-player 10 s or wave respawns),
  `EliminationMode` (last team standing, offline exclusion, timer cap).
- **CTL grammar v2** in `LaserTag.Client`: `countdown n=`, `gameover winner=`,
  `activate`/`deactivate`, optional `id=` on every verb; `ParseControl`;
  `UdpControlSender` (subnet broadcast, 3× repeat ~20 ms).
- **✅ Multi-device caveat RESOLVED (2026-07-27):** firmware now enforces the
  CTL `id=` filter (chase-mode spec/plan). Boards on OLDER firmware still
  apply addressed CTLs globally — reflash every board before multi-device
  matches (fleet table below tracks who has it).
- **Damage multiplier (firmware, NEEDS S3 REFLASH):** global
  `damageMultiplier` 1–32 (presets 1/2/4/8/16 + custom) + per-SHOOTER-team
  `teamDamageMult` handicap (0 = inherit). `mult` serial verb; REST PATCH;
  NVS. EVT reports effective damage. 16x: dmg-2 rocket = full 32 hp.
- **Quack SFX staged:** `assets/sfx/quack-attack{,-3s}.wav` (16 kHz mono s16
  from the user's MP3). Use the **3 s** trim (full 10 s would trip the ~5 s
  WDT with blocking playback); audition = copy to card as `/sfx/test.wav` +
  `sdplay`. MAX98357A is a dumb PCM DAC — MP3 would need a firmware decoder
  (libhelix/ESP8266Audio), noted as an option, not built.
- Also this session: carrier **build guide**
  `instructions/BUILD_LASERTAG_CARRIER_ESP32_MATRIX.md` (netlist-verified
  jumper table, core vs optional stages, bring-up), PCB rev1 render embedded
  in README/PCB_FROM_PLATFORMIO + **PCBWay referral link**, stale mDNS notes
  fixed (mDNS resolves for ping/REST; espota still needs the IP), matrix OTA
  port → `.33`, OpenAPI ConfigDoc backfilled (teamSfx/deathSfx/startHp + new
  multiplier fields).

### Sound on the ESP32-S3-Matrix (committed `90f003a`)
Wired a **MAX98357A I2S amp** to the S3 and built procedural SFX with per-team +
death assignment. End-to-end verified from the serial log (mapping, death,
reset); only the death-sound *character* is pending the user's ears (they last
heard `sfx 6` and approved direction).

- **Wiring (S3):** BCLK=**GPIO38**, LRC/WS=**GPIO39**, DIN=**GPIO40**, VIN=5V,
  GND. **SD left unwired** (internal pull-up → amp always enabled). These pins
  are in `BoardProfile` (`i2sBclkPin/i2sWsPin/i2sDinPin`) and NVS-overridable.
- **SFX bank** (`lib/Sound/SfxData.h`, GENERATED by `tools/gen_sfx.py`, ~220 KB
  flash): `0 siren-wail, 1 siren-warble, 2 siren-rise, 3 siren-fall,
  4 siren-wail-fast, 5 siren-twotone, 6 death-woowoo`. All procedural (numpy FM
  synthesis), 16 kHz mono 16-bit, peak-normalised full-scale. **NB:** the planned
  **microSD spike** (Next Steps #1) targets moving this bank to `/sfx/*.wav` on a
  card — reclaiming the ~220 KB and letting clips be added without a regen/reflash,
  with the baked bank kept as the no-card fallback.
- **Assignment (in `ConfigDoc`, build-time default + runtime PATCH + NVS):**
  `teamSfx` keyed map (teams **1→0, 2→2, 3→3, 4→5**) + `deathSfx`=**6**. Hit by
  team T plays its siren; the **fatal shot plays death only** (hit siren
  suppressed). `Sound::playIndex(int)` replaced the old audition cycling; the
  matrix resolves team→idx via `teamSfxIndex()` (mirror of `teamColourHex`).
- **Selectable lives:** `config.startHp` ∈ {4,8,16,32}, **default restored to 32**
  (the temporary `StartHp=4` constant is gone). Health bar already scales —
  `idleWithHealth(hp, config.startHp)` lights `hp/startHp × 32` central cells, so
  full = full columns at any setting.
- **Test/ops affordances:** serial verbs `sfx <idx>` (play any entry, bypasses
  game state — used to audition death without dying) and `lives <n>` (set+persist
  +revive). Reset: `CTL reset` already works on serial+UDP via `onLine`; added
  `CommandKind::Reset` to `/api/command`, routed through `handleControl` (DRY).
  Recurring `[sfx] last=idx/N name=... plays=K` line rides the heartbeat so the
  last-played value is always in the freshest serial output.

Layers complete and on `main`:
1. **V1** (tag `v1.0`): Vatos decode/encode, NEC decode, IR TX, OLED.
2. **V2 control plane**: `lib/ControlProto` (pure wire codec + `TagEvent`),
   `lib/TagNet` (WiFi/OTA/UDP/HTTP), `src/matrix_main.cpp` (heartbeat,
   device-authoritative hp, `EVT`/`CTL`, `/api/*` REST, NVS config). Host:
   `dotnet/LaserTag.Client` (parser, roster, REST client) + xUnit tests +
   `openapi/lasertag.yaml` + `LaserTag.Smoke` harness.
3. **Firewall/UDP tooling**: `tools/setup-firewall.ps1`/`.sh`, `NetworkDiagnostics`.
4. **Board-capability HAL** (the just-finished feature): `lib/Board` (compile-time
   `BoardProfile` per board via `-D BOARD_*`, whitelisted NVS overrides, hex
   colour; native-tested), `lib/HitDisplay` (WS2812 matrix / 3-pin RGB),
   `lib/IrTx`, `lib/Sound` (piezo; DAC stub), `lib/BoardNvs` (`cfg` command).
   Both firmwares refactored onto the HAL.
5. **Matrix health bar**: max hp **32**; the 4 central columns of the 8×8 deplete
   top-down as health drops (outer columns stay rainbow). Row-major pixel mapping
   confirmed correct on hardware.

**Tests:** native 57 (test_board 11 + test_controlproto 38 + test_storage 8);
.NET 165 (Client 110 + Game 55);
all envs build (`lolin32`, `lolin32_displaytest`, `esp32-s3-matrix`, `native`).

## Hardware (all live on HAL firmware, OTA-flashable)

### S3-Matrix fleet (4 boards) — WHOLE FLEET ON 2.1.0 (2026-07-29) ✅
Hostnames persisted in NVS. Verified together on 2026-07-29 (all four powered
and online simultaneously for the first time).

| # | Hostname (mDNS .local) | Last IP | deviceId | Firmware | RSSI |
|---|---|---|---|---|---|
| 1 | `lasertag-matrix` | .34 | `752b38` | ✅ 2.1.0 (espota'd 2026-07-29) | −77 |
| 2 | `lasertag-matrix2` | .180 | `eb278c` | ✅ 2.1.0 (espota'd 2026-07-29) | −70 |
| 3 | `lasertag-matrix3` | .225 | `eb20f8` | ✅ 2.1.0 | −81 |
| 4 | `lasertag-matrix4` | .218 | `e45614` | ✅ 2.1.0 | −74 |

**The whole fleet now has `/api/update`, so `ota all` works — espota is no
longer needed for any board.** Boards 1+2 were on the pre-2.1.0 image (no
`/api/update`), so each needed exactly one bootstrap espota:
`python ~/.platformio/packages/framework-arduinoespressif32/tools/espota.py
-i <board-ip> -I <pc-lan-ip> -p 3232 -f .pio/build/esp32-s3-matrix-ota/firmware.bin -r`
(the `-I` host IP is required; without it the board's ack has nowhere to go).
The `id=` filter is now enforced fleet-wide, so **all four are cleared for
multi-device matches**. WiFi
provisioning for a fresh board: `tools/set-wifi.ps1 -Port COMx` (creds
extractable via `netsh wlan show profile ... key=clear`); hostname via
`PATCH /api/config {"hostname":...}` + reboot (boot reads it from NVS since
`9632b44`). All roam on DHCP — reservations would end the stale-OTA-IP chore.

- **ESP32-S3-Matrix** (target): WiFi roams via DHCP (seen .24 → .28 → **.33**);
  **mDNS `lasertag-matrix.local` NOW resolves on this Windows host** — use it
  instead of chasing the IP (this contradicts the older platformio.ini/README
  "mDNS doesn't resolve here" notes — those are stale; something changed). For
  OTA still use the *current IP* with espota `-I <pc-lan-ip>` (mDNS name is
  unreliable for espota). fw 2.0.0, deviceId `752b38`. 8×8 WS2812 GPIO14 (**RGB**
  order), IR RX GPIO1, act LED GPIO7, **MAX98357A I2S on GPIO38/39/40**.
  Native USB-CDC enumerates as **COM6** (VID 303A:1001); CH340 on COM-x is the
  Lolin32. Health-bar idle. **IR TX on GPIO37** (this session) + manual
  `fire <team> <damage>` serial verb and `{cmd:"fire",team,damage}` REST command
  (`CommandKind::Fire`); verified end-to-end (self-RX loopback registers exact
  damage). Bench uses a bare LED+220Ω = short range; **PCB uses a 2N2222A driver
  from 5V** (see Board BOM). Retaliation auto-fire still TODO (Next Steps #5).
- **Lolin32 OLED** (monitor/target): ⚠ **OFFLINE as of 2026-07-29** and its
  recorded IP is stale — 192.168.1.48 now answers ping but refuses port 80, so
  DHCP has reassigned it. Re-discover via the heartbeat roster (below) after
  powering it, not by its old address. ⚠ **It is an ESP32, not an ESP32-S3** —
  see the `ota all` cross-chip hazard in Gotchas. Last known: fw 2.0.0 (so it
  needs one bootstrap espota, like boards 1+2 did). IR RX GPIO25,
  IR TX GPIO13, OLED (SSD1306 128×32, SDA5/SCL4), act LED GPIO26, external 8×8
  WS2812 on **GPIO14** (**GRB** order — assumed; verify if colours look off).
  Refactored onto the HAL: IR monitor/decode/OLED + IrTx + panel idle rainbow.

## Board BOM / discrete circuitry — ESP32-S3-Matrix carrier (for PCB phase, Next Steps #9)
All 0.1″ pitch; peripherals on female sockets (LEDs/IR-RX socket-or-direct). Power:
single **5V** in via a 2-pin terminal block → board 5V pin; the board's onboard LDO
supplies **3V3-OUT** for microSD + IR-RX. **Don't also power via USB while the
terminal block is live** (no backfeed isolation assumed). Switch (+ any battery/
charger) sits upstream of the terminal block; doesn't affect the board circuit.

> **AUTHORITATIVE SPEC = `.docs/pcb-blocks.md`** (see its RECONCILED 2026-07-04
> section). The board was expanded to **full scope** (adds I2C OLED, external
> WS2812 out, optional level-shifter board) with a **GP2 role selector** (default
> none / recommended touch / button / audio-mute). The tables below are the
> **lean-core summary**; `pcb-blocks.md` owns the complete per-block netlist + BOM.

### Connectors (female, 0.1″)
- **ESP32-S3-Matrix carrier:** 2× `1×10`, rows **22.86 mm (0.9″)** apart. Pads —
  L-row `5V·GND·3V3·GP7·GP6·GP5·GP4·GP3·GP2·GP1`; R-row
  `GP33·GP34·GP35·GP36·GP37·GP38·GP39·GP40·GP43·GP44`.
- **MAX98357A audio `1×7`:** order **LRC·BCLK·DIN·GAIN·SD·GND·Vin** (user's
  "RCLK"=BCLK, "GNC"=GND).
- **microSD `1×6`:** order **3V3·CS·MOSI·CLK·MISO·GND**.
- **IR receiver `1×3`:** 38 kHz (Vatos `CarrierHz`=38000 ✓; VS1738 / HS0038 /
  VS1838B / LF1638B all match band). Socket order **OUT·GND·VCC** — **verify each
  substitute's pinout** before swapping (clone batches differ; reversed VCC/GND
  kills the part). Design ref = **HS0038(B)** (best ambient-light rejection);
  VS1838B a confirmed-pinout spare.
- **Power in:** 2-pin terminal block.

### Net map (carrier → peripheral)
| Carrier | → | Peripheral pin | Notes |
|---|---|---|---|
| GP39 | → | audio LRC (WS) | I2S |
| GP38 | → | audio BCLK | I2S |
| GP40 | → | audio DIN | I2S |
| 5V | → | audio Vin | amp on 5V |
| GND | → | audio GND | |
| jumper | | audio GAIN | **optional** 3-way strap: float=9 dB (default) / GND=15 dB / Vin=3 dB. Volume is done in SW (`kVolume`) — no dynamic control needed |
| GP2 | → | audio SD (shutdown) | audio hard-mute = one option of the **GP2 role selector** (default none / touch / button / audio-mute) — GP4/5 are I2C in the full board, so mute lives on GP2 via solder-jumper |
| GP33 | → | microSD CLK | SPI |
| GP34 | → | microSD MOSI | |
| GP35 | → | microSD MISO | |
| GP36 | → | microSD CS | |
| 3V3 | → | microSD 3V3 | **3.3V only** |
| GND | → | microSD GND | |
| GP1 | ← | IR-RX OUT | confirm module pinout |
| 3V3 | → | IR-RX VCC | |
| GND | → | IR-RX GND | |
| GP37 | → | IR-TX driver (470Ω→Q1.B) | see below |
| GP7 | → | hit indicator = **onboard green LED**, already firmware-driven on IR-RX (no new part/code). Optional external repeat: GP7→220Ω→LED on a header | |
| 5V(sw) | → | power-LED (330Ω, 1k on battery) | always-on |
| 3V3(SD) | | 22–47 µF bulk cap → GND at socket | inrush; **optional** dedicated LDO — see below |

### IR-emitter driver (discrete, GPIO can't reach range direct)
`5V ─[33Ω]─▶|IR-LED─ Q1.C` · `GP37 ─[470Ω]─ Q1.B` · `Q1.E ─ GND` — Q1 = **2N2222A**.
33Ω ≈ 100 mA; swap **22Ω ≈ 150 mA** if range still short. Status: design, pending
hardware validation.

### BOM
| Ref | Part | Qty |
|---|---|---|
| U1 | Waveshare ESP32-S3-Matrix | 1 |
| U2 | MAX98357A I2S amp breakout | 1 |
| U3 | microSD breakout (3.3V SPI) | 1 |
| U4 | IR receiver (VS1838B / TSOP38xx) | 1 |
| Q1 | 2N2222A NPN TO-92 (BC337-40 / S8050 equiv) | 1 |
| D1 | IR LED 940 nm | 1 |
| D2 | power LED | 1 |
| D3 | hit LED — *optional* external (onboard green LED already serves) | 0–1 |
| R1 | 33Ω ¼W — IR series (opt. 22Ω) | 1 |
| R2 | 470Ω ¼W — Q1 base | 1 |
| R3 | power LED — **330Ω default** (list 330Ω–1k; 1k = battery-sipping) | 1 |
| R4 | 220Ω ¼W — *optional* external hit LED | 0–1 |
| C1 | 22–47 µF bulk cap — microSD 3V3 at socket | 1 |
| U5 | *optional* 3V3 LDO from 5V for microSD (durability): AP2112-3.3 / MCP1826 / AMS1117-3.3 + 10 µF in/out | 0–1 |
| J1 | 2-pin terminal block, 0.1″ | 1 |
| LS1 | 4/8Ω speaker | 1 |
| — | female headers: 2×`1×10`, `1×7`, `1×6`, `1×3`, + 2-pin LED ×2–3 | — |

**Assembly-time DNP options:** D3 (external hit LED) and U5 (+its in/out caps) are
fit-or-omit at build. **Lay out U5 with a bypass link** (0Ω / solder-jumper) so SD
3V3 sources from the onboard 3V3 when U5 is unpopulated; D3 is trivially omitted.

### Resolved (2026-07-04)
1. **IR-RX:** 38 kHz confirmed; design to **HS0038(B)**, socket OUT·GND·VCC,
   verify substitute pinouts (VS1738/VS1838B/LF1638B on hand — all 38 kHz).
2. **Hit LED:** = existing onboard green GP7 (firmware-driven on IR-RX). No new
   part/code; optional external repeat via GP7→220Ω. GP2 idea dropped.
3. **Audio:** SD hard-mute is one option of the **GP2 role selector** (GP4/5 are I2C
   in the full board); **GAIN** = fixed strap + optional 3-way jumper (volume is SW).
4. **microSD supply:** bulk cap (22–47 µF) always; add **dedicated 3V3 LDO from
   5V** (U5) for durability so SD peaks don't brown out the ESP.

### Still open
- Each **GP2 role** (touch / button / audio-mute) needs a `BoardProfile` field +
  firmware when that variant is built; decide during the PCB-phase firmware pass.

## Key decisions
- **Hybrid board config**: peripheral presence + IR pins are compile-time
  (`BoardProfile`, selected by `-D BOARD_LOLIN32`/`-D BOARD_S3_MATRIX`); a
  whitelisted subset (matrixPin/W/H/order, rgb pins, activityLedPin) is runtime-
  overridable via NVS using the `cfg <key> <value>` serial command.
- **HAL refactor was behaviour-preserving** (verified): matrix telemetry/hp/EVT/CTL
  identical post-refactor; Lolin32 OLED/decode/TX/OTA unchanged.
- **Settings authority = NVS (today).** The microSD spike (Next Steps #1) proposes
  an *optional* `/config.json` on the card as a human-editable import/export +
  override source mirroring NVS (teamSfx/deathSfx/startHp/board overrides). Default
  intent: NVS stays authoritative; SD is read on boot to seed/override and written
  on change — but the precedence rule (SD-on-boot vs NVS-wins) is a spike decision,
  not yet settled. Devices without a card must behave exactly as today.
- `s=stunned` (post-hit cooldown, hp>0) vs `s=dead` (hp==0). Writes require a JSON
  Content-Type (ESP32 WebServer drops urlencoded bodies).
- **Game manager decomposition = 3 specs, grammar-first** (agreed 2026-07-12):
  Spec A host manager (done) / Spec B firmware verbs+id= / Spec C hunt+
  retaliation. Grammar v2 designed once in Spec A so B/C plug in without wire
  changes. Host architecture = **.NET Generic Host** (user's pick over thin
  console) with one lock (`GameService`) serializing engine access.
- **Scoring is per-TEAM** — the Vatos IR frame carries shooter team only, no
  player id (needs future gun electronics). Score keys isolated so a player
  key can slot in later. **Damage handicap keys by SHOOTER team** (damage
  dealt), not victim team.
- **Host never pushes authoritative hp** — devices own hp; host mirrors from
  HB/EVT and issues respawns via `CTL reset`. Known limitation (spec notes):
  a device reboot mid-match revives it (hp volatile) — accepted for now.

## Gotchas (carry forward — these cost real time)
- **microSD status (2026-07-30): JP3 was a real fault and is fixed; the card
  now talks but BROWNS OUT during initialisation.** Best current diagnosis, and
  it points at a designed-in option we left unpopulated.
  - Bridging **JP3** took the card from electrically dead (cs/miso/sck
    FLOATING) to alive: CMD0 -> `0x01`, CMD8 -> `01000001AA` (valid SDv2), on
    the documented pin order.
  - With the **64 GB** card and a correct 2 s ACMD41 deadline, the card answers
    CMD55/ACMD41/CMD58 with `0x01` ("still initialising" — the healthy
    in-progress reply) and then **stops responding partway through**, ending
    `0xFF` after ~667 polls. Starting init and dying under it is the classic
    supply-sag signature. Identical at 400/200/100 kHz, so not signal
    integrity.
  - The **2 GB** card never answers CMD55 at all, so it behaves differently and
    may be separately faulty — do not treat the two cards as interchangeable
    evidence.
  - **LIKELY FIX — populate U5 (design decision D9, currently DNP).** The
    carrier already reserves a dedicated 3V3 LDO for the SD rail, fed from
    VCC5, precisely for "supply durability": U5 + C10/C11, then move **JP3** to
    the U5-output leg. Today `SD_VDD` shares the ESP32-S3-Matrix module's small
    onboard 3V3 regulator with the MCU, WiFi bursts and the matrix, and an SD
    card draws ~100 mA at init. C4 (100 nF) and C5 (10 µF) ARE fitted and are
    not enough on their own. Any external 3V3 supply for J5 pin 1 tests the
    theory without fitting U5.
  - Module is 3.3 V-native (supply pin labelled 3V3) — do NOT feed it 5 V.
  - Eliminated: pin mapping (24 permutations), bus speed, both cards, format,
    partition type, LED rail sag, board input supply (LiPo vs 5 V USB-C), CRC
    (now computed, and proven right because CMD0/CMD8 check it and pass), CS
    handling, command pacing, poll timing/deadline, and pin contention
    (GP33-36 dedicated per netlist).
  - **Not blocking anything** — clips live in flash (see Current State).
- **microSD dead on a PCB carrier? Check JP3 before anything else.** JP3 is the
  card's ONLY power path (`VCC3V3` → `SD_VDD` → J5 pin 1) and its footprint is
  the **Open** solder-jumper variant, so it ships UNBRIDGED and must be closed
  with solder. Unpowered presents as pure silence, not an error: CS/MISO/SCK
  all read floating and the card answers nothing at any speed, on any card, in
  any pin permutation. The board's 3V3 rail still measures fine — `SD_VDD` is
  downstream of JP3, so probe **J5 pin 1**, not the rail. `sdpins` diagnoses it
  remotely. The build guide previously contradicted itself here (the assembly
  step said to bridge it; the jumper table implied it arrived bridged) — fixed
  2026-07-30.
- **Discover boards by the heartbeat roster, NOT by scanning the subnet.** An
  HTTP sweep of all 254 addresses found only 1 of the 2 boards that were up
  (slow boards time out before answering), while the UDP roster found both.
  Allow **~30 s** of listening, not 10 — a short window shows an empty table
  and looks exactly like "no boards". Board IPs in these docs are ALWAYS
  suspect: everything roams on DHCP, and a stale IP may now answer as some
  other device entirely (`.48` did).
- **Never send a request body with `JsonContent.Create` to a board.** It
  serializes lazily, so the length is unknown and HttpClient falls back to
  `Transfer-Encoding: chunked` — and the ESP32 Arduino `WebServer` reads a body
  only when `Content-Length` is present, so the device sees an EMPTY body and
  answers `400 empty body — send Content-Type: application/json` (a misleading
  message: the content type was correct all along). `LaserTagClient` now
  pre-serializes via a `JsonBody` helper, guarded by a regression test. Note
  the guard must sample `ContentLength` BEFORE anything reads the content —
  reading buffers it and retroactively populates the header, which made the
  first version of that test pass against the very bug it existed to catch.
- **Heartbeat discovery can take up to ~90 s**, not the ~30 s assumed earlier
  — it varies run to run. A command issued against a half-filled roster
  silently covers only the boards discovered so far (`teams split` did exactly
  this), so the host now prints how many devices it is acting on.
- **`ota all` does not check the board type — it pushes ONE binary to every
  online outdated board.** Heartbeats carry no chip/model field, so if the
  **Lolin32 (ESP32)** is online alongside the S3 fleet it will be sent the
  **ESP32-S3** image. ESP-IDF's image validation should reject the wrong
  chip id and leave the board on its old firmware, but **this has never been
  tested** — until it is, use `ota <id>` when the Lolin32 is on the network.
- **`mode` in `/api/status` / `HB mode=` is the board's ROLE**
  (`idle`/`target`/`scoreboard`, set via `POST /api/mode`) — **not** the match
  phase. Boards correctly read `mode=idle` all the way through a running match.
  There is no per-board readback of "am I in a match"; use hp, or the host's
  own snapshot. This wasted a test cycle by looking exactly like a bug.
- **OTA flashing reliably:** use `espota.py` with an explicit host IP, e.g.
  `~/.platformio/penv/Scripts/python.exe ~/.platformio/packages/framework-arduinoespressif32/tools/espota.py -i <device-ip> -I <pc-lan-ip> -p 3232 -f .pio/build/<env>/firmware.bin`.
  The `-I <pc-lan-ip>` (this session: **192.168.1.59**) is ESSENTIAL — without it
  espota advertises the wrong interface (PC has Bluetooth + HHD virtual COM
  adapters) and fails "No response". `pio run -e <env>-ota -t upload` also works
  but rebuilds the whole env. OTA is lossy (RSSI ~-70): 2–16 min, sometimes retry.
- **USB flash (Lolin32)** needs the **GPIO0→GND jumper** (download mode): jumper
  on → `pio run -e lolin32 -t upload --upload-port COM14` → **remove jumper** →
  reset. Boot banner `boot:0x7 DOWNLOAD_BOOT` = jumper still on; `boot:0x17` = app.
  CP210x on COM14. Both boards are now OTA-capable, so USB shouldn't be needed.
- **`CTL` must target the subnet broadcast `192.168.1.255` (or unicast)** — NOT
  `255.255.255.255` (not delivered). CTL is lossy UDP; send 3–4× for reliability.
- **Windows Defender** intermittently fails the `firmware.elf` link with
  `collect2: CreateProcess`/`Access is denied` — just re-run the build.
- **New libs only compile when `#include`d** — PlatformIO LDF skips unreferenced
  libs, so a new HAL lib is first compiled when wired into a firmware.
- **FastLED data pin is compile-time** (`addLeds<WS2812B, 14, ...>`) — so the
  `matrixPin` runtime override is stored but only `matrixOrder` actually applies
  at runtime. Documented in `lib/HitDisplay/HitDisplay.h`.
- **I2S MUST be stopped at idle.** Uses legacy `driver/i2s.h` (espressif32 6.12.0
  = Arduino core 3.x). Leaving the peripheral running clocks stale DMA buffers →
  **continuous loud noise** from the amp (hit this; cost a reflash). `playPcm`
  does `i2s_start` → write → `delay(20)` (DMA drain) → `i2s_zero_dma_buffer` →
  `i2s_stop` per clip; `begin()` installs then immediately stops. Channel fmt is
  `I2S_CHANNEL_FMT_ALL_LEFT` (mono to both slots; with SD floating the amp picks
  left). Death clip blocks ~2.3 s (under the ~5 s idle WDT — fine).
- **Volume = single runtime knob** `kVolume=0.15f` in `Sound.cpp` (samples baked
  full-scale, scaled at playback). Retune there — no need to regenerate SfxData.h.
- **Regenerate SFX:** `python tools/gen_sfx.py` (needs numpy; ffmpeg only if you
  re-add downloaded clips — current bank is 100% procedural, no network/licence).
- **Serial driving from PowerShell:** `SerialPort` with `DtrEnable=$false;
  RtsEnable=$false` (set before Open, matches `monitor_rts=0/dtr=0` — DTR/RTS
  toggle resets the S3 into bootloader) and **`NewLine="`n"`** (a trailing `\r`
  breaks `CTL`/verb parsing). Read with `ReadExisting()` after a short sleep.
- **Contract tests run via `pio test -e native`** (NOT `pio run -e native`, which
  tries to compile the Arduino `src/` and fails). 51 tests (see Tests above).
  .NET suite: `dotnet test dotnet/LaserTag.sln` (134).
- Matrix stunned/dark interval is **1–5 s (TESTING)**; revert to 5–15 s for play.
- WiFi 2.4GHz `CommunityFibre10Gb_28750`; creds in NVS (survive OTA). Set via
  `tools/set-wifi.ps1 -Port COMx -Ssid ... -Password ...`.

## Recent Changes (this session, 2026-07-26/27 — all committed, tree clean)
- `9632b44` hostname-from-NVS boot (multi-board mDNS/OTA); OTA IP → .34;
  Android options doc. Boards 2/3/4 flashed + provisioned (COM7/8/9,
  VID 303A:1001); board 1 verified then unpowered.
- `a252cb7` chase spec; `77e8438` chase plan.
- `ab2d72d`/`2c3a105` ControlProto CTL v2.1 + scoreGrid (native 51→57).
- `fff20e6`/`a7d202f` firmware cues + chase state machine; `edfc007` signed
  elapsed-time review fix (future `lastAnimMs` wrapped unsigned compares).
- `2a6b1b8` host client verbs (+8 tests); `fedc2de` ChaseMode (+12);
  `fddadb4` REPL `start chase` + score pusher + docs (.NET 134→153).
- Bench: chase loop verified live on boards 3+4 (see Current State quirks).

## Recent Changes (2026-07-12/13 — all committed, tree clean)
- `dotnet/LaserTag.Game/` + `dotnet/LaserTag.Game.Tests/` — NEW: MatchEngine,
  DeathmatchMode, EliminationMode, DurationParser, IGameMode/MatchContext/
  state types (43 tests).
- `dotnet/LaserTag.Host/` — NEW: Generic Host console (GameService lock,
  UdpTelemetryService 4210, 250 ms tick service, Spectre REPL).
- `dotnet/LaserTag.Client/` — grammar v2 (`ControlKind`/`Control`/`ParseControl`/
  `FormatControl`), `IControlSender`, `UdpControlSender`, `BroadcastAddress`
  (RFC1918 + gateway-preferred discovery).
- `lib/ControlProto/` — `ConfigDoc` gains `damageMultiplier` (1–32) +
  `teamDamageMult[4]` (0=inherit), serialize + validated PATCH.
- `src/matrix_main.cpp` — `damageMultForTeam()` applied in `applyHit` (EVT
  reports effective dmg); NVS `dmgMult`/`teamDmg<n>`; `mult` serial verb.
- `test/test_controlproto/` — golden + 2 multiplier tests (native 51).
- `dotnet/openapi/lasertag.yaml` — ConfigDoc backfill + multiplier fields.
- `instructions/BUILD_LASERTAG_CARRIER_ESP32_MATRIX.md` — NEW build guide.
- `assets/sfx/quack-attack{,-3s}.wav` — NEW staged clips.
- `hardware/lasertag-carrier/board-render-rev1.png` — NEW, embedded in docs.
- `README.md`/`PCB_FROM_PLATFORMIO.md`/`platformio.ini`/`tools/README.md` —
  game-manager section + firmware-compat warning, build-guide/render/referral
  links, mDNS note fixes, OTA port `.33`.
- Specs/plans: `docs/superpowers/specs/2026-07-12-game-manager-design.md`
  (+post-impl notes), `docs/superpowers/plans/2026-07-12-game-manager.md`.

## Next Steps
**RF is closed** — these guns are IR-only as far as this project is concerned;
see the RF section above before reopening it.

**Managers:** the highest-value next step is **installing the APK on a real
phone** — everything else about the Android app is proven except the one thing
that matters most (receiving broadcasts under the multicast lock).

**✅ RESOLVED 2026-07-29 — team assignment shipped** (see Current State). Teams
are assigned from the host CLI, both managers and the JSON API; team 0 =
neutral is the default. The fleet currently sits 2v2 (`752b38`+`eb20f8` = 1,
`eb278c`+`e45614` = 2).

**Immediately actionable:** (a) ✅ **DONE 2026-07-29 — boards 1+2 powered, OTA'd
and fleet-tested**; the whole fleet is 2.1.0 and enforces `id=`. (b) **eyeball
the chase visuals** on the fleet — spin colour, dim scoreboard, countdown
blink, gameover hold (host-side logic is bench-verified; the LEDs aren't). This
now also covers **activate/deactivate**, which the 2026-07-29 test could not
confirm remotely (it only changes `vis`). (c) **exercise the dormant-penalty
IR path** — now genuinely reachable: board-to-board IR is VERIFIED (see Current
State), so aim `eb278c` at a dormant chase board and confirm the
`EVT hit … dormant=1` penalty path. Fit IR LEDs to the PCB carriers (2N2222A
footprint present, no emitter yet) so any board can shoot, not just the
prototype. (d) audition the quack
(`assets/sfx/quack-attack-3s.wav` → card as `/sfx/test.wav`, `sdplay`);
(e) run the APK on a real phone (see Managers above). Remaining Spec B
leftovers: OLED-shows-health + configurable sound paths (8b); Spec C leftover:
retaliation mode (#5).

**Found by the 2026-07-29 review (none blocking, all real):**
- **`/api/update` is unauthenticated** — anyone on the LAN can flash any board,
  and there is no guard against flashing **mid-match** (the board reboots and
  drops out). Fine for a home LAN; note it before this ever leaves one.
- **`/api/update` reports the OLD version on success** — the response is
  `{"ok":true,"version":LT_FW_VERSION}` compiled into the *outgoing* image, so
  it names the version being replaced, not installed. Cosmetic today because
  the host re-reads the version from the post-reboot heartbeat.
- **`ota all` has no board-type guard** (see Gotchas) — the one with real
  bricking potential if ESP-IDF's chip-id check turns out not to save us.
- **No late join:** `MatchEngine` fixes the lobby at start and ignores
  heartbeats from unenrolled devices, so a board that boots after the countdown
  can never join. Deliberate, but undocumented outside the code.
- **No match-phase readback on a board** (see Gotchas) — worth a `phase` field
  in StatusDoc/HB if boards ever need to self-check, and it would have made the
  integration test direct rather than inferential.

0. **When PCBWay boards arrive (order: 10× rev1, link at top):** visually check
   against `board-front.svg` / `board-render-rev1.png`; buy/gather parts per
   `hardware/lasertag-carrier/bom.csv`
   (BUY items: 100nF ceramics ×3, 2N2222A, sockets/headers; most passives in
   stock); assemble per **`instructions/BUILD_LASERTAG_CARRIER_ESP32_MATRIX.md`**
   (square pad = pin 1 everywhere; module 5V corner
   to J1's square pad; MAX98357A LRC into J3's square end; microSD 3V3 into
   J5's); bring-up: 5V current check before seating modules → seat S3 →
   `fire`/`sdplay`/`sfx` smoke tests. Then remove the temporary order-tracking
   link from Current State.
1. **microSD spike — implementation complete, hardware verification pending.** Built:
   native-testable WAV parser (`lib/Storage`), BoardProfile SD pins (CS=GP36,
   MOSI=GP34, MISO=GP35, SCK=GP33, ESP32-S3-Matrix only), SdCard wrapper
   (`lib/SdCard`, native-buildable), `Sound::playRaw()` + `sdplay` serial verb,
   `gen_sfx.py --wav` export mode. All native tests passing (48: test_board 11 +
   test_controlproto 29 + test_storage 8). **Immediate next step:** generate a test
   clip (`python tools/gen_sfx.py --wav /sfx/test.wav`), copy to a real microSD card
   wired per pins above, flash S3 firmware, run `sdplay` over serial to confirm
   mount/list/playback and no-card/no-file error paths. Design spec and plan:
   `docs/superpowers/specs/2026-06-29-microsd-spike-design.md`,
   `docs/superpowers/plans/2026-06-29-microsd-spike.md`.
2. ✅ **DONE:** audio work committed to `main` (`90f003a`). `SfxData.h` is
   generated (`tools/gen_sfx.py`).
3. **Death-sound polish** if the user wants it — tune in `tools/gen_sfx.py`
   `synth_death()` (woo count/speed, descent depth, tail), regen, reflash.
   Optionally wire Start/Respawn cues (currently silent on the DAC board).
4. ✅ **DONE (2026-07-12):** `esp32-s3-matrix-ota` `upload_port` set to the
   last-seen IP (**192.168.1.33**); stale "mDNS doesn't resolve" notes fixed in
   `platformio.ini`, `README.md`, `tools/README.md` (mDNS resolves for
   ping/REST; espota still needs the IP). Re-check the IP before any OTA — the
   matrix roams via DHCP.
5. **Retaliation mode (Spec 2)** — already brainstormed (decisions captured in
   chat, not yet specced): on a hit, the target fires damage-1 Vatos shots in
   **every colour EXCEPT the shooter's** (randomised order, timed to the triple
   flash); receivers own-team-filter → shooter's team takes 3, others 2. Needs an
   IR TX on the target (Lolin32 has one; S3 matrix needs one added). Start by
   running `superpowers:brainstorming` → spec → plan, building on the HAL.
6. **OLED-shows-health** standing rule — wire when a device has both hp + an LCD
   (i.e. when the Lolin32 gains a target/hp role, likely via retaliation mode).
7. ✅ **DONE:** .NET host CLI delivered by Spec A (spec: `docs/superpowers/specs/2026-07-12-game-manager-design.md`; plan: `docs/superpowers/plans/2026-07-12-game-manager.md`). `LaserTag.Game` + `LaserTag.Host` implement match orchestration + CTL grammar v2 (countdown/gameover/activate/deactivate/id=). UDP CTL sender (subnet broadcast, 3× repeat) in `UdpControlSender`. FIRST: firmware id= filter on CTL (see spec post-impl notes) — current firmware ignores unknown CTL keys including id=, so id=-addressed reset/start is applied by every device, not just the target; per-player respawns/rejoin re-issues are unsafe for multi-device play until this lands. Remaining follow-ups: Spec B firmware pass (countdown/gameover/activate/deactivate/id= handling + OLED-health), Spec C hunt+retaliation modes, Claude-skill wrapper over the console.
8. Housekeeping: revert matrix dark time to 5–15 s before real play; add an
   `/api/*` REST section to the README if desired.
8a. ✅ **DONE (2026-07-13): Damage multiplier.** `config.damageMultiplier`
   (global 1–32, presets 1/2/4/8/16 + custom) + `config.teamDamageMult`
   per-SHOOTER-team handicap override keyed like `teamSfx` (0 = inherit
   global). hp loss = dmg × mult (16x: a dmg-2 rocket wipes startHp 32).
   EVT hit reports the EFFECTIVE damage so host mirrors stay truthful.
   Plumbing: ConfigDoc serialize/PATCH (validated, native-tested), NVS
   (`dmgMult`, `teamDmg<n>`), serial verb `mult | mult <1-32> |
   mult <team> <0-32>`, OpenAPI schema (also backfilled the missing
   teamSfx/deathSfx/startHp there). S3 needs a reflash to pick it up.
8b. **Configurable sound sources (requested 2026-07-12):** replace hardcoded
   SD names (`sdplay` plays fixed `/sfx/test.wav` in `src/matrix_main.cpp`)
   with path-based config; support baked bank + microSD paths, and consider
   PSRAM caching of SD clips (S3 has 2 MB PSRAM) so playback needn't re-read
   the card. Candidate ConfigDoc shape: per-event sound refs (`teamSfx`/
   `deathSfx`/future `gameoverSfx`) accepting either a bank index or a path.
   First real clip staged: `assets/sfx/quack-attack{,-3s}.wav` (16 kHz mono
   s16 from the user's MP3; 3 s trim fits the ~5 s idle WDT — the full 10 s
   clip would trip it with today's blocking playback).
9. **PCB design & testing — RESEARCH + PLANNING PHASE (gated on hardware freeze).**
   **→ Full design notes: `.docs/pcb-design.md`** (toolchain, single-source→two-
   outputs architecture, config plumbing, pin rules + provisional map, connectors,
   passives, Wokwi MAX98357A stub, level-shifter board).
   Goal: a custom carrier/breakout PCB for the platform — ESP32 module + *optional*
   connectors for OLED/LCD, WS2812 LED matrix, MAX98357A audio, microSD, IR TX/RX.
   This is **research+plan now, fabricate later**: the connector set isn't final
   until the **microSD pins (#1)** and **S3 IR TX (#5)** land, so draft with
   placeholders and **gate copper on a pin/peripheral freeze**.
   **Decided so far:** S3-Matrix carrier first (per-board, not shared); JST-XH for
   all small/peripheral connectors + female 2.54mm sockets for the ESP/audio/LCD
   modules; **primary = plain KiCad** (registry check: atopile too thin — 5/7 parts
   absent; SKiDL optional code route) + Wokwi via a netlist→diagram.json script;
   optional pins default to `-1` via the existing hybrid config
   (`irTxPin` already exists = shoot-back enable; add `sd*Pin`). **Pins confirmed**
   against the official pinout: only GP1-7 + GP33-40 + TX/RX are broken out → IR TX
   **GP37** (moved from GP2, flashed + tested), microSD SPI **GP33-36**
   (SCK/MOSI/MISO/CS). See pcb-design.md / pcb-blocks.md.

   ### Tooling decision (researched 2026-06-28)
   No single tool does both ESP simulation AND PCB layout well — it's a two-track
   pipeline, and the PCB side converges on one hub:
   - **PICK = KiCad** for the PCB. Fully **open S-expression text** formats
     (`.kicad_sch`/`.kicad_pcb` — diff/version/AI-editable), **Python** scripting
     (`pcbnew`), and it's the format every other path emits: atopile compiles to
     it, SKiDL emits it, EasyEDA imports to it. Multiple community **MCP servers**
     exist (lamaalrajih/kicad-mcp, mixelpixx, kicad-mcp-pro — early/varying
     maturity; treat the open text format as the reliable workhorse, MCP as bonus).
   - **Drive KiCad from here as code** via **atopile** (`.ato` text, `ato` CLI,
     part registry at packages.atopile.io, compiles → `.kicad_pcb`; modern,
     AI-friendly — I can author `.ato` from Claude Code, it picks parts + runs
     checks + updates the layout) or **SKiDL** (Python → KiCad netlist).
     **RESOLVED (task 1, 2026-07-04):** atopile registry coverage for OUR parts is
     **thin** — passives/connectors/USB only; ESP32-S3 pkg is archived (Oct 2024),
     and SSD1306 / MAX98357A / microSD socket / WS2812 have no real registry
     presence. `ato create part -s <LCSC#>` auto-gens footprint+symbol+3D from a
     JLCPCB/LCSC part number (all 6 parts have common SKUs), but that's ~5 custom
     `.ato` modules to author+maintain — comparable to defining them in KiCad.
     **⇒ For this part set, plain KiCad + an MCP server is the lower-risk pick;**
     reach for atopile only if we want code-based reuse/constraint-solving and will
     maintain those parts.
   - **Simulation = Wokwi** (separate track). Runs the **actual PlatformIO
     firmware** on a sim ESP32-S3, open **`diagram.json`**, **`wokwi-cli`** for CI,
     and an **official (experimental) MCP server** (`wokwi-cli mcp`, needs token).
     "Wokwi-in-the-loop": validate wiring + firmware before committing copper.
   - **Dropped:** EasyEDA (text JSON format + JS API + tight JLCPCB integration,
     but weaker on MCP/CLI/work-from-here than KiCad). Fallback only if
     assembled-board fab cost dominates later → EasyEDA + JLCPCB.

   ### What "generate PCBs from here" actually delivers
   Automatable from Claude Code: **schematic/netlist, BOM, part selection,
   footprint assignment**. Manual/GUI (or Freerouting, variable quality):
   **placement + routing + final fab export**. This board is mostly connectors +
   an ESP32 module + passives, so automation is more viable here than for general
   PCBs — but the KiCad GUI still owns final layout. No finished manufacturable
   board pops out of the CLI.

   ### Plan (tasks)
   1. ✅ **DONE (2026-07-04): atopile registry coverage checked — thin.** Favour
      plain KiCad + MCP for this part set (see RESOLVED note above). (Known sim
      gap still stands: **MAX98357A is NOT a stock Wokwi part** — stub it or write
      a custom chip.)
   2. ✅ **DONE (2026-07-04): stack trial-installed, all runnable from here.**
      - **KiCad 10.0.3** already on the box (winget). `kicad-cli` works by full
        path `C:\Program Files\KiCad\10.0\bin\kicad-cli.exe` — **not on PATH**
        (user-level PATH add if wanted, no admin).
      - **wokwi-cli 0.26.1** in `C:\Users\james\.wokwi\bin` (on User PATH). Installed
        via `iwr https://wokwi.com/ci/install.ps1 -useb | iex` (**NOT npm** — the
        npm package 404s). `mcp` subcommand confirmed. **Needs `WOKWI_CLI_TOKEN`**
        (from wokwi.com CI dashboard) before it can simulate — not yet obtained.
      - **atopile 0.12.5** via `uv tool install atopile` (`ato` on PATH).
      - **kicad-mcp** (lamaalrajih) NOT installed but requirements satisfied
        (uv + Python ≥3.10 present; shells out to `kicad-cli`, no KiCad-bundled
        Python needed). Wire up in a follow-up: `make install` → point MCP client
        at its `.venv` python + `main.py`, set `KICAD_SEARCH_PATHS` in `.env`.
   3. Wokwi sim of the current S3 build (firmware + `diagram.json`) as the wiring
      ground-truth before schematic capture. (Not yet done; `wokwi-cli` installed,
      needs `WOKWI_CLI_TOKEN`.)
   4. ✅ **DONE (2026-07-04): board authored in atopile & builds.** User chose
      **atopile** (over the plain-KiCad lean) and it worked: project at
      **`hardware/lasertag-carrier/`** (committed `8ce4e3f`). `ato build` compiles
      the full carrier from `pcb-blocks.md` → KiCad-10 netlist + `.kicad_pcb`,
      **37 components / 38 nets / 0 errors**, every part on a real KiCad footprint
      (offline, no LCSC picker). Toolchain fully validated on this box; `ato build`
      needs env `PYTHONUTF8=1 PYTHONIOENCODING=utf-8 NO_COLOR=1 ATO_NON_INTERACTIVE=1`
      (else cp1252 emoji crash); the "KiCad plugin/config" warning is GUI-only,
      **non-blocking**. **⚠ Toolchain pin: `atopile==0.12.5` on `--python 3.13`**
      (`uv tool install "atopile==0.12.5" --python 3.13 --force`). **Do NOT upgrade
      to 0.15.7** — tested 2026-07-05, it *regressed*: emptied
      `layouts/default/default.kicad_pcb` to 0 footprints (its layout-sync needs the
      KiCad GUI plugin, uninstallable until KiCad is launched once) AND didn't fix
      the caveats. A naive rollback also broke atopile under **Python 3.14**
      (`TypeError` in typer) — hence the explicit `--python 3.13` pin. Layout was
      restored from git. **Caveats — MODELING consequences, NOT a version bug**
      (0.15.7 left them UNCHANGED, proving they come from the offline
      custom-`component` approach): (a) designators render `U1-U37` — real refdes preserved in
      each part's `atopile_address` property; (b) `bom.csv` empty AND electrical
      values (470Ω/33Ω/470µF…) live only in `.ato` comments, NOT the netlist
      (custom footprint-only components carry no value/LCSC part) — full 37-line
      BOM lives in `pcb-blocks.md`; (c) two jumper footprints patched locally
      (`footprints/LocalJumper.pretty`, KiCad-10 `allow_soldermask_bridges`).
      A stdlib-generics probe (`Resistor`/`Capacitor` with real values) confirmed
      the alt path yields correct `R1`/`C1` designators + values + auto-LCSC BOM —
      but it forces **SMD** parts, wrong for this hand-soldered board.
   5. ✅ **DONE (2026-07-12) — and it WAS automatable end-to-end.** All three
      caveats closed by script (designators from `atopile_address`, values +
      BOM from spec), then: 100×80mm board (grew from 75×50 — same fab price
      tier, killed all silk collisions), directed placement per user floor
      plan, **Freerouting** autoroute (pcbnew DSN→SES round trip, GND pour
      added post-route), full silkscreen pass (pin-1/signal labels, values
      in-body, socket names under breakout bodies, board titles), DRC to
      **0 unconnected / 0 electrical**, Gerbers+drill exported and **ordered**
      (see PCB ORDERED at top). Scope deltas during layout: **level-shifter
      block moved off-board** (standalone Block 7; J8 output feeds it by
      cable), **J4 speaker header removed** (MAX98357A module has its own
      terminals; nets went nowhere), **added SW2 power switch + JP6 always-on
      bridge, J14+R9 activity-LED header (GP7), H1-H4 M3 holes**. D1 = the
      off-board IR LED (on a cable into J7) — that's why the board starts at
      D2. **The `.kicad_pcb` is now the source of truth; NEVER `ato build`
      again** (regenerates layout). Full toolchain write-up + every gotcha:
      **`PCB_FROM_PLATFORMIO.md`** (repo root; distilled to reusable skills in
      `~/on-demand/PCBs/`). Authoritative BOM = `hardware/lasertag-carrier/bom.csv`
      (32 BOM lines / 38 fitted parts; supersedes the lean table above for the carrier build).

   Open questions (all deferred until boards arrive): assemble + bring-up;
   build the standalone level-shifter board if ring/status LEDs wanted;
   connector standardisation (Qwiic/STEMMA) for a future rev.

## User prefs
Concise/direct; DRY + interfaces for extensibility; markdown for tickets/PRs;
good comments + XML docs on public APIs; commit/push only when asked (and this is
the user's personal repo — merge to `main` directly, no PR review needed). Branch
naming rules apply to fnz-qhub repos, not this one.
