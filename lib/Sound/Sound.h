#pragma once
#include <BoardProfile.h>

namespace Sound {

enum class Cue : uint8_t { Hit, Dead, Respawn, Start };

/// Initialises audio from the profile. Piezo and I2sDac are implemented.
/// No-op when audio is None.
void begin(const Board::BoardProfile &p);

void cue(Cue c); // no-op if no audio output is present
bool present();

/// <summary>
/// Sets playback volume as a fraction of the master software gain: 255 = the
/// full gain (kVolume), 0 = silent. Applied to every subsequent sample on both
/// the one-shot and streaming paths; takes effect mid-clip. No-op on piezo.
/// Safe to call from core 1 while the playback task streams on core 0 (the
/// gain is a single aligned 32-bit store — see the note in Sound.cpp).
/// </summary>
/// <param name="v">Volume 0..255.</param>
void setVolume(uint8_t v);

// --- SFX bank (I2sDac only) ------------------------------------------------
// The bank is the embedded SfxData.h sample set. The caller (which owns the
// team/death assignment config) selects what to play by index. Out-of-range
// indices are a logged no-op. These report the last-played entry for logging.

// Bank plays are ASYNCHRONOUS: playIndex queues the entry for the playback
// task (core 0) and returns immediately. Policy is one clip at a time; a cue
// requested while something is playing sets the abort flag so the current
// clip stops within one chunk and the cue plays next — game feedback (hit and
// death sirens) is never silently dropped behind a long file clip.
void        playIndex(int idx); // queue bank entry idx; no-op if out of range
void        playRaw(const int16_t *data, size_t samples); // I2sDac only; no-op otherwise

// --- Asynchronous file-clip playback (I2sDac only) --------------------------
// All playback runs on a dedicated FreeRTOS task pinned to core 0 (the
// Arduino loop owns core 1), created by begin(). Sound owns the task and the
// I2S output but stays filesystem-agnostic: the application registers the
// callback that actually reads and streams the clip.

/// <summary>
/// Application callback that streams one file clip through
/// streamBegin/streamChunk/streamEnd. Invoked ON the playback task (core 0);
/// it must poll abortRequested() between chunks and stop promptly when it
/// returns true.
/// </summary>
/// <param name="path">Absolute clip path, as passed to playFileAsync.</param>
/// <returns>True when the clip streamed (or was aborted mid-play); false when
/// it could not be opened or parsed.</returns>
using FileStreamFn = bool (*)(const char *path);

/// <summary>
/// Registers the file-clip streamer callback. Call once, before the first
/// playFileAsync. Without a registered streamer, file plays are rejected.
/// </summary>
/// <param name="fn">Streamer to run on the playback task.</param>
void setFileStreamer(FileStreamFn fn);

/// <summary>
/// Queues a file clip for asynchronous playback and returns immediately.
/// One clip at a time: a request while playback is busy is rejected, so the
/// caller can report "busy" synchronously. The caller should validate the
/// path (existence, WAV header) BEFORE queueing — the task only streams.
/// </summary>
/// <param name="path">Absolute clip path handed to the registered streamer.</param>
/// <returns>True when queued; false when busy, unconfigured or path too long.</returns>
bool playFileAsync(const char *path);

/// <summary>True while the playback task is playing or has work queued.</summary>
bool busy();

/// <summary>
/// True when the currently streaming clip should stop. Polled by writeMono
/// between DMA chunks and by the file streamer between reads.
/// </summary>
bool abortRequested();

// Streaming playback, for clips too large to hold in RAM. Call streamBegin,
// then streamChunk repeatedly, then streamEnd. A 10 s clip is 313 KB against
// ~270 KB of free heap, so loading whole is not merely wasteful but impossible.
bool        streamBegin();
void        streamChunk(const int16_t *data, size_t samples);
void        streamEnd();
uint8_t     sfxCount();      // bank size
uint8_t     sfxLastIndex();  // index of the most recently played entry
const char *sfxLastName();   // name of the most recently played entry
uint32_t    sfxPlays();      // total plays since boot

} // namespace Sound
