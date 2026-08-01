#pragma once
// OledStatus — optional I2C status display for the carrier's J9 OLED header.
//
// Designed for the 1.5" SH1107 128x128 module, probed at boot on the carrier's
// I2C pins (GP4=SDA / GP5=SCL, rev1 netlist). Boards without a module detect
// nothing and every call becomes a no-op, so this is safe fleet-wide.
#include <stdint.h>

namespace OledStatus {

/// <summary>
/// Probes I2C for the display (0x3C then 0x3D) and initialises it when found.
/// </summary>
/// <param name="sdaPin">I2C SDA pin (carrier J9: GP4).</param>
/// <param name="sclPin">I2C SCL pin (carrier J9: GP5).</param>
/// <returns>True when a display was found and initialised.</returns>
bool begin(int sdaPin, int sclPin);

/// <summary>True when begin() found a display.</summary>
bool present();

/// <summary>
/// Renders the live status screen: hostname, firmware, IP, team, hp and
/// uptime. Call at ~1 Hz; no-op when no display is present.
/// </summary>
/// <param name="hostname">Board hostname.</param>
/// <param name="fw">Firmware version string.</param>
/// <param name="ip">Current IP address as text.</param>
/// <param name="team">Own team index (0 = none).</param>
/// <param name="hp">Current health.</param>
/// <param name="maxHp">Configured starting health.</param>
/// <param name="uptimeS">Seconds since boot.</param>
void showStatus(const char *hostname, const char *fw, const char *ip, int team,
                int hp, int maxHp, uint32_t uptimeS);

} // namespace OledStatus
