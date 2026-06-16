#pragma once
#include <BoardProfile.h>

namespace BoardNvs {

/// Reads whitelisted overrides from NVS (namespace "board") and applies them to
/// `p` via Board::applyOverride (invalid values fall back silently).
void loadOverrides(Board::BoardProfile &p);

/// Handles a `cfg <key> <value>` line: validates via Board::applyOverride,
/// persists to NVS, prints the result on Serial. Returns true if the line was a
/// cfg command (handled), false otherwise (caller continues its own parsing).
bool handleCfgLine(const char *line);

} // namespace BoardNvs
