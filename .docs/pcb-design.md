# PCB Design & Testing

Design notes for a custom carrier/breakout PCB for the laser-tag platform. Companion
to `.docs/handoff.md` Next Steps #9. **Research/plan now, fabricate after a pin
freeze** (microSD pins + S3 IR TX must land first — see handoff #1, #5).

## Scope (decided)
- **S3-Matrix carrier first.** Design the carrier for the active ESP32-S3-Matrix
  target (audio + microSD + shoot-back IR TX), prove it, then replicate the pattern
  for the Lolin32. Carriers are **module-specific** (the two modules have different
  pinouts), so one board per module — not a shared board.
- A carrier = the ESP32-S3-Matrix module dropped into **female 2.54mm sockets**, with
  break-out connectors for the optional peripherals. Everything swappable.

## Toolchain (researched 2026-06-28)
No single tool does both ESP simulation AND PCB layout — two tracks, one PCB hub:
- **KiCad = the PCB hub.** Open S-expression text (`.kicad_sch`/`.kicad_pcb`),
  Python (`pcbnew`), `kicad-cli`. Every other path emits/imports it. Community MCP
  servers exist (early; open text is the workhorse, MCP a bonus).
- **Primary tool = plain KiCad** (registry check 2026-06-28, below). KiCad ships
  footprints/symbols for every part here; atopile's registry doesn't. Drive it from
  here via **CLI + file-based Python** (see *KiCad v10 automation* below — MCP is
  PCB-only on v10; there is no schematic API yet).
- **Optional code front-end = SKiDL** (Python → KiCad netlist) — uses KiCad's *own*
  libraries, so it dodges atopile's part-creation tax while keeping code-as-source.
- **atopile parked:** revisit when its registry fills out audio/LED/connector parts,
  or if we move to JLCPCB assembly (`ato create part <LCSC>` auto-pulls parts then).
- **Wokwi = simulation track.** Runs the actual PlatformIO firmware on a sim
  ESP32-S3; open `diagram.json`; `wokwi-cli` for CI; official (experimental) MCP
  server (`wokwi-cli mcp`, needs token). Validates wiring + firmware logic.
- **Dropped:** EasyEDA (good JSON/JLCPCB story, weaker MCP/CLI/work-from-here).
  Fallback only if assembled-board fab cost dominates later → EasyEDA + JLCPCB.

## KiCad v10 automation — how we drive it from here (#1, researched 2026-06-28)
v10 supports both APIs, but with a gap that decides our approach:
- **PCB (pcbnew):** the official **IPC API** (`kicad-python`/`kipy` ≥0.5, needs KiCad
  9+ with the API server enabled in Preferences/Tools) works on v10; legacy SWIG
  `pcbnew` also still works (removed only in v11). IPC-native **MCP servers exist** —
  `Finerestaurant/kicad-mcp-python` (PCB-only; needs the IPC server running) and
  `lamaalrajih/kicad-mcp`.
- **Schematic (eeschema): NO API on v10.** The IPC eeschema API was *not* shipped in
  v10 — it's PCB-only — so **every KiCad MCP is PCB-only**. (A community schematic
  Python API surfaced on the forum Apr 2026 but isn't standard.)
- **Conclusion — don't write an MCP, don't depend on one for schematic.** An MCP
  can't help where it matters (IPC has no schematic on v10). Instead:
  - **Schematic = our primary source → file-based:** `kicad-skip` (Python edits
    `.kicad_sch` S-expr directly, version-agnostic) or **SKiDL** (code → netlist).
  - **PCB + exports = CLI/scripts:** `kicad-cli` (Gerbers / BOM / DRC / netlist
    export — no IPC needed) + optional `pcbnew`/IPC-MCP for interactive layout later.
  - **Our deliverable = a small CLI + Claude skill** wrapping `kicad-cli` +
    `kicad-skip` + `gen-wokwi` — not a bespoke MCP.

## Single source of truth → two generated outputs
**One primary, script-generated secondary. Not parallel hand-maintained files.**
- **Primary = the KiCad project** (`.kicad_sch`/`.kicad_pcb` — S-expr text, scriptable
  via MCP / `pcbnew` / `kicad-skip`; or authored as code via SKiDL). Gerbers + BOM
  come straight from `kicad-cli`.
- **Wokwi `diagram.json` is generated** from the KiCad **netlist** by a small
  script. Direction is **one-way / downhill only**: `diagram.json` is strictly lossy
  (parts + named-pin nets + cosmetic x/y; **no** footprints, values, MPNs), so
  richer→leaner works but never the reverse. **Wokwi is never the master.**
- No turnkey KiCad→Wokwi converter exists; netlist parsers do (`kinparse`,
  `kicad-netlist-to-json`). The real work is a **part-map table**
  `{component → Wokwi part-id + pin-name map}` for the ~6-8 part types here, plus
  auto-placement (grid) and stub parts for what Wokwi lacks (MAX98357A). Bounded,
  reusable, written once.
- Workflow: edit KiCad schematic (or SKiDL source) → `kicad-cli` (Gerbers/BOM) +
  `gen-wokwi` (diagram.json for sim).

## Templates / skills / scripts
- Reusable blocks as **KiCad hierarchical sheets** (connectors + passives baked in):
  `esp32_s3_matrix_carrier`, `i2c_oled_xh`, `i2s_audio`, `microsd_spi`, `ws2812_out`,
  `level_shifter`. (Same blocks as SKiDL functions if we go the code route.)
  **→ Full per-block netlist spec (components, every net, connector pinouts, BOM):
  `.docs/pcb-blocks.md`.**
- A Claude skill wrapping `kicad-cli` (→ Gerbers/BOM) + the `gen-wokwi` script
  (→ diagram.json), optionally driving the KiCad MCP server. All runnable from here.

## Config plumbing for optional pins
Reuse the **existing hybrid pattern** — compile-time `BoardProfile` default + NVS
whitelist + serial `cfg` + `/api` PATCH. Optional = **`-1` (absent)**, the codebase
convention (NOT null).
- **Shoot-back IR TX:** `BoardProfile.irTxPin` **already exists** (`-1`=absent).
  Shoot-back is "enabled" exactly when `irTxPin` is set. S3 = add a real pin to the
  S3 profile (currently -1) + extend the NVS/`cfg`/REST whitelist for `irTxPin`.
  Reuse the proven `lib/IrTx` driver and the Lolin32 IR-TX circuit (it works today
  on GPIO13).
- **microSD SPI:** add `sdCsPin/sdMosiPin/sdMisoPin/sdSckPin` (`int8_t`, `-1`=absent)
  to `BoardProfile` (`hasSdCard` already exists), compile-time per board +
  NVS-overridable; extend the whitelist + `ConfigDoc` + `/api`.

## Pin selection — CONFIRMED against the official pinout
Source: Waveshare ESP32-S3-Matrix pinout (docs.waveshare.com/ESP32-S3-Matrix). The
board breaks out **only two 10-pad rows** (2.54mm). The carrier's female sockets =
**two 1×10 sockets at 2.54mm pitch, rows 22.86mm (0.9″ = 9×2.54) apart** — a whole
number of holes, so the module footprint sits cleanly on a standard 0.1″ grid
(KiCad: 2× `PinSocket_1x10_P2.54mm`, 22.86mm between rows). Layout:

- **Left row:** 5V · GND · 3V3(OUT) · GP7 · GP6 · GP5 · GP4 · GP3 · GP2 · GP1
- **Right row:** GP33 · GP34 · GP35 · GP36 · GP37 · GP38 · GP39 · GP40 · TX(GP43) · RX(GP44)

The onboard matrix (GPIO14) and IMU (GPIO10–13) are **internal, not on the header**,
so they can't conflict. (GP33–37 are free here: the S3FH4R2 has *quad* PSRAM on the
flash bus, not octal — Waveshare wouldn't expose PSRAM pins.)

Exposed-pad status:

| Pad | Status |
|---|---|
| GP1, GP7, GP38, GP39, GP40 | **in use** — IR RX / act LED / I2S BCLK,WS,DIN |
| GP3 | strapping — avoid for SPI/level-sensitive |
| TX/RX = GP43/GP44 | UART0 — keep as console fallback; avoid for SPI |
| **GP2, GP4, GP5, GP6, GP33, GP34, GP35, GP36, GP37** | **free & clean** |

(Earlier guesses GP21/41/42/47/48 were wrong — **those pins are not broken out**.)

**Confirmed assignment:**

| Function | Pin | Notes |
|---|---|---|
| IR TX (shoot-back) | **GP2** | left row, next to GP1 IR RX → one IR connector corner |
| microSD SCK | **GP33** | right-row block GP33–36, adjacent → clean corner connector |
| microSD MOSI (DI) | **GP34** | |
| microSD MISO (DO) | **GP35** | |
| microSD CS | **GP36** | |

Allocated to **optional connectors** (per the block spec): **GP4=I2C SDA, GP5=I2C SCL**
(OLED/sensors), **GP6=external WS2812 DATA**. True spare: **GP37** (+ GP43/44 if UART0
is sacrificed; GP3 strapping — avoid). microSD card is 3.3V — feed the socket from
**3V3(OUT)** (a bare socket needs no level shift; a breakout with onboard regulator
wants 5V). MAX98357A VIN = 5V. **IR RX VCC = 3V3** (GP1 is 3.3V-max — never 5V).

## Connectors (decided)
- **Female 2.54mm sockets:** ESP32-S3-Matrix module (**2×10, rows 22.86mm/0.9″
  apart**), MAX98357A module (**1×7**: LRC, BCLK, DIN, GAIN, SD, GND, VIN), external
  LCD. All on a standard 0.1″ grid → the carrier can be perfboard-prototyped first.
- **JST-XH** for everything smaller (IR LED, speaker, external WS2812 strip data,
  I2C OLED/LCD + future I2C sensors) — **one connector family** to stock.
- **IR receiver:** 3-pin (OUT / VCC / GND, 2.54mm) pads at the **top edge** — a header
  for a module, but usually direct-soldered. Same "direct-solder *or* header" approach
  for discrete LEDs (THT holes + standard through-hole resistor) and any local R/C/diode.
- Each optional connector marked **DNP** so a board can be populated only as needed.
- **Footprints:** standard through-hole sizing for discrete R (axial), C (radial), and
  diodes — soldered directly; SMD only where space demands.

## Passives (from the on-hand stash, where they help)
On hand: **220Ω** resistors, **0.47µF–1000µF** caps.
- **WS2812 (onboard + external strip):** 220Ω series on the data line at the first
  pixel; **1000µF** bulk across 5V/GND at the strip input. Both textbook, both in kit.
- **MAX98357A:** bulk cap on VIN (100–470µF) for Class-D current spikes.
- **microSD:** 100nF decoupling on VCC; optional small series R on SPI lines.
- **GAP — buy:** standard **100nF (0.1µF)** per-IC decoupling caps (smallest on hand
  is 0.47µF, which is bulk-ish, not a high-freq decoupling substitute). One cheap
  strip of 100nF ceramics is the single most useful addition.

## Optional level-shifter board (external strips only)
The onboard matrix runs fine on 3.3V data today, so 3.3→5V shifting is **only** for
long external 5V WS2812 strips. Keep it a **separate small board** (standard 0.1"
proto spacing, female connectors): 74AHCT125 (or SN74LVC2T45) buffer + 220Ω + 1000µF,
inline. Genuinely optional — not bundled into the carrier.

## MAX98357A in Wokwi — stub only
Wokwi's custom-chip C API has GPIO/I2C/SPI/UART/timers/ADC/DAC/framebuffer but
**no I2S** (open feature request wokwi-features #213). A MAX98357A "part" is a **pin
stub, not an audio simulator** — it can't decode I2S or make sound.
- **Required:** `max98357a.chip.json` (pins VIN, GND, SD, GAIN, DIN, BCLK, LRC,
  SPK±) + trivial `max98357a.chip.c` (`chip_init`; optionally watch BCLK to flag
  "audio active"). Likely reuse a community stub.
- **Implication:** Wokwi validates **wiring + firmware logic** (game state, matrix,
  serial, microSD SPI, IR RX) — **not audio**. Sound character still needs real ears.

## Parts reference (LCSC, for KiCad BOM / future JLCPCB)
KiCad has stock footprints/symbols for all of these; LCSC numbers are for ordering.
| Part | LCSC | KiCad footprint hint |
|---|---|---|
| MAX98357A I2S amp | C2682727 | (module on female header — or `Package_TO_SOT`/TDFN if bare) |
| WS2812B 5050 | C2761795 | `LED_WS2812B_5050` (onboard matrix is internal; this = external) |
| JST-XH 2P / 3P / 4P | C158012 / C144394 / C144395 | `Connector_JST:JST_XH_B*B-XH-A` |
| 74AHCT125 (3.3→5V buffer) | C7466 | `Package_SO:SOIC-14` (level-shifter board) |
| SSD1306 128×64 OLED | C5261074 | external module on XH/header |
| ESP32-S3-Matrix module | n/a | 2× `PinSocket_1x10_P2.54mm` (female) |

## Open / next
1. ✅ **Pad list confirmed** — pins frozen (IR TX GP2; microSD GP33-36).
2. ✅ **Registry check** — atopile thin (5/7 non-passive parts absent) → **plain
   KiCad primary** + KiCad MCP; SKiDL optional code route; atopile parked.
3. KiCad **v10 installed** ✅. Automation decided (#1): schematic via `kicad-skip`/
   SKiDL, exports via `kicad-cli`, PCB via `pcbnew`/IPC-MCP later; add `wokwi-cli`
   (needs token). No bespoke MCP — v10 IPC has no schematic API.
4. ✅ Footprint dims known: module 2×10 @2.54mm, rows 22.86mm (0.9″); audio 1×7.
5. Wokwi sim of current S3 build as wiring ground-truth before schematic capture.
6. Build the reusable hierarchical sheets + the `gen-wokwi` part-map script.
7. **GATE:** freeze pins/peripherals (after microSD spike #1 + S3 IR TX #5) →
   placement, routing, DRC, fab export.
