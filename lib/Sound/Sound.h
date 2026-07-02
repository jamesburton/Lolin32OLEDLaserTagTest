#pragma once
#include <BoardProfile.h>

namespace Sound {

enum class Cue : uint8_t { Hit, Dead, Respawn, Start };

/// Initialises audio from the profile. Piezo and I2sDac are implemented.
/// No-op when audio is None.
void begin(const Board::BoardProfile &p);

void cue(Cue c); // no-op if no audio output is present
bool present();

// --- SFX bank (I2sDac only) ------------------------------------------------
// The bank is the embedded SfxData.h sample set. The caller (which owns the
// team/death assignment config) selects what to play by index. Out-of-range
// indices are a logged no-op. These report the last-played entry for logging.

void        playIndex(int idx); // play bank entry idx; no-op if out of range
void        playRaw(const int16_t *data, size_t samples); // I2sDac only; no-op otherwise
uint8_t     sfxCount();      // bank size
uint8_t     sfxLastIndex();  // index of the most recently played entry
const char *sfxLastName();   // name of the most recently played entry
uint32_t    sfxPlays();      // total plays since boot

} // namespace Sound
