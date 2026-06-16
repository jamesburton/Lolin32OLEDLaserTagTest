/*
 * Wemos Lolin32 OLED — IR monitor, decoder, and transmitter.
 *
 * Reads IR via a VS1838B on GPIO25, frames it with the shared IrFramer, and:
 *   - decodes NEC remotes  -> Serial "NEC addr=0x.. cmd=0x.."
 *   - decodes Vatos shots  -> Serial "VATOS team=N dmg=N" + OLED + telemetry
 *   - always emits the raw "FRAME n=.. data=.." line for the C# trainer
 * It can also transmit Vatos shots via an IR LED on GPIO13 (BOOT button or any
 * serial line triggers a cycling test shot).
 *
 * WiFi (creds over serial), OTA, and UDP telemetry are provided by TagNet.
 *
 * OLED: SSD1306 128x32 on I2C, SDA=GPIO5, SCL=GPIO4, address 0x3C.
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <IrFramer.h>
#include <TagNet.h>
#include <Vatos.h>
#include <driver/adc.h>
#include <driver/gpio.h>
#include <driver/i2s.h>
#include <BoardProfile.h>
#include <IrTx.h>
#include <HitDisplay.h>
#include <Sound.h>
#include <BoardNvs.h>
#include <ControlProto.h>

// Display driver selection: some Lolin32 OLED clones ship with an SH1106
// controller instead of the SSD1306, which makes SSD1306 output scrambled.
// This board tested as SSD1306 with a 128x32 panel (config finder, 2026-06).
#define USE_SH1106 0

#if USE_SH1106
#include <Adafruit_SH110X.h>
#define MONO_WHITE SH110X_WHITE
#define MONO_BLACK SH110X_BLACK
#else
#include <Adafruit_SSD1306.h>
#define MONO_WHITE SSD1306_WHITE
#define MONO_BLACK SSD1306_BLACK
#endif

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32

// On-board OLED I2C wiring (fixed by the board layout)
#define OLED_SDA 5
#define OLED_SCL 4
#define OLED_ADDR 0x3C

// VS1838B demodulating receiver: GPIO25 (idles HIGH, pulses LOW)
#define IR_PIN 25

// Activity LED: pulses each time a frame is received. Active-high, GPIO26.
#define LED_PIN 26
constexpr uint32_t LED_PULSE_MS = 80;

// BOOT button (GPIO0): press to transmit a test shot (cycles team/damage).
#define BOOT_PIN 0

// Carrier sampling only applies to the KEYES board's analog A0 output. The
// VS1838B has no analog pin, so disable it when reading the VS1838B.
#define ENABLE_CARRIER_SAMPLER 0

// KEYES analog output (A0) on GPIO36 (SVP) = ADC1 channel 0.
#define IR_ADC_CHANNEL ADC1_CHANNEL_0
constexpr uint32_t ADC_SAMPLE_RATE = 500000; // Hz
constexpr size_t ADC_BACKLOG_SAMPLES = 4096; // DMA backlog drained pre-capture
constexpr size_t ADC_BURST_SAMPLES = 2048;   // ~4ms capture window
uint16_t adcBuf[ADC_BACKLOG_SAMPLES];

#if USE_SH1106
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
#else
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
#endif

// Stats / latest-decode state for the OLED
uint32_t frameCount = 0;
size_t lastFrameEdges = 0;
uint32_t lastFrameTotalUs = 0;

bool lastNecOk = false;
uint16_t lastNecAddr = 0;
uint8_t lastNecCmd = 0;
uint32_t necCount = 0;

bool lastVatosOk = false;
Vatos::Shot lastVatosShot = {0, 0};
uint32_t vatosCount = 0;

float lastCarrierKhz = 0;
uint16_t lastCarrierP2p = 0;

uint32_t ledOffAtMs = 0; // activity LED off time (0 = off)

// Attempt to decode a frame as NEC. On the VS1838B (active-low) an NEC frame is
// leader mark + leader space, then 32 bits, each a ~560µs mark (LOW) followed by
// a space (HIGH) that is ~560µs for '0' or ~1690µs for '1', LSB first. Validated
// via the command/inverse byte (holds for standard and extended NEC).
bool decodeNec(const IrFramer::Edge *edges, size_t n, uint16_t &addr,
               uint8_t &cmd) {
  constexpr size_t LEADER_EDGES = 2;
  constexpr size_t NEC_BITS = 32;
  if (n < LEADER_EDGES + NEC_BITS * 2) {
    return false;
  }

  // Leader: a mark (LOW=0) then a long space (HIGH=1, ~4.5ms)
  if (edges[0].level != 0 || edges[1].level != 1 || edges[1].durationUs < 3000) {
    return false;
  }

  uint32_t bits = 0;
  for (size_t i = 0; i < NEC_BITS; i++) {
    const IrFramer::Edge &mark = edges[LEADER_EDGES + i * 2];
    const IrFramer::Edge &space = edges[LEADER_EDGES + i * 2 + 1];
    if (mark.level != 0 || space.level != 1) {
      return false;
    }
    if (space.durationUs > 1100) { // midpoint of '0' (~560) and '1' (~1690)
      bits |= (1UL << i);          // NEC is LSB first
    }
  }

  const uint8_t b0 = bits & 0xFF;         // address low
  const uint8_t b1 = (bits >> 8) & 0xFF;  // address high (or ~addr)
  const uint8_t b2 = (bits >> 16) & 0xFF; // command
  const uint8_t b3 = (bits >> 24) & 0xFF; // ~command
  if (static_cast<uint8_t>(b2 ^ b3) != 0xFF) {
    return false; // command integrity check failed
  }

  addr = static_cast<uint16_t>(b0 | (b1 << 8));
  cmd = b2;
  return true;
}

// Process a completed frame: decode, emit Serial lines (for the C# trainer) and
// telemetry, and update the OLED state.
void handleFrame(const IrFramer::Edge *edges, size_t n) {
  frameCount++;
  lastFrameEdges = n;
  lastFrameTotalUs = 0;
  for (size_t i = 0; i < n; i++) {
    lastFrameTotalUs += edges[i].durationUs;
  }

  // Pulse the activity LED; loop() switches it off after LED_PULSE_MS
  digitalWrite(LED_PIN, HIGH);
  ledOffAtMs = millis() + LED_PULSE_MS;

  // Decoded line FIRST (precedes the FRAME line). Try NEC, then Vatos.
  uint16_t addr = 0;
  uint8_t cmd = 0;
  lastNecOk = decodeNec(edges, n, addr, cmd);
  lastVatosOk = false;
  if (lastNecOk) {
    lastNecAddr = addr;
    lastNecCmd = cmd;
    necCount++;
    Serial.printf("NEC addr=0x%04X cmd=0x%02X\n", addr, cmd);
  } else if (n == Vatos::FrameEdges) {
    uint32_t durs[Vatos::FrameEdges];
    for (size_t i = 0; i < n; i++) {
      durs[i] = edges[i].durationUs;
    }
    Vatos::Shot shot;
    lastVatosOk = Vatos::decode(durs, n, shot);
    if (lastVatosOk) {
      lastVatosShot = shot;
      vatosCount++;
      Serial.printf("VATOS team=%u(%s) dmg=%u\n", shot.team,
                    Vatos::teamName(shot.team), shot.damage);
      TagNet::eventf("hit team=%u(%s) dmg=%u", shot.team,
                     Vatos::teamName(shot.team), shot.damage);
    }
  }

  // Always emit the raw frame for the C# trainer
  Serial.print(F("FRAME n="));
  Serial.print(n);
  Serial.print(F(" data="));
  for (size_t i = 0; i < n; i++) {
    if (i > 0) {
      Serial.print(',');
    }
    Serial.print(edges[i].level == 1 ? 'H' : 'L');
    Serial.print(edges[i].durationUs);
  }
  Serial.println();
}

// Configure continuous I2S-ADC DMA sampling of the module's A0 output
void setupCarrierSampler() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN);
  cfg.sample_rate = ADC_SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 1024;

  i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
  i2s_set_adc_mode(ADC_UNIT_1, IR_ADC_CHANNEL);
  i2s_adc_enable(I2S_NUM_0);
}

// Capture a fresh burst from A0 and estimate the carrier frequency from zero
// crossings around the mean (with hysteresis against ADC noise).
bool measureCarrier() {
  size_t bytesRead = 0;
  i2s_read(I2S_NUM_0, adcBuf, ADC_BACKLOG_SAMPLES * sizeof(uint16_t), &bytesRead,
           pdMS_TO_TICKS(25));
  i2s_read(I2S_NUM_0, adcBuf, ADC_BURST_SAMPLES * sizeof(uint16_t), &bytesRead,
           pdMS_TO_TICKS(25));
  const size_t n = bytesRead / sizeof(uint16_t);
  if (n < 256) {
    return false;
  }

  // I2S-ADC quirk: samples arrive pair-swapped within each 32-bit word
  for (size_t i = 0; i + 1 < n; i += 2) {
    const uint16_t t = adcBuf[i];
    adcBuf[i] = adcBuf[i + 1];
    adcBuf[i + 1] = t;
  }

  uint32_t sum = 0;
  uint16_t mn = 0x0FFF, mx = 0;
  for (size_t i = 0; i < n; i++) {
    adcBuf[i] &= 0x0FFF; // strip the 4-bit channel tag
    sum += adcBuf[i];
    mn = min(mn, adcBuf[i]);
    mx = max(mx, adcBuf[i]);
  }

  const uint16_t mean = sum / n;
  const uint16_t p2p = mx - mn;
  lastCarrierP2p = p2p;
  if (p2p < 40) {
    return false; // flat — no carrier reaching A0
  }

  const uint16_t hyst = max(15, p2p / 8);
  uint32_t crossings = 0;
  bool above = adcBuf[0] > mean;
  for (size_t i = 1; i < n; i++) {
    if (!above && adcBuf[i] > mean + hyst) {
      above = true;
      crossings++;
    } else if (above && adcBuf[i] < mean - hyst) {
      above = false;
    }
  }

  lastCarrierKhz = (float)crossings * ADC_SAMPLE_RATE / n / 1000.0f;
  Serial.printf("CARRIER khz=%.1f p2p=%u mean=%u\n", lastCarrierKhz, p2p, mean);
  return true;
}

// Transmit the next test shot, cycling through all 16 team/damage codes.
void sendNextTestShot() {
  static uint8_t txIndex = 0;
  ControlProto::TagEvent ev =
      ControlProto::tagEventFromVatosShot(txIndex / 4 + 1, txIndex % 4 + 1);
  txIndex = (txIndex + 1) % 16;
  IrTx::fire(ev);
  TagNet::eventf("tx team=%d dmg=%d", ev.team, ev.damage);
}

// Any serial line that isn't a WiFi/cfg command transmits a test shot.
void onSerialLine(const char *line) {
  if (BoardNvs::handleCfgLine(line)) return;
  sendNextTestShot();
}

void setup() {
  Serial.begin(115200);

  // The OLED is NOT on the default I2C pins; start the bus explicitly
  Wire.begin(OLED_SDA, OLED_SCL);

#if USE_SH1106
  if (!display.begin(OLED_ADDR, false)) {
    Serial.println(F("SH1106 allocation failed"));
#else
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, false, false)) {
    Serial.println(F("SSD1306 allocation failed"));
#endif
    for (;;) {
      delay(1000);
    }
  }

  IrFramer::begin(IR_PIN);

  // Activity LED. Weakest drive strength (~5mA) protects a resistor-less LED.
  pinMode(LED_PIN, OUTPUT);
  gpio_set_drive_capability(static_cast<gpio_num_t>(LED_PIN), GPIO_DRIVE_CAP_0);
  digitalWrite(LED_PIN, LOW);

  // Board HAL init: load NVS overrides onto the profile, then start IR TX,
  // hit-display panel, and audio. LED_PIN (26) == profile.activityLedPin, so
  // the activity LED setup above is unchanged.
  Board::BoardProfile profile = Board::active();
  BoardNvs::loadOverrides(profile);
  IrTx::begin(profile);
  HitDisplay::begin(profile, nullptr); // panel idle rainbow; no team-colour table needed
  Sound::begin(profile);

  pinMode(BOOT_PIN, INPUT_PULLUP);

#if ENABLE_CARRIER_SAMPLER
  setupCarrierSampler();
#endif

  TagNet::begin("lasertag-lolin32");
  TagNet::onLine(onSerialLine);

  Serial.print(F("IR monitor running (RX GPIO "));
  Serial.print(IR_PIN);
  Serial.print(F(", TX GPIO "));
  Serial.print(profile.irTxPin);
  Serial.println(F("); BOOT button or any serial line transmits a test shot"));

  display.clearDisplay();
  display.setTextColor(MONO_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("IR monitor + NEC"));
  display.print(F("IR on GPIO "));
  display.println(IR_PIN);
  display.println(F("Waiting for frames..."));
  display.display();
}

void loop() {
  static uint32_t lastRefreshMs = 0;
  static uint32_t lastCarrierAttemptMs = 0;

  // Switch the activity LED off once its pulse has elapsed
  if (ledOffAtMs != 0 && millis() >= ledOffAtMs) {
    digitalWrite(LED_PIN, LOW);
    ledOffAtMs = 0;
  }

  // BOOT button (active-low) transmits a cycling test shot
  static bool bootWasDown = false;
  const bool bootDown = digitalRead(BOOT_PIN) == LOW;
  if (bootDown && !bootWasDown) {
    sendNextTestShot();
  }
  bootWasDown = bootDown;

  // OTA + serial commands (WiFi provisioning, and serial-triggered shots)
  TagNet::handle();

  // Throttled panel-idle tick (rainbow animation at ~50 fps)
  static uint32_t lastIdleMs = 0;
  if (millis() - lastIdleMs >= 20) { lastIdleMs = millis(); HitDisplay::idle(); }

#if ENABLE_CARRIER_SAMPLER
  if (digitalRead(IR_PIN) && millis() - lastCarrierAttemptMs > 300) {
    lastCarrierAttemptMs = millis();
    measureCarrier();
  }
#else
  (void)lastCarrierAttemptMs;
#endif

  // Process completed IR frames
  const IrFramer::Edge *edges;
  size_t n;
  while (IrFramer::poll(&edges, &n)) {
    handleFrame(edges, n);
  }

  // Refresh the OLED twice a second; 128x32 gives four 8px text lines
  const uint32_t nowMs = millis();
  if (nowMs - lastRefreshMs >= 500) {
    lastRefreshMs = nowMs;

    display.clearDisplay();
    display.setCursor(0, 0);
    display.print(F("F:"));
    display.print(frameCount);
    display.print(F(" NEC:"));
    display.print(necCount);
    display.print(F(" V:"));
    display.println(vatosCount);

    if (lastVatosOk) {
      display.setTextSize(2);
      display.print(Vatos::teamName(lastVatosShot.team));
      display.print(F(" "));
      display.println(lastVatosShot.damage);
      display.setTextSize(1);
    } else if (lastNecOk) {
      display.setTextSize(2);
      display.printf("%04X:%02X", lastNecAddr, lastNecCmd);
      display.setTextSize(1);
      display.println();
    } else if (frameCount > 0) {
      display.print(F("Raw: "));
      display.print(lastFrameEdges);
      display.print(F(" edges "));
      display.print(lastFrameTotalUs / 1000);
      display.println(F("ms"));
    }

#if ENABLE_CARRIER_SAMPLER
    display.print(F("Carrier: "));
    if (lastCarrierKhz > 0) {
      display.print(lastCarrierKhz, 1);
      display.print(F("kHz"));
    } else {
      display.print(F("-- p2p="));
      display.print(lastCarrierP2p);
    }
#else
    display.print(TagNet::online() ? F("wifi:up ") : F("wifi:-- "));
    display.print(F("line:"));
    display.print(digitalRead(IR_PIN) ? F("H") : F("L"));
#endif

    display.display();
  }

  delay(10);
}
