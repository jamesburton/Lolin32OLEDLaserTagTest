#include "IrTx.h"
#include <Arduino.h>
#include <Vatos.h>
#include <driver/gpio.h>

namespace IrTx {
namespace {
int8_t txPin = -1;

#if ESP_ARDUINO_VERSION_MAJOR >= 3
inline void carrierBegin(int pin) { ledcAttach(pin, Vatos::CarrierHz, 8); }
inline void carrierOn(int pin) { ledcWrite(pin, 128); }
inline void carrierOff(int pin) { ledcWrite(pin, 0); }
#else
constexpr int kChannel = 0;
inline void carrierBegin(int pin) {
  ledcSetup(kChannel, Vatos::CarrierHz, 8);
  ledcAttachPin(pin, kChannel);
}
inline void carrierOn(int) { ledcWrite(kChannel, 128); }
inline void carrierOff(int) { ledcWrite(kChannel, 0); }
#endif
} // namespace

void begin(const Board::BoardProfile &p) {
  txPin = p.irTxPin;
  if (txPin < 0) {
    return;
  }
  carrierBegin(txPin);
  gpio_set_drive_capability(static_cast<gpio_num_t>(txPin), GPIO_DRIVE_CAP_0);
  carrierOff(txPin);
}

bool present() { return txPin >= 0; }

void fire(const ControlProto::TagEvent &ev) {
  if (txPin < 0) {
    return;
  }
  const Vatos::Shot shot = {static_cast<uint8_t>(ev.team > 0 ? ev.team : 1),
                            static_cast<uint8_t>(ev.damage > 0 ? ev.damage : 1)};
  bool bits[Vatos::FrameEdges];
  if (!Vatos::encode(shot, bits)) {
    return;
  }
  for (size_t i = 0; i < Vatos::FrameEdges; i++) {
    const uint32_t durUs = bits[i] ? Vatos::LongUs : Vatos::ShortUs;
    if ((i & 1) == 0) {
      carrierOn(txPin);
      delayMicroseconds(durUs);
      carrierOff(txPin);
    } else {
      delayMicroseconds(durUs);
    }
  }
  carrierOff(txPin);
}

} // namespace IrTx
