# RF Protocol Analysis Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Detect, capture and document the Vatos kit's 2.4 GHz link, using an ESP8266-hosted nRF24L01+ as a probe and a .NET analysis pipeline.

**Architecture:** A register-level nRF24 driver (`lib/Nrf24Raw`) runs under a serial-command probe firmware (`src/rf_probe.cpp`) that emits `SCAN`/`RF` lines in the existing firmware line-protocol style. A pure .NET library (`LaserTag.Rf`) parses those lines and does the offline work — bit-shift realignment, CRC16 validation, address recovery — with an interactive capture app (`tools/RfTrainer`) on top for labelled captures.

**Tech Stack:** PlatformIO / Arduino (espressif8266, `nodemcuv2`), C++17, Unity native tests, .NET 10, xUnit, Spectre.Console.

## Global Constraints

- Scope is **receive-only**: detect, capture, document. No transmitting at the Vatos kit, no spoofing, no pairing. (Spec §1, §8.)
- Target hardware is the user's own kit only. (Spec §8.)
- Probe wiring is fixed: CE=GPIO4, CSN=GPIO5, SCK=GPIO14, MOSI=GPIO13, MISO=GPIO12, IRQ unconnected, VCC=3V3. Never GPIO15/GPIO2 for CE or CSN — boot-strapping pins. (Spec §3.)
- The RPD threshold is approximately −64 dBm: all capture work happens within a few metres of the kit. (Spec §4.)
- All .NET projects target **net10.0**, matching the rest of the solution. Ad-hoc analysis can use .NET 10 file-based apps (`dotnet run foo.cs` with a `#:project` directive) rather than scaffolding throwaway projects.
- Occupancy figures must be normalised by sample count before any A/B comparison, and any candidate found by sweeping must be confirmed by dwelling on it. Both retracted candidates on 2026-07-28 came from skipping that second step.
- Do not use the RF24 Arduino library — it clamps address width to 3–5 bytes and cannot express the 2-byte promiscuous trick. (Spec §5.)
- Failure must be specific, never silent: an empty scan reports "no channel exceeded the RPD threshold", not "no traffic". (Spec §6.)
- `lib/Nrf24Raw` must not depend on ESP8266-specific headers beyond `SPI.h`/`Arduino.h`, so it ports to the ESP32 boards unchanged. (Spec §3.)

---

### Task 0: Phase 0 — confirm the silicon (hard gate, no code)

**Files:**
- Create: `docs/rf-protocol.md` (findings stub only)

This task is a gate. If it fails, tasks 1–7 are invalidated — an nRF24-based
sniffer structurally cannot decode XN297 or BK2425 traffic, and its silence
would be misread as "the kit is quiet".

- [ ] **Step 1: BLE sweep with the kit off**

Use a phone BLE scanner (nRF Connect, LightBlue) or `hcitool lescan`. Record the
list of advertising devices with the Vatos kit **powered off**. This is the
baseline — without it, any device seen later is unattributable.

- [ ] **Step 2: BLE sweep with the kit on and firing**

Repeat with a gun and vest powered and firing. Any new advertiser that appears
and disappears with the kit means the link is BLE, not nRF24 — stop and re-plan
around an nRF52840 or CC2531 sniffer.

- [ ] **Step 3: Read the chip marking**

Open one gun and one vest. Photograph the 2.4 GHz section and record the
marking on the radio IC and any module can. Expected outcomes:

| Marking | Meaning | Action |
| ------- | ------- | ------ |
| nRF24L01 / nRF24L01+ / SI24R1 / RFX24C01 | nRF24-compatible | Proceed to Task 1 |
| XN297 / XN297L | Scrambled preamble, **not** bit-compatible | Stop; re-plan (SDR or XN297-capable radio) |
| BK2425 / BK2401 / BK5811 | Beken; partially compatible, different preamble handling | Stop; re-plan |
| An SoC with no separate radio (e.g. BLE part) | BLE or proprietary | Stop; re-plan |

- [ ] **Step 4: Record the verdict**

Create `docs/rf-protocol.md` with a "Phase 0 — hardware identification" section
recording: what the BLE sweeps showed, the chip markings with photos
referenced, and the resulting go/no-go decision with the date. Confirmed facts
and speculation must be visibly separated, matching the style of
`docs/gun-protocol.md`.

- [ ] **Step 5: Commit**

```bash
git add docs/rf-protocol.md
git commit -m "docs: Phase 0 RF hardware identification (go/no-go)"
```

---

### Task 1: PlatformIO env + Nrf24Raw register access + `selftest`

**Files:**
- Create: `lib/Nrf24Raw/Nrf24Raw.h`, `lib/Nrf24Raw/Nrf24Raw.cpp`
- Create: `src/rf_probe.cpp`
- Modify: `platformio.ini` (append a new env)

**Interfaces:**
- Produces: `class Nrf24Raw` with `void begin(uint8_t cePin, uint8_t csnPin)`,
  `uint8_t readReg(uint8_t reg)`, `void writeReg(uint8_t reg, uint8_t value)`,
  `void writeReg(uint8_t reg, const uint8_t *buf, uint8_t len)`,
  `void cmd(uint8_t command)`, `void readPayload(uint8_t *buf, uint8_t len)`,
  `void ceHigh()`, `void ceLow()`. Register and command constants are exposed as
  `Nrf24Raw::kConfig` etc.

- [ ] **Step 1: Write the driver header**

Create `lib/Nrf24Raw/Nrf24Raw.h`:

```cpp
#pragma once

#include <Arduino.h>

/**
 * Register-level nRF24L01+ access.
 *
 * Deliberately not the RF24 Arduino library: RF24 clamps the address width to
 * 3-5 bytes, so it cannot express the 2-byte promiscuous-mode trick this probe
 * depends on. Everything here is raw register I/O, which also keeps the driver
 * portable to the ESP32 boards (only Arduino.h + SPI.h are used).
 */
class Nrf24Raw
{
public:
    // Registers.
    static constexpr uint8_t kConfig = 0x00;
    static constexpr uint8_t kEnAa = 0x01;
    static constexpr uint8_t kEnRxaddr = 0x02;
    static constexpr uint8_t kSetupAw = 0x03;
    static constexpr uint8_t kSetupRetr = 0x04;
    static constexpr uint8_t kRfCh = 0x05;
    static constexpr uint8_t kRfSetup = 0x06;
    static constexpr uint8_t kStatus = 0x07;
    static constexpr uint8_t kRpd = 0x09;
    static constexpr uint8_t kRxAddrP0 = 0x0A;
    static constexpr uint8_t kRxPwP0 = 0x11;
    static constexpr uint8_t kFifoStatus = 0x17;

    // Commands.
    static constexpr uint8_t kRRxPayload = 0x61;
    static constexpr uint8_t kFlushRx = 0xE2;
    static constexpr uint8_t kFlushTx = 0xE1;
    static constexpr uint8_t kNop = 0xFF;

    // STATUS bits.
    static constexpr uint8_t kRxDr = 0x40;

    /// Starts SPI and configures the CE/CSN pins. CE is left low (standby).
    void begin(uint8_t cePin, uint8_t csnPin);

    /// Reads a single-byte register.
    uint8_t readReg(uint8_t reg);

    /// Writes a single-byte register.
    void writeReg(uint8_t reg, uint8_t value);

    /// Writes a multi-byte register (e.g. an address), LSB first.
    void writeReg(uint8_t reg, const uint8_t *buf, uint8_t len);

    /// Issues a bare command byte (FLUSH_RX, FLUSH_TX, NOP).
    void cmd(uint8_t command);

    /// Reads len bytes of the top RX FIFO payload.
    void readPayload(uint8_t *buf, uint8_t len);

    void ceHigh();
    void ceLow();

private:
    uint8_t cePin_ = 0;
    uint8_t csnPin_ = 0;

    void select();
    void deselect();
};
```

- [ ] **Step 2: Write the driver implementation**

Create `lib/Nrf24Raw/Nrf24Raw.cpp`:

```cpp
#include "Nrf24Raw.h"

#include <SPI.h>

namespace
{
// The nRF24L01+ tolerates 10 MHz; 8 MHz leaves margin for dupont wiring.
constexpr uint32_t kSpiHz = 8000000;
const SPISettings kSpi(kSpiHz, MSBFIRST, SPI_MODE0);
}

void Nrf24Raw::begin(uint8_t cePin, uint8_t csnPin)
{
    cePin_ = cePin;
    csnPin_ = csnPin;
    pinMode(cePin_, OUTPUT);
    pinMode(csnPin_, OUTPUT);
    digitalWrite(cePin_, LOW);
    digitalWrite(csnPin_, HIGH); // CSN idles high.
    SPI.begin();
    delay(5); // Power-on-reset settle.
}

void Nrf24Raw::select()
{
    SPI.beginTransaction(kSpi);
    digitalWrite(csnPin_, LOW);
}

void Nrf24Raw::deselect()
{
    digitalWrite(csnPin_, HIGH);
    SPI.endTransaction();
}

uint8_t Nrf24Raw::readReg(uint8_t reg)
{
    select();
    SPI.transfer(reg & 0x1F); // R_REGISTER is 0x00 | reg.
    uint8_t value = SPI.transfer(kNop);
    deselect();
    return value;
}

void Nrf24Raw::writeReg(uint8_t reg, uint8_t value)
{
    select();
    SPI.transfer(0x20 | (reg & 0x1F)); // W_REGISTER.
    SPI.transfer(value);
    deselect();
}

void Nrf24Raw::writeReg(uint8_t reg, const uint8_t *buf, uint8_t len)
{
    select();
    SPI.transfer(0x20 | (reg & 0x1F));
    for (uint8_t i = 0; i < len; ++i)
    {
        SPI.transfer(buf[i]);
    }

    deselect();
}

void Nrf24Raw::cmd(uint8_t command)
{
    select();
    SPI.transfer(command);
    deselect();
}

void Nrf24Raw::readPayload(uint8_t *buf, uint8_t len)
{
    select();
    SPI.transfer(kRRxPayload);
    for (uint8_t i = 0; i < len; ++i)
    {
        buf[i] = SPI.transfer(kNop);
    }

    deselect();
}

void Nrf24Raw::ceHigh()
{
    digitalWrite(cePin_, HIGH);
}

void Nrf24Raw::ceLow()
{
    digitalWrite(cePin_, LOW);
}
```

- [ ] **Step 3: Write the probe firmware with `selftest`**

Create `src/rf_probe.cpp`:

```cpp
/**
 * RF probe firmware (ESP8266 + nRF24L01+).
 *
 * Serial command surface for 2.4 GHz reconnaissance of the Vatos kit's
 * data-sync link. See docs/superpowers/specs/2026-07-28-rf-protocol-analysis-design.md.
 *
 * Wiring: CE=GPIO4 (D2), CSN=GPIO5 (D1), SCK=GPIO14 (D5), MOSI=GPIO13 (D7),
 * MISO=GPIO12 (D6), IRQ unconnected, VCC=3V3 with a 10uF cap at the module.
 */

#include <Arduino.h>

#include "Nrf24Raw.h"

namespace
{
constexpr uint8_t kCePin = 4;
constexpr uint8_t kCsnPin = 5;

Nrf24Raw radio;

/// Reads back written registers so a wiring fault is visible, not inferred.
void commandSelfTest()
{
    // 0x2A and 0x03 are arbitrary but distinctive: neither is a reset default,
    // so reading them back proves a real write/read round trip rather than a
    // floating bus returning a plausible value.
    radio.writeReg(Nrf24Raw::kRfCh, 0x2A);
    radio.writeReg(Nrf24Raw::kSetupAw, 0x03);
    uint8_t ch = radio.readReg(Nrf24Raw::kRfCh);
    uint8_t aw = radio.readReg(Nrf24Raw::kSetupAw);
    uint8_t config = radio.readReg(Nrf24Raw::kConfig);
    uint8_t status = radio.readReg(Nrf24Raw::kStatus);

    Serial.printf("SELFTEST rfch=0x%02X(exp 0x2A) setupaw=0x%02X(exp 0x03) config=0x%02X status=0x%02X\n",
                  ch, aw, config, status);

    if (ch == 0x2A && aw == 0x03)
    {
        Serial.println(F("SELFTEST ok — radio is responding"));
    }
    else if ((ch == 0x00 && aw == 0x00) || (ch == 0xFF && aw == 0xFF))
    {
        Serial.println(F("SELFTEST FAIL — all 0x00/0xFF: check MISO/MOSI not swapped, CSN wiring, and 3V3 power"));
    }
    else
    {
        Serial.println(F("SELFTEST FAIL — readback mismatch: check SCK/CSN wiring and keep leads under 10cm"));
    }
}
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    radio.begin(kCePin, kCsnPin);
    Serial.println(F("RF probe ready — commands: selftest"));
}

void loop()
{
    if (!Serial.available())
    {
        return;
    }

    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line == "selftest")
    {
        commandSelfTest();
    }
    else if (line.length() > 0)
    {
        Serial.println(F("unknown command — try: selftest"));
    }
}
```

- [ ] **Step 4: Add the PlatformIO env**

Append to `platformio.ini`:

```ini
; --- ESP8266 RF probe ------------------------------------------------------
; nRF24L01+ pulled from the LC Technology NRF24L01-TTL_V2 adaptor and wired to
; an ESP8266 (CP210x). Receive-only 2.4GHz reconnaissance; see
; docs/superpowers/specs/2026-07-28-rf-protocol-analysis-design.md.
; Wiring: CE=GPIO4 CSN=GPIO5 SCK=GPIO14 MOSI=GPIO13 MISO=GPIO12 (IRQ unused).
[env:esp8266-rfprobe]
platform = espressif8266
board = nodemcuv2
framework = arduino
monitor_speed = 115200
build_src_filter = +<rf_probe.cpp>
```

- [ ] **Step 5: Build**

Run: `pio run -e esp8266-rfprobe`
Expected: SUCCESS. (Builds are slow on this machine — Defender — so allow
several minutes and run it in the background.)

Also confirm the existing envs still build, since `build_src_filter` changes are
easy to get wrong: `pio run -e esp32-s3-matrix`.

- [ ] **Step 6: Flash and bench-verify**

Run: `pio run -e esp8266-rfprobe -t upload --upload-port COM6`
Then open the monitor and type `selftest`.
Expected: `SELFTEST rfch=0x2A(exp 0x2A) setupaw=0x03(exp 0x03) …` followed by
`SELFTEST ok`. If it reports all `0x00`/`0xFF`, fix the wiring before going on —
every later task assumes a working SPI link.

- [ ] **Step 7: Commit**

```bash
git add lib/Nrf24Raw src/rf_probe.cpp platformio.ini
git commit -m "feat(rf): ESP8266 nRF24 probe env + register-level driver + selftest"
```

---

### Task 2: RPD channel scan

**Files:**
- Modify: `src/rf_probe.cpp`

**Interfaces:**
- Consumes: `Nrf24Raw` from Task 1.
- Produces: serial command `scan [sweeps]` emitting one `SCAN ch=<n> hits=<n>`
  line per channel with a non-zero count, then `SCAN done sweeps=<n> active=<n>`.

- [ ] **Step 1: Add the scan command**

Add to the anonymous namespace in `src/rf_probe.cpp`, above `commandSelfTest`:

```cpp
constexpr uint8_t kChannels = 126; // nRF24 channels 0..125 = 2400..2525 MHz.

/// Configures the radio as a bare receiver for RPD sampling.
void configureForScan()
{
    radio.ceLow();
    radio.writeReg(Nrf24Raw::kEnAa, 0x00);       // No auto-ack.
    radio.writeReg(Nrf24Raw::kEnRxaddr, 0x00);   // No pipes: we only want RPD.
    radio.writeReg(Nrf24Raw::kRfSetup, 0x00);    // 1 Mbps, 0 dBm.
    radio.writeReg(Nrf24Raw::kConfig, 0x03);     // PWR_UP | PRIM_RX, CRC off.
    delay(2);                                    // Power-up settle (1.5ms).
}

/**
 * Sweeps every channel counting RPD trips.
 *
 * The RPD latches when input power exceeds roughly -64 dBm, so this finds
 * occupied channels but only at close range — a few metres at most.
 */
void commandScan(uint16_t sweeps)
{
    uint16_t hits[kChannels] = {0};
    configureForScan();

    for (uint16_t s = 0; s < sweeps; ++s)
    {
        for (uint8_t ch = 0; ch < kChannels; ++ch)
        {
            radio.writeReg(Nrf24Raw::kRfCh, ch);
            radio.ceHigh();
            delayMicroseconds(400); // 130us PLL settle + RPD integration.
            radio.ceLow();
            if (radio.readReg(Nrf24Raw::kRpd) & 0x01)
            {
                ++hits[ch];
            }
        }

        yield(); // ESP8266: feed the soft WDT during long sweeps.
    }

    uint8_t active = 0;
    for (uint8_t ch = 0; ch < kChannels; ++ch)
    {
        if (hits[ch] > 0)
        {
            ++active;
            Serial.printf("SCAN ch=%u mhz=%u hits=%u\n", ch, 2400u + ch, hits[ch]);
        }
    }

    Serial.printf("SCAN done sweeps=%u active=%u\n", sweeps, active);
    if (active == 0)
    {
        Serial.println(F("SCAN empty — no channel exceeded the RPD threshold (~-64dBm). "
                         "This is NOT proof of silence: move within a couple of metres and retry."));
    }
}
```

- [ ] **Step 2: Wire it into the command loop**

In `loop()`, replace the `unknown command` branch chain so it reads:

```cpp
    if (line == "selftest")
    {
        commandSelfTest();
    }
    else if (line == "scan" || line.startsWith("scan "))
    {
        long sweeps = line.length() > 5 ? line.substring(5).toInt() : 0;
        commandScan(sweeps > 0 ? static_cast<uint16_t>(sweeps) : 20);
    }
    else if (line.length() > 0)
    {
        Serial.println(F("unknown command — try: selftest, scan [sweeps]"));
    }
```

Also update the ready banner in `setup()` to
`Serial.println(F("RF probe ready — commands: selftest, scan [sweeps]"));`

- [ ] **Step 3: Build and flash**

Run: `pio run -e esp8266-rfprobe -t upload --upload-port COM6`
Expected: SUCCESS.

- [ ] **Step 4: Bench-verify against a known transmitter**

This is the ground-truth step — verify the scanner against a radio we control
*before* pointing it at the kit, so that a later silence is evidence about the
kit rather than about our code.

1. Run `scan 20` with everything nearby powered off. Note the baseline: WiFi
   occupies wide swathes around channels 1–22, 26–48 and 51–73 (2401–2473 MHz),
   so expect some hits. Record which channels are quiet.
2. Plug the LC Technology board back in, set it to a known quiet channel with
   `AT+FREQ=2.505G` (channel 105) at 115200 baud, and send characters through it.
3. Run `scan 20` again. Expected: a clear hit spike at channel 105 that was
   quiet in the baseline.

If step 3 shows no spike, the scanner is wrong — fix it before Task 3.

- [ ] **Step 5: Capture the kit**

Run `scan 40` with the Vatos kit off, then again with it powered and firing,
from within two metres. Save both outputs. A channel that is quiet in the first
and busy in the second is the candidate. Record both in `docs/rf-protocol.md`.

- [ ] **Step 6: Commit**

```bash
git add src/rf_probe.cpp docs/rf-protocol.md
git commit -m "feat(rf): RPD channel scan + baseline vs kit-active captures"
```

---

### Task 3: Promiscuous capture

**Files:**
- Modify: `src/rf_probe.cpp`

**Interfaces:**
- Consumes: `Nrf24Raw` from Task 1.
- Produces: serial command `sniff ch=<n> rate=<250k|1m|2m>` emitting
  `RF ch=<n> rate=<r> ts=<micros> n=32 data=<64 hex chars>` per capture, ended by
  any keypress, then `SNIFF done captured=<n>`.

- [ ] **Step 1: Add the sniff command**

Add to the anonymous namespace in `src/rf_probe.cpp`:

```cpp
/// Maps a rate token to its RF_SETUP bits. Returns false if unrecognised.
bool rateBits(const String &token, uint8_t &bits)
{
    if (token == "1m")
    {
        bits = 0x00; // RF_DR_LOW=0, RF_DR_HIGH=0.
    }
    else if (token == "2m")
    {
        bits = 0x08; // RF_DR_HIGH.
    }
    else if (token == "250k")
    {
        bits = 0x20; // RF_DR_LOW.
    }
    else
    {
        return false;
    }

    return true;
}

/**
 * Promiscuous capture (Goodspeed): an illegal 2-byte address width with the
 * pseudo-address 0x00AA and CRC disabled makes the radio accept any burst whose
 * bits happen to match, spilling the real 5-byte address into the payload.
 *
 * Roughly one capture in twenty is a genuine packet; the rest is noise that the
 * host-side CRC check filters out. That yield is expected, not a fault.
 */
void commandSniff(uint8_t channel, const String &rateToken)
{
    uint8_t bits = 0;
    if (!rateBits(rateToken, bits))
    {
        Serial.println(F("SNIFF FAIL — rate must be 250k, 1m or 2m"));
        return;
    }

    radio.ceLow();
    radio.writeReg(Nrf24Raw::kEnAa, 0x00);      // Auto-ack off: it needs a real address.
    radio.writeReg(Nrf24Raw::kSetupRetr, 0x00); // No retransmit.
    radio.writeReg(Nrf24Raw::kSetupAw, 0x00);   // Illegal 2-byte width: the trick.
    radio.writeReg(Nrf24Raw::kRfSetup, bits);
    radio.writeReg(Nrf24Raw::kRfCh, channel);

    const uint8_t pseudoAddress[2] = {0xAA, 0x00}; // LSB first = 0x00AA.
    radio.writeReg(Nrf24Raw::kRxAddrP0, pseudoAddress, sizeof(pseudoAddress));
    radio.writeReg(Nrf24Raw::kRxPwP0, 32);
    radio.writeReg(Nrf24Raw::kEnRxaddr, 0x01);
    radio.writeReg(Nrf24Raw::kConfig, 0x03); // PWR_UP | PRIM_RX, EN_CRC cleared.
    delay(2);
    radio.cmd(Nrf24Raw::kFlushRx);
    radio.ceHigh();

    Serial.printf("SNIFF start ch=%u rate=%s — send any line to stop\n", channel, rateToken.c_str());
    uint32_t captured = 0;
    uint8_t payload[32];
    while (!Serial.available())
    {
        if ((radio.readReg(Nrf24Raw::kStatus) & Nrf24Raw::kRxDr) == 0)
        {
            yield();
            continue;
        }

        radio.readPayload(payload, sizeof(payload));
        radio.writeReg(Nrf24Raw::kStatus, Nrf24Raw::kRxDr); // Write 1 to clear.

        Serial.printf("RF ch=%u rate=%s ts=%lu n=32 data=", channel, rateToken.c_str(), micros());
        for (uint8_t i = 0; i < sizeof(payload); ++i)
        {
            Serial.printf("%02X", payload[i]);
        }

        Serial.println();
        ++captured;

        // The RX FIFO holds three packets; flushing keeps the stream fresh
        // rather than replaying a backlog after a burst.
        radio.cmd(Nrf24Raw::kFlushRx);
    }

    while (Serial.available())
    {
        Serial.read();
    }

    radio.ceLow();
    Serial.printf("SNIFF done captured=%lu\n", captured);
}
```

- [ ] **Step 2: Parse the sniff arguments in the command loop**

Add before the final `else if (line.length() > 0)` branch:

```cpp
    else if (line.startsWith("sniff "))
    {
        int chAt = line.indexOf("ch=");
        int rateAt = line.indexOf("rate=");
        if (chAt < 0 || rateAt < 0)
        {
            Serial.println(F("usage: sniff ch=<0-125> rate=<250k|1m|2m>"));
        }
        else
        {
            long ch = line.substring(chAt + 3).toInt();
            String rate = line.substring(rateAt + 5);
            rate.trim();
            if (ch < 0 || ch > 125)
            {
                Serial.println(F("SNIFF FAIL — channel must be 0-125"));
            }
            else
            {
                commandSniff(static_cast<uint8_t>(ch), rate);
            }
        }
    }
```

Update the banner and the unknown-command hint to list
`selftest, scan [sweeps], sniff ch= rate=`.

- [ ] **Step 3: Build and flash**

Run: `pio run -e esp8266-rfprobe -t upload --upload-port COM6`
Expected: SUCCESS.

- [ ] **Step 4: Bench-verify against the LC board**

Set the LC board to channel 105 at 1 Mbps with a known address
(`AT+TXA=` / `AT+RXA=`) and stream a repeating string through it. Run
`sniff ch=105 rate=1m`. Expected: `RF …` lines appear, and among the noise the
LC board's configured address bytes recur. The rate must match exactly — a
2 Mbps transmitter is invisible to a 1 Mbps listener, which is the single most
common reason a sniff session looks dead.

- [ ] **Step 5: Commit**

```bash
git add src/rf_probe.cpp
git commit -m "feat(rf): promiscuous capture command (2-byte address, CRC off)"
```

---

### Task 4: LaserTag.Rf — line parsing and CRC16

**Files:**
- Create: `dotnet/LaserTag.Rf/LaserTag.Rf.csproj`, `dotnet/LaserTag.Rf/RfCapture.cs`, `dotnet/LaserTag.Rf/RfLineParser.cs`, `dotnet/LaserTag.Rf/Nrf24Crc.cs`
- Create: `dotnet/LaserTag.Rf.Tests/LaserTag.Rf.Tests.csproj`, `dotnet/LaserTag.Rf.Tests/RfLineParserTests.cs`, `dotnet/LaserTag.Rf.Tests/Nrf24CrcTests.cs`
- Modify: `dotnet/LaserTag.sln`

**Interfaces:**
- Produces: `record RfCapture(int Channel, string Rate, long TimestampUs, byte[] Data)`;
  `static bool RfLineParser.TryParse(string line, out RfCapture capture)`;
  `static ushort Nrf24Crc.Compute(ReadOnlySpan<byte> data, int bitLength)`.

- [ ] **Step 1: Create the projects and add them to the solution**

```bash
cd dotnet
dotnet new classlib -n LaserTag.Rf -f net10.0
dotnet new xunit -n LaserTag.Rf.Tests -f net10.0
dotnet add LaserTag.Rf.Tests reference LaserTag.Rf
dotnet sln LaserTag.sln add LaserTag.Rf LaserTag.Rf.Tests
rm LaserTag.Rf/Class1.cs LaserTag.Rf.Tests/UnitTest1.cs
```

- [ ] **Step 2: Write the failing parser test**

Create `dotnet/LaserTag.Rf.Tests/RfLineParserTests.cs`:

```csharp
using LaserTag.Rf;

namespace LaserTag.Rf.Tests;

public class RfLineParserTests
{
    [Fact]
    public void TryParse_ValidRfLine_ReturnsCapture()
    {
        bool ok = RfLineParser.TryParse(
            "RF ch=76 rate=1m ts=1234567 n=4 data=AABBCCDD", out RfCapture capture);

        Assert.True(ok);
        Assert.Equal(76, capture.Channel);
        Assert.Equal("1m", capture.Rate);
        Assert.Equal(1234567, capture.TimestampUs);
        Assert.Equal(new byte[] { 0xAA, 0xBB, 0xCC, 0xDD }, capture.Data);
    }

    [Theory]
    [InlineData("SCAN ch=76 mhz=2476 hits=3")]
    [InlineData("RF ch=76 rate=1m ts=1 n=4 data=ZZZZ")]
    [InlineData("RF ch=76 rate=1m ts=1 n=4")]
    [InlineData("")]
    public void TryParse_NonCaptureLines_ReturnsFalse(string line)
    {
        Assert.False(RfLineParser.TryParse(line, out _));
    }

    [Fact]
    public void TryParse_OddLengthHex_ReturnsFalse()
    {
        // A truncated line from a mid-transmission serial connect must be
        // rejected rather than silently half-decoded.
        Assert.False(RfLineParser.TryParse("RF ch=1 rate=1m ts=1 n=2 data=ABC", out _));
    }
}
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `dotnet test dotnet/LaserTag.Rf.Tests`
Expected: FAIL — `RfLineParser` does not exist.

- [ ] **Step 4: Implement the capture record and parser**

Create `dotnet/LaserTag.Rf/RfCapture.cs`:

```csharp
namespace LaserTag.Rf;

/// <summary>
/// One promiscuous-mode capture emitted by the RF probe firmware.
/// </summary>
/// <param name="Channel">nRF24 channel 0-125 (2400 + channel MHz).</param>
/// <param name="Rate">Air data rate token: 250k, 1m or 2m.</param>
/// <param name="TimestampUs">Probe-side microsecond timestamp.</param>
/// <param name="Data">Raw captured bytes, not yet realigned or validated.</param>
public record RfCapture(int Channel, string Rate, long TimestampUs, byte[] Data);
```

Create `dotnet/LaserTag.Rf/RfLineParser.cs`:

```csharp
using System.Globalization;

namespace LaserTag.Rf;

/// <summary>
/// Parses the RF probe firmware's serial line protocol.
/// </summary>
public static class RfLineParser
{
    /// <summary>
    /// Attempts to parse one <c>RF …</c> capture line.
    /// </summary>
    /// <param name="line">A single line of probe output.</param>
    /// <param name="capture">The parsed capture when this returns true.</param>
    /// <returns>True if the line was a well-formed capture line.</returns>
    public static bool TryParse(string line, out RfCapture capture)
    {
        capture = default!;
        if (string.IsNullOrWhiteSpace(line) || !line.StartsWith("RF ", StringComparison.Ordinal))
        {
            return false;
        }

        int? channel = null;
        string? rate = null;
        long? ts = null;
        string? hex = null;
        foreach (string token in line.Split(' ', StringSplitOptions.RemoveEmptyEntries).Skip(1))
        {
            int eq = token.IndexOf('=');
            if (eq <= 0)
            {
                continue;
            }

            string key = token[..eq];
            string value = token[(eq + 1)..];
            switch (key)
            {
                case "ch" when int.TryParse(value, out int c):
                    channel = c;
                    break;
                case "rate":
                    rate = value;
                    break;
                case "ts" when long.TryParse(value, out long t):
                    ts = t;
                    break;
                case "data":
                    hex = value;
                    break;
            }
        }

        if (channel is null || rate is null || ts is null || hex is null || hex.Length % 2 != 0)
        {
            return false;
        }

        var data = new byte[hex.Length / 2];
        for (int i = 0; i < data.Length; ++i)
        {
            if (!byte.TryParse(hex.AsSpan(i * 2, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out data[i]))
            {
                return false;
            }
        }

        capture = new RfCapture(channel.Value, rate, ts.Value, data);
        return true;
    }
}
```

- [ ] **Step 5: Run the parser tests**

Run: `dotnet test dotnet/LaserTag.Rf.Tests`
Expected: PASS.

- [ ] **Step 6: Write the failing CRC test**

Create `dotnet/LaserTag.Rf.Tests/Nrf24CrcTests.cs`:

```csharp
using LaserTag.Rf;

namespace LaserTag.Rf.Tests;

public class Nrf24CrcTests
{
    [Fact]
    public void Compute_KnownVector_MatchesCcittFalse()
    {
        // CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over "123456789" is
        // 0x29B1 — the standard check value, which pins the polynomial and
        // initial value independently of any nRF24 capture.
        byte[] data = "123456789"u8.ToArray();

        Assert.Equal(0x29B1, Nrf24Crc.Compute(data, data.Length * 8));
    }

    [Fact]
    public void Compute_PartialBitLength_IgnoresTrailingBits()
    {
        // nRF24 CRCs cover a non-byte-aligned bit range (address + control +
        // payload), so trailing bits beyond bitLength must not contribute.
        byte[] full = [0xAB, 0xFF];
        byte[] masked = [0xAB, 0x00];

        Assert.Equal(Nrf24Crc.Compute(full, 8), Nrf24Crc.Compute(masked, 8));
    }
}
```

- [ ] **Step 7: Run it to verify it fails**

Run: `dotnet test dotnet/LaserTag.Rf.Tests`
Expected: FAIL — `Nrf24Crc` does not exist.

- [ ] **Step 8: Implement the CRC**

Create `dotnet/LaserTag.Rf/Nrf24Crc.cs`:

```csharp
namespace LaserTag.Rf;

/// <summary>
/// CRC-16/CCITT as used by the nRF24L01+ (polynomial 0x1021, initial 0xFFFF).
/// </summary>
public static class Nrf24Crc
{
    private const ushort Polynomial = 0x1021;

    /// <summary>
    /// Computes the CRC over a bit range, MSB first.
    /// </summary>
    /// <param name="data">The bytes to cover.</param>
    /// <param name="bitLength">
    /// How many bits of <paramref name="data"/> participate. nRF24 packets are
    /// not byte-aligned, so this is a bit count rather than a byte count.
    /// </param>
    /// <returns>The 16-bit CRC.</returns>
    public static ushort Compute(ReadOnlySpan<byte> data, int bitLength)
    {
        ushort crc = 0xFFFF;
        for (int bit = 0; bit < bitLength; ++bit)
        {
            int value = (data[bit / 8] >> (7 - (bit % 8))) & 1;
            bool xorNeeded = ((crc >> 15) & 1) != value;
            crc <<= 1;
            if (xorNeeded)
            {
                crc ^= Polynomial;
            }
        }

        return crc;
    }
}
```

- [ ] **Step 9: Run the tests**

Run: `dotnet test dotnet/LaserTag.Rf.Tests`
Expected: PASS (all four tests).

- [ ] **Step 10: Commit**

```bash
git add dotnet/LaserTag.Rf dotnet/LaserTag.Rf.Tests dotnet/LaserTag.sln
git commit -m "feat(rf): LaserTag.Rf capture line parser + nRF24 CRC16"
```

---

### Task 5: Realignment and address recovery

**Files:**
- Create: `dotnet/LaserTag.Rf/BitShifter.cs`, `dotnet/LaserTag.Rf/AddressRecovery.cs`, `dotnet/LaserTag.Rf/PacketValidator.cs`
- Create: `dotnet/LaserTag.Rf.Tests/BitShifterTests.cs`, `dotnet/LaserTag.Rf.Tests/AddressRecoveryTests.cs`, `dotnet/LaserTag.Rf.Tests/PacketValidatorTests.cs`

**Interfaces:**
- Consumes: `RfCapture` and `Nrf24Crc` from Task 4.
- Produces: `static byte[] BitShifter.ShiftLeft(ReadOnlySpan<byte> data, int bits)`;
  `record AddressCandidate(byte[] Address, int Occurrences)`;
  `static IReadOnlyList<AddressCandidate> AddressRecovery.FindCandidates(IEnumerable<RfCapture> captures, int addressLength = 5, int minOccurrences = 3)`;
  `record ValidatedPacket(byte[] Address, int Pid, bool NoAck, byte[] Payload, int BitShift)`;
  `static bool PacketValidator.TryValidate(ReadOnlySpan<byte> data, int addressLength, out ValidatedPacket packet)`.

- [ ] **Step 1: Write the failing bit-shifter test**

Create `dotnet/LaserTag.Rf.Tests/BitShifterTests.cs`:

```csharp
using LaserTag.Rf;

namespace LaserTag.Rf.Tests;

public class BitShifterTests
{
    [Fact]
    public void ShiftLeft_ZeroBits_ReturnsCopy()
    {
        byte[] data = [0x12, 0x34];

        Assert.Equal(data, BitShifter.ShiftLeft(data, 0));
    }

    [Fact]
    public void ShiftLeft_OneBit_PullsInNextByteMsb()
    {
        // 0x80 0x01 = 1000_0000 0000_0001; shifted left one bit the second
        // byte's MSB (0) enters the first byte's LSB.
        Assert.Equal(new byte[] { 0x00, 0x02 }, BitShifter.ShiftLeft([0x80, 0x01], 1));
    }

    [Fact]
    public void ShiftLeft_SevenBits_RecoversMisalignedPacket()
    {
        // A capture that arrived one bit-position late: shifting realigns it.
        Assert.Equal(new byte[] { 0xFF, 0x80 }, BitShifter.ShiftLeft([0x01, 0xFF], 7));
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `dotnet test dotnet/LaserTag.Rf.Tests --filter BitShifterTests`
Expected: FAIL — `BitShifter` does not exist.

- [ ] **Step 3: Implement the bit shifter**

Create `dotnet/LaserTag.Rf/BitShifter.cs`:

```csharp
namespace LaserTag.Rf;

/// <summary>
/// Bit-level realignment for promiscuous captures.
/// </summary>
/// <remarks>
/// The 2-byte pseudo-address trick can latch a packet one to seven bit
/// positions off, so every candidate must be tried at all eight offsets before
/// concluding it is noise.
/// </remarks>
public static class BitShifter
{
    /// <summary>
    /// Shifts the buffer left by the given number of bits, zero-filling the tail.
    /// </summary>
    /// <param name="data">The bytes to shift.</param>
    /// <param name="bits">How many bits to shift left (0-7).</param>
    /// <returns>A new, shifted buffer of the same length.</returns>
    /// <exception cref="ArgumentOutOfRangeException">
    /// Thrown when <paramref name="bits"/> is outside 0-7.
    /// </exception>
    public static byte[] ShiftLeft(ReadOnlySpan<byte> data, int bits)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(bits);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(bits, 7);

        var result = new byte[data.Length];
        for (int i = 0; i < data.Length; ++i)
        {
            int high = data[i] << bits;
            int low = i + 1 < data.Length ? data[i + 1] >> (8 - bits) : 0;

            // Shifting a byte right by 8 is undefined-ish in C# (it masks the
            // count to 5 bits and returns the original), so guard bits == 0.
            result[i] = (byte)(bits == 0 ? data[i] : (high | low));
        }

        return result;
    }
}
```

- [ ] **Step 4: Run the tests**

Run: `dotnet test dotnet/LaserTag.Rf.Tests --filter BitShifterTests`
Expected: PASS.

- [ ] **Step 5: Write the failing address-recovery test**

Create `dotnet/LaserTag.Rf.Tests/AddressRecoveryTests.cs`:

```csharp
using LaserTag.Rf;

namespace LaserTag.Rf.Tests;

public class AddressRecoveryTests
{
    private static RfCapture Capture(params byte[] data) => new(76, "1m", 0, data);

    [Fact]
    public void FindCandidates_PlantedAddress_RanksItFirst()
    {
        // Five captures share a planted 5-byte address; the noise around it
        // differs each time, so only the address should recur.
        byte[] address = [0xE7, 0xE7, 0xE7, 0xE7, 0xE7];
        var captures = new List<RfCapture>();
        for (byte i = 0; i < 5; ++i)
        {
            captures.Add(Capture([.. address, (byte)(0x10 + i), (byte)(0x20 + i), (byte)(0x30 + i)]));
        }

        IReadOnlyList<AddressCandidate> candidates = AddressRecovery.FindCandidates(captures);

        Assert.Equal(address, candidates[0].Address);
        Assert.Equal(5, candidates[0].Occurrences);
    }

    [Fact]
    public void FindCandidates_BelowMinOccurrences_ReturnsEmpty()
    {
        // Pure noise must yield nothing rather than a confident wrong answer.
        var captures = new List<RfCapture>
        {
            Capture(0x01, 0x02, 0x03, 0x04, 0x05, 0x06),
            Capture(0x11, 0x12, 0x13, 0x14, 0x15, 0x16),
        };

        Assert.Empty(AddressRecovery.FindCandidates(captures));
    }
}
```

- [ ] **Step 6: Run to verify it fails**

Run: `dotnet test dotnet/LaserTag.Rf.Tests --filter AddressRecoveryTests`
Expected: FAIL — `AddressRecovery` does not exist.

- [ ] **Step 7: Implement address recovery**

Create `dotnet/LaserTag.Rf/AddressRecovery.cs`:

```csharp
namespace LaserTag.Rf;

/// <summary>
/// A repeated byte sequence that may be a real nRF24 pipe address.
/// </summary>
/// <param name="Address">The candidate address bytes.</param>
/// <param name="Occurrences">How many captures contained it.</param>
public record AddressCandidate(byte[] Address, int Occurrences);

/// <summary>
/// Recovers real pipe addresses from promiscuous-mode captures.
/// </summary>
/// <remarks>
/// With the address width set illegally short, the target's real address lands
/// inside the payload. It is the one sequence that repeats across otherwise
/// unrelated captures, so frequency counting surfaces it.
/// </remarks>
public static class AddressRecovery
{
    /// <summary>
    /// Ranks repeated byte sequences by how many captures contain them.
    /// </summary>
    /// <param name="captures">Captures to analyse, at any bit alignment.</param>
    /// <param name="addressLength">Address width in bytes (nRF24 uses 3-5).</param>
    /// <param name="minOccurrences">
    /// Discard sequences seen in fewer captures than this. Promiscuous capture
    /// is roughly 19 parts noise to 1 part signal, so a low floor produces
    /// confident nonsense.
    /// </param>
    /// <returns>Candidates, most frequent first.</returns>
    public static IReadOnlyList<AddressCandidate> FindCandidates(
        IEnumerable<RfCapture> captures,
        int addressLength = 5,
        int minOccurrences = 3)
    {
        var counts = new Dictionary<string, (byte[] Address, int Count)>();
        foreach (RfCapture capture in captures)
        {
            // Count each distinct sequence once per capture: a sequence that
            // repeats inside one noisy packet is not corroborating evidence.
            var seenHere = new HashSet<string>();
            for (int offset = 0; offset + addressLength <= capture.Data.Length; ++offset)
            {
                byte[] slice = capture.Data[offset..(offset + addressLength)];
                string key = Convert.ToHexString(slice);
                if (!seenHere.Add(key))
                {
                    continue;
                }

                counts[key] = counts.TryGetValue(key, out var existing)
                    ? (existing.Address, existing.Count + 1)
                    : (slice, 1);
            }
        }

        return counts.Values
            .Where(v => v.Count >= minOccurrences)
            .OrderByDescending(v => v.Count)
            .ThenBy(v => Convert.ToHexString(v.Address), StringComparer.Ordinal)
            .Select(v => new AddressCandidate(v.Address, v.Count))
            .ToList();
    }
}
```

- [ ] **Step 8: Write the failing packet-validator test**

Create `dotnet/LaserTag.Rf.Tests/PacketValidatorTests.cs`:

```csharp
using LaserTag.Rf;

namespace LaserTag.Rf.Tests;

public class PacketValidatorTests
{
    /// <summary>
    /// Builds a valid Enhanced ShockBurst packet: address, then a 9-bit packet
    /// control field (6-bit length, 2-bit PID, 1-bit no-ack), then payload,
    /// then CRC16 over everything from the address onward.
    /// </summary>
    private static byte[] BuildPacket(byte[] address, byte[] payload, int pid, bool noAck)
    {
        var bits = new List<int>();
        void PushByte(byte b)
        {
            for (int i = 7; i >= 0; --i)
            {
                bits.Add((b >> i) & 1);
            }
        }

        foreach (byte b in address)
        {
            PushByte(b);
        }

        for (int i = 5; i >= 0; --i)
        {
            bits.Add((payload.Length >> i) & 1);
        }

        bits.Add((pid >> 1) & 1);
        bits.Add(pid & 1);
        bits.Add(noAck ? 1 : 0);
        foreach (byte b in payload)
        {
            PushByte(b);
        }

        byte[] covered = new byte[(bits.Count + 7) / 8];
        for (int i = 0; i < bits.Count; ++i)
        {
            covered[i / 8] |= (byte)(bits[i] << (7 - (i % 8)));
        }

        ushort crc = Nrf24Crc.Compute(covered, bits.Count);
        for (int i = 15; i >= 0; --i)
        {
            bits.Add((crc >> i) & 1);
        }

        byte[] packet = new byte[(bits.Count + 7) / 8];
        for (int i = 0; i < bits.Count; ++i)
        {
            packet[i / 8] |= (byte)(bits[i] << (7 - (i % 8)));
        }

        return packet;
    }

    [Fact]
    public void TryValidate_WellFormedPacket_RecoversFields()
    {
        byte[] address = [0xE7, 0xE7, 0xE7, 0xE7, 0xE7];
        byte[] payload = [0xDE, 0xAD, 0xBE, 0xEF];

        bool ok = PacketValidator.TryValidate(
            BuildPacket(address, payload, pid: 2, noAck: false), 5, out ValidatedPacket packet);

        Assert.True(ok);
        Assert.Equal(address, packet.Address);
        Assert.Equal(payload, packet.Payload);
        Assert.Equal(2, packet.Pid);
        Assert.False(packet.NoAck);
        Assert.Equal(0, packet.BitShift);
    }

    [Fact]
    public void TryValidate_BitShiftedPacket_RealignsAndRecovers()
    {
        // Promiscuous capture routinely latches a packet a few bit positions
        // late; the validator must try every offset before giving up.
        byte[] address = [0x11, 0x22, 0x33, 0x44, 0x55];
        byte[] payload = [0x01, 0x02];
        byte[] packet = BuildPacket(address, payload, pid: 1, noAck: true);
        byte[] shifted = new byte[packet.Length + 1];
        for (int i = 0; i < packet.Length; ++i)
        {
            shifted[i] |= (byte)(packet[i] >> 3);
            shifted[i + 1] = (byte)(packet[i] << 5);
        }

        bool ok = PacketValidator.TryValidate(shifted, 5, out ValidatedPacket recovered);

        Assert.True(ok);
        Assert.Equal(address, recovered.Address);
        Assert.Equal(payload, recovered.Payload);
        Assert.Equal(3, recovered.BitShift);
    }

    [Fact]
    public void TryValidate_Noise_ReturnsFalse()
    {
        // ~19 of every 20 promiscuous captures are noise. Rejecting them is the
        // validator's main job, so a false positive here matters more than a miss.
        byte[] noise = [0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55];

        Assert.False(PacketValidator.TryValidate(noise, 5, out _));
    }
}
```

- [ ] **Step 9: Run to verify it fails**

Run: `dotnet test dotnet/LaserTag.Rf.Tests --filter PacketValidatorTests`
Expected: FAIL — `PacketValidator` does not exist.

- [ ] **Step 10: Implement the validator**

Create `dotnet/LaserTag.Rf/PacketValidator.cs`:

```csharp
namespace LaserTag.Rf;

/// <summary>
/// A capture that survived CRC validation as an Enhanced ShockBurst packet.
/// </summary>
/// <param name="Address">The recovered pipe address.</param>
/// <param name="Pid">The 2-bit packet identifier (sequence counter).</param>
/// <param name="NoAck">True when the no-acknowledge flag was set.</param>
/// <param name="Payload">The payload bytes.</param>
/// <param name="BitShift">Bit offset at which the packet was found (0-7).</param>
public record ValidatedPacket(byte[] Address, int Pid, bool NoAck, byte[] Payload, int BitShift);

/// <summary>
/// Separates genuine nRF24 packets from promiscuous-capture noise.
/// </summary>
/// <remarks>
/// An ESB packet is address, then a 9-bit packet control field, then payload,
/// then a CRC16 over everything from the address onward. Because the control
/// field is 9 bits the payload is not byte-aligned, which is why validation
/// works on bit offsets rather than bytes. Recomputing the CRC is the only
/// reliable way to tell a real packet from the roughly 19-in-20 that are noise.
/// </remarks>
public static class PacketValidator
{
    private const int ControlFieldBits = 9;
    private const int CrcBits = 16;

    /// <summary>
    /// Attempts to interpret a raw capture as a valid ESB packet at any bit offset.
    /// </summary>
    /// <param name="data">Raw captured bytes.</param>
    /// <param name="addressLength">Expected address width in bytes (3-5).</param>
    /// <param name="packet">The recovered packet when this returns true.</param>
    /// <returns>True if some bit offset yields a CRC-valid packet.</returns>
    public static bool TryValidate(ReadOnlySpan<byte> data, int addressLength, out ValidatedPacket packet)
    {
        packet = default!;
        for (int shift = 0; shift < 8; ++shift)
        {
            byte[] aligned = BitShifter.ShiftLeft(data, shift);
            int addressBits = addressLength * 8;
            int available = aligned.Length * 8;
            if (available < addressBits + ControlFieldBits + CrcBits)
            {
                return false; // Too short at any offset.
            }

            int payloadLength = (int)ReadBits(aligned, addressBits, 6);
            int coveredBits = addressBits + ControlFieldBits + (payloadLength * 8);
            if (payloadLength == 0 || coveredBits + CrcBits > available)
            {
                continue;
            }

            ushort expected = (ushort)ReadBits(aligned, coveredBits, CrcBits);
            if (Nrf24Crc.Compute(aligned, coveredBits) != expected)
            {
                continue;
            }

            var payload = new byte[payloadLength];
            for (int i = 0; i < payloadLength; ++i)
            {
                payload[i] = (byte)ReadBits(aligned, addressBits + ControlFieldBits + (i * 8), 8);
            }

            packet = new ValidatedPacket(
                aligned[..addressLength],
                (int)ReadBits(aligned, addressBits + 6, 2),
                ReadBits(aligned, addressBits + 8, 1) == 1,
                payload,
                shift);
            return true;
        }

        return false;
    }

    /// <summary>Reads up to 32 bits, MSB first, from an arbitrary bit offset.</summary>
    private static uint ReadBits(ReadOnlySpan<byte> data, int bitOffset, int count)
    {
        uint value = 0;
        for (int i = 0; i < count; ++i)
        {
            int bit = bitOffset + i;
            value = (value << 1) | (uint)((data[bit / 8] >> (7 - (bit % 8))) & 1);
        }

        return value;
    }
}
```

- [ ] **Step 11: Run the whole test project**

Run: `dotnet test dotnet/LaserTag.Rf.Tests`
Expected: PASS (all tests from Tasks 4 and 5).

- [ ] **Step 12: Commit**

```bash
git add dotnet/LaserTag.Rf dotnet/LaserTag.Rf.Tests
git commit -m "feat(rf): bit realignment, address recovery, ESB packet validation"
```

---

### Task 6: RfTrainer capture app

**Files:**
- Create: `tools/RfTrainer/RfTrainer.csproj`, `tools/RfTrainer/Program.cs`
- Modify: `dotnet/LaserTag.sln`, `tools/README.md`

**Interfaces:**
- Consumes: `RfLineParser`, `RfCapture`, `AddressRecovery`, `PacketValidator` from Tasks 4-5.
- Produces: a console app writing newline-delimited JSON captures to
  `rf-captures.jsonl` with a `label` field per record.

- [ ] **Step 1: Create the project**

```bash
dotnet new console -n RfTrainer -o tools/RfTrainer -f net10.0
dotnet add tools/RfTrainer reference dotnet/LaserTag.Rf
dotnet add tools/RfTrainer package System.IO.Ports
dotnet add tools/RfTrainer package Spectre.Console
dotnet sln dotnet/LaserTag.sln add tools/RfTrainer
```

- [ ] **Step 2: Write the capture app**

Create `tools/RfTrainer/Program.cs`:

```csharp
using System.IO.Ports;
using System.Text.Json;
using LaserTag.Rf;
using Spectre.Console;

// Labelled capture recorder for the RF probe. Labelled diffing is what cracked
// the IR protocol (docs/gun-protocol.md); the same method applies here.
string port = args.ElementAtOrDefault(0) ?? "COM6";
string output = args.ElementAtOrDefault(1) ?? "rf-captures.jsonl";

using var serial = new SerialPort(port, 115200, Parity.None, 8, StopBits.One) { NewLine = "\n" };
serial.Open();
AnsiConsole.MarkupLineInterpolated($"[green]listening[/] on {port}, appending to {output}");
AnsiConsole.MarkupLine("Type a [bold]label[/] then run a scenario; blank line ends it. Ctrl+C to quit.");

var captures = new List<RfCapture>();
while (true)
{
    string label = AnsiConsole.Ask<string>("label (e.g. [grey]gun-a-fires-red-dmg2[/]):");
    AnsiConsole.MarkupLine("[yellow]recording[/] — press Enter to stop");

    var sessionCaptures = new List<RfCapture>();
    using var stop = new CancellationTokenSource();
    Task reader = Task.Run(() =>
    {
        while (!stop.Token.IsCancellationRequested)
        {
            try
            {
                string line = serial.ReadLine().Trim();
                if (!RfLineParser.TryParse(line, out RfCapture capture))
                {
                    continue;
                }

                sessionCaptures.Add(capture);
                File.AppendAllText(output, JsonSerializer.Serialize(new
                {
                    label,
                    capture.Channel,
                    capture.Rate,
                    capture.TimestampUs,
                    Data = Convert.ToHexString(capture.Data),
                }) + Environment.NewLine);
            }
            catch (TimeoutException)
            {
                // Expected between packets; keep waiting.
            }
        }
    });

    Console.ReadLine();
    stop.Cancel();
    await reader;

    captures.AddRange(sessionCaptures);

    // Report the CRC-valid yield explicitly: promiscuous capture is roughly 19
    // parts noise to 1 part signal, so a low ratio is expected behaviour, not a
    // fault, and saying so stops it being misread as a bug.
    int valid = sessionCaptures.Count(c => PacketValidator.TryValidate(c.Data, 5, out _));
    AnsiConsole.MarkupLineInterpolated(
        $"  captured [bold]{sessionCaptures.Count}[/] for '{label}' — [bold]{valid}[/] CRC-valid (a low ratio is normal)");

    IReadOnlyList<AddressCandidate> candidates = AddressRecovery.FindCandidates(captures);
    if (candidates.Count == 0)
    {
        AnsiConsole.MarkupLine("  [grey]no address candidate yet — capture more, or the yield is all noise[/]");
        continue;
    }

    var table = new Table().AddColumns("address", "captures");
    foreach (AddressCandidate c in candidates.Take(5))
    {
        table.AddRow(Convert.ToHexString(c.Address), c.Occurrences.ToString());
    }

    AnsiConsole.Write(table);
}
```

- [ ] **Step 3: Build**

Run: `dotnet build tools/RfTrainer`
Expected: SUCCESS. (Build the app directly — `dotnet test` on the solution does
not surface console-app build errors, a trap this repo has hit twice.)

- [ ] **Step 4: Document it**

Add to `tools/README.md`, after the `IrSignalTrainer` section:

```markdown
## RfTrainer

Records labelled 2.4 GHz captures from the ESP8266 RF probe
(`pio run -e esp8266-rfprobe`) to newline-delimited JSON, and ranks recurring
byte sequences as candidate nRF24 addresses after each session.

```sh
dotnet run --project tools/RfTrainer                       # COM6, rf-captures.jsonl
dotnet run --project tools/RfTrainer COM6 captures.jsonl
```

Start `sniff ch=<n> rate=<250k|1m|2m>` on the probe first, then label each
scenario ("gun-a-fires-red-dmg2", "vest-power-on", "pairing") and run it several
times. Roughly one capture in twenty is a real packet; the rest is noise, so
prefer many short labelled sessions over one long unlabelled one.
```

- [ ] **Step 5: Commit**

```bash
git add tools/RfTrainer tools/README.md dotnet/LaserTag.sln
git commit -m "feat(rf): RfTrainer labelled capture recorder"
```

---

### Task 7: Document findings and hand off

**Files:**
- Modify: `docs/rf-protocol.md`, `README.md`, `.docs/handoff.md`

- [ ] **Step 1: Write up the protocol findings**

Extend `docs/rf-protocol.md` (created in Task 0) with sections mirroring
`docs/gun-protocol.md`: the channel(s) in use and how they were found, the air
data rate, recovered addresses, packet structure with a field table, and what
each labelled scenario changed. State plainly what is confirmed by capture and
what is inference — the IR doc's credibility comes from that separation.

- [ ] **Step 2: Record the open questions**

Add a "Still unknown" section listing what was not determined, so the
interoperate-vs-transmit decision (deferred by the spec) has an explicit input.

- [ ] **Step 3: Update the README**

Add an "RF protocol analysis" subsection under the tools/firmware documentation
covering the `esp8266-rfprobe` env, the wiring table from the spec, and the
`selftest` / `scan` / `sniff` commands.

- [ ] **Step 4: Update the handoff**

Add a Current State section to `.docs/handoff.md` covering the probe hardware,
what Phase 0 concluded, where captures live, and the next decision point.

- [ ] **Step 5: Commit**

```bash
git add docs/rf-protocol.md README.md .docs/handoff.md
git commit -m "docs: RF protocol findings + probe usage"
```
