# Wemos Lolin32 OLED — Device Info

## Overview

The Wemos Lolin32 OLED is an ESP32 development board with a built-in 0.96-inch
SSD1306 OLED display. It is a compact board with fewer broken-out GPIOs than a
standard ESP32 dev board, intended for small display-driven projects.

## Hardware Specifications

| Item              | Detail                                          |
| ----------------- | ----------------------------------------------- |
| Microcontroller   | ESP32 (dual-core Xtensa LX6, Wi-Fi + Bluetooth) |
| Display           | 0.96" OLED, 128×64 pixels, monochrome           |
| Display controller| SSD1306                                         |
| Display interface | I2C                                             |
| Buttons           | BOOT and EN (RST)                               |
| Battery           | LiPo connector with on-board charging (Lolin32) |
| Logic level       | 3.3 V                                           |

## OLED Display Wiring (fixed, on-board)

| Signal | ESP32 Pin |
| ------ | --------- |
| SDA    | GPIO 5    |
| SCL    | GPIO 4    |

- **I2C address:** `0x3C`
- These pins are **not** the ESP32 Arduino defaults (21/22), so I2C must be
  started explicitly with `Wire.begin(5, 4);`.

## Software / Libraries (Arduino framework)

- **Adafruit SSD1306** — OLED driver
- **Adafruit GFX Library** — graphics primitives / text rendering

Critical initialization for this board:

```cpp
Wire.begin(5, 4);                                        // SDA=5, SCL=4
display.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false); // don't reset/re-init pins
```

The `false, false` arguments stop the library resetting the display and
re-initialising I2C on the default pins.

## Reference Example (Random Nerd Tutorials)

The manufacturer/community example sketch initializes Serial at 115200, starts
I2C on pins 5/4, initializes the SSD1306 at 0x3C, and draws to the display:

```cpp
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void setup() {
  Serial.begin(115200);
  Wire.begin(5, 4); // SDA = 5, SCL = 4 on Wemos Lolin32

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }

  display.clearDisplay();
  // ... draw bitmap / text ...
  display.display();
}

void loop() {}
```

## PlatformIO Notes

- Board id: `lolin32`
- Platform: `espressif32`, framework: `arduino`
- Monitor speed: 115200

## Sources

- [Random Nerd Tutorials — ESP32 Built-in OLED Board (Wemos Lolin32)](https://randomnerdtutorials.com/esp32-built-in-oled-ssd1306/)
- [FablabTorino wiki — Wemos Lolin board (ESP32 with 128x64 SSD1306 I2C OLED)](https://github.com/FablabTorino/AUG-Torino/wiki/Wemos-Lolin-board-(ESP32-with-128x64-SSD1306-I2C-OLED-display))
- [Maker Advisor — Wemos Lolin32 (ESP32 with built-in OLED Display)](https://makeradvisor.com/tools/wemos-lolin32-esp32-oled/)
