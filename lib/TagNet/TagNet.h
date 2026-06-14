/*
 * TagNet — shared WiFi, OTA, and UDP telemetry for the laser-tag boards.
 *
 * WiFi credentials are set over Serial and stored in NVS (flash), so a headless
 * board can be provisioned without a rebuild:
 *
 *   ssid <network name>      set the pending SSID (value = rest of line)
 *   pass <password>          set the pending password (value = rest of line)
 *   wifi-save                save pending creds to NVS and (re)connect
 *   wifi-clear               erase stored creds and disconnect
 *   wifi-status              print SSID / IP / connection state
 *
 * (See tools/set-wifi.ps1.) Once connected, ArduinoOTA enables wireless flashing
 * and telemetry events are broadcast as one UDP line per event (device-name
 * prefixed) to TagNet::UdpPort, mirrored to Serial. With no creds the device
 * runs offline and network calls are no-ops, so game logic is network-agnostic.
 *
 * Non-WiFi serial lines are forwarded to an optional handler (see onLine), so a
 * board can keep its own serial commands.
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace TagNet {

/// UDP port that telemetry is broadcast to (the PC listener binds here).
constexpr uint16_t UdpPort = 4210;

/// Handler for command lines (from serial, UDP, or HTTP) that aren't WiFi commands.
typedef void (*LineHandler)(const char *line);

/// Provides app-specific status text for the HTTP "/" page.
typedef String (*StatusProvider)();

/// <summary>
/// Loads stored WiFi credentials and connects (if any), then starts OTA and the
/// telemetry socket. Does not block waiting for credentials; provision later
/// over serial.
/// </summary>
/// <param name="hostname">Device/OTA name, e.g. "lasertag-matrix".</param>
void begin(const char *hostname);

/// <summary>Services OTA and processes serial commands; call every loop().</summary>
void handle();

/// <summary>Registers a handler for command lines that aren't WiFi commands.</summary>
/// <param name="handler">Callback, or nullptr to clear.</param>
void onLine(LineHandler handler);

/// <summary>Registers an app status provider shown on the HTTP "/" page.</summary>
/// <param name="provider">Callback returning status text, or nullptr to clear.</param>
void onStatus(StatusProvider provider);

/// <summary>Sends a telemetry line (Serial + broadcast UDP if online).</summary>
/// <param name="line">The event text.</param>
void event(const char *line);

/// <summary>printf-style variant of event().</summary>
/// <param name="fmt">Format string.</param>
void eventf(const char *fmt, ...);

/// <summary>True if WiFi is connected.</summary>
bool online();

} // namespace TagNet
