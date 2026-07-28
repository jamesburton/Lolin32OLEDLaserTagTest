# Android Game Controller — Options (2026-07-27 exploration)

> **SUPERSEDED 2026-07-28 — both options were built.** Option B (phone-as-host)
> shipped as `dotnet/LaserTag.App`, and Option C (web UI) shipped as
> `dotnet/LaserTag.Web`; they share their screens via `dotnet/LaserTag.Ui` and
> their engine via `dotnet/LaserTag.Runtime`. See
> [the managers design](superpowers/specs/2026-07-28-managers-design.md) and the
> README's "Managers" section. This page is kept for the reasoning that led
> there.
>
> **One claim below proved wrong:** the `maui-android` workload was *not*
> installed — only `android`, `ios`, `maccatalyst` and `maui-windows` were.
> `dotnet workload install maui-android` fixed it. A second trap the exploration
> missed: MAUI builds a service provider but never starts `IHostedService`, so
> the UDP listener and tick loop must be started by hand.

Goal: control the laser-tag game manager from a phone. Explored overnight per
user request; no code committed yet — this doc is the decision aid.

## The pivotal architecture question: where does the engine run?

The current manager is `LaserTag.Host`, a .NET console app that must sit on the
same LAN, own UDP 4210 (heartbeats/EVT in, CTL broadcasts out), and tick
`MatchEngine`. Everything below hinges on whether the phone *talks to* that
host or *replaces* it.

**Crucial enabler:** `LaserTag.Client` and `LaserTag.Game` are pure .NET class
libraries (no console/OS dependencies; the injectable clock and `IControlSender`
were designed for this). They reference nothing Windows-specific and can be
consumed by an Android app unchanged.

## Option A — Phone as remote control (host keeps running on a PC)

Add a small HTTP+WebSocket API to `LaserTag.Host` (ASP.NET Core minimal API in
the existing Generic Host); the phone app is a thin client (device list, start
/stop buttons, live scoreboard over the socket).

- **Pros:** tiny app; host stays the single authority; also free browser UI
  (see Option C) since the API exists anyway.
- **Cons:** still needs a PC/laptop (or Pi) running at play time — the actual
  pain point we're trying to remove; two things to keep alive instead of one.

## Option B — Phone IS the host (recommended)

A .NET MAUI Android app referencing `LaserTag.Game` + `LaserTag.Client`
directly: the phone binds UDP 4210, receives heartbeats/EVTs, runs
`MatchEngine`/modes, broadcasts CTL. No PC involved at play time.

- **Pros:** zero-infrastructure games (phone + boards); reuses the tested
  engine verbatim (134 tests stay authoritative); the REPL's GameService
  pattern ports almost 1:1 (lock + 250 ms tick via `IDispatcherTimer` or a
  background service).
- **Cons / gotchas found in exploration:**
  - **Android drops inbound broadcast UDP unless the app holds a
    `WifiManager.MulticastLock`** — must acquire it while the screen is on;
    this is THE classic "works on PC, silent on Android" trap.
  - Keep-alive: matches are minutes long — `Activity` keep-screen-on flag is
    simpler and kinder than a foreground service; a backgrounded match can
    miss EVTs (device self-timeout in chase mode already tolerates host gaps —
    a design synergy worth noting).
  - Subnet broadcast address discovery: trivial on Android
    (`WifiManager`/`ConnectivityManager` LinkProperties) — simpler than the
    PC's multi-NIC mess.
  - The `maui-android` workload is ALREADY installed on this machine (VS
    18.x), so this builds locally today; first build downloads Android SDK
    bits (~minutes).
- **UI shape:** 3 screens — Devices (roster + per-board identify/team), Match
  (mode picker + params + big start/stop), Live (scoreboard + event feed).
  MVVM with CommunityToolkit.Mvvm; or Blazor Hybrid if we prefer HTML UI and
  future code-share with a web page.

## Option C — No app at all: web UI served by the host

Option A's API plus a static single-page UI served by `LaserTag.Host`; phone
uses the browser, nothing to install.

- **Pros:** zero install/store friction; works on iPhone guests too.
- **Cons:** PC still required (same as A); browser JS cannot do raw UDP, so it
  cannot evolve into Option B later — a dead end for the "no PC" goal, though
  a nice freebie alongside A.

## Recommendation

**B**, as a new `dotnet/LaserTag.App` MAUI project (Android target first;
`maui-windows` is installed too, so a desktop flavour comes almost free for
bench use). Port `GameService`'s locking/tick pattern; add a
`MulticastLock`-holding UDP service on Android. Keep `LaserTag.Host` — it
remains the dev/bench tool and the Claude-driveable surface.

If a PC-at-play-time is acceptable after all, A+C (one minimal API + web page)
is the cheapest path and keeps phones OS-agnostic — a reasonable fallback if
MAUI friction bites.

## Suggested first milestone (next session)

1. `dotnet new maui` → `LaserTag.App`, reference Game/Client, Devices screen
   fed by the UDP listener (MulticastLock + roster) — proves the whole risk
   surface (broadcast RX on Android) in one step.
2. Match screen driving `MatchEngine` (dm/elim/chase).
3. Live scoreboard screen (snapshot polling at 4 Hz is plenty).
4. Later: foreground-service hardening, iOS target if ever needed.
