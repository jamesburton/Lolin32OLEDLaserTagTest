#include "BoardProfile.h"
#include <string.h>

namespace Board {

static const BoardProfile kLolin32 = {
    /*name*/ "lolin32",
    /*irRxPin*/ 25, /*irTxPin*/ 13,
    /*hitDisplay*/ HitDisplayKind::Ws2812Matrix,
    /*matrixPin*/ 14, /*matrixW*/ 8, /*matrixH*/ 8, /*matrixOrder*/ ColourOrder::Grb,
    /*rgbR*/ -1, /*rgbG*/ -1, /*rgbB*/ -1, /*rgbCommonAnode*/ false,
    /*screen*/ ScreenKind::Ssd1306, /*screenW*/ 128, /*screenH*/ 32,
    /*sdaPin*/ 5, /*sclPin*/ 4, /*i2cAddr*/ 0x3C,
    /*audio*/ AudioKind::None, /*piezoPin*/ -1,
    /*i2sBclkPin*/ -1, /*i2sWsPin*/ -1, /*i2sDinPin*/ -1,
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
    /*audio*/ AudioKind::I2sDac, /*piezoPin*/ -1,
    /*i2sBclkPin*/ 38, /*i2sWsPin*/ 39, /*i2sDinPin*/ 40,
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

bool applyOverride(BoardProfile &p, const char *key, long value) {
  if (strcmp(key, "matrixPin") == 0 && validPin(value)) { p.matrixPin = (int8_t)value; return true; }
  if (strcmp(key, "matrixW") == 0 && value > 0 && value <= 64) { p.matrixW = (uint8_t)value; return true; }
  if (strcmp(key, "matrixH") == 0 && value > 0 && value <= 64) { p.matrixH = (uint8_t)value; return true; }
  if (strcmp(key, "matrixOrder") == 0 && (value == 0 || value == 1)) {
    p.matrixOrder = value == 1 ? ColourOrder::Rgb : ColourOrder::Grb; return true;
  }
  if (strcmp(key, "rgbR") == 0 && validPin(value)) { p.rgbR = (int8_t)value; return true; }
  if (strcmp(key, "rgbG") == 0 && validPin(value)) { p.rgbG = (int8_t)value; return true; }
  if (strcmp(key, "rgbB") == 0 && validPin(value)) { p.rgbB = (int8_t)value; return true; }
  if (strcmp(key, "activityLedPin") == 0 && validPin(value)) { p.activityLedPin = (int8_t)value; return true; }
  if (strcmp(key, "i2sBclkPin") == 0 && validPin(value)) { p.i2sBclkPin = (int8_t)value; return true; }
  if (strcmp(key, "i2sWsPin") == 0 && validPin(value))   { p.i2sWsPin   = (int8_t)value; return true; }
  if (strcmp(key, "i2sDinPin") == 0 && validPin(value))  { p.i2sDinPin  = (int8_t)value; return true; }
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
  for (int i = 0; i < 6; i++) { if (!hexNibble(hex[i], n[i])) return false; }
  out.r = (n[0] << 4) | n[1];
  out.g = (n[2] << 4) | n[3];
  out.b = (n[4] << 4) | n[5];
  return true;
}

} // namespace Board
