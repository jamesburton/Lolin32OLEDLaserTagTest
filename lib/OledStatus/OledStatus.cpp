#include "OledStatus.h"
#include <Adafruit_SH110X.h>
#include <Arduino.h>
#include <Wire.h>

namespace OledStatus {
namespace {
Adafruit_SH1107 *disp = nullptr;

// Probe a single I2C address: true when a device ACKs.
bool ack(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}
} // namespace

bool begin(int sdaPin, int sclPin) {
  // Probe both pin orientations: some modules' headers carry SCL/SDA in the
  // opposite order to the carrier's J9 socket, and I2C pins are freely
  // remappable, so a swapped module can be used plugged straight in. Probing
  // swapped lines is electrically harmless (open-drain with pull-ups).
  uint8_t addr = 0;
  Wire.begin(sdaPin, sclPin);
  if (ack(0x3C)) {
    addr = 0x3C;
  } else if (ack(0x3D)) {
    addr = 0x3D;
  } else {
    Wire.end();
    Wire.begin(sclPin, sdaPin); // swapped orientation
    if (ack(0x3C)) {
      addr = 0x3C;
    } else if (ack(0x3D)) {
      addr = 0x3D;
    } else {
      Wire.end();
      return false; // no module fitted — stay silent
    }
  }
  disp = new Adafruit_SH1107(128, 128, &Wire);
  if (!disp->begin(addr, true)) {
    delete disp;
    disp = nullptr;
    return false;
  }
  disp->clearDisplay();
  disp->setTextColor(SH110X_WHITE);
  disp->setTextSize(1);
  disp->setCursor(0, 0);
  disp->println("OLED OK");
  disp->display();
  return true;
}

bool present() { return disp != nullptr; }

void showStatus(const char *hostname, const char *fw, const char *ip, int team,
                int hp, int maxHp, uint32_t uptimeS) {
  if (disp == nullptr) {
    return;
  }
  disp->clearDisplay();
  disp->setTextSize(1);
  disp->setCursor(0, 0);
  disp->println(hostname);
  disp->print("fw ");
  disp->println(fw);
  disp->println(ip);

  disp->setCursor(0, 34);
  disp->print("team ");
  if (team > 0) {
    disp->println(team);
  } else {
    disp->println("none");
  }

  // Big HP readout — the number a player glances at.
  disp->setTextSize(3);
  disp->setCursor(0, 52);
  disp->print("HP ");
  disp->println(hp);

  // Health bar under the number, full width, scaled to maxHp.
  const int barY = 84, barH = 10, barW = 128;
  disp->drawRect(0, barY, barW, barH, SH110X_WHITE);
  if (maxHp > 0 && hp > 0) {
    int fill = (hp * (barW - 2) + maxHp / 2) / maxHp;
    if (fill > barW - 2) fill = barW - 2;
    disp->fillRect(1, barY + 1, fill, barH - 2, SH110X_WHITE);
  }

  disp->setTextSize(1);
  disp->setCursor(0, 104);
  disp->print("up ");
  disp->print(uptimeS / 60);
  disp->print("m ");
  disp->print(uptimeS % 60);
  disp->print("s");
  disp->display();
}

} // namespace OledStatus
