/*
 * IrFramer — shared IR edge-framing for the laser-tag boards.
 *
 * Attaches a CHANGE interrupt to an IR receiver pin, timestamps every edge,
 * and groups edges into frames separated by an idle gap. poll() returns each
 * completed frame as a sequence of edge durations + levels, which callers
 * decode (NEC, Vatos, ...) however they like.
 *
 * Single instance (one IR receiver per board), implemented with file-scope
 * state so the ISR stays in IRAM and lock-free.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace IrFramer {

/// One edge: how long the line held its previous level, and that level.
struct Edge {
  uint32_t durationUs; ///< time spent at the previous level, microseconds
  uint8_t level;       ///< the level that just ended: 1 = HIGH, 0 = LOW
};

/// <summary>Starts framing on the given pin.</summary>
/// <param name="pin">IR receiver output pin (idles HIGH, pulses LOW).</param>
/// <param name="frameGapUs">Idle gap that ends a frame (default 50 ms).</param>
void begin(uint8_t pin, uint32_t frameGapUs = 50000);

/// <summary>
/// Returns the next completed frame, if one is ready. The returned buffer is
/// owned by IrFramer and is valid only until the next poll() call, so process
/// it before polling again.
/// </summary>
/// <param name="edges">Receives a pointer to the frame's edges.</param>
/// <param name="count">Receives the number of edges in the frame.</param>
/// <returns>True if a frame was returned.</returns>
bool poll(const Edge **edges, size_t *count);

/// <summary>Total edges seen since begin() (for diagnostics).</summary>
uint32_t totalEdges();

/// <summary>
/// True while a frame is mid-reception (edges arriving, or within the idle gap
/// after the last edge). Use this to avoid time-critical work — e.g. driving
/// addressable LEDs — during IR reception, which would corrupt edge timing.
/// </summary>
bool receiving();

} // namespace IrFramer
