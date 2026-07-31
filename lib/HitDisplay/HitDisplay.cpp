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
uint8_t mw = 0, mh = 0;

// External WS2812 output (carrier J8 / GP6, S3 carriers only): its own frame
// buffer so it can render independently of the onboard matrix.
CRGB *extLeds = nullptr;
ExtRole extRole = ExtRole::Mirror;
bool extDirty = true;      // repaint needed (role/team change)
int extLastTeam = -1;
uint32_t extLastFrameMs = 0;

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

// Row-major pixel mapping: index = y*W + x. NOTE: if the panel is serpentine
// this needs a per-row flip; verified empirically on hardware.
uint16_t xy(int x, int y) { return (uint16_t)y * mw + x; }

// Push both controllers, mirroring the onboard frame onto the external buffer
// first when that's its role. Every onboard draw funnels through here so
// Mirror stays in lockstep without its own frame loop.
void showAll() {
  if (extLeds != nullptr && extRole == ExtRole::Mirror) {
    memcpy(extLeds, leds, numLeds * sizeof(CRGB));
  }
  FastLED.show();
}
} // namespace

void begin(const Board::BoardProfile &p, TeamColourFn colours) {
  kind = p.hitDisplay;
  colourFn = colours;
  if (kind == Board::HitDisplayKind::Ws2812Matrix) {
    numLeds = (uint16_t)p.matrixW * p.matrixH;
    mw = p.matrixW;
    mh = p.matrixH;
    leds = new CRGB[numLeds];
    if (p.matrixOrder == Board::ColourOrder::Rgb) {
      FastLED.addLeds<WS2812B, 14, RGB>(leds, numLeds);
    } else {
      FastLED.addLeds<WS2812B, 14, GRB>(leds, numLeds);
    }
#if defined(BOARD_S3_MATRIX)
    // Carrier J8 external WS2812 out (GP6, rev1 netlist), on its OWN frame
    // buffer so it renders independently of the onboard matrix (ExtRole) —
    // harmless when nothing is fitted. Typical 8x8 WS2812B panels/strips are
    // GRB (unlike the onboard matrix's RGB). S3-only: GPIO6 is a flash pin on
    // the classic ESP32, where driving it would crash the board.
    extLeds = new CRGB[numLeds];
    fill_solid(extLeds, numLeds, CRGB::Black);
    FastLED.addLeds<WS2812B, 6, GRB>(extLeds, numLeds);
#endif
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
    showAll();
  }
}

void idle() {
  if (kind == Board::HitDisplayKind::Ws2812Matrix) {
    fill_rainbow(leds, numLeds, rainbowHue++, 4);
    showAll();
  } else if (kind == Board::HitDisplayKind::RgbLed) {
    rgbWrite({0, 0, 0});
  }
}

void idleWithHealth(int hp, int maxHp) {
  if (kind != Board::HitDisplayKind::Ws2812Matrix || mw == 0 || mh == 0) {
    idle();
    return;
  }
  fill_rainbow(leds, numLeds, rainbowHue++, 4);

  // The central 4 columns form the health bar.
  const int centreCols = 4;
  const int x0 = (mw - centreCols) / 2; // first central column (W=8 -> 2)
  const int total = centreCols * mh;    // 32 on an 8x8
  int lit = maxHp > 0 ? (hp * total + maxHp / 2) / maxHp : 0; // rounded
  if (lit < 0) lit = 0;
  if (lit > total) lit = total;

  // Blank the (total - lit) central cells, draining from the TOP (y=0 first),
  // row by row.
  int blank = total - lit;
  for (int y = 0; y < mh && blank > 0; y++) {
    for (int x = x0; x < x0 + centreCols && blank > 0; x++) {
      leds[xy(x, y)] = CRGB::Black;
      blank--;
    }
  }
  showAll();
}

void solid(Board::Rgb c) {
  if (kind == Board::HitDisplayKind::Ws2812Matrix) {
    fill_solid(leds, numLeds, toCrgb(c));
    showAll();
  } else if (kind == Board::HitDisplayKind::RgbLed) {
    rgbWrite(c);
  }
}

void ledTest() {
  if (kind != Board::HitDisplayKind::Ws2812Matrix || numLeds < 4) {
    return;
  }
  // Paint both buffers directly (bypassing role/mirror) so the diagnostic
  // exercises each output regardless of configuration.
  fill_solid(leds, numLeds, CRGB::Black);
  leds[0] = CRGB(255, 0, 0);
  leds[1] = CRGB(0, 255, 0);
  leds[2] = CRGB(0, 0, 255);
  leds[3] = CRGB(255, 255, 255);
  if (extLeds != nullptr) {
    memcpy(extLeds, leds, numLeds * sizeof(CRGB));
    extDirty = true; // repaint the role content once the test is over
  }
  FastLED.show();
}

void setExtRole(ExtRole r) {
  if (r != extRole) {
    extRole = r;
    extDirty = true;
  }
}

void extTick(uint32_t nowMs, int team) {
  if (kind != Board::HitDisplayKind::Ws2812Matrix || extLeds == nullptr) {
    return;
  }
  if (team != extLastTeam) {
    extLastTeam = team;
    extDirty = true;
  }
  switch (extRole) {
  case ExtRole::Mirror:
    return; // painted by showAll() in lockstep with onboard frames
  case ExtRole::Off:
    if (extDirty) {
      fill_solid(extLeds, numLeds, CRGB::Black);
      extDirty = false;
      FastLED.show();
    }
    return;
  case ExtRole::Team:
    if (extDirty) {
      fill_solid(extLeds, numLeds, toCrgb(teamRgb(team)));
      extDirty = false;
      FastLED.show();
    }
    return;
  case ExtRole::Pulse:
    // Breathing team colour, ~12 breaths/min family; self-throttled frames.
    if (nowMs - extLastFrameMs < 30) {
      return;
    }
    extLastFrameMs = nowMs;
    {
      CRGB c = toCrgb(teamRgb(team));
      c.nscale8_video(beatsin8(12, 24, 255));
      fill_solid(extLeds, numLeds, c);
      extDirty = false;
      FastLED.show();
    }
    return;
  }
}

void flashTeam(int team) { solid(teamRgb(team)); }

void spinFrame(Board::Rgb c, uint8_t phase) {
  if (kind != Board::HitDisplayKind::Ws2812Matrix || mw != 8 || mh != 8) {
    solid(c); // RGB-LED fallback: steady colour
    return;
  }
  // Clockwise perimeter walk: top row L->R, right col T->B, bottom row R->L,
  // left col B->T = 28 cells.
  static const uint8_t px[28] = {0, 1, 2, 3, 4, 5, 6, 7, 7, 7, 7, 7, 7, 7,
                                 7, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0};
  static const uint8_t py[28] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6,
                                 7, 7, 7, 7, 7, 7, 7, 7, 6, 5, 4, 3, 2, 1};
  fill_solid(leds, numLeds, CRGB::Black);
  for (uint8_t i = 0; i < 4; i++) {
    const uint8_t p = (uint8_t)((phase + i) % 28);
    leds[xy(px[p], py[p])] = toCrgb(c);
  }
  showAll();
}

void scoreboard(const uint8_t grid[64], uint8_t num, uint8_t den) {
  if (kind != Board::HitDisplayKind::Ws2812Matrix || mw != 8 || mh != 8 ||
      den == 0) {
    return;
  }
  fill_solid(leds, numLeds, CRGB::Black);
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      const uint8_t team = grid[y * 8 + x];
      if (team == 0) {
        continue;
      }
      Board::Rgb c = teamRgb(team);
      leds[xy(x, y)] = CRGB((uint8_t)((uint16_t)c.r * num / den),
                            (uint8_t)((uint16_t)c.g * num / den),
                            (uint8_t)((uint16_t)c.b * num / den));
    }
  }
  showAll();
}

void dark() { solid({0, 0, 0}); }

} // namespace HitDisplay
