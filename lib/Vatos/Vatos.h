/*
 * Vatos laser-tag IR protocol — decoder/encoder library.
 *
 * Reverse-engineered from a Vatos infrared laser-tag set (see
 * docs/gun-protocol.md). A shot is a 41-edge frame on a ~38 kHz carrier:
 * 21 IR bursts interleaved with 20 gaps, starting and ending with a burst
 * (so even edge indices are bursts, odd are gaps). Each edge is a short
 * (~380 µs = 0) or long (~800 µs = 1) symbol.
 *
 * The frame carries the firing team and the shot's damage (not the weapon —
 * weapons that deal equal damage send identical frames):
 *
 *   bits 22-24  team   (MSB-first: 1=Blue 2=Red 3=Green 4=White)
 *   bits 30-32  damage (MSB-first: 1..4)
 *   bits 37-40  checksum (a fixed function of team+damage)
 *   all other bits are constant framing.
 *
 * This library is platform-independent: decode() takes raw edge durations and
 * encode() produces the bit pattern; carrier generation lives in the caller.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Vatos {

/// Number of edges (bursts + gaps) in a Vatos frame.
constexpr size_t FrameEdges = 41;

/// Durations above this (µs) are "long" (1), at or below are "short" (0).
constexpr uint32_t QuantThresholdUs = 600;

/// Nominal symbol durations used when transmitting.
constexpr uint32_t ShortUs = 380;
constexpr uint32_t LongUs = 800;

/// IR carrier frequency.
constexpr uint32_t CarrierHz = 38000;

/// A decoded (or to-be-transmitted) shot.
struct Shot {
  uint8_t team;   ///< 1=Blue, 2=Red, 3=Green, 4=White
  uint8_t damage; ///< 1..4
};

/// <summary>
/// Decodes an array of edge durations (µs, interleaved burst/gap as captured)
/// into a shot, validating the constant framing and checksum.
/// </summary>
/// <param name="edgeDurationsUs">Edge durations in order; must be FrameEdges long.</param>
/// <param name="count">Number of edges supplied.</param>
/// <param name="out">Receives the decoded team and damage on success.</param>
/// <returns>True if this is a valid Vatos frame.</returns>
bool decode(const uint32_t *edgeDurationsUs, size_t count, Shot &out);

/// <summary>
/// Builds the 41-bit symbol pattern (true = long/1) for a shot.
/// </summary>
/// <param name="shot">The team (1..4) and damage (1..4) to encode.</param>
/// <param name="bits">Receives FrameEdges symbols; even indices are bursts.</param>
/// <returns>True if the team and damage are in range.</returns>
bool encode(const Shot &shot, bool bits[FrameEdges]);

/// <summary>Returns a human-readable team name, or "?" if out of range.</summary>
/// <param name="team">Team number 1..4.</param>
/// <returns>"Blue", "Red", "Green", "White", or "?".</returns>
const char *teamName(uint8_t team);

} // namespace Vatos
