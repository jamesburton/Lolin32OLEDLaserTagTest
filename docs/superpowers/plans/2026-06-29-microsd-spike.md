# microSD Spike Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove microSD-over-SPI → WAV parsing → I2S playback end-to-end on the ESP32-S3-Matrix, behind a native-testable storage HAL, with zero impact on no-card boards (Lolin32).

**Architecture:** Two new PlatformIO libs — `lib/Storage` (pure WAV-chunk parser, no Arduino dependency, native-testable) and `lib/SdCard` (thin ESP32-only Arduino-`SD` wrapper, excluded from the native build like `Sound`/`IrTx`/`BoardNvs` already are). `BoardProfile` gains four SD pins. `matrix_main.cpp` gains one serial verb (`sdplay`) wiring `SdCard::readFile` → `Storage::parseWav` → a new `Sound::playRaw` passthrough to the existing (untouched) `playPcm`.

**Tech Stack:** PlatformIO / Arduino-ESP32 core, Arduino `SD` + `SPI` libraries, Unity (native tests).

## Global Constraints

- Spike scope only: load-whole-file (no streaming), one hardcoded test clip, no runtime SFX-bank replacement, no `/config.json`. (spec: Scope / Backlog)
- WAV validation is strict: exactly 16000 Hz / 16-bit / mono, reject anything else — no resampling/conversion. (spec: WavFile)
- Every failure path (no card, mount fail, file missing, bad WAV) is a logged no-op — never crashes, never blocks the game loop. (spec: Error Handling)
- SD pins on `kS3Matrix`: CS=GP36, MOSI=GP34, MISO=GP35, SCK=GP33; `kLolin32` unchanged at `-1,-1,-1,-1`. (spec: Pins)
- `Sound::playPcm`'s internals (start/write/drain/stop I2S sequencing) must not change — only a new thin public wrapper is added. (spec: Data Flow)
- Native tests must keep passing unmodified; the new `test_storage` suite must build and pass under `pio test -e native` alongside the existing suites.

---

## File Structure

| File | Responsibility |
|---|---|
| `lib/Storage/WavFile.h` / `.cpp` | Pure WAV chunk parser. No Arduino/SD/ESP32 dependency — native-testable. |
| `lib/SdCard/SdCard.h` / `.cpp` | Thin ESP32-only wrapper around Arduino `SD`/`SPI` (mount, list, read-whole-file). Separate PlatformIO lib from `Storage` so it can be `lib_ignore`d on native (mirrors `Sound`/`IrTx`/`BoardNvs`). |
| `lib/Board/BoardProfile.h` / `.cpp` | Modify: add 4 SD pin fields + whitelist entries + `kS3Matrix`/`kLolin32` values + computed `hasSdCard`. |
| `lib/Sound/Sound.h` / `.cpp` | Modify: add public `playRaw()` passthrough to existing `playPcm()`. |
| `src/matrix_main.cpp` | Modify: add `sdplay` serial verb in `onLine()`. |
| `platformio.ini` | Modify: add `lib_ignore = SdCard` to `[env:native]`; add `SD` lib note to `esp32-s3-matrix` env (bundled with Arduino-ESP32 core — no new `lib_deps` entry needed, confirm during Task 5). |
| `test/test_storage/test_storage.cpp` | New native Unity suite for `WavFile::parseWav`. |
| `tools/gen_sfx.py` | Modify: add a `--wav` mode (or a small sibling script) to render one clip as a `.wav` file for the hardware test SD card. |

This deviates from the spec's literal `lib/Storage/{WavFile,SdCard}.{h,cpp}` layout by splitting `SdCard` into its own top-level lib directory. Reason: PlatformIO's native build compiles every `.cpp` in a lib folder it includes; `SdCard.cpp` needs `<SD.h>`/`<SPI.h>`, which don't exist for the native (host) toolchain. Keeping `WavFile` and `SdCard` in the same folder would break `pio test -e native` outright. Splitting into two libs — one native-clean, one `lib_ignore`d — is exactly the pattern the codebase already uses for `Sound` (Arduino-only) vs `Board` (native-clean). The responsibilities and testability split from the spec are preserved; only the physical folder boundary moves.

---

## Task 1: WavFile parser (native-testable)

**Files:**
- Create: `lib/Storage/WavFile.h`
- Create: `lib/Storage/WavFile.cpp`
- Test: `test/test_storage/test_storage.cpp`

**Interfaces:**
- Produces: `Storage::WavView` struct and `bool Storage::parseWav(const uint8_t *buf, size_t len, WavView &out, const char *&err)` — consumed by Task 4 (`sdplay` verb) and used standalone by this task's tests.

- [ ] **Step 1: Write `lib/Storage/WavFile.h`**

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace Storage {

/// A validated view into a caller-owned WAV PCM buffer. `pcm` points inside
/// the buffer passed to parseWav — it is not a copy and is only valid as
/// long as that buffer is alive.
struct WavView {
  uint32_t sampleRate;
  uint8_t  bitsPerSample;
  uint8_t  channels;
  const int16_t *pcm;
  size_t   sampleCount; // per-channel sample count
};

/// Parses a RIFF/WAVE buffer in place (no copy, no allocation). Walks the
/// "fmt " and "data" chunks. Requires exactly 16000 Hz / 16-bit / mono
/// (matches the embedded SfxData.h bank) -- anything else is rejected, not
/// converted. On success returns true and fills `out`. On failure returns
/// false and sets `err` to a short reason string (points into a static
/// string literal, safe to log directly, do not free).
bool parseWav(const uint8_t *buf, size_t len, WavView &out, const char *&err);

} // namespace Storage
```

- [ ] **Step 2: Write `lib/Storage/WavFile.cpp`**

```cpp
#include "WavFile.h"
#include <string.h>

namespace Storage {
namespace {

uint32_t readU32LE(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

uint16_t readU16LE(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

bool tag(const uint8_t *p, const char *four) { return memcmp(p, four, 4) == 0; }

} // namespace

bool parseWav(const uint8_t *buf, size_t len, WavView &out, const char *&err) {
  // RIFF header: "RIFF" <u32 size> "WAVE"
  if (len < 12) { err = "buffer too small for RIFF header"; return false; }
  if (!tag(buf, "RIFF")) { err = "not RIFF"; return false; }
  if (!tag(buf + 8, "WAVE")) { err = "not WAVE"; return false; }

  bool haveFmt = false, haveData = false;
  uint16_t fmtChannels = 0, fmtBits = 0;
  uint32_t fmtRate = 0;
  const int16_t *dataPtr = nullptr;
  uint32_t dataBytes = 0;

  size_t pos = 12;
  while (pos + 8 <= len) {
    const uint8_t *chunkId = buf + pos;
    uint32_t chunkSize = readU32LE(buf + pos + 4);
    size_t chunkBody = pos + 8;

    if (chunkBody + chunkSize > len) {
      err = "chunk exceeds buffer";
      return false;
    }

    if (tag(chunkId, "fmt ")) {
      if (chunkSize < 16) { err = "fmt chunk too small"; return false; }
      uint16_t audioFormat = readU16LE(buf + chunkBody);
      if (audioFormat != 1 /* PCM */) { err = "non-PCM format"; return false; }
      fmtChannels = readU16LE(buf + chunkBody + 2);
      fmtRate     = readU32LE(buf + chunkBody + 4);
      fmtBits     = readU16LE(buf + chunkBody + 14);
      haveFmt = true;
    } else if (tag(chunkId, "data")) {
      dataPtr   = reinterpret_cast<const int16_t *>(buf + chunkBody);
      dataBytes = chunkSize;
      haveData = true;
    }

    // Chunks are word-aligned: odd sizes have a padding byte.
    pos = chunkBody + chunkSize + (chunkSize & 1);
  }

  if (!haveFmt) { err = "missing fmt chunk"; return false; }
  if (!haveData) { err = "missing data chunk"; return false; }
  if (fmtRate != 16000) { err = "unsupported sample rate (need 16000)"; return false; }
  if (fmtBits != 16) { err = "unsupported bit depth (need 16)"; return false; }
  if (fmtChannels != 1) { err = "unsupported channel count (need mono)"; return false; }
  if (dataBytes < 2 || (dataBytes % 2) != 0) {
    err = "data chunk not a whole number of 16-bit samples";
    return false;
  }

  out.sampleRate     = fmtRate;
  out.bitsPerSample  = (uint8_t)fmtBits;
  out.channels       = (uint8_t)fmtChannels;
  out.pcm            = dataPtr;
  out.sampleCount    = dataBytes / 2;
  return true;
}

} // namespace Storage
```

- [ ] **Step 3: Write `test/test_storage/test_storage.cpp`**

```cpp
/*
 * Native unit tests for the Storage library's WAV parser.
 * Run with: pio test -e native -f test_storage
 */

#include <WavFile.h>
#include <unity.h>
#include <string.h>

using namespace Storage;

void setUp() {}
void tearDown() {}

// Builds a minimal valid 16kHz/16-bit/mono WAV into `buf`, returns its length.
// `sampleCount` int16 samples of value 0 follow the header.
size_t buildValidWav(uint8_t *buf, size_t bufCap, uint32_t rate, uint16_t bits,
                      uint16_t channels, size_t sampleCount) {
  size_t dataBytes = sampleCount * 2;
  size_t total = 44 + dataBytes;
  TEST_ASSERT_TRUE(total <= bufCap);

  memcpy(buf, "RIFF", 4);
  uint32_t riffSize = (uint32_t)(total - 8);
  memcpy(buf + 4, &riffSize, 4);
  memcpy(buf + 8, "WAVE", 4);

  memcpy(buf + 12, "fmt ", 4);
  uint32_t fmtSize = 16;
  memcpy(buf + 16, &fmtSize, 4);
  uint16_t audioFormat = 1;
  memcpy(buf + 20, &audioFormat, 2);
  memcpy(buf + 22, &channels, 2);
  memcpy(buf + 24, &rate, 4);
  uint32_t byteRate = rate * channels * (bits / 8);
  memcpy(buf + 28, &byteRate, 4);
  uint16_t blockAlign = (uint16_t)(channels * (bits / 8));
  memcpy(buf + 32, &blockAlign, 2);
  memcpy(buf + 34, &bits, 2);

  memcpy(buf + 36, "data", 4);
  uint32_t dataSize = (uint32_t)dataBytes;
  memcpy(buf + 40, &dataSize, 4);
  memset(buf + 44, 0, dataBytes);

  return total;
}

void test_parses_valid_16k_16bit_mono() {
  uint8_t buf[128];
  size_t len = buildValidWav(buf, sizeof(buf), 16000, 16, 1, 10);

  WavView view;
  const char *err = nullptr;
  TEST_ASSERT_TRUE(parseWav(buf, len, view, err));
  TEST_ASSERT_EQUAL_UINT32(16000, view.sampleRate);
  TEST_ASSERT_EQUAL_UINT8(16, view.bitsPerSample);
  TEST_ASSERT_EQUAL_UINT8(1, view.channels);
  TEST_ASSERT_EQUAL_UINT32(10, (uint32_t)view.sampleCount);
  TEST_ASSERT_NOT_NULL(view.pcm);
}

void test_rejects_wrong_sample_rate() {
  uint8_t buf[128];
  size_t len = buildValidWav(buf, sizeof(buf), 44100, 16, 1, 10);

  WavView view;
  const char *err = nullptr;
  TEST_ASSERT_FALSE(parseWav(buf, len, view, err));
  TEST_ASSERT_NOT_NULL(err);
}

void test_rejects_wrong_bit_depth() {
  uint8_t buf[128];
  size_t len = buildValidWav(buf, sizeof(buf), 16000, 8, 1, 10);

  WavView view;
  const char *err = nullptr;
  TEST_ASSERT_FALSE(parseWav(buf, len, view, err));
  TEST_ASSERT_NOT_NULL(err);
}

void test_rejects_stereo() {
  uint8_t buf[128];
  size_t len = buildValidWav(buf, sizeof(buf), 16000, 16, 2, 10);

  WavView view;
  const char *err = nullptr;
  TEST_ASSERT_FALSE(parseWav(buf, len, view, err));
  TEST_ASSERT_NOT_NULL(err);
}

void test_rejects_missing_data_chunk() {
  // Valid RIFF/WAVE + fmt chunk only, no data chunk.
  uint8_t buf[64];
  memcpy(buf, "RIFF", 4);
  uint32_t riffSize = 28;
  memcpy(buf + 4, &riffSize, 4);
  memcpy(buf + 8, "WAVE", 4);
  memcpy(buf + 12, "fmt ", 4);
  uint32_t fmtSize = 16;
  memcpy(buf + 16, &fmtSize, 4);
  uint16_t audioFormat = 1, channels = 1, bits = 16;
  uint32_t rate = 16000, byteRate = 32000;
  uint16_t blockAlign = 2;
  memcpy(buf + 20, &audioFormat, 2);
  memcpy(buf + 22, &channels, 2);
  memcpy(buf + 24, &rate, 4);
  memcpy(buf + 28, &byteRate, 4);
  memcpy(buf + 32, &blockAlign, 2);
  memcpy(buf + 34, &bits, 2);
  size_t len = 36;

  WavView view;
  const char *err = nullptr;
  TEST_ASSERT_FALSE(parseWav(buf, len, view, err));
  TEST_ASSERT_NOT_NULL(err);
}

void test_rejects_truncated_buffer() {
  uint8_t buf[4] = {'R', 'I', 'F', 'F'};
  WavView view;
  const char *err = nullptr;
  TEST_ASSERT_FALSE(parseWav(buf, sizeof(buf), view, err));
  TEST_ASSERT_NOT_NULL(err);
}

void test_rejects_non_riff_buffer() {
  uint8_t buf[16] = {0};
  memcpy(buf, "JUNK", 4);
  WavView view;
  const char *err = nullptr;
  TEST_ASSERT_FALSE(parseWav(buf, sizeof(buf), view, err));
  TEST_ASSERT_NOT_NULL(err);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_parses_valid_16k_16bit_mono);
  RUN_TEST(test_rejects_wrong_sample_rate);
  RUN_TEST(test_rejects_wrong_bit_depth);
  RUN_TEST(test_rejects_stereo);
  RUN_TEST(test_rejects_missing_data_chunk);
  RUN_TEST(test_rejects_truncated_buffer);
  RUN_TEST(test_rejects_non_riff_buffer);
  return UNITY_END();
}
```

- [ ] **Step 4: Run the test to verify it fails to build first (file doesn't exist yet — sanity check the harness picks it up)**

Run: `pio test -e native -f test_storage`
Expected: build error referencing `WavFile.h` not found, OR if run after Step 1-3 are all saved, proceed to Step 5 directly (Steps 1-3 create the source together since the parser is small and single-purpose — this is a case where red/green happens together rather than a separate empty-stub step, given the parser has no meaningful "smaller" increment below full chunk-walking).

- [ ] **Step 5: Run the test to verify it passes**

Run: `pio test -e native -f test_storage`
Expected: `7 Tests 0 Failures 0 Ignored`, all `PASS`.

- [ ] **Step 6: Run the full native suite to confirm no regressions**

Run: `pio test -e native`
Expected: `test_board`, `test_controlproto`, and `test_storage` all pass; total count is the prior total + 7.

- [ ] **Step 7: Commit**

```bash
git add lib/Storage/WavFile.h lib/Storage/WavFile.cpp test/test_storage/test_storage.cpp
git commit -m "Add native-testable WAV chunk parser (Storage lib)"
```

---

## Task 2: BoardProfile SD pins

**Files:**
- Modify: `lib/Board/BoardProfile.h`
- Modify: `lib/Board/BoardProfile.cpp`
- Modify: `test/test_board/test_board.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `BoardProfile::sdCsPin/sdMosiPin/sdMisoPin/sdSckPin` (`int8_t`), `BoardProfile::hasSdCard()` (method, was a field — see Step 1), and 4 new `applyOverride` whitelist keys (`"sdCsPin"`, `"sdMosiPin"`, `"sdMisoPin"`, `"sdSckPin"`). Consumed by Task 4 (`sdplay` verb reads `Board::active().sdCsPin` etc.) and Task 3 (`SdCard::begin` takes these 4 values).

- [ ] **Step 1: Modify `lib/Board/BoardProfile.h`** — add the 4 pin fields, change `hasSdCard` from a field to a method

Change this block:

```cpp
  AudioKind audio;
  int8_t piezoPin;
  int8_t i2sBclkPin, i2sWsPin, i2sDinPin; // I2sDac only; -1 = absent

  bool hasSdCard;
  int8_t activityLedPin;
};
```

to:

```cpp
  AudioKind audio;
  int8_t piezoPin;
  int8_t i2sBclkPin, i2sWsPin, i2sDinPin; // I2sDac only; -1 = absent

  int8_t sdCsPin, sdMosiPin, sdMisoPin, sdSckPin; // -1 = absent (no card)

  int8_t activityLedPin;

  /// True when a card is wired (sdCsPin >= 0). Computed rather than a
  /// separately hand-set field so it can never disagree with the pins.
  bool hasSdCard() const { return sdCsPin >= 0; }
};
```

Update the whitelist doc comment above `applyOverride`:

```cpp
/// Applies one whitelisted runtime override onto a profile copy. Returns false
/// (and leaves the profile unchanged) for an unknown key or an out-of-range
/// value (safe fallback). Whitelisted keys: matrixPin, matrixW, matrixH,
/// matrixOrder (0=Grb,1=Rgb), rgbR, rgbG, rgbB, activityLedPin, i2sBclkPin,
/// i2sWsPin, i2sDinPin, sdCsPin, sdMosiPin, sdMisoPin, sdSckPin.
bool applyOverride(BoardProfile &p, const char *key, long value);
```

- [ ] **Step 2: Modify `lib/Board/BoardProfile.cpp`** — update both profile initializers and add whitelist entries

Change `kLolin32`'s last two lines:

```cpp
    /*i2sBclkPin*/ -1, /*i2sWsPin*/ -1, /*i2sDinPin*/ -1,
    /*hasSdCard*/ false, /*activityLedPin*/ 26,
};
```

to:

```cpp
    /*i2sBclkPin*/ -1, /*i2sWsPin*/ -1, /*i2sDinPin*/ -1,
    /*sdCsPin*/ -1, /*sdMosiPin*/ -1, /*sdMisoPin*/ -1, /*sdSckPin*/ -1,
    /*activityLedPin*/ 26,
};
```

Change `kS3Matrix`'s last two lines:

```cpp
    /*i2sBclkPin*/ 38, /*i2sWsPin*/ 39, /*i2sDinPin*/ 40,
    /*hasSdCard*/ false, /*activityLedPin*/ 7,
};
```

to:

```cpp
    /*i2sBclkPin*/ 38, /*i2sWsPin*/ 39, /*i2sDinPin*/ 40,
    /*sdCsPin*/ 36, /*sdMosiPin*/ 34, /*sdMisoPin*/ 35, /*sdSckPin*/ 33,
    /*activityLedPin*/ 7,
};
```

Add 4 lines to `applyOverride`, right after the existing `i2sDinPin` line:

```cpp
  if (strcmp(key, "i2sDinPin") == 0 && validPin(value))  { p.i2sDinPin  = (int8_t)value; return true; }
  if (strcmp(key, "sdCsPin") == 0 && validPin(value))    { p.sdCsPin    = (int8_t)value; return true; }
  if (strcmp(key, "sdMosiPin") == 0 && validPin(value))  { p.sdMosiPin  = (int8_t)value; return true; }
  if (strcmp(key, "sdMisoPin") == 0 && validPin(value))  { p.sdMisoPin  = (int8_t)value; return true; }
  if (strcmp(key, "sdSckPin") == 0 && validPin(value))   { p.sdSckPin   = (int8_t)value; return true; }
  return false; // unknown key or out-of-range value -> safe fallback
```

(This replaces the old bare `return false; // unknown key...` line — the 4 new checks are inserted before it.)

- [ ] **Step 2b: Search the codebase for any other reads of `.hasSdCard`** (it changed from field to method call)

Run: `grep -rn "hasSdCard" --include=*.cpp --include=*.h .`
Expected: only the `BoardProfile.h`/`.cpp` occurrences from Step 1/2 exist today (spec confirms `hasSdCard` is currently unused elsewhere — this step is a safety check, not expected to find anything to fix). If any other call site is found, update it from `p.hasSdCard` to `p.hasSdCard()`.

- [ ] **Step 3: Add test cases to `test/test_board/test_board.cpp`** — insert before the `// --- main` section

```cpp
// --- SD pins --------------------------------------------------------------

void test_s3_matrix_profile_sd_pins() {
#if defined(BOARD_S3_MATRIX)
  const BoardProfile &p = active();
  TEST_ASSERT_EQUAL_INT8(36, p.sdCsPin);
  TEST_ASSERT_EQUAL_INT8(34, p.sdMosiPin);
  TEST_ASSERT_EQUAL_INT8(35, p.sdMisoPin);
  TEST_ASSERT_EQUAL_INT8(33, p.sdSckPin);
  TEST_ASSERT_TRUE(p.hasSdCard());
#else
  const BoardProfile &p = active();
  TEST_ASSERT_EQUAL_INT8(-1, p.sdCsPin);
  TEST_ASSERT_FALSE(p.hasSdCard());
#endif
}

void test_override_valid_sd_cs_pin() {
  BoardProfile p = active();
  TEST_ASSERT_TRUE(applyOverride(p, "sdCsPin", 5));
  TEST_ASSERT_EQUAL_INT8(5, p.sdCsPin);
  TEST_ASSERT_TRUE(p.hasSdCard());
}

void test_override_out_of_range_sd_pin_ignored() {
  BoardProfile p = active();
  int8_t before = p.sdSckPin;
  TEST_ASSERT_FALSE(applyOverride(p, "sdSckPin", 999));
  TEST_ASSERT_EQUAL_INT8(before, p.sdSckPin);
}
```

Add the corresponding `RUN_TEST` lines inside `main()`, after `RUN_TEST(test_override_matrix_order);`:

```cpp
  RUN_TEST(test_s3_matrix_profile_sd_pins);
  RUN_TEST(test_override_valid_sd_cs_pin);
  RUN_TEST(test_override_out_of_range_sd_pin_ignored);
```

- [ ] **Step 4: Run the native test suite**

Run: `pio test -e native -f test_board`
Expected: all tests pass, including the 3 new ones (native build has no `BOARD_S3_MATRIX` flag, so `active()` returns `kLolin32` — the `#else` branch of `test_s3_matrix_profile_sd_pins` exercises).

- [ ] **Step 5: Build the S3 firmware to confirm the profile struct change compiles for that target too**

Run: `pio run -e esp32-s3-matrix`
Expected: build succeeds (this doesn't run `test_s3_matrix_profile_sd_pins`'s `#if defined(BOARD_S3_MATRIX)` branch — that only runs if `test_board` is ever compiled with that flag, which it isn't today; this step just confirms `BoardProfile.cpp`'s new `kS3Matrix` initializer is syntactically correct under the real build flags).

- [ ] **Step 6: Commit**

```bash
git add lib/Board/BoardProfile.h lib/Board/BoardProfile.cpp test/test_board/test_board.cpp
git commit -m "Add SD card pins to BoardProfile (S3-Matrix: CS36/MOSI34/MISO35/SCK33)"
```

---

## Task 3: SdCard ESP32-only wrapper

**Files:**
- Create: `lib/SdCard/SdCard.h`
- Create: `lib/SdCard/SdCard.cpp`
- Modify: `platformio.ini`

**Interfaces:**
- Consumes: nothing new (takes raw pins, not a `BoardProfile`, to keep it decoupled — matches how `Sound::begin` takes the whole profile but the lower-level `i2s_set_pin` calls take raw pin numbers).
- Produces: `Storage::sdBegin(int8_t,int8_t,int8_t,int8_t) -> bool`, `Storage::sdList(const char*, void(*)(const char*)) -> size_t`, `Storage::sdReadFile(const char*, size_t&) -> uint8_t*`. Consumed by Task 4.

- [ ] **Step 1: Write `lib/SdCard/SdCard.h`**

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace Storage {

/// Mounts the SD card over SPI on the given pins. Any pin < 0 means "no
/// card wired" and this returns false without touching the SPI/SD APIs.
/// Safe to call more than once (re-mounts). Logs the outcome via Serial.
bool sdBegin(int8_t csPin, int8_t mosiPin, int8_t misoPin, int8_t sckPin);

/// Lists entries directly under `path` (non-recursive), calling `onEntry`
/// once per entry name (not a full path). Returns the entry count. Logs and
/// returns 0 if the card isn't mounted or the directory doesn't exist.
size_t sdList(const char *path, void (*onEntry)(const char *name));

/// Reads a whole file into a newly heap-allocated buffer. Caller owns the
/// buffer and must free() it. Returns nullptr and sets len=0 on any failure
/// (not mounted, not found, read error) -- logs the reason via Serial.
uint8_t *sdReadFile(const char *path, size_t &len);

} // namespace Storage
```

- [ ] **Step 2: Write `lib/SdCard/SdCard.cpp`**

```cpp
#include "SdCard.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

namespace Storage {
namespace {
bool mounted = false;
}

bool sdBegin(int8_t csPin, int8_t mosiPin, int8_t misoPin, int8_t sckPin) {
  mounted = false;
  if (csPin < 0 || mosiPin < 0 || misoPin < 0 || sckPin < 0) {
    Serial.println("[sd] no card configured (pin(s) absent)");
    return false;
  }
  SPI.begin(sckPin, misoPin, mosiPin, csPin);
  if (!SD.begin(csPin)) {
    Serial.println("[sd] mount FAILED");
    return false;
  }
  mounted = true;
  Serial.printf("[sd] mounted, type=%d size=%lluMB\n", (int)SD.cardType(),
                (unsigned long long)(SD.cardSize() / (1024 * 1024)));
  return true;
}

size_t sdList(const char *path, void (*onEntry)(const char *name)) {
  if (!mounted) {
    Serial.println("[sd] list: not mounted");
    return 0;
  }
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) {
    Serial.printf("[sd] list: '%s' not a directory\n", path);
    return 0;
  }
  size_t count = 0;
  File entry = dir.openNextFile();
  while (entry) {
    onEntry(entry.name());
    count++;
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
  return count;
}

uint8_t *sdReadFile(const char *path, size_t &len) {
  len = 0;
  if (!mounted) {
    Serial.println("[sd] read: not mounted");
    return nullptr;
  }
  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.printf("[sd] read: '%s' not found\n", path);
    return nullptr;
  }
  size_t fileLen = f.size();
  uint8_t *buf = (uint8_t *)malloc(fileLen);
  if (buf == nullptr) {
    Serial.printf("[sd] read: malloc(%u) failed\n", (unsigned)fileLen);
    f.close();
    return nullptr;
  }
  size_t got = f.read(buf, fileLen);
  f.close();
  if (got != fileLen) {
    Serial.printf("[sd] read: short read (%u of %u bytes)\n", (unsigned)got,
                  (unsigned)fileLen);
    free(buf);
    return nullptr;
  }
  len = fileLen;
  return buf;
}

} // namespace Storage
```

- [ ] **Step 3: Modify `platformio.ini`** — exclude `SdCard` from the native build (mirrors the existing `lib_ignore` list)

Change:

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

to:

```ini
lib_ignore =
    TagNet
    Vatos
    IrFramer
    HitDisplay
    IrTx
    Sound
    BoardNvs
    SdCard
```

- [ ] **Step 4: Confirm the Arduino `SD` library is available to the `esp32-s3-matrix` env**

Run: `pio run -e esp32-s3-matrix -t menuconfig` is not applicable; instead just build:

Run: `pio run -e esp32-s3-matrix`
Expected: build succeeds. `SD`/`SPI` ship with the `espressif32` Arduino core (`framework = arduino`), so no new `lib_deps` entry should be required — this step's job is to prove that assumption against the actual toolchain. If the build fails with a missing-header error for `SD.h`, add `SD` explicitly to `esp32-s3-matrix`'s `lib_deps` in `platformio.ini` and rebuild.

- [ ] **Step 5: Run the native suite to confirm `SdCard` is correctly excluded**

Run: `pio test -e native`
Expected: all tests still pass (unchanged count from Task 2) — confirms native never tries to compile `SdCard.cpp`.

- [ ] **Step 6: Commit**

```bash
git add lib/SdCard/SdCard.h lib/SdCard/SdCard.cpp platformio.ini
git commit -m "Add ESP32-only SdCard wrapper (Arduino SD/SPI), excluded from native"
```

---

## Task 4: Sound::playRaw + sdplay serial verb

**Files:**
- Modify: `lib/Sound/Sound.h`
- Modify: `lib/Sound/Sound.cpp`
- Modify: `src/matrix_main.cpp`

**Interfaces:**
- Consumes: `Storage::sdBegin`, `Storage::sdReadFile`, `Storage::sdList` (Task 3); `Storage::parseWav`, `Storage::WavView` (Task 1); `Board::active().sdCsPin` etc. (Task 2).
- Produces: `Sound::playRaw(const int16_t*, size_t)` — public, no other consumers planned in this spike.

- [ ] **Step 1: Modify `lib/Sound/Sound.h`** — add `playRaw` next to `playIndex`

Change:

```cpp
void        playIndex(int idx); // play bank entry idx; no-op if out of range
uint8_t     sfxCount();      // bank size
```

to:

```cpp
void        playIndex(int idx); // play bank entry idx; no-op if out of range
void        playRaw(const int16_t *data, size_t samples); // I2sDac only; no-op otherwise
uint8_t     sfxCount();      // bank size
```

- [ ] **Step 2: Modify `lib/Sound/Sound.cpp`** — implement `playRaw` as a thin passthrough, placed right after `playIndex`'s closing brace (before `uint8_t sfxCount()`)

```cpp
void playRaw(const int16_t *data, size_t samples) {
#if defined(ESP32)
  if (kind != Board::AudioKind::I2sDac) return;
  if (data == nullptr || samples == 0) return;
  playPcm(data, samples);
#else
  (void)data;
  (void)samples;
#endif
}
```

- [ ] **Step 3: Verify the S3 firmware still builds with the new Sound API**

Run: `pio run -e esp32-s3-matrix`
Expected: builds (this only adds a new function; nothing calls it yet).

- [ ] **Step 4: Add the `sdplay` serial verb to `src/matrix_main.cpp`**

First, add the include near the top with the other lib includes (find the existing `#include` block, e.g. where `Sound.h`/`Board*.h` are included, and add):

```cpp
#include <SdCard.h>
#include <WavFile.h>
```

Then, in `onLine()`, add a new branch right after the existing `lives ` branch (find the closing of that `else if` block around line 338-342 shown below):

```cpp
  } else if (strncmp(line, "lives ", 6) == 0) {
    // Select starting health (4/8/16/32), persist it, and revive to it.
    int n = atoi(line + 6);
    if (n == 4 || n == 8 || n == 16 || n == 32) {
      config.startHp = n;
```

...add the new branch immediately after that `lives` block's closing (find where it ends, likely a few lines further with a closing `}` and persist/revive calls, then insert):

```cpp
  } else if (strcmp(line, "sdplay") == 0) {
    // Spike bench helper: mount the card, list /sfx/, and play one .wav
    // through the existing I2S path. Bypasses game state entirely.
    const Board::BoardProfile &prof = Board::active();
    if (!prof.hasSdCard()) {
      Serial.println("[sd] sdplay: no SD card configured on this board");
    } else if (!Storage::sdBegin(prof.sdCsPin, prof.sdMosiPin, prof.sdMisoPin,
                                  prof.sdSckPin)) {
      Serial.println("[sd] sdplay: mount failed");
    } else {
      Storage::sdList("/sfx", [](const char *name) {
        Serial.printf("[sd] /sfx/%s\n", name);
      });
      size_t fileLen = 0;
      uint8_t *buf = Storage::sdReadFile("/sfx/test.wav", fileLen);
      if (buf == nullptr) {
        Serial.println("[sd] sdplay: could not read /sfx/test.wav");
      } else {
        Storage::WavView view;
        const char *err = nullptr;
        if (!Storage::parseWav(buf, fileLen, view, err)) {
          Serial.printf("[sd] sdplay: WAV rejected (%s)\n", err);
        } else {
          Serial.printf("[sd] sdplay: playing %u samples @ %uHz\n",
                        (unsigned)view.sampleCount, (unsigned)view.sampleRate);
          Sound::playRaw(view.pcm, view.sampleCount);
        }
        free(buf);
      }
    }
```

Note: match the existing brace/`else if` chain style in the surrounding code exactly (the file uses `} else if (...) {` on one line) — read the ~15 lines around the `lives` branch in the actual file before inserting, since the exact closing brace placement of the `lives` branch must be preserved.

- [ ] **Step 5: Build the S3 firmware**

Run: `pio run -e esp32-s3-matrix`
Expected: build succeeds. Fix any include-path or brace-matching issues surfaced by the compiler (this is expected to need minor adjustment to match the exact surrounding code shape from Step 4's note).

- [ ] **Step 6: Run the full native suite one more time (sanity check nothing ESP32-only leaked into a native-built file)**

Run: `pio test -e native`
Expected: unchanged pass count from Task 3 (native never compiles `matrix_main.cpp` — confirms this, doesn't newly test it).

- [ ] **Step 7: Commit**

```bash
git add lib/Sound/Sound.h lib/Sound/Sound.cpp src/matrix_main.cpp
git commit -m "Add sdplay serial verb: SD -> WAV -> Sound::playRaw"
```

---

## Task 5: Hardware verification clip + on-device test

**Files:**
- Modify: `tools/gen_sfx.py`
- No source changes — this task produces the SD-card test asset and runs the hardware check.

**Interfaces:**
- Consumes: nothing new.
- Produces: a `.wav` file to copy onto the physical microSD card. No code interfaces (this is the manual hardware-verification task from the spec's Testing section).

- [ ] **Step 1: Read `tools/gen_sfx.py`** to find where it writes the C header, and the numpy array for one clip (e.g. the death cue) before it's serialized.

Run: `grep -n "def \|RATE\|def main" tools/gen_sfx.py`

- [ ] **Step 2: Add a `--wav <name>` CLI option** that writes one already-synthesized clip's int16 numpy array to a standard WAV file instead of (or in addition to) the C header, using Python's stdlib `wave` module (no new dependency):

```python
import argparse
import wave

def write_wav(path, samples_int16, rate=16000):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)  # 16-bit
        w.setframerate(rate)
        w.writeframes(samples_int16.tobytes())
```

Wire this into the script's existing `argparse`/`main()` (exact integration point depends on the current structure found in Step 1 — add a `--wav OUTPUT.wav` flag that, when passed, calls `write_wav()` on the death-cue clip's samples and exits, skipping the normal header-generation path).

- [ ] **Step 3: Generate the test clip**

Run: `python tools/gen_sfx.py --wav /tmp/test.wav` (adjust the flag/clip-name per the actual Step 2 integration)
Expected: a valid 16kHz/16-bit/mono `.wav` file is created.

- [ ] **Step 4: Verify the generated WAV against the native parser** (belt-and-suspenders — reuses Task 1's parser via a tiny throwaway check, not a permanent test)

Run: `python -c "import wave; w = wave.open('/tmp/test.wav'); print(w.getframerate(), w.getsampwidth(), w.getnchannels())"`
Expected: `16000 2 1`

- [ ] **Step 5: Copy the file onto the physical microSD card**

Manual step: format the card FAT32, create a `/sfx/` directory, copy `/tmp/test.wav` to `/sfx/test.wav`, insert into the S3-Matrix's card slot (wired per Task 2's pins: CS=GP36, MOSI=GP34, MISO=GP35, SCK=GP33, VCC=3V3, GND=GND).

- [ ] **Step 6: Flash the firmware and verify over serial**

Run: `pio run -e esp32-s3-matrix -t upload` (or OTA per the handoff's existing flashing gotchas), then open the serial monitor and send `sdplay`.

Expected serial output (values will vary slightly):
```
[sd] mounted, type=... size=...MB
[sd] /sfx/test.wav
[sd] sdplay: playing NNNN samples @ 16000Hz
```
And audible playback through the wired MAX98357A/speaker.

- [ ] **Step 7: Test the no-card and no-file error paths**

With the card removed: send `sdplay`, expect `[sd] mounted, type=...` to NOT appear — instead `[sd] mount failed`. With the card present but `/sfx/test.wav` absent (rename it): send `sdplay`, expect `[sd] sdplay: could not read /sfx/test.wav`. Confirms the Error Handling table in the spec end-to-end on real hardware.

- [ ] **Step 8: Commit the gen_sfx.py change**

```bash
git add tools/gen_sfx.py
git commit -m "Add --wav export mode to gen_sfx.py for SD-card test clips"
```

---

## Task 6: Update handoff doc

**Files:**
- Modify: `.docs/handoff.md`

**Interfaces:** none — documentation only.

- [ ] **Step 1: Update `.docs/handoff.md` Next Steps #1** to mark the spike done and record the outcome (pins confirmed working, library choices, file layout, serial verb name), following the doc's existing terse style. Read the current `#1` entry first (it's long — the "SPIKE — microSD reader" block) and replace it with a short "done" summary plus a pointer to this plan file and the design spec, keeping the same numbered-list position so later items don't need renumbering.

- [ ] **Step 2: Update the "Tests" line** near the top of the handoff to reflect the new native test count (`test_storage` adds 7 tests; if BoardProfile's 3 new tests were also added, add those too — read the current exact wording first since the doc has shown some internal inconsistency between "36/36" and "38" that should NOT be propagated further; state the suites explicitly: `test_board` + `test_controlproto` + `test_storage`, with each suite's own count, rather than a single ambiguous total).

- [ ] **Step 3: Commit**

```bash
git add .docs/handoff.md
git commit -m "Update handoff: microSD spike complete"
```

---

## Self-Review Notes

- **Spec coverage:** Scope bullets (mount/list/read/parse/play, native WAV parser, BoardProfile pins, serial verb) → Tasks 1-4. Pins table → Task 2. Error handling table → Task 5 Step 7 (hardware-verified) + Tasks 3/4 (code-level no-ops). Testing section (native + hardware) → Task 1 (native) + Task 5 (hardware). Backlog items are explicitly NOT tasked (correct — they're deferred).
- **Placeholder scan:** no TBD/TODO; all code blocks are complete; Task 4 Step 4's note about matching exact brace style is a genuine implementation-time judgment call (the file's precise current line numbers may drift), not a placeholder — the branch's full logic is given verbatim.
- **Type consistency:** `Storage::WavView`/`parseWav` signature (Task 1) matches its use in Task 4 exactly. `Storage::sdBegin/sdList/sdReadFile` signatures (Task 3) match Task 4's calls exactly. `Sound::playRaw(const int16_t*, size_t)` (Task 4) matches its declaration and call site.
- **File-structure deviation from spec:** documented above (File Structure section) — `SdCard` split into its own lib for native-build correctness; approved as an implementation-level detail, not a spec contradiction.
