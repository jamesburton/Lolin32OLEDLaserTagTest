#include "SdPath.h"

#include <string.h>

namespace Storage {

bool isSafeSdPath(const char *path) {
  if (path == nullptr || path[0] != '/') {
    return false;
  }

  const size_t len = strnlen(path, MaxSdPathLength);
  if (len == 0 || len >= MaxSdPathLength) {
    return false; // empty, or unterminated within the bound
  }

  for (size_t i = 0; i < len; i++) {
    const unsigned char c = (unsigned char)path[i];
    // Printable ASCII only: control bytes and high-bit bytes have no business
    // in a path we are about to hand to the SD library.
    if (c < 0x20 || c > 0x7E || c == '\\') {
      return false;
    }
  }

  // Reject any ".." SEGMENT. A substring test would wrongly reject a
  // legitimate name like "/sfx/a..b.wav", so each segment is compared whole.
  const char *seg = path + 1;
  while (true) {
    const char *slash = strchr(seg, '/');
    const size_t segLen = slash != nullptr ? (size_t)(slash - seg) : strlen(seg);
    if (segLen == 2 && seg[0] == '.' && seg[1] == '.') {
      return false;
    }

    if (slash == nullptr) {
      break;
    }

    seg = slash + 1;
  }

  return true;
}

} // namespace Storage
