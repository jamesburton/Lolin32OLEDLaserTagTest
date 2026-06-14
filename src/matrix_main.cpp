/*
 * ESP32-S3-Matrix — laser-tag target.
 *
 * Idles showing a flowing rainbow on the on-board 8x8 WS2812 matrix. When a
 * Vatos shot is received it flashes the firing team's colour a few times, then
 * goes dark for a random 5-15 s ("dead"), then resumes the rainbow.
 *
 * WiFi (config portal), OTA, and UDP telemetry are provided by TagNet.
 *
 * Wiring: IR receiver (VS1838B) OUT -> GPIO1; matrix data is GPIO14 (on-board).
 */

#include <Arduino.h>
#include <FastLED.h>
#include <driver/gpio.h>

#include <IrFramer.h>
#include <TagNet.h>
#include <Vatos.h>

// On-board 8x8 WS2812 matrix data pin
#define MATRIX_PIN 14
#define NUM_LEDS 64

// IR receiver output (free header pin; GPIO10-14 are taken by IMU/matrix)
#define IR_PIN 1

// Activity LED: pulses on every received IR frame. Active-high, resistor-less,
// so driven at minimum drive strength (~5mA) to protect the pin.
#define ACT_LED_PIN 7
constexpr uint32_t LedPulseMs = 80;

// Hit response timing
constexpr uint8_t FlashCount = 4;       // colour blinks on a hit
constexpr uint32_t FlashOnMs = 150;
constexpr uint32_t FlashOffMs = 150;
constexpr uint32_t DarkMinMs = 1000;    // "dead" time after a hit (testing)
constexpr uint32_t DarkMaxMs = 5000;
// Global brightness (0-255). Kept low to reduce optical interference with the
// adjacent IR receiver; tune live with the serial command "bright <0-255>".
uint8_t brightness = 13;                // ~5%, also capped by the power limiter

CRGB leds[NUM_LEDS];

enum class Mode { Rainbow, Flash, Dark };
Mode mode = Mode::Rainbow;

uint8_t rainbowHue = 0;
CRGB hitColour = CRGB::White;
uint8_t flashesLeft = 0;
bool flashOn = false;
uint32_t nextEventMs = 0; // next flash toggle, or end of the dark period
uint32_t ledOffAtMs = 0;  // activity LED off time (0 = off)
uint32_t hitCount = 0;
bool debugFrames = false; // when on, broadcast every raw frame over UDP

// Map a Vatos team to its colour.
CRGB teamColour(uint8_t team) {
  switch (team) {
  case 1:
    return CRGB::Blue;
  case 2:
    return CRGB::Red;
  case 3:
    return CRGB::Green;
  case 4:
    return CRGB::White;
  default:
    return CRGB::Magenta;
  }
}

void showSolid(const CRGB &colour) {
  fill_solid(leds, NUM_LEDS, colour);
  FastLED.show();
}

// Begin the hit response: flash the team colour, then schedule the first toggle.
void onHit(const Vatos::Shot &shot) {
  hitColour = teamColour(shot.team);
  flashesLeft = FlashCount;
  flashOn = true;
  mode = Mode::Flash;
  hitCount++;
  showSolid(hitColour);
  nextEventMs = millis() + FlashOnMs;
  TagNet::eventf("hit team=%u(%s) dmg=%u", shot.team, Vatos::teamName(shot.team),
                 shot.damage);
}

// App status for the HTTP "/" page.
String matrixStatus() {
  const char *m = mode == Mode::Rainbow  ? "rainbow"
                  : mode == Mode::Flash  ? "flash"
                                         : "dark";
  char buf[96];
  snprintf(buf, sizeof(buf), "mode=%s brightness=%u hits=%lu debug=%d\n", m,
           brightness, (unsigned long)hitCount, debugFrames ? 1 : 0);
  return String(buf);
}

// Serial command handler (non-WiFi lines). "bright <0-255>" tunes the matrix
// brightness live so the IR-vs-LED interference can be dialled in without a
// reflash.
void onSerialLine(const char *line) {
  if (strncmp(line, "bright ", 7) == 0) {
    brightness = (uint8_t)constrain(atoi(line + 7), 0, 255);
    FastLED.setBrightness(brightness);
    FastLED.show();
    Serial.printf("brightness=%u\n", brightness);
  } else if (strncmp(line, "hit ", 4) == 0) {
    // Simulate a hit for testing without the gun: "hit <team> <dmg>"
    int t = 0, d = 0;
    if (sscanf(line + 4, "%d %d", &t, &d) == 2 && t >= 1 && t <= 4 && d >= 1 &&
        d <= 4 && mode == Mode::Rainbow) {
      onHit({static_cast<uint8_t>(t), static_cast<uint8_t>(d)});
    }
  } else if (strncmp(line, "debug ", 6) == 0) {
    debugFrames = atoi(line + 6) != 0; // broadcast raw frames over UDP
  }
}

void setup() {
  Serial.begin(115200);

  // This matrix is RGB-ordered (not the usual GRB) — with GRB, Red showed green.
  FastLED.addLeds<WS2812B, MATRIX_PIN, RGB>(leds, NUM_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 500); // keep within USB current
  FastLED.setBrightness(brightness);
  showSolid(CRGB(0, 0, 8)); // dim blue: starting / WiFi config

  // Resistor-less activity LED: minimum drive strength protects the pin
  pinMode(ACT_LED_PIN, OUTPUT);
  gpio_set_drive_capability(static_cast<gpio_num_t>(ACT_LED_PIN),
                            GPIO_DRIVE_CAP_0);
  digitalWrite(ACT_LED_PIN, LOW);

  TagNet::begin("lasertag-matrix");
  TagNet::onLine(onSerialLine);   // bright / hit / debug commands
  TagNet::onStatus(matrixStatus); // HTTP "/" status
  IrFramer::begin(IR_PIN);
  randomSeed(esp_random());

  mode = Mode::Rainbow;
  TagNet::event("ready");
}

void loop() {
  TagNet::handle();

  // Switch the activity LED off once its pulse has elapsed
  if (ledOffAtMs != 0 && millis() >= ledOffAtMs) {
    digitalWrite(ACT_LED_PIN, LOW);
    ledOffAtMs = 0;
  }

  // Register hits only while idling in rainbow mode (ignore while flashing/dead)
  const IrFramer::Edge *edges;
  size_t n;
  while (IrFramer::poll(&edges, &n)) {
    // Pulse the activity LED on every received frame
    digitalWrite(ACT_LED_PIN, HIGH);
    ledOffAtMs = millis() + LedPulseMs;

    // Attempt to decode every frame (for diagnostics); act only in rainbow mode
    Vatos::Shot shot;
    bool ok = false;
    if (n == Vatos::FrameEdges) {
      uint32_t durations[Vatos::FrameEdges];
      for (size_t i = 0; i < n; i++) {
        durations[i] = edges[i].durationUs;
      }
      ok = Vatos::decode(durations, n, shot);
    }

    if (debugFrames) {
      String line = "frame n=" + String((unsigned)n);
      line += ok ? " dec=" + String(shot.team) + ":" + String(shot.damage)
                 : String(" dec=none");
      line += " data=";
      for (size_t i = 0; i < n; i++) {
        line += (edges[i].level ? 'H' : 'L');
        line += edges[i].durationUs;
        if (i + 1 < n) {
          line += ',';
        }
      }
      TagNet::event(line.c_str());
    }

    if (ok && mode == Mode::Rainbow) {
      onHit(shot);
    }
  }

  const uint32_t now = millis();
  switch (mode) {
  case Mode::Rainbow: {
    static uint32_t lastFrameMs = 0;
    if (now - lastFrameMs >= 20) {
      lastFrameMs = now;
      fill_rainbow(leds, NUM_LEDS, rainbowHue++, 4);
      FastLED.show();
    }
    break;
  }
  case Mode::Flash: {
    if (now >= nextEventMs) {
      flashOn = !flashOn;
      if (flashOn) {
        showSolid(hitColour);
        nextEventMs = now + FlashOnMs;
      } else {
        showSolid(CRGB::Black);
        nextEventMs = now + FlashOffMs;
        if (--flashesLeft == 0) {
          mode = Mode::Dark;
          nextEventMs = now + random(DarkMinMs, DarkMaxMs + 1);
          TagNet::eventf("dark %lums", (unsigned long)(nextEventMs - now));
        }
      }
    }
    break;
  }
  case Mode::Dark: {
    if (now >= nextEventMs) {
      mode = Mode::Rainbow;
      TagNet::event("rainbow");
    }
    break;
  }
  }
}
