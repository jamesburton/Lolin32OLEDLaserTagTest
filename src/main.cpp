/*
 * Wemos Lolin32 OLED — IR frame monitor.
 *
 * Captures edge timings from an IR sensor module (KEYES photodiode + LM393
 * comparator board, D0 output) on GPIO 16, groups them into frames separated
 * by idle gaps, and emits one parseable line per frame over Serial:
 *
 *   FRAME n=12 data=H5581,L144,H1022,L245,...
 *
 * (H/L = level held, number = duration in µs.) The OLED shows a live frame
 * counter and a summary of the most recent frame. Consumed by the C# trainer
 * app in tools/, which tags frames against known devices/buttons.
 *
 * OLED: SSD1306 128x32 on I2C, SDA=GPIO5, SCL=GPIO4, address 0x3C.
 * IR:   module D0 -> GPIO 16, VCC -> 3V3, GND -> GND, A0 unused.
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Vatos.h>
#include <driver/adc.h>
#include <driver/gpio.h>
#include <driver/i2s.h>

// Display driver selection: some Lolin32 OLED clones ship with an SH1106
// controller instead of the SSD1306, which makes SSD1306 output scrambled.
// Set to 1 for SH1106, 0 for SSD1306.
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

// IR sensor digital output.
//   KEYES comparator board (D0): GPIO 16
//   VS1838B demodulating receiver: GPIO 25 (idles HIGH, pulses LOW)
#define IR_PIN 25

// Activity LED: briefly pulses each time a signal frame is received.
// Active-high: GPIO 26 -> resistor -> LED -> GND.
#define LED_PIN 26
constexpr uint32_t LED_PULSE_MS = 80;

// IR transmit LED on GPIO 13 (GPIO 13 -> IR LED -> GND). Driven as a 38 kHz
// carrier via LEDC. Min drive strength keeps a resistor-less LED safe; range
// is short, so keep it aimed at the receiver for loopback testing.
#define IR_TX_PIN 13
#define IR_TX_LEDC_CHANNEL 0

// BOOT button (GPIO 0): press to transmit a test shot (cycles team/damage).
#define BOOT_PIN 0

// Carrier sampling only applies to the KEYES board's analog A0 output. The
// VS1838B has no analog pin, so disable it when reading the VS1838B.
#define ENABLE_CARRIER_SAMPLER 0

// KEYES analog output (A0) on GPIO 36 (SVP) = ADC1 channel 0.
// Burst-sampled via I2S-ADC DMA to estimate the carrier frequency.
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

// Ring buffer of edge timings written by the ISR, drained in loop().
// Each entry is how long the line held its previous level, in µs, plus the
// level it held (so "4500 µs LOW" style records come out the other end).
constexpr size_t EDGE_BUF_SIZE = 512;

struct Edge {
  uint32_t durationUs; // time spent at the previous level
  uint8_t prevLevel;   // the level that just ended (HIGH/LOW)
};

volatile Edge edgeBuf[EDGE_BUF_SIZE];
volatile size_t edgeHead = 0; // ISR writes here
size_t edgeTail = 0;          // loop() reads from here
volatile uint32_t lastEdgeUs = 0;
volatile uint32_t totalEdges = 0;

// Frame assembly: edges accumulate here until the line goes quiet for
// FRAME_GAP_US, at which point the frame is emitted and reset.
constexpr uint32_t FRAME_GAP_US = 50000; // gun frames are ~22ms end to end
constexpr size_t MAX_FRAME_EDGES = 160;
Edge frameEdges[MAX_FRAME_EDGES];
size_t frameLen = 0;
uint32_t frameCount = 0;
size_t lastFrameEdges = 0;
uint32_t lastFrameTotalUs = 0;

// Latest successfully decoded NEC frame, for the OLED
bool lastNecOk = false;
uint16_t lastNecAddr = 0;
uint8_t lastNecCmd = 0;
uint32_t necCount = 0;

// Latest successfully decoded Vatos shot, for the OLED
bool lastVatosOk = false;
Vatos::Shot lastVatosShot = {0, 0};
uint32_t vatosCount = 0;

// Latest carrier measurement, for the OLED
float lastCarrierKhz = 0;
uint16_t lastCarrierP2p = 0;

// Activity LED: millis() at which the current pulse should switch off (0 = off)
uint32_t ledOffAtMs = 0;

void IRAM_ATTR onIrEdge() {
  const uint32_t now = micros();
  const size_t h = edgeHead;
  edgeBuf[h].durationUs = now - lastEdgeUs;

  // The line just changed, so the level that ENDED is the inverse of now
  edgeBuf[h].prevLevel = digitalRead(IR_PIN) ? LOW : HIGH;
  edgeHead = (h + 1) % EDGE_BUF_SIZE;
  lastEdgeUs = now;
  totalEdges++;
}

// Attempt to decode the assembled frame as an NEC message.
//
// On the VS1838B (active-low) an NEC frame is: leader mark + leader space,
// then 32 bits, each a ~560µs mark followed by a space that is ~560µs for a
// '0' or ~1690µs for a '1' (LSB first). The 32 bits are addr, addr2, cmd,
// ~cmd. We validate via the command/inverse-command byte, which holds for
// both standard and extended (16-bit address) NEC.
//
// Returns true and fills addr (16-bit)/cmd on success; leaves the gun and any
// non-NEC protocol to fall through to raw-frame fingerprinting.
bool decodeNec(uint16_t &addr, uint8_t &cmd) {
  constexpr size_t LEADER_EDGES = 2;
  constexpr size_t NEC_BITS = 32;
  if (frameLen < LEADER_EDGES + NEC_BITS * 2) {
    return false;
  }

  // Leader: a mark (LOW) followed by a long space (HIGH, ~4.5ms)
  if (frameEdges[0].prevLevel != LOW || frameEdges[1].prevLevel != HIGH ||
      frameEdges[1].durationUs < 3000) {
    return false;
  }

  uint32_t bits = 0;
  for (size_t i = 0; i < NEC_BITS; i++) {
    const Edge &mark = frameEdges[LEADER_EDGES + i * 2];
    const Edge &space = frameEdges[LEADER_EDGES + i * 2 + 1];
    if (mark.prevLevel != LOW || space.prevLevel != HIGH) {
      return false;
    }

    // Midpoint between the ~560µs '0' and ~1690µs '1' spaces
    if (space.durationUs > 1100) {
      bits |= (1UL << i); // NEC is LSB first
    }
  }

  const uint8_t b0 = bits & 0xFF;         // address low
  const uint8_t b1 = (bits >> 8) & 0xFF;  // address high (or ~addr)
  const uint8_t b2 = (bits >> 16) & 0xFF; // command
  const uint8_t b3 = (bits >> 24) & 0xFF; // ~command
  if (static_cast<uint8_t>(b2 ^ b3) != 0xFF) {
    return false; // command integrity check failed — not a clean NEC frame
  }

  addr = static_cast<uint16_t>(b0 | (b1 << 8));
  cmd = b2;
  return true;
}

// Emit the assembled frame over Serial and record its stats for the OLED.
// Always emits the raw FRAME line (fingerprinting fallback); additionally
// emits a decoded NEC line when the frame decodes cleanly.
void finalizeFrame() {
  if (frameLen < 2) { // single stray edges are noise, not a frame
    frameLen = 0;
    return;
  }

  frameCount++;
  lastFrameEdges = frameLen;
  lastFrameTotalUs = 0;
  for (size_t i = 0; i < frameLen; i++) {
    lastFrameTotalUs += frameEdges[i].durationUs;
  }

  // Pulse the activity LED; loop() switches it off after LED_PULSE_MS
  digitalWrite(LED_PIN, HIGH);
  ledOffAtMs = millis() + LED_PULSE_MS;

  // Emit the optional decoded line FIRST so the consumer can attach it to the
  // FRAME line that immediately follows (which always terminates the event).
  // Try NEC, then the Vatos gun protocol; otherwise it's a raw frame.
  uint16_t addr = 0;
  uint8_t cmd = 0;
  lastNecOk = decodeNec(addr, cmd);
  lastVatosOk = false;
  if (lastNecOk) {
    lastNecAddr = addr;
    lastNecCmd = cmd;
    necCount++;

    Serial.printf("NEC addr=0x%04X cmd=0x%02X\n", addr, cmd);
  } else if (frameLen == Vatos::FrameEdges) {
    uint32_t durs[Vatos::FrameEdges];
    for (size_t i = 0; i < frameLen; i++) {
      durs[i] = frameEdges[i].durationUs;
    }
    Vatos::Shot shot;
    lastVatosOk = Vatos::decode(durs, frameLen, shot);
    if (lastVatosOk) {
      lastVatosShot = shot;
      vatosCount++;

      Serial.printf("VATOS team=%u(%s) dmg=%u\n", shot.team,
                    Vatos::teamName(shot.team), shot.damage);
    }
  }

  Serial.print(F("FRAME n="));
  Serial.print(frameLen);
  Serial.print(F(" data="));
  for (size_t i = 0; i < frameLen; i++) {
    if (i > 0) {
      Serial.print(',');
    }
    Serial.print(frameEdges[i].prevLevel == HIGH ? 'H' : 'L');
    Serial.print(frameEdges[i].durationUs);
  }
  Serial.println();

  frameLen = 0;
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

// Capture a fresh burst from A0 and estimate the carrier frequency from
// zero crossings around the mean (with hysteresis against ADC noise).
// Returns true if a plausible oscillation was present.
bool measureCarrier() {
  size_t bytesRead = 0;

  // Drain the DMA backlog so the analysis window is current
  i2s_read(I2S_NUM_0, adcBuf, ADC_BACKLOG_SAMPLES * sizeof(uint16_t),
           &bytesRead, pdMS_TO_TICKS(25));
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
    return false; // flat — no carrier reaching A0 (or smoothed away)
  }

  const uint16_t hyst = max(15, p2p / 8);
  uint32_t crossings = 0;
  bool above = adcBuf[0] > mean;
  for (size_t i = 1; i < n; i++) {
    if (!above && adcBuf[i] > mean + hyst) {
      above = true;
      crossings++; // count rising crossings only = full cycles
    } else if (above && adcBuf[i] < mean - hyst) {
      above = false;
    }
  }

  lastCarrierKhz = (float)crossings * ADC_SAMPLE_RATE / n / 1000.0f;

  Serial.print(F("CARRIER khz="));
  Serial.print(lastCarrierKhz, 1);
  Serial.print(F(" p2p="));
  Serial.print(p2p);
  Serial.print(F(" mean="));
  Serial.println(mean);
  return true;
}

// 38 kHz carrier control on the IR TX pin. The LEDC API changed in Arduino-
// ESP32 core 3.x (pin-addressed) from 2.x (channel-addressed).
#if ESP_ARDUINO_VERSION_MAJOR >= 3
#define CARRIER_BEGIN() ledcAttach(IR_TX_PIN, Vatos::CarrierHz, 8)
#define CARRIER_ON() ledcWrite(IR_TX_PIN, 128) // 50% duty
#define CARRIER_OFF() ledcWrite(IR_TX_PIN, 0)
#else
#define CARRIER_BEGIN()                                                        \
  ledcSetup(IR_TX_LEDC_CHANNEL, Vatos::CarrierHz, 8);                          \
  ledcAttachPin(IR_TX_PIN, IR_TX_LEDC_CHANNEL)
#define CARRIER_ON() ledcWrite(IR_TX_LEDC_CHANNEL, 128)
#define CARRIER_OFF() ledcWrite(IR_TX_LEDC_CHANNEL, 0)
#endif

// Transmit a Vatos shot by gating the 38 kHz carrier per encoded symbol.
// Even symbols are IR bursts (carrier on), odd are gaps (carrier off).
void vatosSend(const Vatos::Shot &shot) {
  bool bits[Vatos::FrameEdges];
  if (!Vatos::encode(shot, bits)) {
    return;
  }

  for (size_t i = 0; i < Vatos::FrameEdges; i++) {
    const uint32_t durUs = bits[i] ? Vatos::LongUs : Vatos::ShortUs;
    if ((i & 1) == 0) {
      CARRIER_ON();
      delayMicroseconds(durUs);
      CARRIER_OFF();
    } else {
      delayMicroseconds(durUs);
    }
  }
  CARRIER_OFF();
}

// Transmit the next test shot, cycling through all 16 team/damage codes.
// Triggered by the BOOT button or any byte received over Serial.
void sendNextTestShot() {
  static uint8_t txIndex = 0;
  const Vatos::Shot shot = {static_cast<uint8_t>(txIndex / 4 + 1),
                            static_cast<uint8_t>(txIndex % 4 + 1)};
  txIndex = (txIndex + 1) % 16;
  Serial.printf("TX team=%u(%s) dmg=%u\n", shot.team, Vatos::teamName(shot.team),
                shot.damage);
  vatosSend(shot);
}

void setup() {
  Serial.begin(115200);

  // The OLED is NOT on the default I2C pins; start the bus explicitly
  Wire.begin(OLED_SDA, OLED_SCL);

#if USE_SH1106
  if (!display.begin(OLED_ADDR, false)) {
    Serial.println(F("SH1106 allocation failed"));
#else
  // periphBegin=false, reset=false: don't let the library re-init I2C pins
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, false, false)) {
    Serial.println(F("SSD1306 allocation failed"));
#endif
    for (;;) {
      delay(1000);
    }
  }

  pinMode(IR_PIN, INPUT);
  lastEdgeUs = micros();
  attachInterrupt(digitalPinToInterrupt(IR_PIN), onIrEdge, CHANGE);

  // Activity LED. Set the weakest drive strength (~5mA) so a resistor-less LED
  // can't overdraw the pin; a 220-330ohm series resistor is still preferable.
  pinMode(LED_PIN, OUTPUT);
  gpio_set_drive_capability(static_cast<gpio_num_t>(LED_PIN), GPIO_DRIVE_CAP_0);
  digitalWrite(LED_PIN, LOW);

  // IR transmit LED via LEDC carrier. Min drive strength protects the pin from
  // a resistor-less LED (at the cost of range — fine for loopback testing).
  CARRIER_BEGIN();
  gpio_set_drive_capability(static_cast<gpio_num_t>(IR_TX_PIN), GPIO_DRIVE_CAP_0);
  CARRIER_OFF();

  // BOOT button transmits a test shot
  pinMode(BOOT_PIN, INPUT_PULLUP);

#if ENABLE_CARRIER_SAMPLER
  setupCarrierSampler();
#endif

  Serial.print(F("IR monitor running (RX GPIO "));
  Serial.print(IR_PIN);
  Serial.print(F(", TX GPIO "));
  Serial.print(IR_TX_PIN);
  Serial.println(F("); BOOT button or any serial byte transmits a test shot"));

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

  // Transmit a test shot (cycling team/damage) on a BOOT-button press OR any
  // byte received over Serial. Aim the IR TX LED at the receiver and the
  // decode should appear on serial + OLED (loopback test).
  static bool bootWasDown = false;
  const bool bootDown = digitalRead(BOOT_PIN) == LOW;
  bool trigger = bootDown && !bootWasDown;
  bootWasDown = bootDown;

  if (Serial.available() > 0) {
    while (Serial.available() > 0) {
      Serial.read(); // drain the trigger byte(s)
    }
    trigger = true;
  }

  if (trigger) {
    sendNextTestShot();
  }

#if ENABLE_CARRIER_SAMPLER
  // While a burst is in progress (digital line active), grab an analog
  // capture to estimate the carrier. Rate-limited so each shot is sampled
  // roughly once and OLED/serial stay responsive.
  if (digitalRead(IR_PIN) && millis() - lastCarrierAttemptMs > 300) {
    lastCarrierAttemptMs = millis();
    measureCarrier();
  }
#else
  (void)lastCarrierAttemptMs;
#endif

  // Drain the ISR ring buffer into the current frame. An edge that spent
  // longer than FRAME_GAP_US at one level is idle time between frames, not
  // part of a frame.
  const size_t head = edgeHead;
  while (edgeTail != head) {
    const Edge e = {edgeBuf[edgeTail].durationUs, edgeBuf[edgeTail].prevLevel};
    edgeTail = (edgeTail + 1) % EDGE_BUF_SIZE;

    if (e.durationUs >= FRAME_GAP_US) {
      finalizeFrame(); // normally already emitted by the timeout below
    } else if (frameLen < MAX_FRAME_EDGES) {
      frameEdges[frameLen++] = e;
    }
  }

  // The closing idle edge only arrives when the NEXT frame starts, so end
  // the frame by timeout once the line has been quiet long enough.
  if (frameLen > 0 && (micros() - lastEdgeUs) > FRAME_GAP_US) {
    finalizeFrame();
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
      // Decoded Vatos shot: team + damage, big enough to read at a glance
      display.setTextSize(2);
      display.print(Vatos::teamName(lastVatosShot.team));
      display.print(F(" "));
      display.println(lastVatosShot.damage);
      display.setTextSize(1);
    } else if (lastNecOk) {
      // Decoded NEC: the headline line, big enough to read at a glance
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
    display.print(F("line:"));
    display.print(digitalRead(IR_PIN) ? F("H") : F("L"));
#endif

    display.display();
  }

  delay(10);
}
