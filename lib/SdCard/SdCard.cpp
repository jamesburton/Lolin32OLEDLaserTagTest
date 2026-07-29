#include "SdCard.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

namespace Storage {
namespace {
bool mounted = false;
uint32_t mountHz = 0;
}

bool sdBegin(int8_t csPin, int8_t mosiPin, int8_t misoPin, int8_t sckPin) {
  mounted = false;
  mountHz = 0;
  if (csPin < 0 || mosiPin < 0 || misoPin < 0 || sckPin < 0) {
    Serial.println("[sd] no card configured (pin(s) absent)");
    return false;
  }

  // End any previous bus setup before re-initialising. Without this a retry
  // after a failed mount re-runs SPI.begin() on an already-configured bus,
  // which does not reliably re-attach the pins — so a card inserted after
  // boot could never mount until a reboot.
  SPI.end();
  SPI.begin(sckPin, misoPin, mosiPin, csPin);

  // Try descending bus speeds. The library's 4 MHz default is fine on a PCB
  // with short traces, but hand-wired prototype leads are long, unshielded and
  // unterminated, and commonly need 1 MHz or slower to enumerate. Starting
  // fast keeps a good board fast; falling back keeps a marginal one working.
  static const uint32_t kSpeeds[] = {4000000, 1000000, 400000};
  for (uint32_t hz : kSpeeds) {
    if (SD.begin(csPin, SPI, hz)) {
      mounted = true;
      mountHz = hz;
      Serial.printf("[sd] mounted at %lu Hz, type=%d size=%lluMB\n",
                    (unsigned long)hz, (int)SD.cardType(),
                    (unsigned long long)(SD.cardSize() / (1024 * 1024)));
      return true;
    }
    SD.end();
  }

  Serial.println("[sd] mount FAILED at every speed (4M/1M/400k)");
  return false;
}

uint32_t sdMountHz() { return mountHz; }

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

bool sdMounted() { return mounted; }

bool sdUsage(uint64_t &totalBytes, uint64_t &usedBytes) {
  totalBytes = 0;
  usedBytes = 0;
  if (!mounted) {
    return false;
  }
  totalBytes = SD.totalBytes();
  usedBytes = SD.usedBytes();
  return true;
}

size_t sdListDetailed(const char *path,
                      void (*onEntry)(const SdEntry &entry, void *ctx),
                      void *ctx) {
  if (!mounted) {
    return 0;
  }
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) {
    return 0;
  }
  size_t count = 0;
  File entry = dir.openNextFile();
  while (entry) {
    SdEntry e;
    // name() returns the full path on some core versions; report the leaf so
    // callers get a stable shape regardless.
    const char *full = entry.name();
    const char *slash = strrchr(full, '/');
    e.name = slash != nullptr ? slash + 1 : full;
    e.isDir = entry.isDirectory();
    e.size = e.isDir ? 0 : (uint32_t)entry.size();
    onEntry(e, ctx);
    count++;
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
  return count;
}

bool sdExists(const char *path, bool &isDir) {
  isDir = false;
  if (!mounted) {
    return false;
  }
  File f = SD.open(path);
  if (!f) {
    return false;
  }
  isDir = f.isDirectory();
  f.close();
  return true;
}

bool sdDelete(const char *path) {
  bool isDir = false;
  if (!sdExists(path, isDir)) {
    Serial.printf("[sd] delete: '%s' not found\n", path);
    return false;
  }
  if (isDir) {
    Serial.printf("[sd] delete: '%s' is a directory — refused\n", path);
    return false;
  }
  const bool ok = SD.remove(path);
  Serial.printf("[sd] delete '%s': %s\n", path, ok ? "ok" : "FAILED");
  return ok;
}

bool sdMakeDir(const char *path) {
  if (!mounted) {
    return false;
  }
  bool isDir = false;
  if (sdExists(path, isDir)) {
    return isDir;
  }
  return SD.mkdir(path);
}

namespace {
File writeFile;
char writePath[128] = "";
uint32_t writtenBytes = 0;

// Creates every parent directory of a file path. SD.open(..., FILE_WRITE)
// fails outright if the directory is missing, so an upload to a new folder
// would otherwise fail for a reason the caller can't act on.
void ensureParentDirs(const char *path) {
  char buf[128];
  strncpy(buf, path, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  for (char *p = buf + 1; *p != '\0'; p++) {
    if (*p != '/') {
      continue;
    }
    *p = '\0';
    if (!SD.exists(buf)) {
      SD.mkdir(buf);
    }
    *p = '/';
  }
}
} // namespace

bool sdWriteOpen(const char *path) {
  if (!mounted) {
    Serial.println("[sd] write: not mounted");
    return false;
  }
  if (writeFile) {
    Serial.println("[sd] write: another upload is already open");
    return false;
  }
  ensureParentDirs(path);
  writeFile = SD.open(path, FILE_WRITE);
  if (!writeFile) {
    Serial.printf("[sd] write: could not open '%s'\n", path);
    return false;
  }
  strncpy(writePath, path, sizeof(writePath) - 1);
  writePath[sizeof(writePath) - 1] = '\0';
  writtenBytes = 0;
  return true;
}

bool sdWriteChunk(const uint8_t *data, size_t len) {
  if (!writeFile) {
    return false;
  }
  const size_t wrote = writeFile.write(data, len);
  writtenBytes += (uint32_t)wrote;
  return wrote == len;
}

uint32_t sdWriteClose() {
  if (!writeFile) {
    return 0;
  }
  writeFile.close();
  Serial.printf("[sd] wrote '%s' (%u bytes)\n", writePath,
                (unsigned)writtenBytes);
  return writtenBytes;
}

void sdWriteAbort() {
  if (!writeFile) {
    return;
  }
  writeFile.close();
  // Remove the partial file: a truncated WAV would be accepted by the upload
  // and then fail at play time, which is a far more confusing failure.
  SD.remove(writePath);
  Serial.printf("[sd] upload of '%s' aborted, partial file removed\n",
                writePath);
  writtenBytes = 0;
}

} // namespace Storage
