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
