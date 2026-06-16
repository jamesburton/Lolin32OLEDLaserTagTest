#include "Sound.h"
#include <Arduino.h>

namespace Sound {
namespace {
Board::AudioKind kind = Board::AudioKind::None;
int8_t piezo = -1;

void chirp(uint16_t freq, uint16_t ms) {
  if (piezo < 0) return;
  tone(piezo, freq, ms);
}
} // namespace

void begin(const Board::BoardProfile &p) {
  kind = p.audio;
  if (kind == Board::AudioKind::Piezo) {
    piezo = p.piezoPin;
    if (piezo >= 0) pinMode(piezo, OUTPUT);
  }
  // I2sDac: stub — left for a later spec.
}

bool present() { return kind == Board::AudioKind::Piezo && piezo >= 0; }

void cue(Cue c) {
  if (kind != Board::AudioKind::Piezo) return; // DAC stub / None -> silent
  switch (c) {
  case Cue::Hit: chirp(1200, 60); break;
  case Cue::Dead: chirp(300, 400); break;
  case Cue::Respawn: chirp(900, 120); break;
  case Cue::Start: chirp(1600, 150); break;
  }
}

} // namespace Sound
