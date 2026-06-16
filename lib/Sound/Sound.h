#pragma once
#include <BoardProfile.h>

namespace Sound {

enum class Cue : uint8_t { Hit, Dead, Respawn, Start };

/// Initialises audio from the profile. Piezo is implemented; I2sDac is a stub
/// (no-op). No-op when audio is None.
void begin(const Board::BoardProfile &p);

void cue(Cue c); // no-op if no audio output is present
bool present();

} // namespace Sound
