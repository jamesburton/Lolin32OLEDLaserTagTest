# Board Capability Config + HAL Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduce a compile-time `BoardProfile` (+ whitelisted NVS overrides) and HAL libraries (`HitDisplay`, `IrTx`, `Sound`) so firmware targets capabilities, not pins — routing existing behaviour through it with no new game logic.

**Architecture:** A pure (Arduino-free, native-testable) `lib/Board` holds the profile struct, the two board profiles selected by `-D BOARD_*`, override validation, and colour parsing. Arduino-only HAL libs implement the outputs and no-op when their peripheral is absent. The two `src/` firmwares are refactored to drive outputs through the HAL.

**Tech Stack:** C++17, PlatformIO (espressif32/arduino), FastLED, Unity (native tests), ArduinoJson (already present).

---

## File structure

```
lib/Board/BoardProfile.h     (pure) struct + enums + active() + applyOverride() + parseHexColour()
lib/Board/BoardProfile.cpp   (pure) the two profiles, override/colour impls
lib/HitDisplay/HitDisplay.h  (Arduino) hit-feedback interface
lib/HitDisplay/HitDisplay.cpp(Arduino) WS2812-matrix + 3-pin-RGB impls
lib/IrTx/IrTx.h              (Arduino) IR transmit interface
lib/IrTx/IrTx.cpp            (Arduino) LEDC carrier + Vatos encode (extracted from main.cpp)
lib/Sound/Sound.h            (Arduino) sound-cue interface
lib/Sound/Sound.cpp          (Arduino) piezo impl + DAC stub
lib/BoardNvs/BoardNvs.h      (Arduino) NVS override load + `cfg` serial command
lib/BoardNvs/BoardNvs.cpp    (Arduino)
test/test_board/test_board.cpp  (native) BoardProfile / applyOverride / parseHexColour tests
src/matrix_main.cpp          refactor onto HitDisplay (behaviour identical)
src/main.cpp                 refactor: IrTx for TX, HitDisplay(matrix) idle, keep OLED
src/matrix_test.cpp          DELETE (superseded)
platformio.ini               -D BOARD_* flags, lib deps; remove lolin32_matrixtest env
```

`lib/Board` is the only HAL piece kept out of `lib_ignore` for the native env (it is pure). `HitDisplay`, `IrTx`, `Sound`, `BoardNvs` are Arduino-only and added to the native env's `lib_ignore`.

---

## Task 1: `BoardProfile` struct + the two profiles (pure)

**Files:**
- Create: `lib/Board/BoardProfile.h`
- Create: `lib/Board/BoardProfile.cpp`
- Create: `test/test_board/test_board.cpp`
- Modify: `platformio.ini` (native env `lib_ignore`; `-D BOARD_*` — done fully in Task 7, but the native test needs a default profile, so define one here)

- [ ] **Step 1: Write the header**

`lib/Board/BoardProfile.h`:
```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace Board {

enum class HitDisplayKind : uint8_t { None, Ws2812Matrix, RgbLed };
enum class ScreenKind : uint8_t { None, Ssd1306, Sh1106 };
enum class AudioKind : uint8_t { None, Piezo, I2sDac };
enum class ColourOrder : uint8_t { Grb, Rgb };

/// A simple 8-bit RGB triple (no FastLED dependency, so this stays pure).
struct Rgb {
  uint8_t r, g, b;
};

/// Compile-time description of a board's hardware (contract: HAL spec §3).
struct BoardProfile {
  const char *name;

  int8_t irRxPin;
  int8_t irTxPin; // -1 = absent

  HitDisplayKind hitDisplay;
  int8_t matrixPin;
  uint8_t matrixW, matrixH;
  ColourOrder matrixOrder;
  int8_t rgbR, rgbG, rgbB;
  bool rgbCommonAnode;

  ScreenKind screen;
  uint8_t screenW, screenH;
  int8_t sdaPin, sclPin;
  uint8_t i2cAddr;

  AudioKind audio;
  int8_t piezoPin;

  bool hasSdCard;
  int8_t activityLedPin;
};

/// Returns the compiled-in profile selected by the -D BOARD_* build flag.
const BoardProfile &active();

/// Applies one whitelisted runtime override onto a profile copy. Returns false
/// (and leaves the profile unchanged) for an unknown key or an out-of-range
/// value (safe fallback). Whitelisted keys: matrixPin, matrixW, matrixH,
/// matrixOrder (0=Grb,1=Rgb), rgbR, rgbG, rgbB, activityLedPin.
bool applyOverride(BoardProfile &p, const char *key, long value);

/// Parses "#RRGGBB" (or "RRGGBB") into an Rgb. Returns false on malformed input.
bool parseHexColour(const char *hex, Rgb &out);

} // namespace Board
```

- [ ] **Step 2: Write the failing test**

`test/test_board/test_board.cpp`:
```cpp
#include <BoardProfile.h>
#include <unity.h>

using namespace Board;

void test_active_profile_has_name() {
  const BoardProfile &p = active();
  TEST_ASSERT_NOT_NULL(p.name);
  TEST_ASSERT_TRUE(p.irRxPin >= 0); // every board has an IR receiver
}

void test_parse_hex_colour_with_hash() {
  Rgb c;
  TEST_ASSERT_TRUE(parseHexColour("#0000FF", c));
  TEST_ASSERT_EQUAL_UINT8(0x00, c.r);
  TEST_ASSERT_EQUAL_UINT8(0x00, c.g);
  TEST_ASSERT_EQUAL_UINT8(0xFF, c.b);
}

void test_parse_hex_colour_without_hash() {
  Rgb c;
  TEST_ASSERT_TRUE(parseHexColour("FF8800", c));
  TEST_ASSERT_EQUAL_UINT8(0xFF, c.r);
  TEST_ASSERT_EQUAL_UINT8(0x88, c.g);
  TEST_ASSERT_EQUAL_UINT8(0x00, c.b);
}

void test_parse_hex_colour_rejects_malformed() {
  Rgb c;
  TEST_ASSERT_FALSE(parseHexColour("#12", c));
  TEST_ASSERT_FALSE(parseHexColour("ZZZZZZ", c));
  TEST_ASSERT_FALSE(parseHexColour("", c));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_active_profile_has_name);
  RUN_TEST(test_parse_hex_colour_with_hash);
  RUN_TEST(test_parse_hex_colour_without_hash);
  RUN_TEST(test_parse_hex_colour_rejects_malformed);
  return UNITY_END();
}
```

- [ ] **Step 3: Run it to verify it fails**

Run: `pio test -e native -f test_board`
Expected: FAIL — `BoardProfile.h` / symbols not found (link error).

- [ ] **Step 4: Write the implementation**

`lib/Board/BoardProfile.cpp`:
```cpp
#include "BoardProfile.h"
#include <string.h>

namespace Board {

// --- Board profiles (HAL spec §5) -------------------------------------------

static const BoardProfile kLolin32 = {
    /*name*/ "lolin32",
    /*irRxPin*/ 25, /*irTxPin*/ 13,
    /*hitDisplay*/ HitDisplayKind::Ws2812Matrix,
    /*matrixPin*/ 14, /*matrixW*/ 8, /*matrixH*/ 8, /*matrixOrder*/ ColourOrder::Grb,
    /*rgbR*/ -1, /*rgbG*/ -1, /*rgbB*/ -1, /*rgbCommonAnode*/ false,
    /*screen*/ ScreenKind::Ssd1306, /*screenW*/ 128, /*screenH*/ 32,
    /*sdaPin*/ 5, /*sclPin*/ 4, /*i2cAddr*/ 0x3C,
    /*audio*/ AudioKind::None, /*piezoPin*/ -1,
    /*hasSdCard*/ false, /*activityLedPin*/ 26,
};

static const BoardProfile kS3Matrix = {
    /*name*/ "s3-matrix",
    /*irRxPin*/ 1, /*irTxPin*/ -1,
    /*hitDisplay*/ HitDisplayKind::Ws2812Matrix,
    /*matrixPin*/ 14, /*matrixW*/ 8, /*matrixH*/ 8, /*matrixOrder*/ ColourOrder::Rgb,
    /*rgbR*/ -1, /*rgbG*/ -1, /*rgbB*/ -1, /*rgbCommonAnode*/ false,
    /*screen*/ ScreenKind::None, /*screenW*/ 0, /*screenH*/ 0,
    /*sdaPin*/ -1, /*sclPin*/ -1, /*i2cAddr*/ 0,
    /*audio*/ AudioKind::None, /*piezoPin*/ -1,
    /*hasSdCard*/ false, /*activityLedPin*/ 7,
};

const BoardProfile &active() {
#if defined(BOARD_S3_MATRIX)
  return kS3Matrix;
#else // default to the Lolin32 (also used by the native test build)
  return kLolin32;
#endif
}

static bool validPin(long v) { return v >= 0 && v <= 48; }
static bool validByte(long v) { return v >= 0 && v <= 255; }

bool applyOverride(BoardProfile &p, const char *key, long value) {
  if (strcmp(key, "matrixPin") == 0 && validPin(value)) {
    p.matrixPin = (int8_t)value;
    return true;
  }
  if (strcmp(key, "matrixW") == 0 && value > 0 && value <= 64) {
    p.matrixW = (uint8_t)value;
    return true;
  }
  if (strcmp(key, "matrixH") == 0 && value > 0 && value <= 64) {
    p.matrixH = (uint8_t)value;
    return true;
  }
  if (strcmp(key, "matrixOrder") == 0 && (value == 0 || value == 1)) {
    p.matrixOrder = value == 1 ? ColourOrder::Rgb : ColourOrder::Grb;
    return true;
  }
  if (strcmp(key, "rgbR") == 0 && validPin(value)) { p.rgbR = (int8_t)value; return true; }
  if (strcmp(key, "rgbG") == 0 && validPin(value)) { p.rgbG = (int8_t)value; return true; }
  if (strcmp(key, "rgbB") == 0 && validPin(value)) { p.rgbB = (int8_t)value; return true; }
  if (strcmp(key, "activityLedPin") == 0 && validPin(value)) {
    p.activityLedPin = (int8_t)value;
    return true;
  }
  (void)validByte;
  return false; // unknown key or out-of-range value -> safe fallback
}

static bool hexNibble(char c, uint8_t &out) {
  if (c >= '0' && c <= '9') { out = c - '0'; return true; }
  if (c >= 'a' && c <= 'f') { out = c - 'a' + 10; return true; }
  if (c >= 'A' && c <= 'F') { out = c - 'A' + 10; return true; }
  return false;
}

bool parseHexColour(const char *hex, Rgb &out) {
  if (hex == nullptr) return false;
  if (hex[0] == '#') hex++;
  if (strlen(hex) != 6) return false;
  uint8_t n[6];
  for (int i = 0; i < 6; i++) {
    if (!hexNibble(hex[i], n[i])) return false;
  }
  out.r = (n[0] << 4) | n[1];
  out.g = (n[2] << 4) | n[3];
  out.b = (n[4] << 4) | n[5];
  return true;
}

} // namespace Board
```

- [ ] **Step 5: Add the native test env wiring**

In `platformio.ini`, under `[env:native]`, extend `lib_ignore` so only the pure libs compile natively:
```ini
lib_ignore =
    TagNet
    Vatos
    IrFramer
    HitDisplay
    IrTx
    Sound
    BoardNvs
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `pio test -e native -f test_board`
Expected: PASS — 4 tests.

- [ ] **Step 7: Commit**

```bash
git add lib/Board test/test_board platformio.ini
git commit -m "Add pure BoardProfile lib (profiles + colour parsing)"
```

---

## Task 2: `applyOverride` validation + safe fallback tests

**Files:**
- Modify: `test/test_board/test_board.cpp`

- [ ] **Step 1: Add the failing tests**

Append to `test/test_board/test_board.cpp` (and add `RUN_TEST` lines in `main`):
```cpp
void test_override_valid_matrix_pin() {
  BoardProfile p = active();
  TEST_ASSERT_TRUE(applyOverride(p, "matrixPin", 27));
  TEST_ASSERT_EQUAL_INT8(27, p.matrixPin);
}

void test_override_unknown_key_ignored() {
  BoardProfile p = active();
  int8_t before = p.matrixPin;
  TEST_ASSERT_FALSE(applyOverride(p, "nope", 5));
  TEST_ASSERT_EQUAL_INT8(before, p.matrixPin);
}

void test_override_out_of_range_pin_ignored() {
  BoardProfile p = active();
  int8_t before = p.matrixPin;
  TEST_ASSERT_FALSE(applyOverride(p, "matrixPin", 999));
  TEST_ASSERT_EQUAL_INT8(before, p.matrixPin); // safe fallback: unchanged
}

void test_override_matrix_order() {
  BoardProfile p = active();
  TEST_ASSERT_TRUE(applyOverride(p, "matrixOrder", 1));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ColourOrder::Rgb),
                        static_cast<int>(p.matrixOrder));
  TEST_ASSERT_FALSE(applyOverride(p, "matrixOrder", 5)); // out of range
}
```
Add to `main()`:
```cpp
  RUN_TEST(test_override_valid_matrix_pin);
  RUN_TEST(test_override_unknown_key_ignored);
  RUN_TEST(test_override_out_of_range_pin_ignored);
  RUN_TEST(test_override_matrix_order);
```

- [ ] **Step 2: Run to verify they pass** (impl already exists from Task 1)

Run: `pio test -e native -f test_board`
Expected: PASS — 8 tests.

- [ ] **Step 3: Commit**

```bash
git add test/test_board
git commit -m "Test BoardProfile override validation + safe fallback"
```

---

## Task 3: `IrTx` HAL (extract carrier + Vatos transmit from main.cpp)

**Files:**
- Create: `lib/IrTx/IrTx.h`
- Create: `lib/IrTx/IrTx.cpp`

Extracts the `CARRIER_*` macros and `vatosSend()` (currently `src/main.cpp:264-296`) and accepts a generic `TagEvent` so the control plane / modes stay protocol-agnostic.

- [ ] **Step 1: Write the header**

`lib/IrTx/IrTx.h`:
```cpp
#pragma once
#include <BoardProfile.h>
#include <ControlProto.h> // ControlProto::TagEvent

namespace IrTx {

/// Configures the IR transmit carrier from the profile's irTxPin. No-op (and
/// present() stays false) when irTxPin < 0.
void begin(const Board::BoardProfile &p);

/// Transmits the event as a Vatos shot (team + damage). No-op if absent.
void fire(const ControlProto::TagEvent &ev);

/// True if the board has an IR transmitter.
bool present();

} // namespace IrTx
```

- [ ] **Step 2: Write the implementation**

`lib/IrTx/IrTx.cpp`:
```cpp
#include "IrTx.h"
#include <Arduino.h>
#include <Vatos.h>
#include <driver/gpio.h>

namespace IrTx {
namespace {
int8_t txPin = -1;

#if ESP_ARDUINO_VERSION_MAJOR >= 3
inline void carrierBegin(int pin) { ledcAttach(pin, Vatos::CarrierHz, 8); }
inline void carrierOn(int pin) { ledcWrite(pin, 128); }
inline void carrierOff(int pin) { ledcWrite(pin, 0); }
#else
constexpr int kChannel = 0;
inline void carrierBegin(int pin) {
  ledcSetup(kChannel, Vatos::CarrierHz, 8);
  ledcAttachPin(pin, kChannel);
}
inline void carrierOn(int) { ledcWrite(kChannel, 128); }
inline void carrierOff(int) { ledcWrite(kChannel, 0); }
#endif
} // namespace

void begin(const Board::BoardProfile &p) {
  txPin = p.irTxPin;
  if (txPin < 0) {
    return;
  }
  carrierBegin(txPin);
  gpio_set_drive_capability(static_cast<gpio_num_t>(txPin), GPIO_DRIVE_CAP_0);
  carrierOff(txPin);
}

bool present() { return txPin >= 0; }

void fire(const ControlProto::TagEvent &ev) {
  if (txPin < 0) {
    return;
  }
  const Vatos::Shot shot = {static_cast<uint8_t>(ev.team > 0 ? ev.team : 1),
                            static_cast<uint8_t>(ev.damage > 0 ? ev.damage : 1)};
  bool bits[Vatos::FrameEdges];
  if (!Vatos::encode(shot, bits)) {
    return;
  }
  for (size_t i = 0; i < Vatos::FrameEdges; i++) {
    const uint32_t durUs = bits[i] ? Vatos::LongUs : Vatos::ShortUs;
    if ((i & 1) == 0) {
      carrierOn(txPin);
      delayMicroseconds(durUs);
      carrierOff(txPin);
    } else {
      delayMicroseconds(durUs);
    }
  }
  carrierOff(txPin);
}

} // namespace IrTx
```

- [ ] **Step 3: Verify it compiles for both boards**

Run: `pio run -e lolin32 && pio run -e esp32-s3-matrix`
Expected: SUCCESS (the lib compiles; not yet wired into the firmwares). If Windows Defender throws a transient `collect2: CreateProcess`/`Access is denied` at link, just re-run.

- [ ] **Step 4: Commit**

```bash
git add lib/IrTx
git commit -m "Add IrTx HAL (carrier + Vatos transmit, protocol-agnostic fire)"
```

---

## Task 4: `HitDisplay` HAL (WS2812 matrix + 3-pin RGB)

**Files:**
- Create: `lib/HitDisplay/HitDisplay.h`
- Create: `lib/HitDisplay/HitDisplay.cpp`

`flashTeam` looks up the team's `#RRGGBB` from a colour table the caller supplies (the existing `teamColours` config). Keeping the lookup caller-supplied avoids `HitDisplay` depending on `ConfigDoc`.

- [ ] **Step 1: Write the header**

`lib/HitDisplay/HitDisplay.h`:
```cpp
#pragma once
#include <BoardProfile.h>
#include <stdint.h>

namespace HitDisplay {

/// Supplies the "#RRGGBB" colour for a team index, or nullptr/"" if unknown.
typedef const char *(*TeamColourFn)(int team);

/// Initialises the configured hit display (matrix or RGB LED). No-op when the
/// profile's hitDisplay is None.
void begin(const Board::BoardProfile &p, TeamColourFn colours);

void idle();              // matrix: flowing rainbow; RGB LED: off
void flashTeam(int team); // solid-fill the team colour (one frame)
void solid(Board::Rgb c);
void dark();
void setBrightness(uint8_t b);
bool present();

} // namespace HitDisplay
```

- [ ] **Step 2: Write the implementation**

`lib/HitDisplay/HitDisplay.cpp`:
```cpp
#include "HitDisplay.h"
#include <Arduino.h>
#include <FastLED.h>

namespace HitDisplay {
namespace {
Board::HitDisplayKind kind = Board::HitDisplayKind::None;
TeamColourFn colourFn = nullptr;

// Matrix state
CRGB *leds = nullptr;
uint16_t numLeds = 0;
uint8_t rainbowHue = 0;

// RGB-LED state
int8_t rPin = -1, gPin = -1, bPin = -1;
bool commonAnode = false;
uint8_t brightness = 13;

CRGB toCrgb(Board::Rgb c) { return CRGB(c.r, c.g, c.b); }

Board::Rgb teamRgb(int team) {
  Board::Rgb c{255, 0, 255}; // magenta = unknown
  if (colourFn) {
    const char *hex = colourFn(team);
    if (hex) {
      Board::parseHexColour(hex, c);
    }
  }
  return c;
}

void rgbWrite(Board::Rgb c) {
  auto chan = [&](int8_t pin, uint8_t v) {
    if (pin < 0) return;
    uint8_t scaled = (uint16_t)v * brightness / 255;
    analogWrite(pin, commonAnode ? 255 - scaled : scaled);
  };
  chan(rPin, c.r);
  chan(gPin, c.g);
  chan(bPin, c.b);
}
} // namespace

void begin(const Board::BoardProfile &p, TeamColourFn colours) {
  kind = p.hitDisplay;
  colourFn = colours;
  if (kind == Board::HitDisplayKind::Ws2812Matrix) {
    numLeds = (uint16_t)p.matrixW * p.matrixH;
    leds = new CRGB[numLeds];
    if (p.matrixOrder == Board::ColourOrder::Rgb) {
      FastLED.addLeds<WS2812B, 14, RGB>(leds, numLeds); // pin is fixed at 14 on both boards
    } else {
      FastLED.addLeds<WS2812B, 14, GRB>(leds, numLeds);
    }
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 500);
    FastLED.setBrightness(brightness);
    dark();
  } else if (kind == Board::HitDisplayKind::RgbLed) {
    rPin = p.rgbR; gPin = p.rgbG; bPin = p.rgbB; commonAnode = p.rgbCommonAnode;
    if (rPin >= 0) pinMode(rPin, OUTPUT);
    if (gPin >= 0) pinMode(gPin, OUTPUT);
    if (bPin >= 0) pinMode(bPin, OUTPUT);
    dark();
  }
}

bool present() { return kind != Board::HitDisplayKind::None; }

void setBrightness(uint8_t b) {
  brightness = b;
  if (kind == Board::HitDisplayKind::Ws2812Matrix) {
    FastLED.setBrightness(b);
    FastLED.show();
  }
}

void idle() {
  if (kind == Board::HitDisplayKind::Ws2812Matrix) {
    fill_rainbow(leds, numLeds, rainbowHue++, 4);
    FastLED.show();
  } else if (kind == Board::HitDisplayKind::RgbLed) {
    rgbWrite({0, 0, 0});
  }
}

void solid(Board::Rgb c) {
  if (kind == Board::HitDisplayKind::Ws2812Matrix) {
    fill_solid(leds, numLeds, toCrgb(c));
    FastLED.show();
  } else if (kind == Board::HitDisplayKind::RgbLed) {
    rgbWrite(c);
  }
}

void flashTeam(int team) { solid(teamRgb(team)); }

void dark() { solid({0, 0, 0}); }

} // namespace HitDisplay
```

> Note: the `addLeds` data pin must be a compile-time constant for FastLED, so it is fixed at `14` (both boards use GPIO14). The runtime `matrixPin` override is therefore validated/stored but only takes effect if a future board changes the literal here; document this limitation in the header comment. `matrixOrder` *is* honoured at runtime via the GRB/RGB branch.

- [ ] **Step 3: Verify it compiles for both boards**

Run: `pio run -e lolin32 && pio run -e esp32-s3-matrix`
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add lib/HitDisplay
git commit -m "Add HitDisplay HAL (WS2812 matrix + 3-pin RGB, team-colour flash)"
```

---

## Task 5: `Sound` HAL (piezo impl + DAC stub)

**Files:**
- Create: `lib/Sound/Sound.h`
- Create: `lib/Sound/Sound.cpp`

- [ ] **Step 1: Write the header**

`lib/Sound/Sound.h`:
```cpp
#pragma once
#include <BoardProfile.h>

namespace Sound {

enum class Cue : uint8_t { Hit, Dead, Respawn, Start };

/// Initialises audio from the profile. Piezo is implemented; I2sDac is a stub
/// (no-op). No-op when audio is None.
void begin(const Board::BoardProfile &p);

void cue(Cue c); // no-op if no audio output is present
bool present();

} // namespace Sound
```

- [ ] **Step 2: Write the implementation**

`lib/Sound/Sound.cpp`:
```cpp
#include "Sound.h"
#include <Arduino.h>

namespace Sound {
namespace {
Board::AudioKind kind = Board::AudioKind::None;
int8_t piezo = -1;

void chirp(uint16_t freq, uint16_t ms) {
  if (piezo < 0) return;
  tone(piezo, freq, ms);
}
} // namespace

void begin(const Board::BoardProfile &p) {
  kind = p.audio;
  if (kind == Board::AudioKind::Piezo) {
    piezo = p.piezoPin;
    if (piezo >= 0) pinMode(piezo, OUTPUT);
  }
  // I2sDac: stub — left for a later spec.
}

bool present() { return kind == Board::AudioKind::Piezo && piezo >= 0; }

void cue(Cue c) {
  if (kind != Board::AudioKind::Piezo) return; // DAC stub / None -> silent
  switch (c) {
  case Cue::Hit: chirp(1200, 60); break;
  case Cue::Dead: chirp(300, 400); break;
  case Cue::Respawn: chirp(900, 120); break;
  case Cue::Start: chirp(1600, 150); break;
  }
}

} // namespace Sound
```

- [ ] **Step 3: Verify it compiles**

Run: `pio run -e esp32-s3-matrix`
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add lib/Sound
git commit -m "Add Sound HAL (piezo cues; I2S DAC stubbed)"
```

---

## Task 6: `BoardNvs` — runtime overrides + `cfg` serial command

**Files:**
- Create: `lib/BoardNvs/BoardNvs.h`
- Create: `lib/BoardNvs/BoardNvs.cpp`

- [ ] **Step 1: Write the header**

`lib/BoardNvs/BoardNvs.h`:
```cpp
#pragma once
#include <BoardProfile.h>

namespace BoardNvs {

/// Reads whitelisted overrides from NVS (namespace "board") and applies them to
/// `p` via Board::applyOverride (invalid values fall back silently).
void loadOverrides(Board::BoardProfile &p);

/// Handles a `cfg <key> <value>` line: validates via Board::applyOverride,
/// persists to NVS, prints the result on Serial. Returns true if the line was a
/// cfg command (handled), false otherwise (caller continues its own parsing).
bool handleCfgLine(const char *line);

} // namespace BoardNvs
```

- [ ] **Step 2: Write the implementation**

`lib/BoardNvs/BoardNvs.cpp`:
```cpp
#include "BoardNvs.h"
#include <Arduino.h>
#include <Preferences.h>

namespace BoardNvs {
namespace {
const char *kKeys[] = {"matrixPin",  "matrixW", "matrixH", "matrixOrder",
                       "rgbR",       "rgbG",    "rgbB",    "activityLedPin"};
constexpr size_t kNumKeys = sizeof(kKeys) / sizeof(kKeys[0]);
} // namespace

void loadOverrides(Board::BoardProfile &p) {
  Preferences nvs;
  nvs.begin("board", true); // read-only
  for (size_t i = 0; i < kNumKeys; i++) {
    if (nvs.isKey(kKeys[i])) {
      Board::applyOverride(p, kKeys[i], nvs.getLong(kKeys[i]));
    }
  }
  nvs.end();
}

bool handleCfgLine(const char *line) {
  if (strncmp(line, "cfg ", 4) != 0) {
    return false;
  }
  char key[24];
  long value = 0;
  if (sscanf(line + 4, "%23s %ld", key, &value) != 2) {
    Serial.println(F("cfg: usage 'cfg <key> <value>'"));
    return true;
  }
  Board::BoardProfile probe = Board::active(); // validate against a copy
  if (!Board::applyOverride(probe, key, value)) {
    Serial.printf("cfg: rejected %s=%ld (unknown key or out of range)\n", key,
                  value);
    return true;
  }
  Preferences nvs;
  nvs.begin("board", false);
  nvs.putLong(key, value);
  nvs.end();
  Serial.printf("cfg: %s=%ld saved (reboots apply it)\n", key, value);
  return true;
}

} // namespace BoardNvs
```

- [ ] **Step 3: Verify it compiles**

Run: `pio run -e esp32-s3-matrix`
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add lib/BoardNvs
git commit -m "Add BoardNvs: NVS override load + cfg serial command"
```

---

## Task 7: Wire `-D BOARD_*` flags; remove the matrix-test env

**Files:**
- Modify: `platformio.ini`
- Delete: `src/matrix_test.cpp`

- [ ] **Step 1: Add the board flags and drop the throwaway env**

In `platformio.ini`:
- Add `build_flags = -D BOARD_LOLIN32` to `[env:lolin32]` and `[env:lolin32_displaytest]` (add to existing `build_flags` if present).
- Add `-D BOARD_S3_MATRIX` to the existing `build_flags` of `[env:esp32-s3-matrix]` (it already has `-D ARDUINO_USB_CDC_ON_BOOT=1`):
  ```ini
  build_flags = -D ARDUINO_USB_CDC_ON_BOOT=1 -D BOARD_S3_MATRIX
  ```
- Delete the `[env:lolin32_matrixtest]` and `[env:lolin32_matrixtest-ota]` blocks and remove the `lolin32_matrixtest` line from the header comment.

- [ ] **Step 2: Delete the throwaway sketch**

```bash
git rm src/matrix_test.cpp
```

Also remove the now-obsolete `-<matrix_test.cpp>` tokens from the `lolin32` / `lolin32_displaytest` `build_src_filter` lines (the file no longer exists).

- [ ] **Step 3: Verify all envs compile**

Run: `pio run -e lolin32 && pio run -e lolin32_displaytest && pio run -e esp32-s3-matrix && pio test -e native -f test_board`
Expected: SUCCESS / PASS.

- [ ] **Step 4: Commit**

```bash
git add platformio.ini
git commit -m "Select board profiles via -D BOARD_*; retire matrix_test env"
```

---

## Task 8: Refactor `matrix_main.cpp` onto `HitDisplay` (behaviour identical)

**Files:**
- Modify: `src/matrix_main.cpp`

The control plane, hp accounting, `EVT`/`CTL`, NVS config, and the state machine stay. Only the LED output is rerouted through `HitDisplay`, and pins/teams come from the profile.

- [ ] **Step 1: Replace direct FastLED with HitDisplay**

In `src/matrix_main.cpp`:
- Add `#include <BoardProfile.h>` and `#include <HitDisplay.h>`; remove `#include <FastLED.h>` and the `leds[]`/`MATRIX_PIN`/`NUM_LEDS` globals.
- Add a profile + a team-colour adapter that returns the configured `teamColour`:
  ```cpp
  static const char *teamColourHex(int team) {
    for (size_t i = 0; i < cp::TeamColourCount; i++) {
      if (config.teamIndex[i] == team) return config.teamColour[i];
    }
    return "#FF00FF";
  }
  ```
- In `setup()`, replace the `FastLED.addLeds/...` block with:
  ```cpp
  Board::BoardProfile profile = Board::active();
  BoardNvs::loadOverrides(profile);
  HitDisplay::begin(profile, teamColourHex);
  HitDisplay::setBrightness(config.brightness);
  ```
  (add `#include <BoardNvs.h>`)
- Replace `showSolid(c)` calls with `HitDisplay::solid(...)` and `teamColour(...)`/`fill_rainbow`/`FastLED.show()` with `HitDisplay::flashTeam(team)`, `HitDisplay::idle()`, `HitDisplay::dark()`. Map the existing `teamColour(int)`→`CRGB` helper onto `HitDisplay::flashTeam(team)` (which now owns the colour lookup), and the rainbow tick (`Vis::Rainbow`) onto `HitDisplay::idle()`.
- `runCommand`'s `Bright` case: `HitDisplay::setBrightness(config.brightness)` instead of `FastLED.setBrightness`.

- [ ] **Step 2: Build and flash; regression-check on hardware**

Run: `pio run -e esp32-s3-matrix-ota -t upload` (device 192.168.1.24)
Then verify (matrix at `192.168.1.24`):
- Idle shows the flowing rainbow (RGB order, correct colours).
- `POST /api/command {"cmd":"hit","team":3,"damage":2}` flashes green then dark, hp decrements; capture `EVT hit` / `EVT state s=stunned` on UDP 4210 — identical to before.
- `CTL reset hp=100` restores.

Expected: behaviour identical to pre-refactor.

- [ ] **Step 3: Commit**

```bash
git add src/matrix_main.cpp
git commit -m "Refactor matrix firmware onto HitDisplay HAL (no behaviour change)"
```

---

## Task 9: Refactor `main.cpp` (Lolin32) onto `IrTx` + add `HitDisplay` idle

**Files:**
- Modify: `src/main.cpp`

Keep the OLED + NEC/Vatos decode exactly as-is. Route transmit through `IrTx`; drive the new 8×8 panel at idle via `HitDisplay`.

- [ ] **Step 1: Route transmit through IrTx and add the panel idle**

In `src/main.cpp`:
- Add `#include <BoardProfile.h>`, `#include <IrTx.h>`, `#include <HitDisplay.h>`, `#include <BoardNvs.h>`, `#include <ControlProto.h>`.
- Delete the `CARRIER_*` macros (`main.cpp:264-276`), `vatosSend()` (`278-296`), and the `IR_TX_PIN`/`IR_TX_LEDC_CHANNEL` defines.
- Rewrite `sendNextTestShot()` to use `IrTx`:
  ```cpp
  void sendNextTestShot() {
    static uint8_t txIndex = 0;
    ControlProto::TagEvent ev = ControlProto::tagEventFromVatosShot(
        txIndex / 4 + 1, txIndex % 4 + 1);
    txIndex = (txIndex + 1) % 16;
    IrTx::fire(ev);
    TagNet::eventf("tx team=%d dmg=%d", ev.team, ev.damage);
  }
  ```
- In `setup()`, replace `CARRIER_BEGIN(); gpio_set_drive_capability(... IR_TX_PIN ...); CARRIER_OFF();` with:
  ```cpp
  Board::BoardProfile profile = Board::active();
  BoardNvs::loadOverrides(profile);
  IrTx::begin(profile);
  HitDisplay::begin(profile, nullptr); // team-colour table not needed for idle
  ```
  Keep the activity-LED setup but source the pin from `profile.activityLedPin` (replace the `LED_PIN` literal usage; keep a local `const int ledPin = profile.activityLedPin;`).
- In `loop()`, after `TagNet::handle();`, add a throttled panel idle tick:
  ```cpp
  static uint32_t lastIdleMs = 0;
  if (millis() - lastIdleMs >= 20) { lastIdleMs = millis(); HitDisplay::idle(); }
  ```
- Route the serial `cfg` command before the test-shot fallback:
  ```cpp
  void onSerialLine(const char *line) {
    if (BoardNvs::handleCfgLine(line)) return;
    sendNextTestShot();
  }
  ```

- [ ] **Step 2: Build and flash (USB), verify on hardware**

Run: `pio run -e lolin32 -t upload --upload-port COM14` (download mode: GPIO0→GND, reset, release).
Verify on the Lolin32:
- OLED still shows the monitor UI and decodes a Vatos shot (`team dmg`).
- The 8×8 panel shows a (dim) flowing rainbow at idle.
- BOOT button still transmits a cycling test shot (IR TX LED fires; `tx ...` on UDP).
- `cfg matrixOrder 1` over serial → reboot → panel colours change order (verifies override path).

Expected: all of the above.

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "Refactor Lolin32 firmware onto IrTx + HitDisplay HAL"
```

---

## Task 10: Final verification sweep

**Files:** none (verification only)

- [ ] **Step 1: All builds + native tests green**

Run:
```bash
pio test -e native
pio run -e lolin32
pio run -e lolin32_displaytest
pio run -e esp32-s3-matrix
```
Expected: native tests PASS (8 board tests + the 28 ControlProto tests); all builds SUCCESS.

- [ ] **Step 2: Update docs**

In `README.md`: update the repo-layout block to list `lib/Board`, `lib/HitDisplay`, `lib/IrTx`, `lib/Sound`, `lib/BoardNvs`, and remove the `lolin32_matrixtest`/`matrix_test.cpp` references. Note the `cfg <key> <value>` serial command in the Wireless/serial section.

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "Document board capability HAL libs + cfg command"
```

---

## Notes for the executor

- **Windows Defender link flake:** `collect2: CreateProcess: No such file or directory` / `Access is denied` at the `firmware.elf` link step is transient — just re-run the build. A persistent failure is a real error.
- **OTA vs USB:** the matrix (`192.168.1.24`) and Lolin32 (`192.168.1.48`) are OTA-capable via the `*-ota` envs, but the link is lossy (low RSSI). If `espota` reports "No response", retry with an explicit host IP (`-I <pc-lan-ip>`), or fall back to USB on COM14 (Lolin32 needs the GPIO0→GND download-mode jumper).
- **No behaviour change** is the bar for Tasks 8–9 (except the Lolin32 gaining the idle panel); if the S3 matrix behaves differently after Task 8, that's a regression to fix before committing.
```
