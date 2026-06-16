#pragma once
// NOTE — FastLED limitation: addLeds<> requires the data pin as a
// compile-time template constant, so the WS2812 pin is fixed at GPIO 14
// (both current boards use GPIO 14 for their matrix). The BoardProfile field
// matrixPin is stored and validated by Board::applyOverride but cannot be
// applied to the literal template argument here. matrixOrder IS honoured at
// runtime via the GRB/RGB branch in begin().
#include <BoardProfile.h>
#include <stdint.h>

namespace HitDisplay {

/// Supplies the "#RRGGBB" colour for a team index, or nullptr/"" if unknown.
typedef const char *(*TeamColourFn)(int team);

/// Initialises the configured hit display (matrix or RGB LED). No-op when the
/// profile's hitDisplay is None.
void begin(const Board::BoardProfile &p, TeamColourFn colours);

void idle();              // matrix: flowing rainbow; RGB LED: off
void flashTeam(int team); // solid-fill the team colour (one frame)
void solid(Board::Rgb c);
void dark();
void setBrightness(uint8_t b);
bool present();

} // namespace HitDisplay
