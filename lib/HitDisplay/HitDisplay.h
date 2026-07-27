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
/// Like idle(), but the matrix's central 4 columns act as a health bar: only
/// round(hp/maxHp * (4*H)) of those central cells stay lit (rainbow), the rest
/// go dark, draining from the top. Falls back to idle() for non-matrix displays.
void idleWithHealth(int hp, int maxHp);
/// Chase "active target" animation: a 4-pixel arc chasing around the 28-pixel
/// perimeter of the 8x8 matrix in colour c. Advance `phase` (0..27) per frame.
/// Non-matrix displays fall back to a steady solid colour.
void spinFrame(Board::Rgb c, uint8_t phase);

/// Paints a scoreboard grid (row-major y*8+x; 0 = off, else the team index
/// whose colour comes from the begin() colour map). Channels are scaled
/// num/den so dormant boards can render dim (e.g. 1/4) and gameover full (1/1).
/// Matrix-only: a no-op on other display kinds.
void scoreboard(const uint8_t grid[64], uint8_t num, uint8_t den);

void flashTeam(int team); // solid-fill the team colour (one frame)
void solid(Board::Rgb c);
void dark();
void setBrightness(uint8_t b);
bool present();

} // namespace HitDisplay
