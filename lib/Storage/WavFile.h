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

/// <summary>
/// Parses only the RIFF header, from a small prefix of the file.
/// </summary>
/// <param name="buf">The first bytes of the file (512 is ample).</param>
/// <param name="len">How many bytes <paramref name="buf"/> holds.</param>
/// <param name="out">Receives format and sampleCount; `pcm` is left null.</param>
/// <param name="dataOffset">Receives the byte offset of the PCM data.</param>
/// <param name="err">Set to a static reason string on failure.</param>
/// <returns>True when the header is valid and the format is supported.</returns>
/// <remarks>
/// Exists so a clip can be STREAMED rather than loaded whole. parseWav needs
/// the entire file in RAM, which caps clips at whatever heap is free (~270 KB
/// here) — a 10 s clip is 313 KB and simply cannot be loaded. This reads the
/// header from a small prefix so playback can then pull PCM in chunks, which
/// removes the length limit entirely.
/// </remarks>
bool parseWavHeader(const uint8_t *buf, size_t len, WavView &out,
                    size_t &dataOffset, const char *&err);

} // namespace Storage
