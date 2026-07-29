#include "FlashFs.h"

#include <Arduino.h>
#include <LittleFS.h>

namespace Storage {
namespace {
bool mounted = false;
File writeFile;
char writePath[128] = "";
uint32_t writtenBytes = 0;

// Creates every parent directory of a file path. LittleFS refuses to open a
// file for writing when its directory is missing, so an upload into a new
// folder would otherwise fail for a reason the caller cannot act on.
void ensureParentDirs(const char *path) {
  char buf[128];
  strncpy(buf, path, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  for (char *p = buf + 1; *p != '\0'; p++) {
    if (*p != '/') {
      continue;
    }
    *p = '\0';
    if (!LittleFS.exists(buf)) {
      LittleFS.mkdir(buf);
    }
    *p = '/';
  }
}
} // namespace

bool fsBegin() {
  if (mounted) {
    return true;
  }
  // formatOnFail: a board that has never held clips has an unformatted
  // partition, and formatting once is the correct silent recovery.
  mounted = LittleFS.begin(true);
  if (mounted) {
    Serial.printf("[fs] mounted, %u of %u bytes used\n",
                  (unsigned)LittleFS.usedBytes(),
                  (unsigned)LittleFS.totalBytes());
  } else {
    Serial.println("[fs] mount FAILED (no data partition?)");
  }
  return mounted;
}

bool fsMounted() { return mounted; }

bool fsUsage(size_t &totalBytes, size_t &usedBytes) {
  totalBytes = 0;
  usedBytes = 0;
  if (!mounted) {
    return false;
  }
  totalBytes = LittleFS.totalBytes();
  usedBytes = LittleFS.usedBytes();
  return true;
}

size_t fsList(const char *path, void (*onEntry)(const FsEntry &entry, void *ctx),
              void *ctx) {
  if (!mounted) {
    return 0;
  }
  File dir = LittleFS.open(path);
  if (!dir || !dir.isDirectory()) {
    return 0;
  }
  size_t count = 0;
  File entry = dir.openNextFile();
  while (entry) {
    FsEntry e;
    // name() may be a full path depending on core version; report the leaf so
    // callers get one stable shape.
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

uint8_t *fsReadFile(const char *path, size_t &len) {
  len = 0;
  if (!mounted) {
    return nullptr;
  }
  File f = LittleFS.open(path, FILE_READ);
  if (!f || f.isDirectory()) {
    Serial.printf("[fs] read: '%s' not found\n", path);
    return nullptr;
  }
  const size_t fileLen = f.size();
  uint8_t *buf = (uint8_t *)malloc(fileLen);
  if (buf == nullptr) {
    Serial.printf("[fs] read: malloc(%u) failed\n", (unsigned)fileLen);
    f.close();
    return nullptr;
  }
  const size_t got = f.read(buf, fileLen);
  f.close();
  if (got != fileLen) {
    Serial.printf("[fs] read: short read (%u of %u)\n", (unsigned)got,
                  (unsigned)fileLen);
    free(buf);
    return nullptr;
  }
  len = fileLen;
  return buf;
}

namespace {
File readFile;
}

bool fsOpenRead(const char *path, size_t &size) {
  size = 0;
  if (!mounted) {
    return false;
  }
  if (readFile) {
    readFile.close();
  }
  readFile = LittleFS.open(path, FILE_READ);
  if (!readFile || readFile.isDirectory()) {
    return false;
  }
  size = readFile.size();
  return true;
}

size_t fsRead(uint8_t *buf, size_t len) {
  if (!readFile) {
    return 0;
  }
  return readFile.read(buf, len);
}

bool fsSeek(size_t pos) {
  return readFile && readFile.seek(pos);
}

void fsCloseRead() {
  if (readFile) {
    readFile.close();
  }
}

bool fsExists(const char *path, bool &isDir) {
  isDir = false;
  if (!mounted) {
    return false;
  }
  File f = LittleFS.open(path);
  if (!f) {
    return false;
  }
  isDir = f.isDirectory();
  f.close();
  return true;
}

bool fsDelete(const char *path) {
  bool isDir = false;
  if (!fsExists(path, isDir)) {
    return false;
  }
  if (isDir) {
    Serial.printf("[fs] delete: '%s' is a directory — refused\n", path);
    return false;
  }
  const bool ok = LittleFS.remove(path);
  Serial.printf("[fs] delete '%s': %s\n", path, ok ? "ok" : "FAILED");
  return ok;
}

bool fsWriteOpen(const char *path) {
  if (!mounted) {
    return false;
  }
  if (writeFile) {
    Serial.println("[fs] write: another upload is already open");
    return false;
  }
  ensureParentDirs(path);
  writeFile = LittleFS.open(path, FILE_WRITE);
  if (!writeFile) {
    Serial.printf("[fs] write: could not open '%s'\n", path);
    return false;
  }
  strncpy(writePath, path, sizeof(writePath) - 1);
  writePath[sizeof(writePath) - 1] = '\0';
  writtenBytes = 0;
  return true;
}

bool fsWriteChunk(const uint8_t *data, size_t len) {
  if (!writeFile) {
    return false;
  }
  const size_t wrote = writeFile.write(data, len);
  writtenBytes += (uint32_t)wrote;
  return wrote == len;
}

uint32_t fsWriteClose() {
  if (!writeFile) {
    return 0;
  }
  writeFile.close();
  Serial.printf("[fs] wrote '%s' (%u bytes)\n", writePath,
                (unsigned)writtenBytes);
  return writtenBytes;
}

void fsWriteAbort() {
  if (!writeFile) {
    return;
  }
  writeFile.close();
  LittleFS.remove(writePath);
  Serial.printf("[fs] upload of '%s' aborted, partial file removed\n",
                writePath);
  writtenBytes = 0;
}

} // namespace Storage
