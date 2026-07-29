#pragma once
#include <stddef.h>

namespace Storage {

/// Maximum accepted SD path length, including the leading '/' and the NUL.
/// FAT/SD paths are short; a bound also keeps the HTTP handlers' buffers
/// fixed-size.
constexpr size_t MaxSdPathLength = 96;

/// <summary>
/// Validates a caller-supplied SD path before it reaches the filesystem.
/// </summary>
/// <param name="path">The candidate path, NUL-terminated.</param>
/// <returns>True when the path is safe to open.</returns>
/// <remarks>
/// The REST surface lets anything on the LAN name a file, so this is the
/// single gate that keeps a request from escaping the card's tree or wedging
/// the SD library. Rejects: null/empty; anything not starting with '/'; any
/// '..' segment (directory traversal); backslashes (so a Windows-style path
/// can never be half-interpreted); non-printable or high-bit bytes; and
/// anything at or beyond <see cref="MaxSdPathLength"/>. Deliberately
/// permissive about which directories exist — that is the filesystem's answer
/// to give, not this function's.
/// </remarks>
bool isSafeSdPath(const char *path);

} // namespace Storage
