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

/// Compile-time description of a board's hardware.
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
  int8_t i2sBclkPin, i2sWsPin, i2sDinPin; // I2sDac only; -1 = absent

  int8_t sdCsPin, sdMosiPin, sdMisoPin, sdSckPin; // -1 = absent (no card)

  int8_t activityLedPin;

  /// True when a card is wired (sdCsPin >= 0). Computed rather than a
  /// separately hand-set field so it can never disagree with the pins.
  bool hasSdCard() const { return sdCsPin >= 0; }
};

/// Returns the compiled-in profile selected by the -D BOARD_* build flag.
const BoardProfile &active();

/// Applies one whitelisted runtime override onto a profile copy. Returns false
/// (and leaves the profile unchanged) for an unknown key or an out-of-range
/// value (safe fallback). Whitelisted keys: matrixPin, matrixW, matrixH,
/// matrixOrder (0=Grb,1=Rgb), rgbR, rgbG, rgbB, activityLedPin, i2sBclkPin,
/// i2sWsPin, i2sDinPin, sdCsPin, sdMosiPin, sdMisoPin, sdSckPin.
bool applyOverride(BoardProfile &p, const char *key, long value);

/// Parses "#RRGGBB" (or "RRGGBB") into an Rgb. Returns false on malformed input.
bool parseHexColour(const char *hex, Rgb &out);

} // namespace Board
