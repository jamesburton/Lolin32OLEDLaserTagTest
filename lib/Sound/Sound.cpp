#include "Sound.h"
#include <Arduino.h>

#if defined(ESP32)
#include "driver/i2s.h"
// The embedded explosion bank is large (~130 KB) and only the I2S-DAC board
// can use it, so pull it in only for that target. Other ESP32 boards keep just
// the procedural synth burst (index 0).
#if defined(BOARD_S3_MATRIX)
#include "SfxData.h"
#define HAVE_SFX_BANK 1
#endif
#endif

namespace Sound {
namespace {

Board::AudioKind kind = Board::AudioKind::None;
int8_t           piezo = -1;

// Last-played SFX bookkeeping (for serial logging).
uint8_t     sfxLast = 0;          // most recently played entry
const char *sfxLastNm = "none";
uint32_t    sfxPlayCount = 0;

void chirp(uint16_t freq, uint16_t ms) {
  if (piezo < 0) return;
  tone(piezo, freq, ms);
}

#if defined(ESP32)
static constexpr i2s_port_t kPort    = I2S_NUM_0;
static constexpr uint32_t   kRate    = 16000;
// Master playback gain, applied to every clip at output. Samples are stored at
// full scale, so this single knob retunes the level without regenerating data.
static constexpr float      kVolume  = 0.15f;
static constexpr int        kHitMs   = 180;
static constexpr int        kHitSamps = (int)(kRate * kHitMs / 1000); // 2880

// BSS-allocated — never on the stack.
static int16_t sBuf[kHitSamps];

// Render the procedural hit (full scale): decaying noise-burst + 120 Hz thump.
static void synthRender() {
  uint32_t rng = 0xDEADBEEFu;
  for (int i = 0; i < kHitSamps; i++) {
    rng         = rng * 1664525u + 1013904223u;
    float noise = (int16_t)(rng >> 16) / 32768.0f;                     // -1..1 white noise
    float thump = sinf(2.0f * M_PI * 120.0f * (float)i / (float)kRate); // 120 Hz body
    float env   = expf(-(float)i / (kHitSamps * 0.10f));                // fast decay ~18 ms τ
    sBuf[i]     = (int16_t)((noise * 0.65f + thump * 0.35f) * env * 32767.0f);
  }
}

// Play one full-scale PCM clip at kVolume. Scales in small chunks (bounded RAM,
// any length) and feeds the DMA. Blocks for the clip duration while it drains;
// hits are followed by a Flash state that masks the delay, so this is fine for v1.
static void playPcm(const int16_t *data, size_t samples) {
  // Start the peripheral only for the clip. Left running, the I2S clock cycles
  // stale DMA buffers continuously and the amp plays it as a constant noise;
  // stopping it removes the clock so the MAX98357A goes silent at idle.
  i2s_start(kPort);
  static int16_t chunk[256];
  size_t i = 0;
  while (i < samples) {
    const size_t n = samples - i < 256 ? samples - i : 256;
    for (size_t j = 0; j < n; j++) {
      chunk[j] = (int16_t)((float)data[i + j] * kVolume);
    }
    size_t written;
    i2s_write(kPort, chunk, n * sizeof(int16_t), &written, portMAX_DELAY);
    i += n;
  }
  // i2s_write returns once samples are queued, not drained. Wait out the DMA
  // (4 x 64 frames @ 16 kHz ~= 16 ms) before stopping so the tail isn't cut.
  delay(20);
  i2s_zero_dma_buffer(kPort);
  i2s_stop(kPort);
}

#endif

} // namespace

void begin(const Board::BoardProfile &p) {
  kind = p.audio;

  if (kind == Board::AudioKind::Piezo) {
    piezo = p.piezoPin;
    if (piezo >= 0) pinMode(piezo, OUTPUT);
    return;
  }

#if defined(ESP32)
  if (kind == Board::AudioKind::I2sDac) {
    i2s_config_t cfg         = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = kRate;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    // ALL_LEFT: mono samples in, driver broadcasts to both I2S slots.
    // MAX98357A with SD floating (pulled HIGH) selects the left slot.
    cfg.channel_format       = I2S_CHANNEL_FMT_ALL_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = 0;
    cfg.dma_buf_count        = 4;
    cfg.dma_buf_len          = 64;
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = true;
    i2s_driver_install(kPort, &cfg, 0, nullptr);

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = p.i2sBclkPin;
    pins.ws_io_num    = p.i2sWsPin;
    pins.data_out_num = p.i2sDinPin;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;
    i2s_set_pin(kPort, &pins);

    // Stay silent until the first cue: zero the buffers and halt the clock so
    // the amp produces nothing at idle (see playHit).
    i2s_zero_dma_buffer(kPort);
    i2s_stop(kPort);
  }
#endif
}

bool present() {
  if (kind == Board::AudioKind::Piezo && piezo >= 0) return true;
#if defined(ESP32)
  if (kind == Board::AudioKind::I2sDac) return true;
#endif
  return false;
}

void cue(Cue c) {
  switch (kind) {
  case Board::AudioKind::Piezo:
    switch (c) {
    case Cue::Hit:     chirp(1200, 60);  break;
    case Cue::Dead:    chirp(300,  400); break;
    case Cue::Respawn: chirp(900,  120); break;
    case Cue::Start:   chirp(1600, 150); break;
    }
    break;
#if defined(ESP32)
  case Board::AudioKind::I2sDac:
    // The DAC bank is driven by the caller via playIndex (team/death assignment
    // lives in config), so the coarse Cue enum is a no-op here.
    (void)c;
    break;
#endif
  default:
    break;
  }
}

void playIndex(int idx) {
#if defined(HAVE_SFX_BANK)
  if (kind != Board::AudioKind::I2sDac) return;
  if (idx < 0 || idx >= (int)kSfxBankCount) {
    Serial.printf("[sfx] REJECT idx=%d (bank size %u)\n", idx,
                  (unsigned)kSfxBankCount);
    return;
  }
  const SfxSample &s = kSfxBank[idx];
  playPcm(s.data, s.len);
  sfxLast = (uint8_t)idx;
  sfxLastNm = s.name;
  sfxPlayCount++;
  Serial.printf("[sfx] play idx=%d/%u name=%s\n", idx, (unsigned)kSfxBankCount,
                s.name);
#elif defined(ESP32)
  // No embedded bank on this board: fall back to the procedural burst.
  if (kind != Board::AudioKind::I2sDac) return;
  (void)idx;
  synthRender();
  playPcm(sBuf, kHitSamps);
  sfxLast = 0;
  sfxLastNm = "synth-burst";
  sfxPlayCount++;
#else
  (void)idx;
#endif
}

uint8_t sfxCount() {
#if defined(HAVE_SFX_BANK)
  return (uint8_t)kSfxBankCount; // embedded samples
#elif defined(ESP32)
  return 1;                      // synth burst fallback only
#else
  return 0;
#endif
}

uint8_t     sfxLastIndex() { return sfxLast; }
const char *sfxLastName()  { return sfxLastNm; }
uint32_t    sfxPlays()     { return sfxPlayCount; }

} // namespace Sound
