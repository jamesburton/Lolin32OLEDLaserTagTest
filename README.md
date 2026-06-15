# Lolin32 OLED Laser Tag

Reverse-engineering, decoding, and transmitting **Vatos** infrared laser-tag
signals across two ESP32 boards:

- **Lolin32 OLED** (ESP32 + 128×32 SSD1306) — IR monitor / decoder / transmitter
  and the C# trainer feeder. Reads NEC remotes and Vatos shots to the OLED, and
  fires Vatos shots via an IR LED.
- **ESP32-S3-Matrix** (ESP32-S3 + 8×8 WS2812) — a wearable **target**: idles in
  a rainbow, tracks its own health, flashes the firing team's colour and goes
  briefly dark when hit, and holds dark when its health reaches zero.

Both boards support **wireless OTA updates and UDP telemetry** (see
[Wireless](#wireless)). A **network control plane** (REST + UDP) configures and
controls devices, with a **.NET client library** on the host side (see
[Control plane](#control-plane-v2)). Shared logic lives in libraries:
`lib/Vatos` (decode/encode), `lib/IrFramer` (IR edge framing), `lib/TagNet`
(WiFi + OTA + HTTP + UDP), and `lib/ControlProto` (the protocol-agnostic wire
codec for the control plane).

> The Vatos IR protocol is not documented publicly; the protocol description in
> `docs/gun-protocol.md` was reverse-engineered from scratch in this project.

---

## Table of contents

- [What it does](#what-it-does)
- [Hardware](#hardware)
- [Build, flash, monitor](#build-flash-monitor)
- [Wireless](#wireless)
- [Control plane (V2)](#control-plane-v2)
- [How it works](#how-it-works)
- [The Vatos protocol (reverse-engineered)](#the-vatos-protocol-reverse-engineered)
- [The Vatos library](#the-vatos-library)
- [The C# signal trainer](#the-c-signal-trainer)
- [The journey (how we got here)](#the-journey-how-we-got-here)
- [Repository layout](#repository-layout)
- [Future work](#future-work)

---

## What it does

- **Receives** 38 kHz IR and decodes it live:
  - **NEC** remotes → `address:command` (e.g. `0707:04`)
  - **Vatos** laser-tag shots → `team` + `damage` (e.g. `Blue 2`)
- **Displays** the decode on the on-board OLED, with a blink LED on every hit.
- **Transmits** valid Vatos shots via an IR LED (38 kHz carrier, correct frame
  + checksum), triggered by the BOOT button or any byte over serial.
- **Trains/recognises** signals from a PC via the C# `IrSignalTrainer` app,
  matching NEC codes exactly and other protocols by full-frame fingerprint.
- **Networks** the target: broadcasts discovery heartbeats and hit/state
  telemetry over UDP, serves a JSON REST API for config/control, accepts
  low-latency UDP control broadcasts, and is driven from a host-side **.NET
  client library** (see [Control plane](#control-plane-v2)).

---

## Hardware

### Lolin32 OLED (monitor / decoder / transmitter)

| Function | Pin | Notes |
| -------- | --- | ----- |
| OLED I²C | SDA = GPIO5, SCL = GPIO4 | SSD1306, **128×32**, address `0x3C` |
| IR receiver (VS1838B) | OUT = GPIO25 | demodulating; idles HIGH, pulses LOW |
| Activity LED | GPIO26 | blinks ~80 ms on each received frame |
| IR transmit LED | GPIO13 | 38 kHz carrier via the LEDC peripheral |
| Test-shot trigger | GPIO0 (BOOT) **or** serial | any serial line also fires a shot |

### ESP32-S3-Matrix (target)

| Function | Pin | Notes |
| -------- | --- | ----- |
| 8×8 WS2812 matrix | GPIO14 | on-board, 64 LEDs (NeoPixel) |
| IR receiver (VS1838B) | OUT = GPIO1 | GPIO10–14 are taken by the IMU/matrix |
| Activity LED | GPIO7 | blinks on each received frame |
| (reserved) | GPIO10–13 | on-board QMI8658 IMU |

Behaviour: rainbow when idle → on a Vatos hit, subtract the shot's damage from
its health, flash the firing team's colour (Blue/Red/Green/White) 4×, then go
dark for a brief configurable "stunned" interval (default ~1–5 s; tune via the
control plane) → resume rainbow, keeping accumulated damage. At **0 health** it
holds dark ("dead") until a respawn / reset. The device is **authoritative for
its own health** and emits `EVT hit` / `EVT state` telemetry as it changes (see
[Control plane](#control-plane-v2)). Matrix current is capped to 500 mA
(`setMaxPowerInVoltsAndMilliamps`) for USB safety.

Notes:

- The on-board OLED on this unit is a **128×32** SSD1306 (not the 128×64 the
  Lolin32 normally ships with). Driving it as 128×64 renders garbled,
  interlaced text — the `src/display_test.cpp` diagnostic cycles candidate
  driver/geometry configs to identify the right one.
- The OLED is **not** on the ESP32 default I²C pins, so `Wire.begin(5, 4)` is
  required.
- The receiver/transmit/indicator LEDs in this build are driven **without series
  resistors** at the ESP32's minimum drive strength (`GPIO_DRIVE_CAP_0`, ~5 mA)
  to protect the pins. This is fine for bench/loopback testing but limits IR
  transmit range — a transistor driver + resistor is needed for real range.

---

## Build, flash, monitor

[PlatformIO](https://platformio.org/) project (Arduino framework; boards
`lolin32` and `esp32-s3-devkitc-1`, plus a `native` env for unit tests).

```sh
pio run -e lolin32                                   # build
pio run -e lolin32 -t upload --upload-port COM14     # flash
pio device monitor -p COM14 -b 115200                # serial monitor
```

Environments:

- `lolin32` — Lolin32 firmware (`src/main.cpp`).
- `lolin32_displaytest` — the OLED config finder (`src/display_test.cpp`).
- `esp32-s3-matrix` — Matrix target firmware (`src/matrix_main.cpp`).
- `native` — host-compiled unit tests for `lib/ControlProto` (no board).
- `*-ota` — wireless-upload variants (see [Wireless](#wireless)).

```sh
pio run -e esp32-s3-matrix -t upload --upload-port COM7   # flash the Matrix
pio test -e native                                        # run ControlProto unit tests
```

### Flashing workaround (Lolin32)

This particular board has **no BOOT/EN buttons** and its USB auto-reset into the
bootloader is unreliable, so uploads may fail with
`Wrong boot mode detected (0x17)`. To flash:

1. Jumper **GPIO0 → GND**.
2. Run the upload — the chip now enters download mode and flashes.
3. **Remove the GPIO0–GND jumper** and reset (re-plug USB) so it boots normally.

If the board disappears from Windows entirely, unplug/replug the USB to
re-enumerate the CP210x serial port.

---

## Wireless

Both firmware targets bring up WiFi, ArduinoOTA, and UDP telemetry via the
shared `lib/TagNet` library.

### Set WiFi credentials (over serial)

Credentials are stored in NVS, set with serial commands (no rebuild):

```powershell
./tools/set-wifi.ps1 -Port COM14 -Ssid "MyNetwork" -Password "s3cret"
```

or manually in a serial monitor: `ssid <name>`, `pass <pw>`, `wifi-save`
(also `wifi-status`, `wifi-clear`). The board prints its IP once connected.

### OTA updates

After one USB flash + WiFi provisioning, update over the air — no cable, no
GPIO0 jumper:

```sh
pio run -e lolin32-ota -t upload          # -> 192.168.1.48 (lasertag-lolin32)
pio run -e esp32-s3-matrix-ota -t upload  # -> 192.168.1.24 (lasertag-matrix)
```

Both `*-ota` envs target the boards' **IP addresses** in `platformio.ini`,
because mDNS (`lasertag-*.local`) does not resolve on every host (notably this
Windows dev machine). Set a DHCP reservation, or update `upload_port` if an IP
changes. OTA can take several minutes over a weak link (low RSSI), but is
reliable.

### Telemetry monitoring

Devices broadcast discovery heartbeats and hit/state telemetry as UDP lines on
port 4210 (see [Control plane](#control-plane-v2) for the grammar). The
`tools/TagMonitor` console app prints them raw:

```sh
dotnet run --project tools/TagMonitor
# lasertag-matrix HB id=752b38 ip=192.168.1.24 fw=2.0.0 team=2 mode=idle hp=100 online=1
# lasertag-matrix EVT hit victim=752b38 shooterTeam=2 dmg=2 proto=vatos hp=80 ts=12345
```

> **No telemetry but REST works?** That's a missing inbound firewall rule **or**
> a lossy weak-RSSI link. Rule out the firewall with `tools/setup-firewall.ps1`
> (Windows, self-elevating) / `tools/setup-firewall.sh` (Linux/macOS).

---

## Control plane (V2)

A protocol- and mode-agnostic network layer for configuring, controlling, and
monitoring devices. **REST** (reliable, JSON) handles config/CRUD/status; **UDP**
(fast, fire-and-forget on port 4210) handles discovery, telemetry, and
low-latency broadcast control. Devices decode IR into a generic `TagEvent`
(behind an `IrProtocol` abstraction — Vatos is the first protocol) and teams are
a generic indexed set, so game modes and the host never hard-code Vatos.

The full design and the authoritative wire contract (with golden test vectors)
live in
[`docs/superpowers/specs/`](docs/superpowers/specs/2026-06-15-control-plane-contract.md).

### REST API (served by the device)

| Method | Route | Purpose |
| ------ | ----- | ------- |
| `GET` | `/api/status` | live runtime status (mode, hp, team, online, fw, uptime, rssi) |
| `GET` | `/api/config` | persisted config (deviceId, ownTeam, enabledTeams, protocolId, brightness, teamColours) |
| `PATCH` | `/api/config` | partial update; persists to NVS; unknown field → `400` |
| `POST` | `/api/mode` | set runtime `activeMode` + timings (not persisted) |
| `POST` | `/api/command` | one-shot actions: `identify`, `bright`, test `hit`, `debug` |

Identity + preferences persist in NVS; game state (mode, timings, health) is
runtime only, so a reboot returns the device to a neutral idle. **Write
requests must send `Content-Type: application/json`** — the ESP32 `WebServer`
discards a urlencoded body (curl's default); the .NET client sets the header
automatically. (`GET /` and `GET /cmd?c=` remain as deprecated aliases.)

### UDP line-protocol (port 4210)

One message per packet; device→broadcast lines are prefixed with the hostname.

```
HB  id=752b38 ip=192.168.1.24 fw=2.0.0 team=2 mode=idle hp=100 online=1   # heartbeat (~2 s)
EVT hit victim=752b38 shooterTeam=2 dmg=2 proto=vatos hp=80 ts=12345      # telemetry
EVT state s=stunned hp=80 ts=12500                                        # ready|idle|stunned|dead|respawn
CTL start ts=30000   |   CTL stop   |   CTL reset hp=100                  # host -> device control
```

`EVT`/`HB` come **from** the device; `CTL` is sent **to** devices. The device is
authoritative for its own health; the host tallies match state from the event
stream. Send `CTL` to the **subnet broadcast** (e.g. `192.168.1.255`) — the
limited broadcast `255.255.255.255` is not delivered on this LAN.

### .NET host library

`dotnet/LaserTag.Client` (net10.0) is the typed host client:

- `LaserTagClient` — REST client for `/api/*` (`GetStatusAsync`,
  `PatchConfigAsync`, `SetModeAsync`, `SendCommandAsync`, …).
- `UdpMessageParser` — parses `HB`/`EVT` lines into typed records; formats `CTL`.
- `DeviceRoster` — live roster with liveness timeout (6 s) + rejoin detection.
- `NetworkDiagnostics` — advisory firewall/port hints.

`dotnet/openapi/lasertag.yaml` describes the REST surface (for generating other
clients). `dotnet/LaserTag.Smoke` is a throwaway harness that exercises the
library against a live device; run it for a quick end-to-end check:

```sh
dotnet test  dotnet/LaserTag.sln                              # unit tests
dotnet run --project dotnet/LaserTag.Smoke -- 192.168.1.24 20 # live REST + UDP roster
```

---

## How it works

### Receive path

An interrupt on the receiver pin timestamps every edge. Edges are grouped into
**frames** separated by ≥ 50 ms of silence (chosen so the gun's full-auto bursts
stay within one frame). Each completed frame is:

1. attempted as **NEC** (leader + 32 bits, validated by the command/inverse
   byte) → emits `NEC addr=0x.... cmd=0x..`;
2. otherwise, if it is 41 edges, attempted as **Vatos** → emits
   `VATOS team=N(name) dmg=N`;
3. always emitted raw as `FRAME n=.. data=L..,H..,...` for fingerprinting.

The optional decoded line is printed **before** the `FRAME` line so a consumer
can attach it to the frame that terminates the event.

### Transmit path

`Vatos::encode()` builds the 41-symbol bit pattern for a `{team, damage}` shot
(including the correct checksum). The firmware then gates a 38 kHz LEDC carrier:
even symbols are IR bursts (carrier on), odd symbols are gaps, each lasting
~380 µs (`0`) or ~800 µs (`1`). Pointing the IR LED at the VS1838B closes a full
**loopback**: transmitted shots are received and decoded by the same board.

### Serial protocol (115 200 baud)

```
NEC addr=0x0707 cmd=0x04            # optional, precedes its FRAME
VATOS team=1(Blue) dmg=2           # optional, precedes its FRAME
FRAME n=75 data=L4520,H4490,...    # always; H/L = level held, value = µs
TX team=1(Blue) dmg=2              # printed when a shot is transmitted
```

---

## The Vatos protocol (reverse-engineered)

A Vatos shot is a **41-edge frame** on a ~38 kHz carrier: 21 IR bursts
interleaved with 20 gaps (it starts and ends with a burst). Every symbol is
short (~380 µs = `0`) or long (~800 µs = `1`); quantising at 600 µs gives a
stable 41-bit pattern.

The frame encodes the firing **team** and the shot's **damage** — *not* the
weapon:

| Field | Bits (MSB-first) | Values |
| ----- | ---------------- | ------ |
| Preamble / framing | 0–21 | constant |
| **Team** | 22–24 | Blue=`001`(1), Red=`010`(2), Green=`011`(3), White=`100`(4) |
| Separator | 25–29 | constant |
| **Damage** | 30–32 | `001`–`100` = damage 1–4 |
| Separator | 33–36 | constant `0000` |
| Checksum | 37–40 | fixed (nonlinear) function of team + damage |

### Key findings

- **It encodes damage, not weapon.** Per the manual: Pistol = 1, Shotgun = 2,
  SMG = 2, MG = 3, Rocket = 4. Because **Shotgun and SMG both deal damage 2,
  they transmit an identical frame** — no receiver can tell them apart. So each
  gun emits at most 4 distinct codes per team (4 teams × 4 damage = 16 total).
- **The audio is mislabelled on this unit:** the spoken "Machine Gun" / "Sub
  Machine Gun" names are swapped relative to the damage actually sent. Identify
  shots by damage, not the voice line.
- **Carrier is ~38 kHz**, confirmed because the (38 kHz) VS1838B demodulates the
  gun cleanly. No 56 kHz receiver needed.
- **Both bursts and gaps carry payload.** An early fingerprint that used only the
  gap durations collided distinct shots (for some teams, damage 2 and damage 3
  share gap patterns and differ only in the bursts). The working approach
  fingerprints/decodes the full frame.

The full derivation, the 16-code matrix, and the bit map are in
[`docs/gun-protocol.md`](docs/gun-protocol.md).

---

## The Vatos library

`lib/Vatos/` is a platform-independent decoder/encoder (no Arduino
dependencies — carrier generation lives in the caller):

```cpp
namespace Vatos {
  struct Shot { uint8_t team; uint8_t damage; };          // team 1-4, damage 1-4

  bool decode(const uint32_t *edgeDurationsUs, size_t count, Shot &out);
  bool encode(const Shot &shot, bool bits[FrameEdges]);    // FrameEdges == 41
  const char *teamName(uint8_t team);                      // "Blue".."White"
}
```

- `decode()` quantises 41 edge durations, validates the constant framing **and**
  the checksum, and extracts team + damage.
- `encode()` produces the 41-symbol pattern (even indices = bursts) for
  transmission, with the correct checksum from a 4×4 lookup table.

Both directions were verified by full **TX → IR → RX → decode** loopback across
multiple team/damage codes, and `decode()` was verified against real gun fire.

---

## The C# signal trainer

`tools/IrSignalTrainer/` is a .NET console app that reads the board's serial
output and tags signals against named devices and buttons (see
[`tools/README.md`](tools/README.md)).

```sh
dotnet run --project tools/IrSignalTrainer            # COM14, signatures.json
```

- **NEC** signals match **exactly** on the decoded `address:command` code.
- **Other** signals (e.g. the gun, before firmware decoding existed) match on a
  quantised **full-frame** bit signature.
- `t` trains (device + button, 4 samples), `l` lists, `q` quits. The library is
  persisted to `signatures.json`.

`signatures.json` in the repo contains an example library trained on a TV remote
(NEC) and the Vatos gun.

---

## The journey (how we got here)

A condensed log of the build, because most of the value was in the process:

1. **Board bring-up.** Adapted the manufacturer OLED example to PlatformIO and
   verified the display + serial. Discovered the panel is 128×**32**, not
   128×64 — the cause of unreadable, interlaced text — using a config-finder
   sketch.
2. **Sensor selection.** Started with a KEYES photodiode + LM393 comparator
   board. It is not a demodulating receiver, so its output fragmented/merged and
   needed constant potentiometer tuning. Swapped to a **VS1838B**, which
   produced clean, decode-grade frames with no tuning. (See
   `docs/sensor-comparison.md`.)
3. **NEC decoding.** Added an NEC decoder to the firmware (OLED shows
   `addr:cmd`) and validated it against a TV remote (address `0x0707`).
4. **Cracking the gun.** The gun was not NEC. Captured frames, decoded the
   structure, and — crucially, with the owner's manual + the spotted audio/icon
   mismatch — realised the IR encodes **team + damage**, which explained an
   apparent "Shotgun = Machine Gun" collision (it is really Shotgun = SMG, both
   damage 2). Captured the full 4-team × 4-damage matrix and isolated the team,
   damage, and checksum bit-fields.
5. **Library + transmit.** Factored the decode/encode into `lib/Vatos/`, added a
   38 kHz IR transmitter, and verified the whole chain by loopback.

---

## Repository layout

```
platformio.ini            PlatformIO config (lolin32 / displaytest / matrix + OTA + native)
src/
  main.cpp                Lolin32: RX + NEC/Vatos decode, OLED, IR transmit
  matrix_main.cpp         ESP32-S3-Matrix: 8x8 LED target + V2 control plane
  display_test.cpp        OLED driver/geometry config finder (separate env)
lib/
  Vatos/                  Platform-independent Vatos decode/encode
  IrFramer/               Shared IR edge-framing (ISR + frame assembly)
  TagNet/                 Shared WiFi (serial creds) + OTA + UDP + HTTP server
  ControlProto/           Protocol-agnostic control-plane wire codec + TagEvent
test/
  test_controlproto/      Native (host-compiled) unit tests for ControlProto
dotnet/                   .NET 10 host ecosystem (LaserTag.sln)
  LaserTag.Client/        Typed REST + UDP client library (parser, roster, client)
  LaserTag.Client.Tests/  xUnit tests
  LaserTag.Smoke/         Throwaway live smoke harness
  openapi/lasertag.yaml   OpenAPI description of the REST surface
tools/
  IrSignalTrainer/        C# serial trainer + signature library
  TagMonitor/             C# UDP telemetry listener
  set-wifi.ps1            Provision WiFi credentials over serial
  setup-firewall.ps1/.sh  Check/fix the host firewall for UDP 4210 telemetry
docs/
  device-info.md          Lolin32 board pinout and OLED details
  sensor-comparison.md    KEYES comparator board vs VS1838B
  gun-protocol.md         The reverse-engineered Vatos IR protocol
  superpowers/specs/      Control-plane design + authoritative wire contract
signatures.json           Example trained signature library (TV remote + gun)
```

---

The V2 **control plane** (REST + UDP config/control/telemetry, device-side
health, the .NET client library) is in place. Next:

- **Firmware game-mode framework + "Team Colours" mode** — pluggable modes on
  top of the `activeMode`/timings plumbing already wired through the control
  plane.
- **Host scoring & orchestration** — tally match state from the `EVT` stream;
  multi-device game start/stop via `CTL` broadcasts.
- **.NET CLI + Claude skill** — a runnable CLI over `LaserTag.Client` (the
  library exists; its `CTL` sender must target the subnet broadcast), wrapped as
  a Claude skill.
- **Real transmit range** — a transistor driver + series resistor on the IR LED
  for gameplay distances rather than bench loopback.
- **Pin down the checksum formula** — currently a verified 4×4 lookup; the exact
  algorithm (symmetric in team+damage) is not yet derived.
