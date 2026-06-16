# Board Capability Config + HAL Design (Spec 1)

**Date:** 2026-06-15
**Status:** Approved (brainstorming), pending implementation plan
**Context:** First of a decomposed effort. Spec 2 = retaliation game mode (built
on this). Later: audio (I²S DAC + microSD + piezo) and display variants.

## 1. Goal & scope

Introduce a board **capability/configuration layer** and a thin **hardware
abstraction layer (HAL)** so firmware and (future) game modes target
*capabilities*, not specific pins — and route the **existing** behaviour through
it with **no new game logic**.

**In scope:**
- `lib/Board` — compile-time `BoardProfile` per board, with a whitelisted subset
  of runtime (NVS) overrides.
- `lib/HitDisplay` — hit-feedback output, implemented for a **WS2812 matrix** and
  a **3-pin RGB LED**.
- `lib/IrTx` — IR shot transmit (no-op when absent).
- `lib/Sound` — sound cues; **piezo** implemented, **I²S DAC** stubbed.
- Refactor the **S3-matrix** firmware onto the HAL with **identical behaviour**.
- Give the **Lolin32** a profile that drives its new 8×8 panel at idle.

**Out of scope (declared in the schema / HAL, implemented later):**
- Retaliation and any new game logic (Spec 2), shared game-mode extraction.
- Audio DAC + microSD drivers, display size/type variants.
- Exposing profile overrides over the REST `/api/config` contract.

## 2. Decisions (from brainstorming)

- **Build order:** foundation (this spec) first, then retaliation (Spec 2).
- **Config model: hybrid.** Peripheral *presence* and IR pins are compile-time;
  a whitelisted subset of pins/params is NVS-overridable at boot with safe
  fallback.
- **Structure: shared HAL + thin per-board entry points** (approach A) — modes
  target the HAL; `src/` files converge toward "pick profile, wire HAL, run
  loop" over time, without a big-bang rewrite.

## 3. `BoardProfile` (compile-time) + hybrid overrides

Selected per build env via `-D BOARD_LOLIN32` / `-D BOARD_S3_MATRIX`.

```cpp
enum class HitDisplayKind { None, Ws2812Matrix, RgbLed };
enum class ScreenKind     { None, Ssd1306, Sh1106 };
enum class AudioKind      { None, Piezo, I2sDac };       // only Piezo impl now
enum class ColourOrder    { Grb, Rgb };

struct BoardProfile {
  const char* name;                       // "lolin32", "s3-matrix"

  int8_t irRxPin, irTxPin;                // -1 = absent

  HitDisplayKind hitDisplay;
    int8_t matrixPin; uint8_t matrixW, matrixH; ColourOrder matrixOrder;
    int8_t rgbR, rgbG, rgbB; bool rgbCommonAnode;

  ScreenKind screen;
    uint8_t screenW, screenH; int8_t sdaPin, sclPin; uint8_t i2cAddr;

  AudioKind audio; int8_t piezoPin;       // I²S pins: stub
  bool hasSdCard;                         // SD pins: stub
  int8_t activityLedPin;
};
```

**Runtime overrides (hybrid):** a whitelisted subset — `matrixPin`,
`matrixW/H`, `matrixOrder`, `rgbR/G/B`, `activityLedPin`, `brightness` — is read
from NVS at boot and applied over the compile-time defaults. **Safe fallback:**
an out-of-range/invalid override is logged and ignored (compile-time default
kept). Peripheral presence and IR pins are **never** runtime-overridable. Set
via a small serial `cfg <key> <value>` command set (REST exposure deferred).

## 4. HAL interfaces

Small namespaces; when the profile marks a peripheral absent, calls are safe
no-ops and `present()` returns false. Firmware/modes target these, never raw
pins.

```cpp
namespace HitDisplay {                 // WS2812 matrix OR 3-pin RGB LED
  void begin(const BoardProfile&);
  void idle();                         // matrix: rainbow; RGB: off/breathe
  void flashTeam(int team);            // flash that team's configured colour
  void solid(CRGB);  void dark();
  void setBrightness(uint8_t);  bool present();
}
namespace IrTx {
  void begin(const BoardProfile&);
  void fire(const TagEvent&);          // no-op if irTxPin < 0
  bool present();
}
namespace Sound {
  void begin(const BoardProfile&);
  void cue(SoundId);                   // piezo chirp now; DAC later; no-op if none
  bool present();
}
```

Team colour comes from the existing `teamColours` config. IR **receive** stays
as today (`IrFramer` → `IrProtocol` → `TagEvent`). `HitDisplay` hides the
matrix-vs-RGB difference behind `flashTeam`/`idle`/`solid`.

## 5. Board profiles & extensibility

| | Lolin32 | ESP32-S3-Matrix |
|---|---|---|
| irRxPin / irTxPin | 25 / **13** | 1 / **-1** (TX coming) |
| hitDisplay | Ws2812Matrix(pin 14, 8×8, **Grb**) | Ws2812Matrix(pin 14, 8×8, **Rgb**) |
| screen | Ssd1306 128×32 (SDA 5 / SCL 4, 0x3C) | None |
| audio | None | None |
| activityLed | 26 | 7 |

- A new **board** = add a profile + `-D BOARD_x`.
- A new **peripheral type** (I²S DAC, a different display) = add a HAL
  implementation behind the existing interface; profiles lacking it stay no-op.
- This is how the S3 picks up IR-TX/retaliation later (set `irTxPin`, recompile)
  and how RGB-LED / audio boards slot in.

## 6. Integration / refactor

- `platformio.ini`: `-D BOARD_LOLIN32` on the lolin32 envs, `-D BOARD_S3_MATRIX`
  on the matrix envs. Add libs `lib/Board`, `lib/HitDisplay`, `lib/IrTx`,
  `lib/Sound`.
- **`matrix_main.cpp`** (S3): replace direct FastLED calls with `HitDisplay`;
  behaviour **identical** (rainbow/flash/dark/dead, hp, `EVT`/`CTL`, the whole
  V2 control plane untouched — game logic, not HAL).
- **`main.cpp`** (Lolin32): route IR transmit through `IrTx`; keep the OLED
  (now described by the profile's `screen`) for decode text; **add
  `HitDisplay`(matrix)** driving the idle rainbow on the new 8×8 panel. The
  existing test-shot transmit stays (via the HAL); *removing diagnostic shots*
  is Spec 2, where the TX is repurposed for retaliation.
- The throwaway `src/matrix_test.cpp` and the `lolin32_matrixtest` env are
  **superseded** by the Lolin32 profile + `HitDisplay` and are removed here.
- `ConfigDoc` / control plane unchanged; `brightness` already lives there.

## 7. Testing

- **Native** (extend `env:native`, Arduino-free): profile selection, NVS-override
  validation + safe-fallback, team→colour mapping, and `HitDisplay`'s pure
  matrix-vs-RGB colour computation.
- **Hardware:** S3-matrix is a **regression check** (rainbow/flash/dark/dead +
  `EVT`/`CTL` identical); Lolin32 shows the idle rainbow on the 8×8 via the HAL,
  the OLED still prints decodes, and `IrTx` still transmits.
- **Builds:** every env compiles (`lolin32`, `lolin32_displaytest`,
  `esp32-s3-matrix`, `native`, and the `*-ota` variants).
