#pragma once
#include <stddef.h>
#include <stdint.h>

namespace Storage {

/// A validated view into a caller-owned WAV PCM buffer. `pcm` points inside
/// the buffer passed to parseWav — it is not a copy and is only valid as
/// long as that buffer is alive.
struct WavView {
  uint32_t sampleRate;
  uint8_t  bitsPerSample;
  uint8_t  channels;
  const int16_t *pcm;
  size_t   sampleCount; // per-channel sample count
};

/// Parses a RIFF/WAVE buffer in place (no copy, no allocation). Walks the
/// "fmt " and "data" chunks. Requires exactly 16000 Hz / 16-bit / mono
/// (matches the embedded SfxData.h bank) -- anything else is rejected, not
/// converted. On success returns true and fills `out`. On failure returns
/// false and sets `err` to a short reason string (points into a static
/// string literal, safe to log directly, do not free).
bool parseWav(const uint8_t *buf, size_t len, WavView &out, const char *&err);

} // namespace Storage
