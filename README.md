# Lolin32 OLED Laser Tag

Reverse-engineering, decoding, and transmitting **Vatos** infrared laser-tag
signals on a Wemos **Lolin32 OLED** board (ESP32 + 128×32 SSD1306).

The firmware reads IR with a VS1838B receiver, decodes both NEC remotes and the
(previously undocumented) Vatos gun protocol live to the OLED, and can transmit
Vatos shots through an IR LED. A companion C# console app tags incoming signals
against named devices/buttons over the serial link.

> The Vatos IR protocol is not documented publicly; the protocol description in
> `docs/gun-protocol.md` was reverse-engineered from scratch in this project.

---

## Table of contents

- [What it does](#what-it-does)
- [Hardware](#hardware)
- [Build, flash, monitor](#build-flash-monitor)
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

---

## Hardware

| Function | Pin | Notes |
| -------- | --- | ----- |
| OLED I²C | SDA = GPIO5, SCL = GPIO4 | SSD1306, **128×32**, address `0x3C` |
| IR receiver (VS1838B) | OUT = GPIO25 | demodulating; idles HIGH, pulses LOW |
| Activity LED | GPIO26 | blinks ~80 ms on each received frame |
| IR transmit LED | GPIO13 | 38 kHz carrier via the LEDC peripheral |
| Test-shot trigger | GPIO0 (BOOT) **or** serial | any serial byte also fires a shot |

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

[PlatformIO](https://platformio.org/) project (Arduino framework, board
`lolin32`).

```sh
pio run -e lolin32                                   # build
pio run -e lolin32 -t upload --upload-port COM14     # flash
pio device monitor -p COM14 -b 115200                # serial monitor
```

Environments:

- `lolin32` — the main firmware (`src/main.cpp`).
- `lolin32_displaytest` — the OLED config finder (`src/display_test.cpp`).

### Flashing workaround (this board)

This particular board has **no BOOT/EN buttons** and its USB auto-reset into the
bootloader is unreliable, so uploads may fail with
`Wrong boot mode detected (0x17)`. To flash:

1. Jumper **GPIO0 → GND**.
2. Run the upload — the chip now enters download mode and flashes.
3. **Remove the GPIO0–GND jumper** and reset (re-plug USB) so it boots normally.

If the board disappears from Windows entirely, unplug/replug the USB to
re-enumerate the CP210x serial port.

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
platformio.ini            PlatformIO config (lolin32 / lolin32_displaytest envs)
src/
  main.cpp                Firmware: RX + NEC/Vatos decode, OLED, IR transmit
  display_test.cpp        OLED driver/geometry config finder (separate env)
lib/
  Vatos/                  Platform-independent Vatos decode/encode library
tools/
  IrSignalTrainer/        C# serial trainer + signature library
docs/
  device-info.md          Board pinout and OLED details
  sensor-comparison.md    KEYES comparator board vs VS1838B
  gun-protocol.md         The reverse-engineered Vatos IR protocol
signatures.json           Example trained signature library (TV remote + gun)
```

---

## Future work

- **Real transmit range** — a transistor driver + series resistor on the IR LED
  for gameplay distances rather than bench loopback.
- **Pin down the checksum formula** — currently a verified 4×4 lookup; the exact
  algorithm (symmetric in team+damage) is not yet derived.
- **Game logic** — health/score tracking on the receiver using the decoded
  team + damage; multi-board play using the transmitter.
