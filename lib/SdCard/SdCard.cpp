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

namespace {
// Sends one 6-byte SD command and returns the first non-0xFF response byte.
// 0xFF means the card never answered within the allowed poll window.
uint8_t sdCommand(uint8_t cmd, uint32_t arg, uint8_t crc) {
  SPI.transfer(0x40 | cmd);
  SPI.transfer((uint8_t)(arg >> 24));
  SPI.transfer((uint8_t)(arg >> 16));
  SPI.transfer((uint8_t)(arg >> 8));
  SPI.transfer((uint8_t)arg);
  SPI.transfer(crc);
  // The card may take up to 8 bytes to respond (spec allows NCR of 0-8).
  for (int i = 0; i < 10; i++) {
    const uint8_t r = SPI.transfer(0xFF);
    if (r != 0xFF) {
      return r;
    }
  }
  return 0xFF;
}
} // namespace

SdProbe sdProbeRaw(int8_t csPin, int8_t mosiPin, int8_t misoPin, int8_t sckPin) {
  SdProbe out{};
  out.responded = false;
  out.r1 = 0xFF;
  out.cmd8Ok = false;

  SPI.end();
  SPI.begin(sckPin, misoPin, mosiPin, csPin);
  pinMode(csPin, OUTPUT);
  digitalWrite(csPin, HIGH);

  // Init must happen at 100-400 kHz; the card only accepts a faster clock
  // after it has left idle.
  SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));

  // >= 74 clocks with CS high and MOSI high puts the card into native SPI mode.
  for (int i = 0; i < 12; i++) {
    SPI.transfer(0xFF);
  }

  digitalWrite(csPin, LOW);
  // CMD0 GO_IDLE_STATE. CRC is fixed (0x95) and mandatory for this one command.
  out.r1 = sdCommand(0, 0, 0x95);
  out.responded = out.r1 != 0xFF;

  if (out.responded) {
    // CMD8 SEND_IF_COND: ask for 2.7-3.6 V with check pattern 0xAA. An SDv2
    // card echoes both back in the trailing 4 bytes of its R7 response.
    const uint8_t r = sdCommand(8, 0x000001AA, 0x87);
    out.cmd8[0] = r;
    for (int i = 0; i < 4; i++) {
      out.cmd8[i + 1] = SPI.transfer(0xFF);
    }
    out.cmd8Ok = (out.cmd8[3] == 0x01) && (out.cmd8[4] == 0xAA);
  }

  digitalWrite(csPin, HIGH);
  SPI.transfer(0xFF); // trailing clocks so the card releases the bus
  SPI.endTransaction();
  return out;
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
