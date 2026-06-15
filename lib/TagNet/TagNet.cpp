#include "TagNet.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include <cstdarg>

namespace TagNet {
namespace {

constexpr uint32_t ConnectTimeoutMs = 10000;

WiFiUDP udp;
Preferences prefs;
WebServer server(80);
bool isOnline = false;
bool servicesStarted = false;
char deviceName[32] = "tag";
char devId[8] = "000000"; // lower 3 bytes of WiFi MAC, lowercase hex
LineHandler lineHandler = nullptr;
StatusProvider statusProvider = nullptr;
HttpSetup httpSetup = nullptr;

// Inbound UDP packet assembly (CTL control lines arrive here)
char udpBuf[160];

// Serial line assembly
char lineBuf[160];
size_t lineLen = 0;

// Pending credentials being entered over serial (committed by wifi-save)
String pendingSsid;
String pendingPass;

// Subnet-directed broadcast address (host bits all 1)
IPAddress broadcastAddr() {
  const IPAddress ip = WiFi.localIP();
  const IPAddress mask = WiFi.subnetMask();
  IPAddress bc;
  for (int i = 0; i < 4; i++) {
    bc[i] = ip[i] | (~mask[i] & 0xFF);
  }
  return bc;
}

void processLine(char *line); // defined below; used by the HTTP handlers

// Run a command string (from HTTP/UDP) through the same parser as serial.
void runCommand(const char *cmd) {
  char buf[160];
  strncpy(buf, cmd, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  processLine(buf);
}

void startServices() {
  ArduinoOTA.setHostname(deviceName);
  ArduinoOTA.begin();
  udp.begin(UdpPort);

  // HTTP control/status endpoint for a central controller:
  //   GET /            -> device + app status
  //   GET /cmd?c=<cmd> -> run a command (e.g. "bright 8", "hit 1 2")
  server.on("/", []() {
    String s = String("device=") + deviceName +
               "\nip=" + WiFi.localIP().toString() +
               "\nuptime=" + (millis() / 1000) + "s\nrssi=" + WiFi.RSSI() + "\n";
    if (statusProvider) {
      s += statusProvider();
    }
    server.send(200, "text/plain", s);
  });
  server.on("/cmd", []() {
    const String c = server.arg("c");
    runCommand(c.c_str());
    server.send(200, "text/plain", "OK: " + c + "\n");
  });

  // Let the app register its own routes (e.g. the /api/* REST surface) before
  // the server starts listening.
  if (httpSetup) {
    httpSetup();
  }
  server.begin();

  servicesStarted = true;
}

// Drain any inbound UDP packets, feeding each through the same line parser as
// serial. This is how host-broadcast control lines (e.g. `CTL start`) reach the
// app's onLine handler. Our own and peers' hostname-prefixed HB/EVT broadcasts
// also arrive here; they simply don't match any command and drop cleanly.
void pumpUdp() {
  int size;
  while ((size = udp.parsePacket()) > 0) {
    int n = udp.read(udpBuf, sizeof(udpBuf) - 1);
    if (n <= 0) {
      continue;
    }
    udpBuf[n] = '\0';
    processLine(udpBuf);
  }
}

// Connect using the given credentials, blocking up to ConnectTimeoutMs.
bool connect(const String &ssid, const String &pass) {
  if (ssid.isEmpty()) {
    return false;
  }
  Serial.printf("TagNet connecting to \"%s\"...\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), pass.c_str());
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < ConnectTimeoutMs) {
    delay(100);
  }

  isOnline = WiFi.status() == WL_CONNECTED;
  if (isOnline) {
    if (!servicesStarted) {
      startServices();
    }
    Serial.printf("TagNet online: %s @ %s\n", deviceName,
                  WiFi.localIP().toString().c_str());
  } else {
    Serial.println(F("TagNet: connection failed"));
  }
  return isOnline;
}

void printStatus() {
  if (isOnline) {
    Serial.printf("wifi-status: connected ssid=%s ip=%s\n",
                  WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  } else {
    const String saved = prefs.getString("ssid", "");
    Serial.printf("wifi-status: offline (saved ssid=%s)\n",
                  saved.isEmpty() ? "<none>" : saved.c_str());
  }
}

// Returns the text after a command prefix (e.g. "ssid "), or nullptr if the
// line does not start with that prefix.
const char *valueAfter(const char *line, const char *prefix) {
  const size_t n = strlen(prefix);
  return strncmp(line, prefix, n) == 0 ? line + n : nullptr;
}

// Handle a WiFi provisioning command. Returns true if the line was a command.
bool handleWifiCommand(const char *line) {
  if (const char *v = valueAfter(line, "ssid ")) {
    pendingSsid = v;
    Serial.printf("ssid set: %s\n", pendingSsid.c_str());
    return true;
  }
  if (const char *v = valueAfter(line, "pass ")) {
    pendingPass = v;
    Serial.println(F("pass set"));
    return true;
  }
  if (strcmp(line, "wifi-save") == 0) {
    prefs.putString("ssid", pendingSsid);
    prefs.putString("pass", pendingPass);
    Serial.println(F("wifi-save: stored"));
    connect(pendingSsid, pendingPass);
    return true;
  }
  if (strcmp(line, "wifi-clear") == 0) {
    prefs.remove("ssid");
    prefs.remove("pass");
    WiFi.disconnect();
    isOnline = false;
    Serial.println(F("wifi-clear: erased"));
    return true;
  }
  if (strcmp(line, "wifi-status") == 0) {
    printStatus();
    return true;
  }
  return false;
}

void processLine(char *line) {
  // Trim trailing whitespace
  size_t len = strlen(line);
  while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t')) {
    line[--len] = '\0';
  }
  if (len == 0) {
    return;
  }
  if (handleWifiCommand(line)) {
    return;
  }
  if (lineHandler) {
    lineHandler(line);
  }
}

void pumpSerial() {
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (lineLen > 0) {
        lineBuf[lineLen] = '\0';
        processLine(lineBuf);
        lineLen = 0;
      }
    } else if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = c;
    }
  }
}

} // namespace

void begin(const char *hostname) {
  strncpy(deviceName, hostname, sizeof(deviceName) - 1);
  deviceName[sizeof(deviceName) - 1] = '\0';

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname);

  // deviceId = lower 3 bytes of the WiFi MAC, lowercase hex, 6 chars. Stable
  // across reboots; used as the wire `id` / `deviceId`.
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(devId, sizeof(devId), "%02x%02x%02x", mac[3], mac[4], mac[5]);

  prefs.begin("tagnet", false);
  const String ssid = prefs.getString("ssid", "");
  const String pass = prefs.getString("pass", "");
  if (!ssid.isEmpty()) {
    connect(ssid, pass);
  } else {
    Serial.println(F("TagNet: no stored WiFi creds — set via serial "
                     "(ssid/pass/wifi-save)"));
  }
}

void handle() {
  if (isOnline) {
    ArduinoOTA.handle();
    server.handleClient();
    pumpUdp();
  }
  pumpSerial();
}

void onLine(LineHandler handler) { lineHandler = handler; }

void onStatus(StatusProvider provider) { statusProvider = provider; }

void onHttpSetup(HttpSetup setup) {
  httpSetup = setup;
  // If services are already up, run it now so late registration still works.
  if (servicesStarted && setup) {
    setup();
  }
}

WebServer &httpServer() { return server; }

const char *deviceId() { return devId; }

const char *hostname() { return deviceName; }

void event(const char *line) {
  Serial.print(deviceName);
  Serial.print(' ');
  Serial.println(line);

  if (!isOnline) {
    return;
  }
  udp.beginPacket(broadcastAddr(), UdpPort);
  udp.print(deviceName);
  udp.print(' ');
  udp.print(line);
  udp.endPacket();
}

void eventf(const char *fmt, ...) {
  char buf[160];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  event(buf);
}

bool online() { return isOnline; }

} // namespace TagNet
