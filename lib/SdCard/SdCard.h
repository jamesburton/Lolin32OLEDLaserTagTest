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
