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

/// <summary>
/// Wiring/colour-order diagnostic: pixels 0-3 lit Red, Green, Blue, White, the
/// rest dark, painted on BOTH the onboard matrix and the external output. If a
/// panel shows a different colour sequence its colour order differs from the
/// configured one. Matrix-only: a no-op on other kinds.
/// </summary>
void ledTest();

/// <summary>
/// What the external WS2812 output (carrier J8 / GP6) renders. The external
/// output has its own frame buffer, independent of the onboard matrix.
/// </summary>
enum class ExtRole : uint8_t {
  Off,    ///< external output dark
  Mirror, ///< copy of the onboard matrix frame (the pre-2.6 behaviour)
  Team,   ///< solid own-team colour
  Pulse,  ///< own-team colour breathing (ongoing ambient effect)
};

/// <summary>Sets the external output's role. Takes effect on the next tick/frame.</summary>
/// <param name="r">The role to render.</param>
void setExtRole(ExtRole r);

/// <summary>
/// Drives the external output's own rendering (Team/Pulse/Off roles). Call
/// every loop iteration; internally throttled to ~30 ms frames and idle for
/// Mirror (which piggybacks on onboard frames). No-op without a matrix.
/// </summary>
/// <param name="nowMs">Current millis().</param>
/// <param name="team">Own-team index for colour lookup (0 = neutral).</param>
void extTick(uint32_t nowMs, int team);

void flashTeam(int team); // solid-fill the team colour (one frame)
void solid(Board::Rgb c);
void dark();
void setBrightness(uint8_t b);
bool present();

} // namespace HitDisplay
