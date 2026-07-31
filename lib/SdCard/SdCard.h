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

/// Result of a raw SPI-level card probe.
struct SdProbe {
  bool responded;   ///< True if the card answered CMD0 at all.
  uint8_t r1;       ///< CMD0 response byte (0x01 = idle, the expected value).
  uint8_t cmd8[5];  ///< CMD8 R7 response (only valid when responded).
  bool cmd8Ok;      ///< True if CMD8 echoed the 0x1AA check pattern.
  bool ready;       ///< True if ACMD41 completed — the card left idle and is usable.
  uint8_t acmd41;   ///< Last ACMD41 reply: 0x01 = still idle, 0x05 = illegal, 0xFF = silent.
  uint8_t cmd55;    ///< Last CMD55 reply. Distinguishes "died after CMD8" from "ACMD41 itself refused".
  uint8_t cmd58;    ///< CMD58 (READ_OCR) reply — a cheap command re-tried after ACMD41.
  uint8_t ocr[4];   ///< OCR register from CMD58 (big-endian). Bit 31 = power-up
                    ///< complete, bit 30 = CCS, bits 15-23 = voltage window the
                    ///< card accepts — the card's OWN report of supply health.
  uint16_t acmd41Tries; ///< How many ACMD41 polls were made.
};

/// <summary>
/// Talks to the card directly over SPI, bypassing the SD library entirely.
/// </summary>
/// <param name="csPin">Chip select.</param>
/// <param name="mosiPin">MOSI.</param>
/// <param name="misoPin">MISO.</param>
/// <param name="sckPin">SCK.</param>
/// <returns>What the card said.</returns>
/// <remarks>
/// Diagnostic of last resort. The Arduino SD library reports mount failures
/// only through log_e(), which writes to Serial and cannot be captured on a
/// headless board — so a failing mount is otherwise a silent "no". This runs
/// the documented SD SPI init handshake (74 idle clocks, CMD0, CMD8) and
/// reports the raw bytes, which separates the two possibilities cleanly:
/// no response at all means power/wiring/CS, while a valid idle response
/// means the card is alive and the fault is above the bus.
/// </remarks>
SdProbe sdProbeRaw(int8_t csPin, int8_t mosiPin, int8_t misoPin, int8_t sckPin,
                   uint32_t clockHz = 400000);

/// True when a card is currently mounted.
bool sdMounted();

/// The SPI clock the card actually mounted at, or 0 when not mounted. Exposed
/// as a diagnostic: a card that only enumerates at the slowest speed is a
/// signal-integrity warning about the wiring, not a healthy result.
uint32_t sdMountHz();

/// Total and used bytes on the mounted card. Both are set to 0 when no card
/// is mounted. Returns false in that case.
bool sdUsage(uint64_t &totalBytes, uint64_t &usedBytes);

/// One entry from a directory listing.
struct SdEntry {
  const char *name; ///< Entry name only, not a full path.
  uint32_t size;    ///< Size in bytes; 0 for a directory.
  bool isDir;
};

/// Lists entries directly under `path`, reporting size and type per entry.
/// Returns the entry count, or 0 when not mounted / not a directory.
/// The `name` in each callback is only valid for the duration of that call.
size_t sdListDetailed(const char *path, void (*onEntry)(const SdEntry &entry, void *ctx),
                      void *ctx);

/// Deletes a file. Returns false when not mounted, absent, or the delete
/// failed. Directories are refused — removing one implicitly could discard a
/// whole sound bank on a single mistyped path.
bool sdDelete(const char *path);

/// Creates a directory, including any missing parents. Returns true if it
/// exists afterwards.
bool sdMakeDir(const char *path);

/// True when the path exists. Sets `isDir` when it does.
bool sdExists(const char *path, bool &isDir);

/// Opens `path` for writing, truncating any existing file, and creates parent
/// directories as needed. Only one write may be open at a time. Returns false
/// if the card is absent or the file could not be opened.
bool sdWriteOpen(const char *path);

/// Appends a chunk to the open write. Returns false on a short write, which
/// leaves the file incomplete -- the caller should abort.
bool sdWriteChunk(const uint8_t *data, size_t len);

/// Closes the open write. Returns the number of bytes written.
uint32_t sdWriteClose();

/// Abandons the open write and deletes the partial file, so a failed upload
/// never leaves a truncated clip that would later fail to parse.
void sdWriteAbort();

} // namespace Storage
