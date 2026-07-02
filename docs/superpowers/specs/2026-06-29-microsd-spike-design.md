# microSD Spike Design (S3-Matrix)

## Goal

Prove microSD-over-SPI → WAV parsing → I2S playback end-to-end on the
ESP32-S3-Matrix, behind a native-testable storage HAL, with **zero impact on
no-card boards** (Lolin32 today; any future board without a card). This is a
**spike**: prove the path, don't build the real feature. Runtime SFX-bank
loading from `/sfx/*.wav` and a mirrored `/config.json` are explicit non-goals
here — see Backlog.

Source: `.docs/handoff.md` Next Steps #1 ("SPIKE — microSD reader, DO THIS
FIRST"). Pins were confirmed during the PCB planning phase
(`.docs/pcb-design.md`, `.docs/pcb-blocks.md`).

## Scope (Minimal Proof)

- Mount a microSD card over SPI on the S3-Matrix.
- List `/sfx/` and read one `.wav` file into RAM.
- Parse the WAV, validate it matches the existing bank's PCM format
  (16 kHz, 16-bit, mono — same as `lib/Sound/SfxData.h`).
- Play it through the **existing, unmodified** `Sound::playPcm()` I2S path.
- A native unit-testable WAV parser (no Arduino/SD dependency).
- `BoardProfile` gains SD pins (compile-time default + NVS-whitelist
  override), following the codebase's existing hybrid-config pattern.
- A serial verb to trigger it on hardware, mirroring the existing `sfx <idx>`
  verb.

**Out of scope for this spike** (see Backlog): streaming/chunked playback,
runtime bank replacement, `/sfx/` becoming the live SFX source, `/config.json`,
SD_MMC evaluation, hot-swap/mount-fail UX beyond a logged no-op, Lolin32 SD
support.

## Pins (confirmed)

From the PCB planning phase — the S3-Matrix only breaks out two 1×10 header
rows; these four pads are free and clean:

| Function | GPIO |
|---|---|
| CS | GP36 |
| MOSI | GP34 |
| MISO | GP35 |
| SCK | GP33 |

Card runs off **3V3(OUT)** (bare microSD sockets are 3.3V native — no level
shift needed).

## Architecture

Two new files under `lib/Storage/`, split by testability — mirrors the
existing split in `lib/Sound/Sound.cpp` between the piezo path and the
`#if defined(ESP32)` I2sDac path.

### `lib/Storage/WavFile.{h,cpp}` — pure parser, native-testable

No Arduino/SD/ESP32 dependency. Operates on a caller-owned byte buffer.

```cpp
namespace Storage {

struct WavView {
  uint32_t sampleRate;
  uint8_t  bitsPerSample;
  uint8_t  channels;
  const int16_t *pcm;   // points into the caller's buffer
  size_t   sampleCount; // per-channel sample count
};

// Parses a RIFF/WAVE buffer in place (no copy). Walks fmt/data chunks.
// Returns false (with a reason logged by the caller via `err`) if the
// buffer isn't a valid WAV, or doesn't match 16 kHz/16-bit mono.
bool parseWav(const uint8_t *buf, size_t len, WavView &out, const char *&err);

} // namespace Storage
```

Validation is deliberately strict for the spike: reject anything that isn't
exactly 16000 Hz / 16-bit / mono (matching `SfxData.h`), with a descriptive
`err` string, rather than resampling or converting. This keeps
`Sound::playPcm()` untouched.

### `lib/Storage/SdCard.{h,cpp}` — thin ESP32-only wrapper

```cpp
namespace Storage {

// No-op / returns false if any pin is -1 (card absent per BoardProfile).
bool sdBegin(int8_t csPin, int8_t mosiPin, int8_t misoPin, int8_t sckPin);

// Lists entries under path (used for the "list /sfx/" proof); returns count.
size_t sdList(const char *path, void (*onEntry)(const char *name));

// Reads a whole file into a heap buffer. Caller frees with `free()`.
// Returns nullptr + len=0 on any failure (not found, read error).
uint8_t *sdReadFile(const char *path, size_t &len);

} // namespace Storage
```

Implemented with the Arduino `SD` library (`SPI.h` + `SD.h`, SdFat
underneath) — bundled with the Arduino-ESP32 core, matches the handoff's
plain "SD" option, least new surface area for a spike. `SD.begin(csPin)`
after `SPI.begin(sckPin, misoPin, mosiPin, csPin)`.

Not native-tested — hardware-only, exercised via the serial verb, same as
`Sound`'s I2sDac path.

## Data Flow

```
serial "sdplay" verb
  -> Storage::sdBegin(profile.sdCsPin, ...)      [once, lazy on first use]
  -> Storage::sdReadFile("/sfx/test.wav", len)   -> heap buffer or nullptr
  -> Storage::parseWav(buffer, len, view, err)   -> WavView or false
  -> Sound::playPcm(view.pcm, view.sampleCount)  [UNCHANGED]
  -> free(buffer)
```

`Sound::playPcm` is `static` (file-local) in `Sound.cpp` today. The spike adds
`Sound::playRaw(const int16_t *data, size_t samples)` to `Sound.h`/`.cpp` — a
thin public passthrough to the existing (still-static) `playPcm`, same
signature — so `matrix_main.cpp`'s `sdplay` verb handler can call it without
touching `playPcm`'s internals (start/write/drain/stop).

## BoardProfile Changes

Add four fields (`int8_t`, `-1` = absent, matching `i2sBclkPin` etc.):

```cpp
int8_t sdCsPin, sdMosiPin, sdMisoPin, sdSckPin;
```

`kS3Matrix`: `36, 34, 35, 33`. `kLolin32`: `-1, -1, -1, -1`.

`hasSdCard` (existing `bool` field) becomes **computed**, not hand-set:
`hasSdCard = (sdCsPin >= 0)`. Small in-scope cleanup — same struct, avoids a
field that could disagree with the pins.

Extend `applyOverride`'s whitelist with `sdCsPin`/`sdMosiPin`/`sdMisoPin`/
`sdSckPin`, following the exact pattern already used for `i2sBclkPin` etc.
(`validPin` range check, returns `true`/`false`).

## Error Handling

Every failure is a **logged no-op** — nothing crashes, nothing blocks the
game loop:

| Failure | Behaviour |
|---|---|
| `sdCsPin == -1` (no card wired) | `sdplay` logs "no SD card configured", returns immediately. `SdCard` functions never called. |
| `SD.begin()` fails (no card inserted / bad wiring) | `sdBegin` returns false, logged, verb aborts. |
| File not found | `sdReadFile` returns `nullptr`, logged with the path. |
| Corrupt/truncated/wrong-format WAV | `parseWav` returns false with a reason string (e.g. "not RIFF/WAVE", "unsupported rate 44100 != 16000", "missing data chunk"), logged. |

## Testing

- **Native** (`pio test -e native`, joins the existing suite): new
  `test_storage` suite for `WavFile::parseWav` against hand-built in-memory
  fixtures — valid
  16 kHz/16-bit/mono header, wrong sample rate, wrong bit depth, wrong channel
  count, truncated buffer, missing `data` chunk, garbage (non-RIFF) buffer.
- **Hardware**: one physical microSD card, `/sfx/test.wav` rendered via the
  existing `tools/gen_sfx.py` (reuse its synth, write one clip as a `.wav`
  instead of/alongside the baked header). `sdplay` verb on the S3, verify via
  serial log (mount success, file size, parsed format, `[sfx]`-style play
  log) and audible output — same verification style as this session's
  `sfx <idx>` work.

## Backlog (explicitly deferred, not part of this spike)

- **Streaming WAV playback from SD**: chunked reads feeding I2S DMA directly
  (no full-file heap load), for clips too large to load whole. This spike
  deliberately chose load-whole-file-then-`playPcm` for simplicity; streaming
  is a real feature to spec later once the need (larger clips) is concrete.
- Runtime SFX bank load from `/sfx/*.wav` replacing baked `SfxData.h`,
  keeping the baked bank as the no-card fallback.
- `/config.json` mirroring NVS config (teamSfx/deathSfx/startHp/board
  overrides), with the SD-on-boot vs NVS-wins precedence rule settled.
- SD_MMC (1-bit/4-bit) vs plain SPI evaluation — this spike uses plain SPI
  only.
- Lolin32 SD support (S3-only for this spike; the board has spare pins if
  revisited).
- Card hot-swap / removal-during-play handling.
