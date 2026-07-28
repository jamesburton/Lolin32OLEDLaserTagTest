# Web + Android Managers — Design

Status: designed and built 2026-07-28 in an autonomous session. Supersedes the
options exploration in [android-controller-options.md](../../android-controller-options.md),
whose recommendation (Option B, phone-as-host) is adopted here — together with
Option C (web UI), because the two turn out to share almost everything.

## 1. Goal

Control a match without a keyboard. Two shells:

- **Web manager** — served from a PC on the LAN, driven from any phone or
  laptop browser. Zero install, works for guests on iPhones.
- **Android manager** — the phone *is* the host: it binds UDP 4210, runs the
  match engine, and broadcasts CTL. No PC at play time, which was the actual
  pain point.

`LaserTag.Host` (the console REPL) stays exactly as it is: it remains the bench
tool and the agent-driveable surface.

## 2. The insight that shapes everything

The two shells differ in *where the engine runs*, not in *what the user sees*.
Both show the same four screens over the same state. So the split is:

```
                    ┌──────────────────────────────┐
                    │  LaserTag.Ui  (Razor library)│  Devices / Match / Live / Firmware
                    └──────────────┬───────────────┘
                                   │ IGameSession
              ┌────────────────────┴────────────────────┐
              │                                          │
   LaserTag.Web (ASP.NET Core)             LaserTag.App (MAUI Blazor Hybrid)
   engine runs on the PC                   engine runs on the phone
              │                                          │
              └──────────────┬───────────────────────────┘
                             │
                  LaserTag.Runtime (GameService, UDP listener, tick loop)
                             │
              LaserTag.Game (MatchEngine, modes) + LaserTag.Client
```

**One UI codebase, two shells.** A screen written once renders identically in a
browser and in the Android app, because in both cases it is Blazor talking to
the same `IGameSession` abstraction.

## 3. Components

### `LaserTag.Runtime` (new library, extracted from `LaserTag.Host`)

`GameService`, `UdpTelemetryService` and `MatchEngineService` move here
unchanged in behaviour. They were already free of console dependencies — the
console coupling lived only in `ConsoleUiService`, which stays in the Host.

Two additions make the runtime hostable on Android:

- **`IPlatformNetworkGuard`** — a no-op on desktop; on Android it acquires and
  releases a `WifiManager.MulticastLock`. Android silently drops inbound
  broadcast UDP without one, and that is the single failure most likely to make
  the app look broken while the PC version works. Putting it behind an
  interface means the trap is handled once, in a named place, rather than
  discovered again later.
- **`GameService.StateChanged`** — an event raised after each tick so a UI can
  re-render on change instead of polling. The existing `Event` (printable
  lines) is kept for the console and the live feed.

### `LaserTag.Ui` (new Razor class library)

The shared screens, plus an `IGameSession` interface that abstracts "the thing
running the match":

```csharp
public interface IGameSession
{
    IReadOnlyList<RosterEntry> Devices();
    MatchSnapshot Snapshot();
    string? StartMatch(IGameMode mode);
    void Stop();
    void SendControl(Control control);
    IReadOnlyList<string> RecentEvents { get; }
    event Action? Changed;
}
```

In both shells this is backed directly by `GameService` — the interface exists
so components stay testable and so a future thin-client shell (phone talking to
a remote host) can implement it over HTTP without touching the UI.

Screens: **Devices** (roster, per-board identify/team/reset), **Match** (mode
picker with per-mode parameters, start/stop), **Live** (scoreboard, alive
counts, event feed), **Firmware** (running-vs-available versions, OTA push) —
the last reusing `FirmwareImage`/`FirmwareUpdater` from the fleet-OTA work.

### `LaserTag.Web`

ASP.NET Core hosting the runtime plus Blazor Server for the UI. Server-side
rendering means the phone browser holds no game state and reconnects cleanly,
and it needs no JSON plumbing for the UI itself.

A small JSON API (`/api/devices`, `/api/match`, `POST /api/match/start`,
`/api/match/stop`, `/api/control`) exists alongside it for scripting and tests.

### `LaserTag.App`

MAUI Blazor Hybrid targeting Android. Hosts the same `LaserTag.Ui` components in
a `BlazorWebView`, with the runtime registered in its DI container so the engine
runs in-process on the phone. Adds the Android `MulticastLock` guard and
keep-screen-on.

## 4. Data flow

Identical in both shells, because it is the same code:

1. Devices broadcast `HB`/`EVT` on UDP 4210.
2. `UdpTelemetryService` parses and feeds `GameService` (single lock).
3. `MatchEngineService` ticks at 4 Hz; `GameService` pushes CTL score updates
   and raises `StateChanged`.
4. The UI re-renders from `Snapshot()`.

## 5. Error handling

- **No usable NIC** — both shells surface the broadcast-discovery failure as a
  visible banner rather than a silent dead roster, with the `--broadcast`
  override explained. This is the Host's existing failure mode, which currently
  prints to a console nobody will be watching on a phone.
- **Android with no multicast lock** — the guard makes this structural, but the
  Devices screen also shows "no heartbeats yet" guidance after 10 seconds of
  silence, naming the lock and the firewall as the two usual causes.
- **OTA failures** — already non-throwing in `FirmwareUpdater`; the UI shows the
  device-reported error, including the "pre-2.1.0 firmware has no /api/update"
  hint.
- **Match start with an empty lobby** — returned as a string by
  `StartMatch`, shown inline.

## 6. Testing

- Runtime extraction must leave all existing tests passing unchanged — that is
  the proof the move was behaviour-preserving.
- New `LaserTag.Web.Tests` using `WebApplicationFactory`: the API returns an
  empty roster cleanly, accepts a start request, rejects an unknown mode, and
  reports match state. A fake `IControlSender` captures CTL instead of
  broadcasting, so tests never touch the network.
- UI components that contain logic (mode parameter parsing) get unit tests; the
  markup does not, since asserting on rendered HTML is brittle and low value.
- The Android app is **build-verified only**. No device or emulator is available
  in this session, so its UDP path cannot be proven here; that limitation is
  recorded rather than papered over, and the first on-device run should be
  treated as the real test of the multicast lock.

## 7. Out of scope

iOS (no signing/hardware), authentication (LAN-trusted, consistent with the
device REST API), and the thin-client shell where a phone drives a remote host —
`IGameSession` leaves the door open without building it.

## 8. Success criteria

1. `LaserTag.Host` behaves exactly as before, with all tests green.
2. The web manager serves the four screens, and a match can be started, watched
   and stopped from a phone browser.
3. The Android app builds to an APK, sharing UI code with the web manager.
4. Documentation states plainly what was verified on hardware and what was not.
