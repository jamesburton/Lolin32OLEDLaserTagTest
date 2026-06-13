/*
 * OLED configuration finder for the Lolin32 OLED board.
 *
 * Cycles through three likely display configurations, ~6 seconds each:
 *   1 = SSD1306 driver, 128x64
 *   2 = SSD1306 driver, 128x32
 *   3 = SH1106 driver, 128x64
 *
 * Each shows a full border rectangle, corner-to-corner diagonals, and a big
 * config number. The config whose border is complete, diagonals are solid
 * lines, and number is crisp is the correct one.
 *
 * Built via the lolin32_displaytest environment (see platformio.ini).
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SH110X.h>

#define OLED_SDA 5
#define OLED_SCL 4
#define OLED_ADDR 0x3C

Adafruit_SSD1306 ssd1306_64(128, 64, &Wire, -1);
Adafruit_SSD1306 ssd1306_32(128, 32, &Wire, -1);
Adafruit_SH1106G sh1106_64(128, 64, &Wire, -1);

// Draw the same diagnostic pattern on any Adafruit_GFX-based display.
// Colour constant 1 == WHITE for both driver families.
template <typename TDisplay>
void drawPattern(TDisplay &d, int width, int height, const char *label) {
  d.clearDisplay();
  d.drawRect(0, 0, width, height, 1);
  d.drawLine(0, 0, width - 1, height - 1, 1);
  d.drawLine(width - 1, 0, 0, height - 1, 1);
  d.setTextColor(1);
  d.setTextSize(3);
  d.setCursor(width / 2 - 8, height / 2 - 10);
  d.print(label);
  d.display();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(OLED_SDA, OLED_SCL);
  Serial.println(F("OLED config finder: cycling 1=SSD1306/64 2=SSD1306/32 3=SH1106/64"));
}

void loop() {
  Serial.println(F("Config 1: SSD1306 128x64"));
  if (ssd1306_64.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, false, false)) {
    drawPattern(ssd1306_64, 128, 64, "1");
  }
  delay(6000);

  Serial.println(F("Config 2: SSD1306 128x32"));
  if (ssd1306_32.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, false, false)) {
    drawPattern(ssd1306_32, 128, 32, "2");
  }
  delay(6000);

  Serial.println(F("Config 3: SH1106 128x64"));
  if (sh1106_64.begin(OLED_ADDR, false)) {
    drawPattern(sh1106_64, 128, 64, "3");
  }
  delay(6000);
}
