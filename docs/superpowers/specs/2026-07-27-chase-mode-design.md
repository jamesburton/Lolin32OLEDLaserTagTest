# Chase Mode + Score Display — Design

Date: 2026-07-27
Status: Approved (interactive Q&A 2026-07-26/27); executed overnight per user
instruction ("you have the night to work out some plans … continue without
interruptions").

## 1. Goal

A fast "chase the target" practice mode for 2+ boards: one board at a time is
the *active* target (team-colour spin animation). If it is not hit within a
randomized window (2–5 s, configurable) it times out and goes dark; after a
short gap another board lights up. Hits score for the shooter's team; an
optional penalty deducts for shooting a dormant board. Works solo (beat your
score) and versus (per-team scores — the Vatos protocol has no player id).

Boards double as scoreboards: team scores render on the 8×8 matrix, growing
from the blank middle outward. This makes boards score *displays* (host stays
authoritative for scores, as it is for match state; hp remains
device-authoritative and untouched by chase).

This spec also delivers the core of Spec B (firmware CTL pass) because chase
depends on it: the `id=` filter, `activate`/`deactivate` dormancy, and
`countdown`/`gameover` cues all land in firmware here.

## 2. Wire contract additions (v2.1)

### 2.1 CTL grammar (host → devices, UDP 4210 subnet broadcast, 3× repeat)

All verbs accept an optional trailing `id=<deviceId>`: a device ignores the
whole line when `id=` is present and does not match its own deviceId. Absent
`id=` means "everyone". (The host has emitted `id=` last since Spec A; firmware
now enforces it — this closes the "addressed reset hits every board" caveat.)

New/now-implemented-in-firmware verbs:

| Verb | Meaning |
|---|---|
| `CTL countdown n=<sec>` | Match starting: show a countdown cue (flash + beep per second, dark otherwise). |
| `CTL gameover winner=<team\|0>` | Match over: scoreboard hold 5 s (if scores were pushed), then winner-colour flood 3 s (`0` = draw → white), then normal idle. |
| `CTL activate [t=<ms>]` | Become the active chase target: spin animation; with `t=`, self-deactivate after `t` ms unhit → `EVT state timeout`. Without `t=`: active until told otherwise. |
| `CTL deactivate` | Return to dormant. |
| `CTL chase on penalty=<0\|1> display=<score\|dark>` | Enter chase match mode: all boards go dormant (dim scoreboard or dark), store the penalty-feedback flag. |
| `CTL chase off` | Leave chase mode; resume normal idle (rainbow, hp-reactive). |
| `CTL score 1=<n> 2=<n> 3=<n> 4=<n>` | Team scores for display. Sent on every change plus a 1 s refresh while a match runs (refresh doubles as loss recovery; single send, no 3× repeat). Negative values clamp to 0 for display. |

The window timer lives on the DEVICE (self-timeout): a lost `deactivate` can
never leave a target lit, and the visual window doesn't wobble with WiFi
jitter. The host keeps a fallback (window + 1.5 s slack): if neither `EVT hit`
nor `EVT state timeout` arrives it assumes timeout, defensively broadcasts
`deactivate id=<dev>`, and moves on.

### 2.2 EVT additions (devices → host)

- `EVT hit … dormant=1` — hit received while dormant/inactive. No hp change,
  no death; hp field reports current (unchanged) hp. Host uses it for the
  wrong-target penalty. Emitted only in chase mode.
- `EVT state s=active|dormant|timeout ts=<ms>` — chase transitions. `timeout`
  is the load-bearing one (host sequencing); `active`/`dormant` confirm
  delivery and aid debugging.

## 3. Firmware behaviour (esp32-s3-matrix)

Chase state machine, orthogonal to hp (hp is neither spent nor shown in chase):

- **Normal** — today's behaviour (rainbow idle, hits apply damage×multiplier).
- **ChaseDormant** — entered on `chase on`. Display: dim scoreboard (25 % of
  configured brightness, min 1) or dark, per `display=`. IR hits do NOT apply
  damage; they emit `EVT hit … dormant=1`. If the penalty flag is set, give a
  short dim-red blink + error tone (audio boards); otherwise stay silent/dark.
- **ChaseActive** — entered on `activate`. Display: spinning perimeter ring in
  `chaseColour` (new persisted config field, default `#FFA500` amber; per-board
  override via the normal config PATCH). A decoded hit → normal team-flash +
  hit SFX, `EVT hit` (no dormant flag, hp untouched), then back to dormant.
  Window expiry → 300 ms red flash → dormant + `EVT state timeout`.

`countdown`/`gameover` cues work in every mode (dm/elim benefit too).
Scoreboard rendering, layout below, is a pure function in ControlProto
(`scoreGrid`: scores + enabled teams → 8×8 team-index grid) so it native-tests;
HitDisplay just paints the grid through the existing colour map.

### 3.1 Scoreboard layout (8×8)

- **2 enabled teams** — vertical split, middle-out: columns 3→0 fill for the
  lower-indexed team, 4→7 for the other; each column fills bottom-to-top,
  1 LED = 1 point, saturating at 32/side. Zero score = blank middle, exactly as
  requested.
- **3–4 enabled teams** — 4×4 quadrants (team 1 TL, 2 TR, 3 BL, 4 BR), each
  filling from the centre of the panel outward, 1 LED = 1 point, saturating at
  16/team. Score keeps counting host-side past saturation; display clamps.
- Cells light in the team's configured colour.

### 3.2 Standalone scoreboard mode

`POST /api/mode {"mode":"scoreboard"}` turns any board into a dedicated
full-brightness scoreboard (ignores IR, ignores `activate`): e.g. a spare board
on the wall during ANY match type once the host pushes scores. HB reports
`mode=scoreboard`; the host excludes such boards from the chase target pool.
This falls out nearly free from the score CTL + renderer.

## 4. Host (LaserTag.Game / LaserTag.Host)

**ChaseMode : IGameMode**, alongside dm/elim:

```
start chase [duration] [first=N] [min=2s] [max=5s] [gap=1s] [penalty=0|1] [display=score|dark]
```

- Both end conditions supported; whichever trips first ends the match
  (duration omitted → unlimited; first omitted → no count cap; at least one
  required).
- Sequencing (host-side, engine tick 250 ms): Gap(until) → pick target →
  `activate id=<dev> t=<window>` with window ~ U[min,max] → wait for
  `EVT hit` (score +1 shooter team, gap, next) or `EVT state timeout` /
  slack expiry (no score, gap, next).
- Target pick: uniform over online, non-scoreboard-mode participants;
  with ≥3 boards the previous target is excluded (no immediate repeat);
  with 2 boards pure random (alternation would be predictable).
- `EVT hit dormant=1` → −penalty for the shooter's team (floor at 0 —
  scores never display or persist negative).
- Scoring is 1 point per hit regardless of weapon damage/multiplier
  (weapon-agnostic chase; a `score=dmg` variant is a documented future option).
- Score pushes: `CTL score …` on every change + 1 s refresh (all modes — dm
  gets wall-scoreboard support for free).
- Match start: `chase on penalty=… display=…` → countdown → first activate.
  Match end: `gameover winner=…` + `chase off`.
- Offline handling: if the active target's device drops offline, treat as
  timeout immediately.

## 5. Permutations explored (and where they landed)

| Option | Decision |
|---|---|
| Dormant boards: dark vs live dim scoreboard | **Dim scoreboard default**, `display=dark` flag (user-selected) |
| Wrong-target penalty | **Config `penalty=`, default off**; red blink + buzz feedback when on (user-selected) |
| Hit response | **Short gap** (default 1 s, `gap=`) before next activation (user-selected) |
| Active look | **Team-colour… configurable-colour spin** (`chaseColour`, default amber) (user-selected) |
| End condition | **Both**: `duration` and/or `first=N` (user-selected) |
| 4-team scoreboard | **Quadrants** (user-selected) |
| Standalone scoreboard board | **In scope** (cheap once score CTL exists) |
| Gameover scoreboard hold | **In scope**: 5 s scoreboard → winner flood |
| Score = damage points | Out for now; 1 pt/hit. Future option flag. |
| Multi-active "swarm" (2+ targets lit) | Future option (grammar already supports it — host just activates several; deferred for rules clarity) |
| Shrinking window (difficulty ramp) | Future option; host-side only (window schedule), no firmware change needed |
| Best-streak tracking (solo) | Future option, host/console only |
| OLED-shows-health (Lolin32) | Still Spec B leftover, not needed for chase |
| **Vibration motor feedback** | Not for this pin-constrained S3-Matrix carrier. Noted for a future **wearable target** board: drive it from the same cue abstraction as sound (hit / penalty / activate cues), one GPIO + MOSFET. |

## 6. Testing

- **Native (PlatformIO)**: parser/encoder golden vectors for every new verb and
  the `id=` filter (match own id, mismatch, absent, `id=` with other keys in
  any position); `scoreGrid` layout tests (2-team middle-out fill order,
  quadrant fill order, clamp, empty, negative-clamped input).
- **xUnit (host)**: ChaseMode sequencing with the injectable clock — window
  randomization within [min,max], gap, hit scoring, dormant penalty + floor,
  timeout advance, slack fallback when the timeout EVT is lost, both end
  conditions (and combined), 2-board vs 3-board pick policy, offline target,
  scoreboard-mode exclusion; Control format/parse round-trips for new verbs.
- **Bench**: two powered boards (matrix3/matrix4) — full chase round-trip over
  the air; verify EVT flow, score pushes, timeout self-deactivation.

## 7. Out of scope

- Firmware master / host-less play (future Spec C successor).
- MP3 decode, configurable sound sources (handoff item 8b), quack-as-gameover.
- Wearable/vibration hardware.
- Android controller app (separate exploration doc this session).
