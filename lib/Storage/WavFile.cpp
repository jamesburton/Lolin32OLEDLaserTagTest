#include "WavFile.h"
#include <string.h>

namespace Storage {
namespace {

uint32_t readU32LE(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

uint16_t readU16LE(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

bool tag(const uint8_t *p, const char *four) { return memcmp(p, four, 4) == 0; }

} // namespace

bool parseWav(const uint8_t *buf, size_t len, WavView &out, const char *&err) {
  // RIFF header: "RIFF" <u32 size> "WAVE"
  if (len < 12) { err = "buffer too small for RIFF header"; return false; }
  if (!tag(buf, "RIFF")) { err = "not RIFF"; return false; }
  if (!tag(buf + 8, "WAVE")) { err = "not WAVE"; return false; }

  bool haveFmt = false, haveData = false;
  uint16_t fmtChannels = 0, fmtBits = 0;
  uint32_t fmtRate = 0;
  const int16_t *dataPtr = nullptr;
  uint32_t dataBytes = 0;

  size_t pos = 12;
  while (pos + 8 <= len) {
    const uint8_t *chunkId = buf + pos;
    uint32_t chunkSize = readU32LE(buf + pos + 4);
    size_t chunkBody = pos + 8;

    if (chunkSize > len - chunkBody) {
      err = "chunk exceeds buffer";
      return false;
    }

    if (tag(chunkId, "fmt ")) {
      if (chunkSize < 16) { err = "fmt chunk too small"; return false; }
      uint16_t audioFormat = readU16LE(buf + chunkBody);
      if (audioFormat != 1 /* PCM */) { err = "non-PCM format"; return false; }
      fmtChannels = readU16LE(buf + chunkBody + 2);
      fmtRate     = readU32LE(buf + chunkBody + 4);
      fmtBits     = readU16LE(buf + chunkBody + 14);
      haveFmt = true;
    } else if (tag(chunkId, "data")) {
      dataPtr   = reinterpret_cast<const int16_t *>(buf + chunkBody);
      dataBytes = chunkSize;
      haveData = true;
    }

    // Chunks are word-aligned: odd sizes have a padding byte.
    pos = chunkBody + chunkSize + (chunkSize & 1);
  }

  if (!haveFmt) { err = "missing fmt chunk"; return false; }
  if (!haveData) { err = "missing data chunk"; return false; }
  if (fmtRate != 16000) { err = "unsupported sample rate (need 16000)"; return false; }
  if (fmtBits != 16) { err = "unsupported bit depth (need 16)"; return false; }
  if (fmtChannels != 1) { err = "unsupported channel count (need mono)"; return false; }
  if (dataBytes < 2 || (dataBytes % 2) != 0) {
    err = "data chunk not a whole number of 16-bit samples";
    return false;
  }

  out.sampleRate     = fmtRate;
  out.bitsPerSample  = (uint8_t)fmtBits;
  out.channels       = (uint8_t)fmtChannels;
  out.pcm            = dataPtr;
  out.sampleCount    = dataBytes / 2;
  return true;
}

} // namespace Storage
