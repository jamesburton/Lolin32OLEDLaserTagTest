# Wokwi Simulation — ESP32-S3-Matrix Laser-Tag Target

Simulates the `esp32-s3-matrix` firmware on an ESP32-S3 DevKitC-1 with all
peripherals wired to their exact firmware pins (see `BoardProfile.cpp` /
`src/matrix_main.cpp`).

---

## Quickstart

### 1. Build the firmware

From the project root:

```bash
pio run -e esp32-s3-matrix
```

Produces `.pio/build/esp32-s3-matrix/firmware.bin` and `firmware.elf`.

### 2. (Optional) Build the MAX98357A WASM stub

The I2S amp stub requires compiling `max98357a.chip.c` to WebAssembly using
[wasi-sdk](https://github.com/WebAssembly/wasi-sdk):

```bash
cd sim/wokwi
wasi-sdk/bin/clang max98357a.chip.c \
  -o max98357a.chip.wasm \
  --target=wasm32-wasip1 \
  -nostdlib \
  -Wl,--no-entry \
  -Wl,--export=chip_init \
  -I <path-to-wokwi-chip-api-headers>
```

Wokwi chip API headers are available from
<https://docs.wokwi.com/chips-api/getting-started>.

**If you skip this step:** comment out the `[[chip]]` block in `wokwi.toml`
and remove the `chip-max98357a` part entry from `diagram.json`. The matrix,
IR receiver, serial, and game-logic tracks run without the audio stub.

### 3. Run the simulation

Requires a `WOKWI_CLI_TOKEN` environment variable (obtain a token at
<https://wokwi.com/dashboard/ci>):

```bash
# CLI simulation (headless, useful for CI) — run from project root
wokwi-cli sim/wokwi --timeout 30000

# Or from inside the sim/wokwi/ directory
wokwi-cli .
```

`wokwi-cli` takes a **directory** that contains `wokwi.toml`; it auto-discovers
`diagram.json` beside it. Additional options: `--elf <path>` to override the ELF,
`--serial-log-file <path>` to capture serial output, `--expect-text <string>` for
pass/fail CI assertions.

---

## Pin map (firmware → Wokwi)

| Firmware signal | GPIO | Wokwi part | Pin |
|---|---|---|---|
| Matrix data (WS2812, RGB) | GPIO14 | `wokwi-led-matrix` (id: `matrix`) | DIN |
| IR receiver output | GPIO1 | `wokwi-ir-receiver` (id: `ir1`) | DAT |
| Activity LED (active-high) | GPIO7 | `wokwi-led` green (id: `actled`) | A |
| I2S BCLK | GPIO38 | `chip-max98357a` (id: `amp1`) | BCLK |
| I2S LRC / WS | GPIO39 | `chip-max98357a` (id: `amp1`) | LRC |
| I2S DIN | GPIO40 | `chip-max98357a` (id: `amp1`) | DIN |

Power rails:

| Rail | Board pin | Connected to |
|---|---|---|
| 5V | `esp:5V` | matrix VDD, amp VIN |
| 3.3V | `esp:3V3.1` | IR receiver VCC |
| GND | `esp:GND.1` | matrix VSS |
| GND | `esp:GND.2` | IR receiver GND, activity LED cathode |
| GND | `esp:GND.3` | amp GND |

Serial / USB-CDC: the firmware uses `ARDUINO_USB_CDC_ON_BOOT=1` which routes
`Serial` to the USB-OTG CDC peripheral. The board attr `"serialInterface":
"USB_SERIAL_JTAG"` is set in `diagram.json`; no TX/RX-to-`$serialMonitor`
wires are needed. **Verify on first run**: if serial output does not appear,
remove the `"serialInterface"` attr from `diagram.json` and add standard
TX/RX→`$serialMonitor` wires as a fallback (ESP32-S3 `esp:43` TX,
`esp:44` RX).

---

## What the sim CAN validate

- **Matrix output:** WS2812 RGB data on GPIO14 — rainbow idle, hit-flash
  colour by team, health-bar depletion, dead/dark state.
- **IR receive:** inject IR frames into `wokwi-ir-receiver` on GPIO1 to
  trigger hit events and test damage accounting / state machine transitions.
- **Serial (USB-CDC):** all serial verbs (`bright`, `hit`, `sfx`, `lives`,
  `debug`) and structured EVT / HB output are visible in the Wokwi serial
  monitor.
- **Game logic / control plane:** REST /api/\* surface and UDP CTL messages
  can be exercised via the serial console or the Wokwi MCP server.
- **GPIO wiring:** confirms every pin assignment matches the `BoardProfile`
  before committing copper to the PCB.

## What the sim CANNOT validate

- **Real I2S audio:** Wokwi's chip C API has no I2S peripheral (feature
  request wokwi-features #213). The MAX98357A stub detects BCLK toggling and
  logs "I2S audio active" but produces no sound. Actual audio character still
  needs real ears on physical hardware.
- **NeoPixel colour order:** `wokwi-led-matrix` has no colour-order attribute
  (its WS2812 emulation uses GRB by default). The firmware profile is RGB, so
  simulated hues may appear swapped vs. hardware; logic is unaffected.
- **Analog signals:** no ADC/DAC stimulus.
- **WiFi / OTA / UDP:** Wokwi simulates the ESP32 WiFi peripheral at a
  protocol level; OTA and LAN-broadcast CTL are not meaningfully testable.

---

## Wokwi part substitutions

| Requested | Used | Reason |
|---|---|---|
| `wokwi-neopixel-matrix` | `wokwi-led-matrix` | `wokwi-led-matrix` is Wokwi's documented WS2812-compatible matrix part; `wokwi-neopixel-matrix` is not a published part id. Pins: DIN, DOUT, VDD, VSS. |
| (none) | `chip-max98357a` | Custom chip stub (no stock Wokwi MAX98357A part exists). Requires `max98357a.chip.wasm` compiled from `max98357a.chip.c`. |

---

## Files in this directory

| File | Purpose |
|---|---|
| `diagram.json` | Wokwi circuit: parts + connections |
| `wokwi.toml` | Firmware paths + custom chip registration |
| `max98357a.chip.json` | MAX98357A stub pin definition |
| `max98357a.chip.c` | MAX98357A stub C source (compile to `.chip.wasm`) |
| `README.md` | This file |

The KiCad schematic (when authored) will be the **master source of truth** for
the circuit; `diagram.json` is generated/maintained from the KiCad netlist and
is strictly a downstream artifact. See `.docs/pcb-design.md` for the
single-source-of-truth architecture.
