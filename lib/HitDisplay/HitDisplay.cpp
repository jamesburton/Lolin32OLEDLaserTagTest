#include "HitDisplay.h"
#include <Arduino.h>
#include <FastLED.h>

namespace HitDisplay {
namespace {
Board::HitDisplayKind kind = Board::HitDisplayKind::None;
TeamColourFn colourFn = nullptr;

CRGB *leds = nullptr;
uint16_t numLeds = 0;
uint8_t rainbowHue = 0;

int8_t rPin = -1, gPin = -1, bPin = -1;
bool commonAnode = false;
uint8_t brightness = 13;

CRGB toCrgb(Board::Rgb c) { return CRGB(c.r, c.g, c.b); }

Board::Rgb teamRgb(int team) {
  Board::Rgb c{255, 0, 255}; // magenta = unknown
  if (colourFn) {
    const char *hex = colourFn(team);
    if (hex) {
      Board::parseHexColour(hex, c);
    }
  }
  return c;
}

void rgbWrite(Board::Rgb c) {
  auto chan = [&](int8_t pin, uint8_t v) {
    if (pin < 0) return;
    uint8_t scaled = (uint16_t)v * brightness / 255;
    analogWrite(pin, commonAnode ? 255 - scaled : scaled);
  };
  chan(rPin, c.r);
  chan(gPin, c.g);
  chan(bPin, c.b);
}
} // namespace

void begin(const Board::BoardProfile &p, TeamColourFn colours) {
  kind = p.hitDisplay;
  colourFn = colours;
  if (kind == Board::HitDisplayKind::Ws2812Matrix) {
    numLeds = (uint16_t)p.matrixW * p.matrixH;
    leds = new CRGB[numLeds];
    if (p.matrixOrder == Board::ColourOrder::Rgb) {
      FastLED.addLeds<WS2812B, 14, RGB>(leds, numLeds);
    } else {
      FastLED.addLeds<WS2812B, 14, GRB>(leds, numLeds);
    }
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 500);
    FastLED.setBrightness(brightness);
    dark();
  } else if (kind == Board::HitDisplayKind::RgbLed) {
    rPin = p.rgbR; gPin = p.rgbG; bPin = p.rgbB; commonAnode = p.rgbCommonAnode;
    if (rPin >= 0) pinMode(rPin, OUTPUT);
    if (gPin >= 0) pinMode(gPin, OUTPUT);
    if (bPin >= 0) pinMode(bPin, OUTPUT);
    dark();
  }
}

bool present() { return kind != Board::HitDisplayKind::None; }

void setBrightness(uint8_t b) {
  brightness = b;
  if (kind == Board::HitDisplayKind::Ws2812Matrix) {
    FastLED.setBrightness(b);
    FastLED.show();
  }
}

void idle() {
  if (kind == Board::HitDisplayKind::Ws2812Matrix) {
    fill_rainbow(leds, numLeds, rainbowHue++, 4);
    FastLED.show();
  } else if (kind == Board::HitDisplayKind::RgbLed) {
    rgbWrite({0, 0, 0});
  }
}

void solid(Board::Rgb c) {
  if (kind == Board::HitDisplayKind::Ws2812Matrix) {
    fill_solid(leds, numLeds, toCrgb(c));
    FastLED.show();
  } else if (kind == Board::HitDisplayKind::RgbLed) {
    rgbWrite(c);
  }
}

void flashTeam(int team) { solid(teamRgb(team)); }

void dark() { solid({0, 0, 0}); }

} // namespace HitDisplay
