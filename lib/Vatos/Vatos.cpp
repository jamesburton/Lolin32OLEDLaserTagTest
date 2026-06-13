#include "Vatos.h"

namespace Vatos {

// Constant framing with field positions zeroed. Layout (41 chars):
//   [0-21] preamble | [22-24] team | [25-29] sep | [30-32] damage |
//   [33-36] sep | [37-40] checksum
// The team/damage/checksum spans are overwritten by encode() and ignored by
// decode()'s framing check.
static const char Template[FrameEdges + 1] =
    "1000000001010101000000" "000" "00000" "000" "0000" "0000";

// Checksum nibble per [team][damage] (1-based; index 0 unused). Derived from
// the captured 4-team x 4-damage matrix; the checksum is a fixed nonlinear
// function of team and damage, so a lookup is both simplest and exact.
static const uint8_t Checksum[5][5] = {
    {0, 0, 0, 0, 0},
    {0, 6, 7, 8, 9},   // Blue
    {0, 7, 8, 9, 11},  // Red
    {0, 8, 9, 11, 12}, // Green
    {0, 9, 11, 12, 13} // White
};

// Field bit ranges
static bool isFieldBit(size_t i) {
  return (i >= 22 && i <= 24) || (i >= 30 && i <= 32) || (i >= 37 && i <= 40);
}

bool decode(const uint32_t *edgeDurationsUs, size_t count, Shot &out) {
  if (count != FrameEdges) {
    return false;
  }

  bool bits[FrameEdges];
  for (size_t i = 0; i < FrameEdges; i++) {
    bits[i] = edgeDurationsUs[i] > QuantThresholdUs;
  }

  // Every non-field bit must match the constant framing
  for (size_t i = 0; i < FrameEdges; i++) {
    if (!isFieldBit(i) && bits[i] != (Template[i] == '1')) {
      return false;
    }
  }

  const uint8_t team = (bits[22] << 2) | (bits[23] << 1) | bits[24];
  const uint8_t damage = (bits[30] << 2) | (bits[31] << 1) | bits[32];
  if (team < 1 || team > 4 || damage < 1 || damage > 4) {
    return false;
  }

  const uint8_t chk =
      (bits[37] << 3) | (bits[38] << 2) | (bits[39] << 1) | bits[40];
  if (chk != Checksum[team][damage]) {
    return false;
  }

  out.team = team;
  out.damage = damage;
  return true;
}

bool encode(const Shot &shot, bool bits[FrameEdges]) {
  if (shot.team < 1 || shot.team > 4 || shot.damage < 1 || shot.damage > 4) {
    return false;
  }

  for (size_t i = 0; i < FrameEdges; i++) {
    bits[i] = (Template[i] == '1');
  }

  // 3-bit MSB-first team and damage fields
  bits[22] = (shot.team >> 2) & 1;
  bits[23] = (shot.team >> 1) & 1;
  bits[24] = shot.team & 1;
  bits[30] = (shot.damage >> 2) & 1;
  bits[31] = (shot.damage >> 1) & 1;
  bits[32] = shot.damage & 1;

  // 4-bit MSB-first checksum
  const uint8_t chk = Checksum[shot.team][shot.damage];
  bits[37] = (chk >> 3) & 1;
  bits[38] = (chk >> 2) & 1;
  bits[39] = (chk >> 1) & 1;
  bits[40] = chk & 1;
  return true;
}

const char *teamName(uint8_t team) {
  switch (team) {
  case 1:
    return "Blue";
  case 2:
    return "Red";
  case 3:
    return "Green";
  case 4:
    return "White";
  default:
    return "?";
  }
}

} // namespace Vatos
