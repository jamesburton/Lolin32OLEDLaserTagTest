# Chase Mode + Score Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Host-driven "chase the active target" game mode with on-matrix team scoreboards, delivering the firmware CTL v2.1 pass (`id=` filter, countdown/gameover cues, activate/deactivate dormancy) it depends on.

**Architecture:** Pure grammar/layout logic lands in ControlProto (native-tested); matrix_main owns the chase visual state machine; the host gains a `ChaseMode : IGameMode`, new Control kinds, and a score pusher. Spec: `docs/superpowers/specs/2026-07-27-chase-mode-design.md`.

**Tech Stack:** C++ (PlatformIO, native Unity tests), .NET 9 (xUnit), existing LaserTag.* projects.

## Global Constraints

- Firmware pure logic goes in `lib/ControlProto` ONLY (no Arduino.h) so it native-tests: `pio test -e native`.
- Host tests: `dotnet test dotnet/LaserTag.sln`. Current baseline: 51 native + 134 .NET green; never regress either.
- CTL `id=` is always the LAST key on the wire (established host convention).
- hp is never pushed by the host, and chase never touches hp.
- All builds: watch for intermittent Windows Defender "Access is denied" link failures — retry once before investigating.
- Commit after each task with the session trailer lines already used on this branch.

---

### Task 1: ControlProto — CTL v2.1 parse + encoders + chaseColour

**Files:**
- Modify: `lib/ControlProto/ControlProto.h`
- Modify: `lib/ControlProto/ControlProto.cpp`
- Test: `test/test_controlproto/test_controlproto.cpp`

**Interfaces:**
- Produces (used by Tasks 3/4): extended `Control` struct (below), `parseControl` recognising all v2.1 verbs, `int formatDormantHitEvent(char*, size_t, const char* victim, int shooterTeam, int dmg, const char* proto, int hp, uint32_t ts)`, `ConfigDoc.chaseColour` (`char[8]`, default `"#FFA500"`).

- [ ] **Step 1: Write the failing tests** — append to `test_controlproto.cpp` (existing Unity style; register each in the runner at the bottom of the file):

```cpp
static void test_parse_control_id_filter_and_new_verbs() {
  cp::Control c;
  // id= captured on any verb.
  TEST_ASSERT_TRUE(cp::parseControl("CTL reset hp=0 id=eb20f8", c));
  TEST_ASSERT_TRUE(c.hasId);
  TEST_ASSERT_EQUAL_STRING("eb20f8", c.id);
  TEST_ASSERT_TRUE(c.hasHp);
  TEST_ASSERT_EQUAL_INT(0, c.hp);
  // No id= -> hasId false (broadcast).
  TEST_ASSERT_TRUE(cp::parseControl("CTL start", c));
  TEST_ASSERT_FALSE(c.hasId);
  // countdown / gameover.
  TEST_ASSERT_TRUE(cp::parseControl("CTL countdown n=5", c));
  TEST_ASSERT_EQUAL(cp::ControlKind::Countdown, c.kind);
  TEST_ASSERT_TRUE(c.hasN);
  TEST_ASSERT_EQUAL_INT(5, c.n);
  TEST_ASSERT_TRUE(cp::parseControl("CTL gameover winner=2", c));
  TEST_ASSERT_EQUAL(cp::ControlKind::GameOver, c.kind);
  TEST_ASSERT_TRUE(c.hasWinner);
  TEST_ASSERT_EQUAL_INT(2, c.winner);
  // activate with window + id last.
  TEST_ASSERT_TRUE(cp::parseControl("CTL activate t=3200 id=752b38", c));
  TEST_ASSERT_EQUAL(cp::ControlKind::Activate, c.kind);
  TEST_ASSERT_TRUE(c.hasT);
  TEST_ASSERT_EQUAL_UINT32(3200, c.t);
  TEST_ASSERT_EQUAL_STRING("752b38", c.id);
  TEST_ASSERT_TRUE(cp::parseControl("CTL activate", c));
  TEST_ASSERT_FALSE(c.hasT);
  TEST_ASSERT_TRUE(cp::parseControl("CTL deactivate", c));
  TEST_ASSERT_EQUAL(cp::ControlKind::Deactivate, c.kind);
}

static void test_parse_control_chase_and_score() {
  cp::Control c;
  TEST_ASSERT_TRUE(cp::parseControl("CTL chase on penalty=1 display=dark", c));
  TEST_ASSERT_EQUAL(cp::ControlKind::ChaseOn, c.kind);
  TEST_ASSERT_EQUAL_INT(1, c.penalty);
  TEST_ASSERT_FALSE(c.displayScore);
  TEST_ASSERT_TRUE(cp::parseControl("CTL chase on penalty=0 display=score", c));
  TEST_ASSERT_EQUAL_INT(0, c.penalty);
  TEST_ASSERT_TRUE(c.displayScore);
  // Defaults when keys omitted: penalty 0, display score.
  TEST_ASSERT_TRUE(cp::parseControl("CTL chase on", c));
  TEST_ASSERT_EQUAL_INT(0, c.penalty);
  TEST_ASSERT_TRUE(c.displayScore);
  TEST_ASSERT_TRUE(cp::parseControl("CTL chase off", c));
  TEST_ASSERT_EQUAL(cp::ControlKind::ChaseOff, c.kind);
  // Unknown chase subtype drops.
  TEST_ASSERT_FALSE(cp::parseControl("CTL chase wat", c));
  // score with all four teams.
  TEST_ASSERT_TRUE(cp::parseControl("CTL score 1=4 2=0 3=12 4=7", c));
  TEST_ASSERT_EQUAL(cp::ControlKind::Score, c.kind);
  TEST_ASSERT_TRUE(c.hasScores);
  TEST_ASSERT_EQUAL_INT(4, c.scores[0]);
  TEST_ASSERT_EQUAL_INT(0, c.scores[1]);
  TEST_ASSERT_EQUAL_INT(12, c.scores[2]);
  TEST_ASSERT_EQUAL_INT(7, c.scores[3]);
  // Missing team keys default to 0.
  TEST_ASSERT_TRUE(cp::parseControl("CTL score 2=9", c));
  TEST_ASSERT_EQUAL_INT(0, c.scores[0]);
  TEST_ASSERT_EQUAL_INT(9, c.scores[1]);
}

static void test_format_dormant_hit_event() {
  char buf[128];
  int n = cp::formatDormantHitEvent(buf, sizeof(buf), "eb20f8", 3, 2, "vatos",
                                    32, 1234);
  TEST_ASSERT_GREATER_THAN(0, n);
  TEST_ASSERT_EQUAL_STRING(
      "EVT hit victim=eb20f8 shooterTeam=3 dmg=2 proto=vatos hp=32 ts=1234 "
      "dormant=1",
      buf);
}

static void test_config_chase_colour_serialize_patch() {
  cp::ConfigDoc cfg;
  strncpy(cfg.deviceId, "a1b2c3", sizeof(cfg.deviceId) - 1);
  strncpy(cfg.hostname, "lasertag-matrix", sizeof(cfg.hostname) - 1);
  cfg.ownTeam = 2;
  cfg.enabledTeams[0] = 1; cfg.enabledTeams[1] = 2;
  cfg.enabledTeams[2] = 3; cfg.enabledTeams[3] = 4;
  cfg.enabledTeamsCount = 4;
  char buf[640];
  cp::serializeConfig(cfg, buf, sizeof(buf));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"chaseColour\":\"#FFA500\""));
  cp::PatchResult r = cp::applyConfigPatch("{\"chaseColour\":\"#00FFAA\"}", cfg);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_STRING("#00FFAA", cfg.chaseColour);
  r = cp::applyConfigPatch("{\"chaseColour\":\"red\"}", cfg);
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_STRING("#00FFAA", cfg.chaseColour); // unchanged on reject
}
```

Also UPDATE the existing golden ConfigDoc test (the one asserting the full
serialized JSON ending `"teamDamageMult":{"1":0,"2":0,"3":0,"4":0}}`): the
expected string now ends
`"teamDamageMult":{"1":0,"2":0,"3":0,"4":0},"chaseColour":"#FFA500"}`.

- [ ] **Step 2: Run to verify failure** — `pio test -e native` → compile error (`hasN`/`formatDormantHitEvent`/`chaseColour` undefined). That IS the red state for a compiled language.

- [ ] **Step 3: Implement.** In `ControlProto.h`: extend the enum/struct:

```cpp
enum class ControlKind {
  None, Start, Stop, Reset,
  Countdown,  ///< `CTL countdown n=<sec>`
  GameOver,   ///< `CTL gameover winner=<team|0>`
  Activate,   ///< `CTL activate [t=<ms>]` — become the chase target
  Deactivate, ///< `CTL deactivate`
  ChaseOn,    ///< `CTL chase on penalty=<0|1> display=<score|dark>`
  ChaseOff,   ///< `CTL chase off`
  Score,      ///< `CTL score 1=<n> 2=<n> 3=<n> 4=<n>`
};

struct Control {
  ControlKind kind = ControlKind::None;
  bool hasTs = false;  uint32_t ts = 0;
  bool hasHp = false;  int hp = 0;
  bool hasN = false;   int n = 0;        ///< countdown seconds
  bool hasWinner = false; int winner = 0; ///< gameover winner (0 = draw)
  bool hasT = false;   uint32_t t = 0;   ///< activate self-timeout window ms
  int penalty = 0;             ///< chase on: penalty feedback flag
  bool displayScore = true;    ///< chase on: dormant display score|dark
  bool hasScores = false; int scores[4] = {0, 0, 0, 0}; ///< score, teams 1..4
  bool hasId = false;  char id[8] = ""; ///< addressing filter target
};
```

Add to `ConfigDoc` (after `teamDamageMult`): `char chaseColour[8] = "#FFA500"; ///< chase active spin colour`.

Declare `formatDormantHitEvent` (same params as `formatHitEvent`).

In `ControlProto.cpp`: add a string-key helper next to `findIntKey`:

```cpp
// Scans `key=value` tokens for `key`; copies the value into out (NUL-safe).
bool findStrKey(const char *rest, const char *key, char *out, size_t outSize) {
  const size_t keyLen = strlen(key);
  const char *p = rest;
  while (*p) {
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) break;
    const char *tokStart = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    const size_t tokLen = (size_t)(p - tokStart);
    if (tokLen > keyLen + 1 && strncmp(tokStart, key, keyLen) == 0 &&
        tokStart[keyLen] == '=') {
      size_t valLen = tokLen - keyLen - 1;
      if (valLen >= outSize) valLen = outSize - 1;
      memcpy(out, tokStart + keyLen + 1, valLen);
      out[valLen] = '\0';
      return true;
    }
  }
  return false;
}
```

Extend `parseControl`: after the verb-specific parsing of each existing verb, add the new verbs (keep the existing early-return style), and capture `id=` for EVERY verb. Restructure as: parse kind + kind-specific keys first, then before each `return true`, run a shared tail — simplest is a small lambda-free helper:

```cpp
// Shared optional-key capture applied to every recognised verb.
static void captureCommon(const char *rest, Control &out) {
  char idBuf[8];
  if (findStrKey(rest, "id", idBuf, sizeof(idBuf))) {
    out.hasId = true;
    memcpy(out.id, idBuf, sizeof(out.id));
  }
}
```

(placed in the anonymous namespace, non-static in namespace is fine too). Then in `parseControl` each `return true;` becomes `captureCommon(rest, out); return true;`, and the new verbs go before the final drop:

```cpp
  if (startsWith(rest, "countdown", nullptr)) {
    out.kind = ControlKind::Countdown;
    if (findIntKey(rest, "n", &v)) { out.hasN = true; out.n = (int)v; }
    captureCommon(rest, out);
    return true;
  }
  if (startsWith(rest, "gameover", nullptr)) {
    out.kind = ControlKind::GameOver;
    if (findIntKey(rest, "winner", &v)) { out.hasWinner = true; out.winner = (int)v; }
    captureCommon(rest, out);
    return true;
  }
  if (startsWith(rest, "deactivate", nullptr)) { // before "activate": not a prefix, but keep the longer verb first for clarity
    out.kind = ControlKind::Deactivate;
    captureCommon(rest, out);
    return true;
  }
  if (startsWith(rest, "activate", nullptr)) {
    out.kind = ControlKind::Activate;
    if (findIntKey(rest, "t", &v) && v > 0) { out.hasT = true; out.t = (uint32_t)v; }
    captureCommon(rest, out);
    return true;
  }
  if (startsWith(rest, "chase on", nullptr) ||
      strcmp(rest, "chase on") == 0) {
    out.kind = ControlKind::ChaseOn;
    if (findIntKey(rest, "penalty", &v)) out.penalty = (int)v;
    char disp[8] = "";
    if (findStrKey(rest, "display", disp, sizeof(disp)))
      out.displayScore = strcmp(disp, "dark") != 0;
    captureCommon(rest, out);
    return true;
  }
  if (startsWith(rest, "chase off", nullptr) || strcmp(rest, "chase off") == 0) {
    out.kind = ControlKind::ChaseOff;
    captureCommon(rest, out);
    return true;
  }
  if (startsWith(rest, "score", nullptr)) {
    out.kind = ControlKind::Score;
    out.hasScores = true;
    static const char *teamKeys[4] = {"1", "2", "3", "4"};
    for (int i = 0; i < 4; i++) {
      out.scores[i] = findIntKey(rest, teamKeys[i], &v) ? (int)v : 0;
    }
    captureCommon(rest, out);
    return true;
  }
```

NOTE the "chase on"/"chase off" matching: `startsWith(rest, "chase on", ...)` matches `"chase on penalty=..."` AND bare `"chase on"`; a `"chase wat"` line matches neither branch and falls through to the drop. (`startsWith` on `"chase on"` also matches `"chase online"` — acceptable, consistent with the file's existing prefix tolerance.)

`formatDormantHitEvent` delegates:

```cpp
int formatDormantHitEvent(char *out, size_t outSize, const char *victim,
                          int shooterTeam, int dmg, const char *proto, int hp,
                          uint32_t ts) {
  const int n = formatHitEvent(out, outSize, victim, shooterTeam, dmg, proto,
                               hp, ts);
  if (n < 0) return n;
  const int m = snprintf(out + n, outSize > (size_t)n ? outSize - n : 0,
                         " dormant=1");
  return m < 0 ? m : n + m;
}
```

`serializeConfig`: append `doc["chaseColour"] = cfg.chaseColour;` after the teamDamageMult block. `applyConfigPatch`: add a `chaseColour` branch mirroring the existing `hostname` string branch PLUS validation — value must be `#` followed by exactly 6 hex chars, else `snprintf(res.error, ..., "chaseColour must be #RRGGBB")` and fail.

- [ ] **Step 4: Run** `pio test -e native` → all green (51 existing + new).
- [ ] **Step 5: Commit** — `git add lib/ControlProto test/test_controlproto && git commit -m "feat(fw): CTL v2.1 grammar — id filter, chase/score verbs, chaseColour"` (+ trailers).

---

### Task 2: ControlProto — scoreGrid layout renderer

**Files:**
- Modify: `lib/ControlProto/ControlProto.h`, `lib/ControlProto/ControlProto.cpp`
- Test: `test/test_controlproto/test_controlproto.cpp`

**Interfaces:**
- Produces (Task 4 paints this): `void scoreGrid(const int scores[4], const int *enabledTeams, size_t enabledCount, uint8_t grid[64]);` — `scores` indexed by team value − 1; `grid` row-major `y*8+x`, cell value 0 = off, else team value (1..4).

- [ ] **Step 1: Failing tests:**

```cpp
static void grid_expect(const uint8_t grid[64], int x, int y, uint8_t v) {
  TEST_ASSERT_EQUAL_UINT8(v, grid[y * 8 + x]);
}

static void test_score_grid_two_team_middle_out() {
  const int teams[2] = {1, 2};
  int scores[4] = {0, 0, 0, 0};
  uint8_t grid[64];
  // Zero score: fully blank.
  cp::scoreGrid(scores, teams, 2, grid);
  for (int i = 0; i < 64; i++) TEST_ASSERT_EQUAL_UINT8(0, grid[i]);
  // Team 1: first point at column 3 (middle-left), bottom row (y=7).
  scores[0] = 1;
  cp::scoreGrid(scores, teams, 2, grid);
  grid_expect(grid, 3, 7, 1);
  grid_expect(grid, 3, 6, 0);
  // 9 points: column 3 full (8) + 1 into column 2, bottom first.
  scores[0] = 9;
  cp::scoreGrid(scores, teams, 2, grid);
  for (int y = 0; y < 8; y++) grid_expect(grid, 3, y, 1);
  grid_expect(grid, 2, 7, 1);
  grid_expect(grid, 2, 6, 0);
  // Team 2 mirrors: first point column 4 bottom.
  scores[1] = 1;
  cp::scoreGrid(scores, teams, 2, grid);
  grid_expect(grid, 4, 7, 2);
  // Saturation: 40 points clamps to the 32-cell half.
  scores[0] = 40;
  cp::scoreGrid(scores, teams, 2, grid);
  for (int x = 0; x <= 3; x++)
    for (int y = 0; y < 8; y++) grid_expect(grid, x, y, 1);
  // Negative clamps to 0 (blank).
  scores[1] = -3;
  cp::scoreGrid(scores, teams, 2, grid);
  grid_expect(grid, 4, 7, 0);
}

static void test_score_grid_quadrants() {
  const int teams[4] = {1, 2, 3, 4};
  int scores[4] = {1, 1, 1, 1};
  uint8_t grid[64];
  cp::scoreGrid(scores, teams, 4, grid);
  // First point of each team sits at its quadrant's centre-most cell.
  grid_expect(grid, 3, 3, 1); // team 1 TL
  grid_expect(grid, 4, 3, 2); // team 2 TR
  grid_expect(grid, 3, 4, 3); // team 3 BL
  grid_expect(grid, 4, 4, 4); // team 4 BR
  // Fill order within TL: column x=3 upward (y=3,2,1,0), then x=2...
  scores[0] = 5;
  cp::scoreGrid(scores, teams, 4, grid);
  grid_expect(grid, 3, 2, 1);
  grid_expect(grid, 3, 1, 1);
  grid_expect(grid, 3, 0, 1);
  grid_expect(grid, 2, 3, 1);
  grid_expect(grid, 2, 2, 0);
  // Saturation at 16.
  scores[3] = 99;
  cp::scoreGrid(scores, teams, 4, grid);
  for (int x = 4; x < 8; x++)
    for (int y = 4; y < 8; y++) grid_expect(grid, x, y, 4);
}
```

- [ ] **Step 2:** `pio test -e native` → compile failure (scoreGrid undefined).
- [ ] **Step 3: Implement** in ControlProto.cpp (header decl with full XML docs):

```cpp
void scoreGrid(const int scores[4], const int *enabledTeams,
               size_t enabledCount, uint8_t grid[64]) {
  memset(grid, 0, 64);
  auto clampScore = [](int s, int cap) {
    if (s < 0) return 0;
    return s > cap ? cap : s;
  };
  if (enabledCount == 2) {
    // Middle-out halves: enabledTeams[0] columns 3..0, [1] columns 4..7;
    // each column fills bottom (y=7) to top, 1 LED = 1 point, cap 32.
    for (int side = 0; side < 2; side++) {
      const int team = enabledTeams[side];
      if (team < 1 || team > 4) continue;
      int remaining = clampScore(scores[team - 1], 32);
      for (int step = 0; step < 4 && remaining > 0; step++) {
        const int x = side == 0 ? 3 - step : 4 + step;
        for (int y = 7; y >= 0 && remaining > 0; y--, remaining--) {
          grid[y * 8 + x] = (uint8_t)team;
        }
      }
    }
    return;
  }
  // Quadrants by team VALUE: 1 TL, 2 TR, 3 BL, 4 BR; fill from the panel
  // centre outward (centre-most column first, centre-most row first), cap 16.
  for (size_t i = 0; i < enabledCount && i < 4; i++) {
    const int team = enabledTeams[i];
    if (team < 1 || team > 4) continue;
    const bool right = team == 2 || team == 4;
    const bool bottom = team == 3 || team == 4;
    int remaining = clampScore(scores[team - 1], 16);
    for (int cs = 0; cs < 4 && remaining > 0; cs++) {   // column step from centre
      const int x = right ? 4 + cs : 3 - cs;
      for (int rs = 0; rs < 4 && remaining > 0; rs++, remaining--) { // row step
        const int y = bottom ? 4 + rs : 3 - rs;
        grid[y * 8 + x] = (uint8_t)team;
      }
    }
  }
}
```

- [ ] **Step 4:** `pio test -e native` → green.
- [ ] **Step 5: Commit** `feat(fw): scoreGrid — 2-team middle-out + quadrant scoreboard layouts`.

---

### Task 3: Firmware — id= filter + countdown/gameover cues + scoreboard/spin painters

**Files:**
- Modify: `src/matrix_main.cpp`
- Modify: `lib/HitDisplay/HitDisplay.h`, `lib/HitDisplay/HitDisplay.cpp`

**Interfaces:**
- Consumes: Task 1 `Control` fields, Task 2 `scoreGrid`.
- Produces (Task 4 uses): `HitDisplay::spinFrame(Board::Rgb c, uint8_t phase)` (28-px perimeter, 4-px arc), `HitDisplay::scoreboard(const uint8_t grid[64], uint8_t num, uint8_t den)` (paints team cells via the colour map, channel-scaled num/den), new `Vis::Countdown`, `Vis::GameOverScore`, `Vis::GameOverFlood` states, `chaseScores[4]` + `haveScores` globals, and the id-filter gate in `onLine`.

No native tests cover this task (Arduino-side); verification is a clean
`pio run -e esp32-s3-matrix` build plus the Task 7 bench pass. Steps are
implement → build → commit.

- [ ] **Step 1: HitDisplay additions.** Header (after `idleWithHealth`):

```cpp
// Chase "active target" animation: a 4-pixel arc chasing around the 28-pixel
// perimeter of the 8x8 matrix in colour c. Advance `phase` (0..27) per frame.
void spinFrame(Board::Rgb c, uint8_t phase);

// Paint a scoreboard grid (row-major y*8+x; 0 = off, else team index whose
// colour comes from the begin() colour map). Channels are scaled num/den so
// dormant boards can render dim (e.g. 1/4) and gameover full (1/1).
void scoreboard(const uint8_t grid[64], uint8_t num, uint8_t den);
```

Implementation (matrix-kind only; both no-op gracefully otherwise):

```cpp
void spinFrame(Board::Rgb c, uint8_t phase) {
  if (kind != Board::HitDisplayKind::Ws2812Matrix || mw != 8 || mh != 8) {
    solid(c); // RGB-LED fallback: steady colour
    return;
  }
  // Clockwise perimeter walk: top row L->R, right col T->B, bottom row R->L,
  // left col B->T = 28 cells.
  static const uint8_t px[28] = {0, 1, 2, 3, 4, 5, 6, 7, 7, 7, 7, 7, 7, 7,
                                 7, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0};
  static const uint8_t py[28] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6,
                                 7, 7, 7, 7, 7, 7, 7, 7, 6, 5, 4, 3, 2, 1};
  fill_solid(leds, numLeds, CRGB::Black);
  for (uint8_t i = 0; i < 4; i++) {
    const uint8_t p = (uint8_t)((phase + i) % 28);
    leds[xy(px[p], py[p])] = toCrgb(c);
  }
  FastLED.show();
}

void scoreboard(const uint8_t grid[64], uint8_t num, uint8_t den) {
  if (kind != Board::HitDisplayKind::Ws2812Matrix || mw != 8 || mh != 8 ||
      den == 0) {
    return;
  }
  fill_solid(leds, numLeds, CRGB::Black);
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      const uint8_t team = grid[y * 8 + x];
      if (team == 0) continue;
      Board::Rgb c = teamRgb(team);
      leds[xy(x, y)] = CRGB((uint8_t)((uint16_t)c.r * num / den),
                            (uint8_t)((uint16_t)c.g * num / den),
                            (uint8_t)((uint16_t)c.b * num / den));
    }
  }
  FastLED.show();
}
```

- [ ] **Step 2: matrix_main — id filter + new state scaffolding.** Extend the `Vis` enum: `enum class Vis { Rainbow, Flash, Dark, Dead, Countdown, GameOverScore, GameOverFlood, ChaseDormant, ChaseActive };` (ChaseDormant/ChaseActive are wired in Task 4 but declared now so this task compiles the full enum once). Add globals after `identifyUntilMs`:

```cpp
// --- Chase / cue runtime state (CTL v2.1; never persisted) ------------------
bool chaseOn = false;          // between `chase on` and `chase off`/stop/gameover
bool chasePenaltyFb = false;   // penalty feedback (red blink + tone) enabled
bool chaseDisplayScore = true; // dormant boards: dim scoreboard vs dark
int chaseScores[4] = {0, 0, 0, 0}; // last host-pushed team scores (1..4)
bool haveScores = false;
uint32_t chaseWindowEndMs = 0; // active self-timeout deadline (0 = none)
uint8_t spinPhase = 0;
uint32_t lastAnimMs = 0;       // spin/scoreboard frame pacing
uint32_t penaltyBlinkUntilMs = 0;
uint32_t cueUntilMs = 0;       // countdown end / gameover phase end
int cueCount = 0;              // countdown seconds remaining tracker
int gameoverWinner = 0;
```

In `onLine`, gate addressed CTLs immediately after parse:

```cpp
  cp::Control ctl;
  if (cp::parseControl(line, ctl)) {
    // v2.1 addressing: an id-addressed CTL is for one device only.
    if (ctl.hasId && strcmp(ctl.id, config.deviceId) != 0) {
      return;
    }
    handleControl(ctl);
    return;
  }
```

- [ ] **Step 3: handleControl new cases** (Countdown/GameOver/Score here; Activate/Deactivate/ChaseOn/ChaseOff get placeholder no-op cases so the switch stays exhaustive — Task 4 fills them):

```cpp
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
      vis = Vis::GameOverScore; // 5 s scoreboard hold, then flood
      cueUntilMs = millis() + 5000;
    } else {
      vis = Vis::GameOverFlood;
      cueUntilMs = millis() + 3000;
    }
    break;
  case cp::ControlKind::Score:
    for (int i = 0; i < 4; i++) chaseScores[i] = c.scores[i];
    haveScores = true;
    break;
```

Also extend `Stop` handling: add `chaseOn = false; haveScores = false;` before its existing body.

- [ ] **Step 4: loop() rendering for the new states** — add to the `switch (vis)`:

```cpp
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
    if (on) HitDisplay::solid({64, 64, 64}); else HitDisplay::dark();
    break;
  }
  case Vis::GameOverScore: {
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
  case Vis::ChaseDormant:
  case Vis::ChaseActive:
    break; // Task 4
```

- [ ] **Step 5: Build** `pio run -e esp32-s3-matrix` → SUCCESS (retry once on a Defender "Access is denied" link failure). Run `pio test -e native` → still green (no regressions).
- [ ] **Step 6: Commit** `feat(fw): CTL id= filter + countdown/gameover cues + scoreboard/spin painters`.

---

### Task 4: Firmware — chase dormancy state machine

**Files:**
- Modify: `src/matrix_main.cpp`

**Interfaces:**
- Consumes: everything Task 3 declared. Produces the on-device behaviour the host's ChaseMode (Task 6) drives: `EVT state s=active|dormant|timeout`, `EVT hit … dormant=1`, self-timeout.

- [ ] **Step 1: handleControl — fill the four chase cases:**

```cpp
  case cp::ControlKind::ChaseOn:
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
    chaseWindowEndMs = c.hasT ? millis() + c.t : 0;
    emitState("active", -1);
    break;
  case cp::ControlKind::Deactivate:
    chaseWindowEndMs = 0;
    vis = chaseOn ? Vis::ChaseDormant : Vis::Rainbow;
    emitState("dormant", -1);
    break;
```

- [ ] **Step 2: hit routing.** In `loop()`, replace the single decode-action line `if (ok && vis == Vis::Rainbow) { applyHit(...); }` with:

```cpp
    if (ok) {
      if (vis == Vis::Rainbow) {
        applyHit(cp::tagEventFromVatosShot(shot.team, shot.damage));
      } else if (vis == Vis::ChaseActive) {
        // Chase success: score is host-side; hp untouched. Team flash + siren,
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
        // Wrong target: report (host may penalize); local feedback only when
        // the penalty is on.
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
```

- [ ] **Step 3: loop() rendering — fill the two chase states:**

```cpp
  case Vis::ChaseDormant: {
    if (penaltyBlinkUntilMs != 0) {
      if (now < penaltyBlinkUntilMs) {
        HitDisplay::solid({48, 0, 0}); // dim red penalty blink
        break;
      }
      penaltyBlinkUntilMs = 0;
    }
    if (now - lastAnimMs >= 250) {
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
      Board::Rgb c{255, 165, 0};
      Board::parseHexColour(config.chaseColour, c);
      HitDisplay::spinFrame(c, spinPhase);
      spinPhase = (uint8_t)((spinPhase + 1) % 28);
    }
    break;
  }
```

- [ ] **Step 4: standalone scoreboard mode.** At the TOP of the `switch (vis)` — before it — add:

```cpp
  // A board in REST mode=scoreboard is a dedicated wall display: paint the
  // latest scores and skip the game visual machine entirely (it also ignores
  // activate — see handleControl — and its hits still count nothing because
  // vis never leaves this path).
  if (strcmp(activeMode, "scoreboard") == 0) {
    if (now - lastAnimMs >= 250) {
      lastAnimMs = now;
      uint8_t grid[64];
      cp::scoreGrid(chaseScores, config.enabledTeams, config.enabledTeamsCount,
                    grid);
      HitDisplay::scoreboard(grid, 1, 1);
    }
    return; // end of loop() work for scoreboard boards
  }
```

(Place after the IR poll so debug/activity LED still work; hit decode in scoreboard mode falls into the `vis == Vis::Rainbow` branch only if vis is Rainbow — guard the hit routing with `strcmp(activeMode, "scoreboard") != 0 &&` at the front of `if (ok …)`.)

- [ ] **Step 5: NVS chaseColour.** In `saveConfig()` add `nvs.putString("chaseCol", config.chaseColour);` (key ≤15 chars); in `loadConfig()`:

```cpp
  String chc = nvs.getString("chaseCol", "#FFA500");
  strncpy(config.chaseColour, chc.c_str(), sizeof(config.chaseColour) - 1);
  config.chaseColour[sizeof(config.chaseColour) - 1] = '\0';
```

- [ ] **Step 6: Build + native suite** — `pio run -e esp32-s3-matrix` SUCCESS; `pio test -e native` green.
- [ ] **Step 7: Commit** `feat(fw): chase dormancy state machine — activate window, dormant hits, penalty blink, scoreboard mode`.

---

### Task 5: Host client — Control kinds + HitEvent.Dormant

**Files:**
- Modify: `dotnet/LaserTag.Client/Models/UdpMessages.cs`
- Modify: `dotnet/LaserTag.Client/UdpMessageParser.cs`
- Test: `dotnet/LaserTag.Client.Tests/UdpMessageParserTests.cs` (existing file; add tests)

**Interfaces:**
- Produces (Tasks 6/7 use): `ControlKind.ChaseOn/ChaseOff/Score`; `Control` gains `int? T`, `int? Penalty`, `string? Display`, `IReadOnlyDictionary<int, int>? Scores`; `HitEvent` gains `bool Dormant` (init-only, default false). `FormatControl`/`ParseControl` round-trip all of it; hit parsing sets Dormant on `dormant=1`.

- [ ] **Step 1: Failing tests** (xUnit, match the file's existing style):

```csharp
[Theory]
[InlineData("CTL activate t=3200 id=eb20f8")]
[InlineData("CTL chase on penalty=1 display=dark")]
[InlineData("CTL chase off")]
[InlineData("CTL score 1=4 2=0 3=12 4=7")]
public void Control_RoundTrips_V21_Verbs(string wire)
{
    var parser = new UdpMessageParser();
    Control? parsed = parser.ParseControl(wire);
    Assert.NotNull(parsed);
    Assert.Equal(wire, parser.FormatControl(parsed!));
}

[Fact]
public void FormatControl_ChaseOn_EmitsPenaltyAndDisplay()
{
    var parser = new UdpMessageParser();
    string wire = parser.FormatControl(new Control
    {
        Kind = ControlKind.ChaseOn, Penalty = 0, Display = "score",
    });
    Assert.Equal("CTL chase on penalty=0 display=score", wire);
}

[Fact]
public void FormatControl_Score_OrdersTeamsAndPutsIdLast()
{
    var parser = new UdpMessageParser();
    string wire = parser.FormatControl(new Control
    {
        Kind = ControlKind.Score,
        Scores = new Dictionary<int, int> { [2] = 9, [1] = 4 },
        Id = "eb20f8",
    });
    Assert.Equal("CTL score 1=4 2=9 3=0 4=0 id=eb20f8", wire);
}

[Fact]
public void ParseHit_ReadsDormantFlag()
{
    var parser = new UdpMessageParser();
    var msg = parser.Parse(
        "lasertag-matrix3 EVT hit victim=eb20f8 shooterTeam=3 dmg=2 proto=vatos hp=32 ts=1234 dormant=1",
        "192.168.1.225");
    HitEvent hit = Assert.IsType<HitEvent>(msg);
    Assert.True(hit.Dormant);
}
```

(Adapt the `Parse` entry-point name/signature to the existing hit-parsing tests in the file — mirror however they invoke hit parsing.)

- [ ] **Step 2:** `dotnet test dotnet/LaserTag.sln --filter UdpMessageParser` → compile failure (new members missing).
- [ ] **Step 3: Implement.**
  - `UdpMessages.cs`: add enum members `ChaseOn`, `ChaseOff`, `Score` (XML-doc each, e.g. `/// <summary> <c>CTL chase on penalty= display=</c> — enter chase match mode.</summary>`); add to `Control`: `public int? T { get; init; }`, `public int? Penalty { get; init; }`, `public string? Display { get; init; }`, `public IReadOnlyDictionary<int, int>? Scores { get; init; }`; add to `HitEvent`: `public bool Dormant { get; init; }`.
  - `FormatControl`: `Activate` case appends `" t=" + control.T` when `T is { }`; new cases:

```csharp
            case ControlKind.ChaseOn:
                sb.Append("chase on");
                sb.Append(" penalty=").Append((control.Penalty ?? 0).ToString(CultureInfo.InvariantCulture));
                sb.Append(" display=").Append(control.Display ?? "score");
                break;

            case ControlKind.ChaseOff:
                sb.Append("chase off");
                break;

            case ControlKind.Score:
                sb.Append("score");
                for (int team = 1; team <= 4; team++)
                {
                    int pts = control.Scores?.GetValueOrDefault(team) ?? 0;
                    sb.Append(' ').Append(team).Append('=')
                      .Append(pts.ToString(CultureInfo.InvariantCulture));
                }

                break;
```

  - `ParseControl`: verb table gains `"chase"` (peek `tokens[2]` for `on`/`off`; `on` reads `penalty`/`display` fields, `off` takes none — unknown subtype → null; note fields then start at token 3) and `"score"` (reads keys `"1".."4"` into a dictionary); `activate` reads optional `t`. Follow the existing `TryGetInt` error pattern (bad int → null).
  - Hit parsing: where `HitEvent` is constructed, set `Dormant = fields.TryGetValue("dormant", out string? d) && d == "1"`.
- [ ] **Step 4:** `dotnet test dotnet/LaserTag.sln` → green.
- [ ] **Step 5: Commit** `feat(host): CTL v2.1 client — chase/score verbs, activate window, dormant hits`.

---

### Task 6: Host game — ChaseMode

**Files:**
- Create: `dotnet/LaserTag.Game/ChaseMode.cs`
- Modify: `dotnet/LaserTag.Game/Participant.cs` (add `public string Mode { get; init; } = "";`) and `dotnet/LaserTag.Game/MatchEngine.cs` (StartMatch + OnHeartbeat copy `hb.Mode` into the participant record: `Mode = hb.Mode`)
- Test: `dotnet/LaserTag.Game.Tests/ChaseModeTests.cs`

**Interfaces:**
- Consumes: Task 5 `Control` members; existing `MatchContext`/`IGameMode`/`MatchResult`.
- Produces: `ChaseMode(TimeSpan? duration, int? firstTo, TimeSpan? minWindow = null, TimeSpan? maxWindow = null, TimeSpan? gap = null, int penalty = 0, string display = "score", Random? rng = null)`; `Name == "chase"`.

- [ ] **Step 1: Failing tests** — the file uses the existing Game.Tests helpers (fake clock + recording sender pattern used by DeathmatchModeTests; reuse those helpers/builders). Core cases (write them all):

```csharp
public class ChaseModeTests
{
    // Helper: build a MatchContext around a mutable clock + captured sends +
    // live scores dict, participants p1..pN online — copy the construction
    // pattern from the existing mode tests in this project.

    [Fact]
    public void OnMatchStart_SendsChaseOn_ThenFirstActivateAfterGap()
    {
        // gap=1s: OnMatchStart at T0 sends ChaseOn (penalty/display echoed);
        // OnTick at T0+0.5s sends nothing; OnTick at T0+1s sends exactly one
        // Activate with a T between min and max (2000..5000 ms default) and an
        // Id belonging to a participant.
    }

    [Fact]
    public void Hit_OnActiveTarget_ScoresShooterTeam_AndSchedulesGap()
    {
        // After an Activate for device X: OnHit(victim=X, shooterTeam=3)
        // adds +1 to team 3, and the next Activate comes only after gap.
    }

    [Fact]
    public void Hit_OnOtherDevice_WhileActive_DoesNotScore()
    {
        // OnHit(victim=Y != X, no dormant flag — e.g. a stale/racing EVT)
        // changes nothing: no score, X stays the active target.
    }

    [Fact]
    public void DormantHit_WithPenalty_DeductsFlooredAtZero()
    {
        // penalty=1, team 3 score 0: dormant hit leaves 0 (no negative).
        // Give team 3 two points, dormant hit -> 1.
    }

    [Fact]
    public void DormantHit_WithoutPenalty_Ignored() { }

    [Fact]
    public void Timeout_State_AdvancesWithoutScoring()
    {
        // EVT state s=timeout from the active device -> no score, gap, next
        // activate targets someone (with 3+ boards, NOT the same device).
    }

    [Fact]
    public void SlackExpiry_WithoutTimeoutEvt_DeactivatesAndAdvances()
    {
        // No hit/timeout arrives; advancing the clock past window+1.5s makes
        // OnTick send a Deactivate id=X and then (after gap) a new Activate.
    }

    [Fact]
    public void ActiveTarget_GoingOffline_AdvancesImmediately() { }

    [Fact]
    public void TwoBoards_AllowImmediateRepeat_ThreeBoardsNever()
    {
        // Seeded Random: with 2 participants the same id may repeat; with 3,
        // run 20 rounds and assert no consecutive repeat.
    }

    [Fact]
    public void FirstTo_EndsWithWinner_DurationEndsWithLeader_TieIsDraw() { }

    [Fact]
    public void ScoreboardModeParticipant_IsNeverActivated()
    {
        // One participant with Mode == "scoreboard" among 3: 20 rounds, it is
        // never the Activate target.
    }

    [Fact]
    public void Constructor_RequiresDurationOrFirstTo()
    {
        Assert.Throws<ArgumentException>(() => new ChaseMode(null, null));
    }
}
```

Fill in each body concretely against the helper; every test drives the mode ONLY through `IGameMode` calls + context, never internals.

- [ ] **Step 2:** `dotnet test --filter ChaseMode` → fails (type missing).
- [ ] **Step 3: Implement `ChaseMode.cs`:**

```csharp
using LaserTag.Client.Models;

namespace LaserTag.Game;

/// <summary>
/// "Chase the target": one board at a time is activated with a randomized
/// self-timeout window; hitting it scores +1 for the shooter's team, an
/// optional penalty deducts for shooting a dormant board (floored at zero).
/// Ends on a fixed duration and/or a first-to-N score, whichever trips first.
/// The device enforces the window (self-timeout); this mode keeps a slack
/// fallback so a lost timeout EVT can never stall the match.
/// </summary>
public sealed class ChaseMode : IGameMode
{
    private static readonly TimeSpan Slack = TimeSpan.FromMilliseconds(1500);

    private readonly int? _firstTo;
    private readonly TimeSpan _minWindow;
    private readonly TimeSpan _maxWindow;
    private readonly TimeSpan _gap;
    private readonly int _penalty;
    private readonly string _display;
    private readonly Random _rng;

    private string? _activeId;
    private string? _previousId;
    private bool _inGap;
    private DateTimeOffset _phaseAt; // gap end, or active window + slack end

    /// <summary>Initializes the mode.</summary>
    /// <param name="duration">Fixed match length, or null for unlimited.</param>
    /// <param name="firstTo">Score that ends the match, or null for none.</param>
    /// <param name="minWindow">Minimum active window. Defaults to 2 s.</param>
    /// <param name="maxWindow">Maximum active window. Defaults to 5 s.</param>
    /// <param name="gap">Dark gap between rounds. Defaults to 1 s.</param>
    /// <param name="penalty">Points deducted for a dormant hit. 0 disables.</param>
    /// <param name="display">Dormant display: "score" or "dark".</param>
    /// <param name="rng">Injectable randomness (tests pass a seeded one).</param>
    /// <exception cref="ArgumentException">
    /// Thrown when neither <paramref name="duration"/> nor
    /// <paramref name="firstTo"/> is provided.
    /// </exception>
    public ChaseMode(
        TimeSpan? duration,
        int? firstTo,
        TimeSpan? minWindow = null,
        TimeSpan? maxWindow = null,
        TimeSpan? gap = null,
        int penalty = 0,
        string display = "score",
        Random? rng = null)
    {
        if (duration is null && firstTo is null)
        {
            throw new ArgumentException("chase needs a duration and/or a first-to target");
        }

        MatchDuration = duration;
        _firstTo = firstTo;
        _minWindow = minWindow ?? TimeSpan.FromSeconds(2);
        _maxWindow = maxWindow ?? TimeSpan.FromSeconds(5);
        _gap = gap ?? TimeSpan.FromSeconds(1);
        _penalty = penalty;
        _display = display;
        _rng = rng ?? new Random();
    }

    /// <inheritdoc/>
    public string Name => "chase";

    /// <inheritdoc/>
    public TimeSpan? MatchDuration { get; }

    /// <inheritdoc/>
    public void OnMatchStart(MatchContext context)
    {
        context.Send(new Control
        {
            Kind = ControlKind.ChaseOn,
            Penalty = _penalty > 0 ? 1 : 0,
            Display = _display,
        });
        _activeId = null;
        _previousId = null;
        _inGap = true;
        _phaseAt = context.Now + _gap;
    }

    /// <inheritdoc/>
    public void OnHit(MatchContext context, HitEvent hit)
    {
        if (hit.Dormant)
        {
            if (_penalty > 0)
            {
                int deduct = Math.Min(_penalty, context.Scores.GetValueOrDefault(hit.ShooterTeam));
                if (deduct > 0)
                {
                    context.AddScore(hit.ShooterTeam, -deduct);
                }
            }

            return;
        }

        if (_activeId is null || hit.Victim != _activeId)
        {
            return; // stale or non-chase hit
        }

        context.AddScore(hit.ShooterTeam, 1);
        EndRound(context);
    }

    /// <inheritdoc/>
    public void OnDeviceState(MatchContext context, StateEvent state, Participant participant)
    {
        if (state.S == "timeout" && participant.Id == _activeId)
        {
            EndRound(context);
        }
    }

    /// <inheritdoc/>
    public void OnTick(MatchContext context)
    {
        if (_activeId is { } id)
        {
            Participant? active = context.Participants.FirstOrDefault(p => p.Id == id);
            if (active is null || !active.Online || context.Now >= _phaseAt)
            {
                // Slack expiry (lost EVT) or the target vanished: defensively
                // deactivate on the wire and move on unscored.
                context.Send(new Control { Kind = ControlKind.Deactivate, Id = id });
                EndRound(context);
            }

            return;
        }

        if (_inGap && context.Now >= _phaseAt)
        {
            ActivateNext(context);
        }
    }

    /// <inheritdoc/>
    public MatchResult? CheckEnd(MatchContext context)
    {
        if (_firstTo is { } target && context.Scores.Count > 0 &&
            context.Scores.Values.Max() >= target)
        {
            return Leader(context);
        }

        if (MatchDuration is { } d && context.Now - context.MatchStartedAt >= d)
        {
            return Leader(context);
        }

        return null;
    }

    private static MatchResult Leader(MatchContext context)
    {
        if (context.Scores.Count == 0)
        {
            return new MatchResult(0);
        }

        int best = context.Scores.Values.Max();
        List<int> leaders = context.Scores.Where(kv => kv.Value == best).Select(kv => kv.Key).ToList();
        return new MatchResult(leaders.Count == 1 ? leaders[0] : 0);
    }

    private void EndRound(MatchContext context)
    {
        _previousId = _activeId;
        _activeId = null;
        _inGap = true;
        _phaseAt = context.Now + _gap;
    }

    private void ActivateNext(MatchContext context)
    {
        List<Participant> pool = context.Participants
            .Where(p => p.Online && p.Mode != "scoreboard")
            .ToList();
        if (pool.Count >= 3 && _previousId is { } prev)
        {
            pool.RemoveAll(p => p.Id == prev);
        }

        if (pool.Count == 0)
        {
            _phaseAt = context.Now + _gap; // nobody available; retry next gap
            return;
        }

        Participant target = pool[_rng.Next(pool.Count)];
        double windowMs = _minWindow.TotalMilliseconds +
            (_rng.NextDouble() * (_maxWindow - _minWindow).TotalMilliseconds);
        _activeId = target.Id;
        _inGap = false;
        _phaseAt = context.Now + TimeSpan.FromMilliseconds(windowMs) + Slack;
        context.Send(new Control
        {
            Kind = ControlKind.Activate,
            Id = target.Id,
            T = (int)windowMs,
        });
    }
}
```

Participant/engine `Mode` plumbing: `Participant` gains `Mode` (default `""`); `MatchEngine.StartMatch` sets `Mode = hb.Mode` when enrolling; `OnHeartbeat`'s reconciliation `with { … }` adds `Mode = hb.Mode`. (Heartbeat already carries `Mode` — check `Heartbeat` model; it parses `mode=` today.)

- [ ] **Step 4:** `dotnet test dotnet/LaserTag.sln` → green.
- [ ] **Step 5: Commit** `feat(host): ChaseMode — windowed activation, penalty floor, slack fallback`.

---

### Task 7: Host wiring — REPL `start chase`, score pusher, docs, full verification

**Files:**
- Modify: `dotnet/LaserTag.Host/ConsoleUiService.cs`, `dotnet/LaserTag.Host/GameService.cs`
- Modify: `dotnet/openapi/lasertag.yaml` (ConfigDoc + ConfigPatch gain `chaseColour`; document `/api/mode` `"scoreboard"` as a known mode value in its description)
- Modify: `README.md` game-manager section (chase usage + the note that the firmware `id=` caveat is FIXED as of this change) and `docs/superpowers/specs/2026-07-12-game-manager-design.md` post-impl notes (strike the id= caveat, pointing at this spec)
- Test: existing suites + build

- [ ] **Step 1: REPL.** Extend the `StartMatch` dispatcher with a `chase` branch:

```csharp
        else if (kind == "chase")
        {
            TimeSpan? duration = DurationParser.TryParse(args.ElementAtOrDefault(2), out TimeSpan d) ? d : null;
            int? firstTo = IntOption(args, "--first");
            if (duration is null && firstTo is null)
            {
                AnsiConsole.MarkupLine("[yellow]usage: start chase <dur and/or --first N> [[--min d]] [[--max d]] [[--gap d]] [[--penalty N]] [[--dark]][/]");
                return;
            }

            mode = new ChaseMode(
                duration,
                firstTo,
                DurationOption(args, "--min"),
                DurationOption(args, "--max"),
                DurationOption(args, "--gap"),
                IntOption(args, "--penalty") ?? 0,
                args.Contains("--dark") ? "dark" : "score");
        }
```

Update the REPL help line to include `start chase <dur|--first N> [--min] [--max] [--gap] [--penalty N] [--dark]`.

- [ ] **Step 2: Score pusher in `GameService.Tick()`.** Add fields:

```csharp
    private readonly Dictionary<int, int> _pushedScores = [];
    private DateTimeOffset _lastScorePushAt = DateTimeOffset.MinValue;
    private bool _finalScoresPushed;
```

Inside `Tick()` capture `MatchSnapshot snap = _engine.Snapshot();` under the lock (alongside the existing winner capture), then AFTER the lock:

```csharp
        // Score display push (spec §2.1): on change + a 1 s refresh while a
        // match is live, plus one final push at Finished so gameover boards
        // hold the true final board. hp is never pushed — scores only.
        bool live = snap.Phase is MatchPhase.Running or MatchPhase.Countdown;
        bool changed = snap.TeamScores.Count != _pushedScores.Count ||
            snap.TeamScores.Any(kv => _pushedScores.GetValueOrDefault(kv.Key) != kv.Value);
        DateTimeOffset now = DateTimeOffset.UtcNow;
        if ((live && (changed || now - _lastScorePushAt >= TimeSpan.FromSeconds(1))) ||
            (snap.Phase == MatchPhase.Finished && changed && !_finalScoresPushed))
        {
            _pushedScores.Clear();
            foreach ((int team, int pts) in snap.TeamScores)
            {
                _pushedScores[team] = pts;
            }

            _lastScorePushAt = now;
            _finalScoresPushed = snap.Phase == MatchPhase.Finished;
            _ = _sender.SendAsync(new Control { Kind = ControlKind.Score, Scores = _pushedScores });
        }

        if (live)
        {
            _finalScoresPushed = false;
        }
```

CAUTION: `Scores = _pushedScores` hands the sender a mutable reference — pass a copy instead: `Scores = new Dictionary<int, int>(_pushedScores)`.

- [ ] **Step 3: Docs.** openapi ConfigDoc/ConfigPatch: `chaseColour` (string, `^#[0-9A-Fa-f]{6}$`, default `#FFA500`, description "Chase-mode active spin colour."). README: add `start chase` usage + scoreboard-mode note; update the multi-device warning (id= filter now enforced by firmware ≥ this build — old firmware still applies addressed CTLs to everyone, so reflash all boards before multi-device matches).
- [ ] **Step 4: Full verification.** `dotnet test dotnet/LaserTag.sln` green; `pio test -e native` green; `pio run -e esp32-s3-matrix` SUCCESS.
- [ ] **Step 5: Commit** `feat(host): start chase + score pusher; docs for CTL v2.1`.

---

## Bench verification (after all tasks; two powered boards)

1. OTA both: `pio run -e esp32-s3-matrix-ota -t upload` (matrix1 .34 if powered) and `--upload-port 192.168.1.225` / `192.168.1.218` (matrix3/matrix4). espota needs the unsandboxed direct call with `-I <host-ip>` if the pio wrapper stalls.
2. `dotnet run --project dotnet/LaserTag.Host` → `devices` shows both; `start chase 2m --first 10 --penalty 1`.
3. Observe: countdown EVTs, `chase on`, alternating `activate id=` lines, `EVT state timeout` after 2–5 s unhit windows, score pushes; `fire` from one board at the other (`POST /api/command {"cmd":"fire","team":3,"damage":1}`) to land a real chase hit and watch `+1`.
4. `stop` → gameover + boards return to rainbow.
