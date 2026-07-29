#pragma once
#include <stddef.h>
#include <stdint.h>

namespace Storage {

/// <summary>
/// On-board flash file storage for sound clips, backed by LittleFS on the
/// partition table's 1.375 MB data partition.
/// </summary>
/// <remarks>
/// <para>
/// This is the PRIMARY clip store. The 4 MB flash map already reserves
/// `spiffs` (0x290000, 0x160000) and nothing else uses it, so clips cost no
/// firmware space: the app partition is separately ~89% full and a 10 s clip
/// (313 KB) would not fit there even if it were desirable to bake it in.
/// </para>
/// <para>
/// Chosen over microSD because it needs no socket, no breakout module and no
/// wiring — the three things that actually failed. At 16 kHz mono the
/// partition holds roughly four 10-second clips, which is the whole
/// requirement. The microSD code remains for diagnostics and for a future
/// board that wants bulk audio.
/// </para>
/// </remarks>

/// Mounts the flash filesystem, formatting it on first use. Safe to call more
/// than once. Returns false only if the partition is missing or unusable.
bool fsBegin();

/// True once the filesystem is mounted.
bool fsMounted();

/// Total and used bytes of the flash partition. Zeroed when not mounted.
bool fsUsage(size_t &totalBytes, size_t &usedBytes);

/// One entry from a directory listing.
struct FsEntry {
  const char *name; ///< Entry name only, not a full path.
  uint32_t size;    ///< Size in bytes; 0 for a directory.
  bool isDir;
};

/// Lists entries directly under `path`. Returns the entry count.
size_t fsList(const char *path, void (*onEntry)(const FsEntry &entry, void *ctx),
              void *ctx);

/// Reads a whole file into a heap buffer the caller must free(). Returns
/// nullptr and sets len=0 on any failure.
uint8_t *fsReadFile(const char *path, size_t &len);

/// Opens a file for streaming reads. Only one read may be open at a time.
/// Sets `size` to the file length. Enables playing clips larger than free RAM.
bool fsOpenRead(const char *path, size_t &size);

/// Reads up to `len` bytes from the open read, returning the count read.
size_t fsRead(uint8_t *buf, size_t len);

/// Moves the open read to an absolute byte offset.
bool fsSeek(size_t pos);

/// Closes the open read.
void fsCloseRead();

/// True when the path exists; sets isDir accordingly.
bool fsExists(const char *path, bool &isDir);

/// Deletes a file. Directories are refused — removing one implicitly could
/// discard a whole sound bank on a single mistyped path.
bool fsDelete(const char *path);

/// Opens a file for writing, truncating it and creating parent directories.
/// Only one write may be open at a time.
bool fsWriteOpen(const char *path);

/// Appends to the open write. False on a short write (out of space).
bool fsWriteChunk(const uint8_t *data, size_t len);

/// Closes the open write, returning bytes written.
uint32_t fsWriteClose();

/// Abandons the open write and deletes the partial file, so a failed upload
/// never leaves a truncated clip that would fail later at play time.
void fsWriteAbort();

} // namespace Storage
