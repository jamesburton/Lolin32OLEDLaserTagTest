#include "BoardNvs.h"
#include <Arduino.h>
#include <Preferences.h>

namespace BoardNvs {
namespace {
// MUST list every key Board::applyOverride accepts. `cfg` validates against
// applyOverride and saves to NVS, but only the keys named here are read back at
// boot — so a key missing from this list saves happily and is then silently
// ignored forever. The I2S and SD pins were in exactly that state.
const char *kKeys[] = {"matrixPin",  "matrixW",    "matrixH",   "matrixOrder",
                       "rgbR",       "rgbG",       "rgbB",      "activityLedPin",
                       "i2sBclkPin", "i2sWsPin",   "i2sDinPin",
                       "sdCsPin",    "sdMosiPin",  "sdMisoPin", "sdSckPin"};
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
