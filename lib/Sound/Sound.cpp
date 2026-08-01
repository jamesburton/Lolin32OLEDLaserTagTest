#include "Sound.h"
#include <Arduino.h>

#if defined(ESP32)
#include "driver/i2s.h"
#include "esp_task_wdt.h"
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
// Effective gain: kVolume scaled by the runtime volume config (setVolume).
// Defaults to full kVolume so boards without the config behave as before.
static float                sGain    = kVolume;
static constexpr int        kHitMs   = 180;
static constexpr int        kHitSamps = (int)(kRate * kHitMs / 1000); // 2880

// --- Playback task state ----------------------------------------------------
// One dedicated task, pinned to core 0, owns every I2S write. The Arduino
// loop (core 1) only ever ENQUEUES work, so a 10 s clip no longer freezes the
// HTTP server, IR decode or game logic for its duration.
struct PlayRequest {
  enum class Kind : uint8_t { Bank, File };
  Kind kind = Kind::Bank;
  int  idx  = 0;      // Bank: SFX bank entry
  char path[96] = ""; // File: clip path, matches ControlProto's path sizes
};

static TaskHandle_t      sPlayTask  = nullptr;
static QueueHandle_t     sPlayQueue = nullptr;
// Serialises playback jobs against the synchronous bench path (playRaw), so
// two writers can never interleave frames into the same I2S DMA queue.
static SemaphoreHandle_t sPlayLock  = nullptr;
static volatile bool     sPlaying   = false; // task is inside a job
static volatile bool     sAbort     = false; // current clip should stop
static FileStreamFn      sFileStreamer = nullptr;

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

// Scale mono samples to kVolume, widen them to stereo frames and feed the DMA.
// Shared by the one-shot and streaming paths so both apply gain, framing and
// watchdog handling identically.
//
// The widening is not cosmetic. The driver always consumes the write buffer as
// interleaved L/R frames; the channel format only decides which slot is put on
// the wire. Writing mono samples straight through therefore consumed two of
// them per frame and discarded every second one -- playback an octave high and
// aliased. Duplicating each sample into both slots makes one input sample equal
// one frame, so the clip plays at its authored rate.
static void writeMono(const int16_t *data, size_t samples) {
  static int16_t frames[512]; // 256 mono samples -> 256 L/R pairs
  size_t i = 0;
  while (i < samples) {
    // Abort poll, between chunks only: a new game cue (hit/death siren) must
    // pre-empt a long clip within ~16 ms, never mid-DMA-write. When the flag
    // is clear this changes nothing — the sample math below is untouched.
    if (sAbort) break;
    const size_t n = samples - i < 256 ? samples - i : 256;
    for (size_t j = 0; j < n; j++) {
      const int16_t s = (int16_t)((float)data[i + j] * sGain);
      frames[j * 2]     = s; // left
      frames[j * 2 + 1] = s; // right
    }
    size_t written;
    i2s_write(kPort, frames, n * 2 * sizeof(int16_t), &written, portMAX_DELAY);
    i += n;

    // Feed the task watchdog. i2s_write blocks on the DMA queue, which yields
    // to the scheduler but never resets the loop task's WDT — so a clip longer
    // than the watchdog window used to reboot the board mid-playback. That is
    // why clips were previously capped at ~3 s. Resetting here removes the
    // length limit; a 10 s clip now plays in full.
    esp_task_wdt_reset();
  }
}

// Play one full-scale PCM clip at kVolume. Scales in small chunks (bounded RAM,
// any length) and feeds the DMA. Blocks its caller for the clip duration while
// it drains — which is fine, because callers are the playback task (core 0)
// and the bench-only playRaw path; the Arduino loop never calls this.
static void playPcm(const int16_t *data, size_t samples) {
  // Start the peripheral only for the clip. Left running, the I2S clock cycles
  // stale DMA buffers continuously and the amp plays it as a constant noise;
  // stopping it removes the clock so the MAX98357A goes silent at idle.
  i2s_start(kPort);
  writeMono(data, samples);
  // i2s_write returns once samples are queued, not drained. Wait out the DMA
  // (4 x 64 frames @ 16 kHz ~= 16 ms) before stopping so the tail isn't cut.
  delay(20);
  i2s_zero_dma_buffer(kPort);
  i2s_stop(kPort);
}

// Play one validated bank entry. Runs on the playback task only.
static void playBankSync(int idx) {
#if defined(HAVE_SFX_BANK)
  const SfxSample &s = kSfxBank[idx];
  playPcm(s.data, s.len);
  sfxLast = (uint8_t)idx;
  sfxLastNm = s.name;
  sfxPlayCount++;
  Serial.printf("[sfx] play idx=%d/%u name=%s\n", idx, (unsigned)kSfxBankCount,
                s.name);
#else
  // No embedded bank on this board: fall back to the procedural burst.
  (void)idx;
  synthRender();
  playPcm(sBuf, kHitSamps);
  sfxLast = 0;
  sfxLastNm = "synth-burst";
  sfxPlayCount++;
#endif
}

// The playback task: drains the request queue forever, one clip at a time.
// Pinned to core 0 so streaming never contends with the Arduino loop (core 1).
// Stack is generous (8 KB) because the file streamer runs HERE — flash reads
// and WAV parsing included — not on the loop task.
static void playTask(void *) {
  PlayRequest req;
  for (;;) {
    if (xQueueReceive(sPlayQueue, &req, portMAX_DELAY) != pdTRUE) continue;
    sPlaying = true;
    sAbort = false;
    // Take the lock BEFORE subscribing to the WDT: blocked on the mutex (e.g.
    // while a bench playRaw holds it for a long clip) a subscribed task could
    // not reset and would trip the watchdog.
    xSemaphoreTake(sPlayLock, portMAX_DELAY);
    // Subscribe to the task WDT only for the job's duration, so writeMono's
    // esp_task_wdt_reset keeps feeding it exactly as it did from the loop
    // task. Idle (blocked on the queue) the task must NOT be subscribed —
    // it could never reset and the watchdog would reboot the board.
    esp_task_wdt_add(nullptr);
    if (req.kind == PlayRequest::Kind::Bank) {
      playBankSync(req.idx);
    } else if (sFileStreamer != nullptr) {
      sFileStreamer(req.path);
    }
    esp_task_wdt_delete(nullptr);
    xSemaphoreGive(sPlayLock);
    sPlaying = false;
  }
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
    // True stereo framing. The clips are mono, so writeMono duplicates each
    // sample into both slots rather than relying on a driver channel format to
    // do it: ALL_LEFT still reads the buffer as L/R pairs, so mono data fed to
    // it played an octave high with every second sample dropped. Sending real
    // pairs is also indifferent to how the MAX98357A's SD pin has resolved
    // (left-only or (L+R)/2), because both slots carry the same audio.
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
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

    // Playback task, created once here so it already exists for the startup
    // clip (which plays before WiFi is up). Depth-4 queue: one playing clip
    // plus a short burst of game cues; playIndex drops the oldest pending on
    // overflow so the newest feedback always lands.
    if (sPlayTask == nullptr) {
      sPlayQueue = xQueueCreate(4, sizeof(PlayRequest));
      sPlayLock  = xSemaphoreCreateMutex();
      xTaskCreatePinnedToCore(playTask, "sndplay", 8192, nullptr, 1, &sPlayTask,
                              0);
    }
  }
#endif
}

void setVolume(uint8_t v) {
#if defined(ESP32)
  // Called from core 1 (REST PATCH) while the playback task reads sGain on
  // core 0. Benign without a lock: sGain is an aligned 32-bit float, and on
  // the ESP32/Xtensa a 32-bit aligned store is atomic — a concurrent reader
  // sees either the old or the new gain, never a torn value.
  sGain = kVolume * ((float)v / 255.0f);
#else
  (void)v;
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
#if defined(ESP32)
  if (kind != Board::AudioKind::I2sDac || sPlayQueue == nullptr) return;
#if defined(HAVE_SFX_BANK)
  if (idx < 0 || idx >= (int)kSfxBankCount) {
    Serial.printf("[sfx] REJECT idx=%d (bank size %u)\n", idx,
                  (unsigned)kSfxBankCount);
    return;
  }
#endif
  // Game cues pre-empt: abort whatever is playing (a long file clip must not
  // delay a hit/death siren) and queue the cue. The task clears the flag when
  // it starts the next job.
  PlayRequest req;
  req.kind = PlayRequest::Kind::Bank;
  req.idx = idx;
  if (busy()) sAbort = true;
  if (xQueueSend(sPlayQueue, &req, 0) != pdTRUE) {
    // Queue full under a cue burst: drop the oldest pending request so the
    // NEWEST game feedback is the one that plays. Never block the game loop.
    PlayRequest dropped;
    xQueueReceive(sPlayQueue, &dropped, 0);
    xQueueSend(sPlayQueue, &req, 0);
  }
#else
  (void)idx;
#endif
}

void playRaw(const int16_t *data, size_t samples) {
#if defined(ESP32)
  if (kind != Board::AudioKind::I2sDac) return;
  if (data == nullptr || samples == 0) return;
  // Deliberately SYNCHRONOUS: the only caller (the `sdplay` bench verb) frees
  // its buffer as soon as this returns, so the data cannot ride the async
  // queue. Pre-empt whatever is playing, then hold the play lock so the task
  // cannot start a queued cue while we write frames from this core.
  if (sPlayLock != nullptr) {
    sAbort = true;
    xSemaphoreTake(sPlayLock, portMAX_DELAY);
    sAbort = false; // don't abort our own clip; a NEW cue may still set it
    playPcm(data, samples);
    xSemaphoreGive(sPlayLock);
  } else {
    playPcm(data, samples);
  }
#else
  (void)data;
  (void)samples;
#endif
}

void setFileStreamer(FileStreamFn fn) {
#if defined(ESP32)
  sFileStreamer = fn;
#else
  (void)fn;
#endif
}

bool playFileAsync(const char *path) {
#if defined(ESP32)
  if (kind != Board::AudioKind::I2sDac || sPlayQueue == nullptr ||
      sFileStreamer == nullptr) {
    return false;
  }
  PlayRequest req;
  req.kind = PlayRequest::Kind::File;
  if (path == nullptr || strlen(path) >= sizeof(req.path)) return false;
  // One clip at a time: a file play while busy is rejected rather than
  // queued, so the REST caller gets an immediate, truthful "busy" outcome
  // (game cues, by contrast, pre-empt — see playIndex).
  if (busy()) return false;
  strncpy(req.path, path, sizeof(req.path) - 1);
  req.path[sizeof(req.path) - 1] = '\0';
  return xQueueSend(sPlayQueue, &req, 0) == pdTRUE;
#else
  (void)path;
  return false;
#endif
}

bool busy() {
#if defined(ESP32)
  return sPlaying ||
         (sPlayQueue != nullptr && uxQueueMessagesWaiting(sPlayQueue) > 0);
#else
  return false;
#endif
}

bool abortRequested() {
#if defined(ESP32)
  return sAbort;
#else
  return false;
#endif
}

bool streamBegin() {
#if defined(ESP32)
  if (kind != Board::AudioKind::I2sDac) return false;
  i2s_start(kPort);
  return true;
#else
  return false;
#endif
}

void streamChunk(const int16_t *data, size_t samples) {
#if defined(ESP32)
  if (kind != Board::AudioKind::I2sDac || data == nullptr || samples == 0) return;
  writeMono(data, samples);
#else
  (void)data; (void)samples;
#endif
}

void streamEnd() {
#if defined(ESP32)
  if (kind != Board::AudioKind::I2sDac) return;
  // i2s_write queues rather than drains; wait out the DMA so the tail is not
  // clipped, then stop the clock so the amp is silent at idle.
  delay(20);
  i2s_zero_dma_buffer(kPort);
  i2s_stop(kPort);
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
