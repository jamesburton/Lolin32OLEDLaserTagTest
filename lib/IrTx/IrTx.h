#pragma once
#include <BoardProfile.h>
#include <ControlProto.h> // ControlProto::TagEvent

namespace IrTx {

/// Configures the IR transmit carrier from the profile's irTxPin. No-op (and
/// present() stays false) when irTxPin < 0.
void begin(const Board::BoardProfile &p);

/// Transmits the event as a Vatos shot (team + damage). No-op if absent.
void fire(const ControlProto::TagEvent &ev);

/// True if the board has an IR transmitter.
bool present();

} // namespace IrTx
