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
#include <WebServer.h>
#include <stdint.h>

namespace TagNet {

/// UDP port that telemetry is broadcast to (the PC listener binds here).
constexpr uint16_t UdpPort = 4210;

/// Handler for command lines (from serial, inbound UDP, or HTTP) that aren't
/// WiFi commands. Inbound UDP on UdpPort is pumped through here too, so
/// host-broadcast control lines (e.g. `CTL start`) reach the app.
typedef void (*LineHandler)(const char *line);

/// Provides app-specific status text for the HTTP "/" page.
typedef String (*StatusProvider)();

/// <summary>
/// Hook invoked once, during service startup, after the built-in routes are
/// registered but before the server begins listening. The app registers its
/// own HTTP routes here via httpServer(). Called only when WiFi connects.
/// </summary>
typedef void (*HttpSetup)();

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

/// <summary>
/// Registers a hook that adds app HTTP routes during service startup. The hook
/// runs after the built-in routes and before the server starts; use
/// httpServer() inside it to register routes. If services are already running
/// (rare), the hook is invoked immediately.
/// </summary>
/// <param name="setup">Callback that registers routes, or nullptr to clear.</param>
void onHttpSetup(HttpSetup setup);

/// <summary>
/// The shared HTTP server, for registering app routes from an onHttpSetup hook
/// (e.g. the `/api/*` REST surface). Routes should be added before the server
/// starts; the onHttpSetup hook fires at the correct time.
/// </summary>
/// <returns>Reference to the WebServer instance.</returns>
WebServer &httpServer();

/// <summary>
/// The device id: the lower 3 bytes of the WiFi MAC as 6-char lowercase hex
/// (e.g. "a1b2c3"). Stable across reboots, used as the wire `id` / `deviceId`.
/// Valid after begin().
/// </summary>
/// <returns>NUL-terminated 6-char lowercase hex string.</returns>
const char *deviceId();

/// <summary>The device/OTA hostname passed to begin().</summary>
/// <returns>NUL-terminated hostname.</returns>
const char *hostname();

/// <summary>Sends a telemetry line (Serial + broadcast UDP if online).</summary>
/// <param name="line">The event text.</param>
void event(const char *line);

/// <summary>printf-style variant of event().</summary>
/// <param name="fmt">Format string.</param>
void eventf(const char *fmt, ...);

/// <summary>True if WiFi is connected.</summary>
bool online();

} // namespace TagNet
