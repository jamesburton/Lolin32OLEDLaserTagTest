# ESP32-S3-Matrix Carrier PCB — Block-Level Schematic Spec

**Date:** 2026-06-28  
**Authority:** `.docs/pcb-design.md`, `.docs/handoff.md` §Next Steps #9,
`lib/Board/BoardProfile.cpp`  
**Purpose:** Netlist-level spec for KiCad hierarchical sheets. Do **not** re-litigate
decisions in `pcb-design.md`; this document turns those decisions into precise
pin-to-pin tables. KiCad file generation should be mechanical from these tables.

---

## RECONCILED 2026-07-04

Hardware bring-up since 2026-06-28 produced the following agreed decisions. These
**supersede** any conflicting row/table/text below; where this section and a later
table disagree, this section wins.

- **D1.** IR TX pin moved **GP2 → GP37** (flashed + tested; committed in `BoardProfile`). Frees GP2.
- **D2.** IR TX driver changed: transistor **S8050 → 2N2222A** (TO-92; BC337-40/S8050
  are equivalents); LED series resistor **220Ω → 33Ω** from **5V** (VCC5); base
  resistor **220Ω → 470Ω** (GP37 → base). New operating point: I_LED ≈ **105 mA**
  (was ~16 mA); I_base ≈ 5.5 mA, hFE ≥ 100 → saturated. A **22Ω** option gives ~150 mA
  if more range is needed.
- **D3.** All JST-XH connectors become **0.1″ (2.54 mm) pin headers/sockets**.
  Affects speaker (J4), IR LED (J7), WS2812-out (J8), OLED (J9), level-shifter I/O
  (J10/J11). JST-XH LCSC part numbers (C158012/C144394/C144395) are removed for these.
- **D4.** microSD primary connector is now a **6-pin 0.1″ female socket** for a
  breakout module (pin order **3V3·CS·MOSI·CLK·MISO·GND**); the push-push socket is
  demoted to "alternative". Net names unchanged.
- **D5.** ADD a power input: 2-pin terminal block **J0** feeding **VCC5/GND**. The
  module's 5V pin (parent J1.1) is powered FROM VCC5. Power switch + optional
  battery/charger are off-board, upstream of J0.
- **D6.** ADD an always-populated power LED: **D2** + series **R7 = 330Ω** (default;
  may fit up to 1kΩ) across VCC5(switched)/GND. NOT DNP.
- **D7.** Audio SD (shutdown) pin: add solder-jumper **JP1** — default open leaves SD
  floating (amp enabled) and keeps **GP2** free as spare/test-point; closing JP1
  routes GP2 → SD for a firmware hard-mute (future option). Parent J1 pin-9/GP2 net
  = `AUDIO_SD` via JP1; audio sheet J3 pin-5 connects to that net.
- **D8.** Audio GAIN pin: add optional 3-way strap jumper **JP2** — float = 9 dB
  (default) / GND = 15 dB / VIN = 3 dB. Volume is done in software; no runtime control.
- **D9.** microSD supply durability: ADD optional **U5** = 3V3 LDO from VCC5
  (AP2112-3.3 / MCP1826 / AMS1117-3.3) with 10 µF in + 10 µF out caps, and bypass
  solder-jumper **JP3**: default routes SD VDD from onboard VCC3V3 (U5 DNP);
  populating U5 + moving JP3 sources SD VDD from U5's output. C4/C5 at the socket
  unchanged.
- **D10.** IR RX (J6) pin order set to **OUT·GND·VCC** (matches project KB and
  on-hand parts) — verify per module. Reference part = **HS0038(B)** (best
  ambient-light rejection); VS1738/VS1838B/LF1638B also on hand (all 38 kHz).
- **D11.** GP2 becomes a **role selector** (supersedes the JP1-only view in D7): a
  solder-jumper bank picks **one** GP2 role per build — **default none** (spare/
  test-point), **recommended touch sensor** (JP5, 3-pin SIG/VCC3V3/GND header),
  or the alternatives **button** (JP4, on-board micro-switch SW1 + external 2-pin
  header J12) and **audio hard-mute** (JP1, Block 2). Close **at most one** of
  JP1/JP4/JP5. See Block 1 (f).

---

## Conventions

### Power rails (global, cross all sheets)

| Net label | Voltage | Source |
|-----------|---------|--------|
| `VCC5`    | +5 V    | External (USB/barrel/supply via J1.1) |
| `VCC3V3`  | +3.3 V  | ESP32-S3-Matrix on-module regulator out (J1.3) |
| `GND`     | 0 V     | Common return |

Power symbols use KiCad global power symbols (`PWR_FLAG`, `VCC`, `GND`). No
hierarchical pins are needed for power.

### Signal nets (cross-sheet via hierarchical pins)

Signal nets originate on the parent sheet (`esp32_s3_matrix_carrier`) and reach each
child sheet via KiCad hierarchical pins (the arrow-labelled ports on each sheet
symbol). Net names are listed below with each block.

### Refdes scheme (globally unique across all carrier sheets)

| Prefix | Usage |
|--------|-------|
| J0     | Power input (parent) |
| J1–J2  | Module sockets (parent) |
| C1–C2  | Parent-sheet decoupling |
| D2, R7 | Power LED + series resistor (parent) |
| SW1, J12–J13, JP4–JP5, R8, C12 | GP2 role selector (parent) |
| J3–J4, C3, JP1–JP2 | `i2s_audio` sheet |
| J5, C4–C5, U5, C10–C11, JP3 | `microsd_spi` sheet |
| J6–J7, R1–R2, Q1, C9 | `ir` sheet |
| J8, R3, C6 | `ws2812_out` sheet |
| J9, R4–R5 | `i2c_oled_xh` sheet |
| J10–J11, U1, R6, C7–C8 | `level_shifter` board |

KiCad hierarchical sheets: each `.kicad_sch` child file is referenced from the
parent by a sheet symbol; hierarchical labels inside the child sheet match the
hierarchical pin names on the parent's sheet symbol.

---

## Block 1 — `esp32_s3_matrix_carrier` (Parent Sheet)

The parent sheet hosts the two module sockets, all power decoupling, and the
hierarchical sheet symbols for every child block. Signal nets fan out from J1/J2
to child sheets.

### (a) Components

| Ref | Part | Value / Spec | KiCad Footprint | LCSC | Notes |
|-----|------|-------------|-----------------|------|-------|
| J0  | Terminal block, 2-pin | 2.54 mm or 5.08 mm | `TerminalBlock_Phoenix:TerminalBlock_Phoenix_MPT-0,5-2-2.54_1x02` | — | Power input (D5); feeds VCC5/GND. Switch + optional battery/charger are off-board, upstream of J0 |
| J1  | PinSocket 1×10 | 2.54 mm pitch | `Connector_PinSocket_2.54mm:PinSocket_1x10_P2.54mm_Vertical` | — | Left module row |
| J2  | PinSocket 1×10 | 2.54 mm pitch | `Connector_PinSocket_2.54mm:PinSocket_1x10_P2.54mm_Vertical` | — | Right module row, 22.86 mm from J1 centre-to-centre |
| C1  | Electrolytic cap | 100 µF / 10 V | `Capacitor_THT:CP_Radial_D6.3mm_P2.50mm` | — | Bulk decoupling, VCC5 |
| C2  | Electrolytic cap | 10 µF / 10 V  | `Capacitor_THT:CP_Radial_D5mm_P2.50mm`   | — | Bulk decoupling, VCC3V3 |
| D2  | LED, 3 mm/5 mm THT | Power indicator | `LED_THT:LED_D5.0mm` | — | Always-populated power LED (D6), VCC5(switched)/GND |
| R7  | Resistor, axial | 330 Ω (default; up to 1 kΩ acceptable) | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` | — | D2 series resistor (D6) |

**Placement note:** J1 and J2 are separated by **22.86 mm (9 × 2.54 mm) between row
centres** — a whole number of 0.1″ grid steps, matching the module. Place J1 left
of J2 as viewed from above.

### (b) Module pad map and net connections

#### J1 — Left row (pin 1 = top/5V end as installed)

| J1 Pin | Module pad | Net | Destination |
|--------|-----------|-----|-------------|
| 1 | 5 V | `VCC5` | Global power + C1+ |
| 2 | GND | `GND`  | Global GND + C1−, C2− |
| 3 | 3V3(OUT) | `VCC3V3` | Global power + C2+ |
| 4 | GP7 | `ACT_LED` | No connection on carrier — see Assumption #9 regarding whether the module has an onboard LED on this pin |
| 5 | GP6 | `WS_DATA` | Hierarchical pin → `ws2812_out` sheet |
| 6 | GP5 | `I2C_SCL` | Hierarchical pin → `i2c_oled_xh` sheet |
| 7 | GP4 | `I2C_SDA` | Hierarchical pin → `i2c_oled_xh` sheet |
| 8 | GP3 | `GP3_STRAP` | No connection (strapping pin; tie low only via module's own resistors) |
| 9 | GP2 | `GP2` | **GP2 role selector — see (f)** (D11). Default: unconnected spare/test-point. Close ONE jumper: touch (JP5, recommended) / button (JP4) / audio SD hard-mute (JP1 → `i2s_audio`) |
| 10 | GP1 | `IR_RX` | Hierarchical pin → `ir` sheet |

#### J2 — Right row (pin 1 = top/GP33 end as installed)

| J2 Pin | Module pad | Net | Destination |
|--------|-----------|-----|-------------|
| 1 | GP33 | `SD_SCK`  | Hierarchical pin → `microsd_spi` sheet |
| 2 | GP34 | `SD_MOSI` | Hierarchical pin → `microsd_spi` sheet |
| 3 | GP35 | `SD_MISO` | Hierarchical pin → `microsd_spi` sheet |
| 4 | GP36 | `SD_CS`   | Hierarchical pin → `microsd_spi` sheet |
| 5 | GP37 | `IR_TX` | Hierarchical pin → `ir` sheet (D1: IR TX moved here from GP2, flashed + tested on hardware) |
| 6 | GP38 | `I2S_BCLK` | Hierarchical pin → `i2s_audio` sheet |
| 7 | GP39 | `I2S_WS`   | Hierarchical pin → `i2s_audio` sheet |
| 8 | GP40 | `I2S_DIN`  | Hierarchical pin → `i2s_audio` sheet |
| 9 | TX / GP43 | `UART_TX` | No connection on carrier (UART0 console — reserved) |
| 10 | RX / GP44 | `UART_RX` | No connection on carrier (UART0 console — reserved) |

### (c) External connector

The module itself is the "external connector" for this sheet. J1 and J2 are female
sockets; the ESP32-S3-Matrix module pins plug in. J0 (D5) is the board's external
power-input connector: a 2-pin terminal block feeding VCC5/GND. The power switch and
any battery/charger circuitry are off-board, upstream of J0.

| J0 Pin | Net | Description |
|--------|-----|-------------|
| 1 | `VCC5` | +5 V in |
| 2 | `GND`  | Return |

### (d) Passives and why

| Ref | Value | Rail | Reason |
|-----|-------|------|--------|
| C1 | 100 µF / 10 V electrolytic | VCC5 / GND | Bulk bypass at the 5 V entry point before distributing to MAX98357A, WS2812 strip, IR LED circuit |
| C2 | 10 µF / 10 V electrolytic | VCC3V3 / GND | Bulk bypass at the 3.3 V out before distributing to microSD, IR RX, I2C |
| R7 | 330 Ω (D6; up to 1 kΩ acceptable) | VCC5(switched) / GND | Power-LED series resistor for D2 |

Both C1/C2 use values from on-hand stock. D2 + R7 are always populated (D6) — the
power LED is not an optional block.

### (e) DNP / optional

All child sheet hierarchical sheet symbols on the parent are themselves optional
(blocks 2–6); their connectors carry the individual DNP flags. C1, C2, J0, D2, and
R7 are always populated (D5, D6) — power input and the power LED are not optional.

### (f) GP2 role selector (variant option — pick one per build) (D11)

GP2 (parent J1.9) is the board's only slack pin, routed to a small solder-jumper
selector so each build picks **one** GP2 role. **Close at most one** of JP1/JP4/JP5.

| Ref | Part | Value / Spec | KiCad Footprint | Notes |
|-----|------|-------------|-----------------|-------|
| SW1 | Tactile micro-switch, THT | 6 mm | `Button_Switch_THT:SW_PUSH_6mm_H5mm` | On-board button (JP4); GP2 → SW1 → GND |
| J12 | Pin header 1×2 | 2.54 mm | `Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical` | External button, parallel with SW1 |
| J13 | Pin header 1×3 | 2.54 mm | `Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical` | Touch-sensor header: **SIG / VCC3V3 / GND** |
| JP4 | Solder jumper, 2-pad | open default | `Jumper:SolderJumper-2_P1.3mm_Open_TrianglePad1.0x1.5mm` | Selects GP2 → button (SW1/J12) |
| JP5 | Solder jumper, 2-pad | open default | `Jumper:SolderJumper-2_P1.3mm_Open_TrianglePad1.0x1.5mm` | Selects GP2 → touch SIG (J13.1) |
| R8 | Resistor, axial | 10 kΩ | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` | **DNP** — GP2 pull-up (internal pull-up used by default) |
| C12 | Ceramic cap | 100 nF | `Capacitor_THT:C_Disc_D4.7mm_W2.5mm_P5.00mm` | **DNP** — button debounce (GP2→GND) |

**Roles (default = none):**
- **None (default):** JP1/JP4/JP5 all open → GP2 is an unconnected spare/test-point.
- **Touch sensor (recommended):** close **JP5** → GP2 = SIG from J13. Compatible with
  TTP223-style modules; **VCC must be 3V3** (5 V would push SIG past GP2's 3.3 V max).
  GP2 is read as a digital input.
- **Button (alternative):** close **JP4** → GP2 → SW1 (and parallel external button
  on J12) → GND, read with the internal pull-up. Optional R8/C12 for hw pull-up/debounce.
- **Audio hard-mute (alternative):** close **JP1** (Block 2) → GP2 drives MAX98357A SD
  low at idle. See Block 2 / Assumption #5.

Net table:

| Net | From | To |
|-----|------|----|
| `GP2` | Hier. pin (parent J1.9) | JP1.a, JP4.a, JP5.a, R8.2, C12.1 |
| `GP2_BTN` | JP4.b | SW1.1, J12.1 |
| `GP2_TOUCH` | JP5.b | J13.1 (SIG) |
| `AUDIO_SD` | JP1.b | Block 2 J3.5 |
| `VCC3V3` | Global | J13.2, R8.1 |
| `GND` | Global | SW1.2, J12.2, J13.3, C12.2 |

**Firmware:** GP2's role becomes a `BoardProfile` field later (button read+debounce /
touch read / SD-mute drive) — no board impact now, only pads + jumpers. Silk:
"GP2 role — close ONE (default none; recommended touch)."

---

## Block 2 — `i2s_audio`

MAX98357A I2S amplifier module on a 1×7 female socket; speaker via a 0.1″ pin
header (D3 — was JST-XH).

### (a) Components

| Ref | Part | Value / Spec | KiCad Footprint | LCSC | Notes |
|-----|------|-------------|-----------------|------|-------|
| J3 | PinSocket 1×7 | 2.54 mm pitch | `Connector_PinSocket_2.54mm:PinSocket_1x07_P2.54mm_Vertical` | — | Accepts MAX98357A module header |
| J4 | Pin header 1×2 | 2.54 mm pitch | `Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical` | — | Speaker output (D3: was JST-XH B2B-XH-A 1x02 / C158012) |
| C3 | Electrolytic cap | 470 µF / 10 V | `Capacitor_THT:CP_Radial_D10mm_P5.00mm` | — | VIN bulk cap (Class-D spike suppression) |
| JP1 | Solder jumper | 2-pad, open by default | `Jumper:SolderJumper-2_P1.3mm_Open_TrianglePad1.0x1.5mm` | — | GP2 → SD hard-mute (D7); default OPEN leaves SD floating (amp enabled) and GP2 free as spare/test-point |
| JP2 | Solder jumper, 3-way | float/GND/VIN strap | `Jumper:SolderJumper-3_P1.3mm_Open_Pad1.0x1.5mm` | — | GAIN strap (D8): float = 9 dB (default) / GND = 15 dB / VIN = 3 dB |

### (b) Net connections

MAX98357A module 1×7 header pinout (left-to-right as labelled on the module PCB):

| J3 Pin | Module signal | Net | From (carrier hierarchical pin) |
|--------|--------------|-----|--------------------------------|
| 1 | LRC (Word Select) | `I2S_WS` | From parent J2.7 / GP39 |
| 2 | BCLK (Bit Clock)  | `I2S_BCLK` | From parent J2.6 / GP38 |
| 3 | DIN (Serial Data) | `I2S_DIN` | From parent J2.8 / GP40 |
| 4 | GAIN | `AUDIO_GAIN` | Via JP2 (D8), 3-way strap: float (default, JP2 open) = 9 dB / GND = 15 dB / VIN = 3 dB. Volume is controlled in software; JP2 is a build-time gain choice, not a runtime control. |
| 5 | SD (Shutdown) | `AUDIO_SD` | Via JP1 (D7) to parent J1.9 / GP2. Default JP1 open → SD floats HIGH (module's internal 100 kΩ pull-up to VIN) → amp always enabled, GP2 free as spare/test-point. Closing JP1 wires GP2 to SD for a firmware hard-mute (future option). |
| 6 | GND | `GND` | Global GND |
| 7 | VIN | `VCC5` | Global VCC5; C3 spans VCC5/GND at J3.7/J3.6 |

Speaker connector (J4):

| J4 Pin | Net | Description |
|--------|-----|-------------|
| 1 | `SPK_P` | Connect via wire to MAX98357A module SPK+ pad |
| 2 | `SPK_N` | Connect via wire to MAX98357A module SPK− pad |

**Note:** SPK± pads are solder pads on the MAX98357A module PCB itself, separate
from the 7-pin header. Short wires from J4 on the carrier to those pads complete
the loop.

Full net table:

| Net | From | To |
|-----|------|----|
| `I2S_WS` | Hier. pin (parent J2.7) | J3.1 |
| `I2S_BCLK` | Hier. pin (parent J2.6) | J3.2 |
| `I2S_DIN` | Hier. pin (parent J2.8) | J3.3 |
| `AUDIO_GAIN` | J3.4 | JP2 common; strap to float/GND/VIN |
| `AUDIO_SD` | J3.5 | JP1 → hier. pin (parent J1.9 / GP2), default open |
| `GND` | J3.6 | GND; C3 − |
| `VCC5` | J3.7 | VCC5; C3 + |
| `SPK_P` | J4.1 | Wire to module SPK+ |
| `SPK_N` | J4.2 | Wire to module SPK− |

### (c) External connector

J4 (pin header 1×2, 2.54 mm — D3) — speaker. Pin 1 = SPK+, Pin 2 = SPK−. Polarity is
speaker-amplifier convention (no fixed polarity for dynamic speakers, but keep
consistent with the MAX98357A module's pad labels).

### (d) Passives and why

| Ref | Value | Rail | Reason |
|-----|-------|------|--------|
| C3 | 470 µF / 10 V electrolytic | VCC5 / GND at J3.7 | MAX98357A is a Class-D amp; switching spikes 100–400 mA. A bulk cap directly at VIN suppresses rail droop. 470 µF is in-stock and sufficient; 100 µF is the minimum recommended. Place as close to J3.7 as possible. |
| JP1 | Solder jumper | GP2 ↔ AUDIO_SD | Default open (D7): SD floats HIGH, GP2 stays a free spare/test-point. Close only to wire GP2 to SD for a firmware hard-mute. |
| JP2 | Solder jumper, 3-way | AUDIO_GAIN strap | Build-time gain select (D8): float = 9 dB (default) / GND = 15 dB / VIN = 3 dB. |

### (e) DNP / optional

J3, J4, C3 are all DNP if audio is not fitted. Mark J3 and J4 both DNP.
JP1 is present whenever J3 is fitted (default left open); JP2 is present whenever
J3 is fitted (default left floating = 9 dB).

---

## Block 3 — `microsd_spi`

microSD card in SPI mode on GP33–36. Primary footprint is now a **6-pin 0.1″
female socket** for an external breakout module (D4); the direct-solder push-push
microSD socket is demoted to "alternative" (footprint-swap, same nets). Supply is
VCC3V3 by default, with an optional dedicated 3V3 LDO (U5, D9) for durability.

### (a) Components

| Ref | Part | Value / Spec | KiCad Footprint | LCSC | Notes |
|-----|------|-------------|-----------------|------|-------|
| J5 | PinSocket 1×6 | 2.54 mm pitch | `Connector_PinSocket_2.54mm:PinSocket_1x06_P2.54mm_Vertical` | — | **Primary (D4)** — breakout module socket. Pin order: 3V3·CS·MOSI·CLK·MISO·GND |
| C4 | Ceramic cap | 100 nF / 10 V | `Capacitor_THT:C_Disc_D4.7mm_W2.5mm_P5.00mm` | — | **BUY** — smallest on-hand (0.47 µF) is too large for high-freq VCC decoupling |
| C5 | Electrolytic cap | 10 µF / 10 V | `Capacitor_THT:CP_Radial_D5mm_P2.50mm` | — | Bulk bypass, in-stock |
| U5 | LDO regulator, 3.3 V | AP2112-3.3 / MCP1826 / AMS1117-3.3 | SOT-23-5 (AP2112) or SOT-223 (MCP1826/AMS1117), per chosen part | TBD | **Optional (D9)** — dedicated 3V3 rail for the microSD socket, sourced from VCC5. DNP by default. |
| C10 | Ceramic cap | 10 µF | `Capacitor_THT:CP_Radial_D5mm_P2.50mm` or 0805 SMD, per U5 footprint | — | U5 input cap. DNP unless U5 fitted. |
| C11 | Ceramic cap | 10 µF | Same as C10 | — | U5 output cap. DNP unless U5 fitted. |
| JP3 | Solder jumper | 2-way, default routes to VCC3V3 | `Jumper:SolderJumper-2_P1.3mm_Open_TrianglePad1.0x1.5mm` (or 3-way select, per layout) | — | **Bypass jumper (D9):** default sources SD VDD from onboard VCC3V3 (U5 DNP); move to source from U5's output when U5 is fitted |

**Alternative footprint (demoted, D4):** direct-solder push-push microSD socket,
`Connector_Card:microSD_HC_Hirose_DM3AT-SF-PEJM5` (or
`Connector_Card:microSD_HC_Wuerth_693072010801`), 9-pin + 2-pin CD, LCSC TBD.
Same net names; footprint-swap only.

### (b) Net connections

J5 6-pin breakout-socket pin order (D4): **3V3 · CS · MOSI · CLK · MISO · GND**.

| J5 Pin | Signal | Net | From (carrier hier. pin) |
|--------|--------|-----|--------------------------|
| 1 | 3V3 | `SD_VDD` | Via JP3: default from `VCC3V3` (U5 DNP); or from U5 output when fitted. C4 +, C5 + |
| 2 | CS | `SD_CS` | Parent J2.4 / GP36 |
| 3 | MOSI | `SD_MOSI` | Parent J2.2 / GP34 |
| 4 | CLK | `SD_SCK` | Parent J2.1 / GP33 |
| 5 | MISO | `SD_MISO` | Parent J2.3 / GP35 |
| 6 | GND | `GND` | Global GND; C4 −, C5 − |

**Alternative (push-push socket, demoted):** card-contact function names (DAT3/CS,
CMD/DI, VSS×2, VDD, CLK, DAT0/DO, DAT1 NC, DAT2 NC, CD/DET NC, CD GND) map to the
same `SD_CS`/`SD_MOSI`/`GND`/`SD_VDD`/`SD_SCK`/`SD_MISO` nets; exact pad numbers
reconcile to the chosen footprint's datasheet — **check pad order before routing**.
Card-detect (CD/DET) has no free GPIO to wire to now that GP37 is claimed by IR TX
(D1); leave NC if the push-push variant with CD is ever used.

Full net table:

| Net | From | To |
|-----|------|----|
| `SD_CS`   | Hier. pin (parent J2.4) | J5 pin 2 |
| `SD_MOSI` | Hier. pin (parent J2.2) | J5 pin 3 |
| `GND`     | Global | J5 pin 6; C4 −; C5 −; U5 GND (if fitted) |
| `SD_VDD`  | JP3 (default: `VCC3V3`; alt: U5 output) | J5 pin 1; C4 +; C5 + |
| `SD_SCK`  | Hier. pin (parent J2.1) | J5 pin 4 |
| `SD_MISO` | Hier. pin (parent J2.3) | J5 pin 5 |
| `VCC5`    | Global | U5 input (if fitted); C10 + |
| `VCC3V3`  | Global | JP3 default leg |

### (c) External connector

J5 is now the 6-pin 0.1″ female socket for the breakout module (primary, D4). The
breakout module plugs into J5; its onboard regulator/level-shifting (if any) is out
of scope here. If the direct-solder push-push variant is used instead, J5 becomes
the microSD card socket itself (the card is the "external" connector) — footprint
swap only, same net names.

### (d) Passives and why

| Ref | Value | Rail | Reason |
|-----|-------|------|--------|
| C4 | 100 nF ceramic | `SD_VDD` / GND at J5 pin 1 | High-frequency decoupling for SPI switching; the standard prescribes 100 nF directly at the card power pin. **Must buy** — smallest in-stock cap (0.47 µF) is too high an impedance at SPI frequencies to substitute. |
| C5 | 10 µF electrolytic | `SD_VDD` / GND | Bulk reservoir for card initialisation inrush (~100 mA peak). In-stock. |
| U5 | LDO, 3.3 V | VCC5 → `SD_VDD` | **Optional (D9):** dedicated regulator isolates the microSD rail from VCC3V3 loading/noise from the rest of the board — improves supply durability under SPI transient load. DNP by default; JP3 must move to select it when fitted. |
| C10 | 10 µF | U5 input (VCC5/GND) | LDO input bulk cap, per typical AP2112/MCP1826/AMS1117 application circuit. DNP unless U5 fitted. |
| C11 | 10 µF | U5 output (`SD_VDD`/GND) | LDO output bulk cap. DNP unless U5 fitted. |

Note: optional 22–33 Ω series resistors on SCK/MOSI/MISO (between J2 and J5) are
standard practice to reduce ringing on long PCB traces. On a compact carrier with
short traces this is unlikely to matter; omit unless signal integrity problems arise.
Mark DNP if placed.

### (e) DNP / optional

J5, C4, C5 all DNP if microSD not fitted. `hasSdCard = false` in `BoardProfile`
achieves the same in firmware. U5, C10, C11 are DNP by default (D9) — the microSD
rail runs from onboard VCC3V3 via JP3 unless the dedicated-LDO option is populated,
in which case JP3 must be moved to the U5-output leg.

---

## Block 4 — `ir`

IR receiver (3-pin direct-solder header, VCC = 3V3) and IR TX emitter (NPN
transistor driver, LED on a 0.1″ pin header, shoot-back circuit). **D1: IR TX now
drives from GP37, not GP2** — flashed and tested on hardware, committed in
`BoardProfile`. **D2: driver upgraded** from S8050/220Ω/16 mA to a higher-current
2N2222A/33Ω/~105 mA circuit for extended range.

### (a) Components

| Ref | Part | Value / Spec | KiCad Footprint | LCSC | Notes |
|-----|------|-------------|-----------------|------|-------|
| J6 | Pin header 1×3 | 2.54 mm pitch | `Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical` | — | IR RX — top PCB edge; direct-solder or header. Pin order OUT·GND·VCC (D10) |
| C9 | Ceramic cap | 100 nF / 10 V | `Capacitor_THT:C_Disc_D4.7mm_W2.5mm_P5.00mm` | — | VCC3V3 decoupling for IR RX; **BUY** |
| R1 | Resistor, axial | **470 Ω** (D2; was 220 Ω) | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` | — | NPN base resistor, GP37 → base |
| R2 | Resistor, axial | **33 Ω** (D2; was 220 Ω) | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` | — | IR LED current limiter, from VCC5. **22 Ω** is an alternative for ~150 mA if more range is needed. |
| Q1 | NPN transistor | **2N2222A, TO-92** (D2; was S8050) | `Package_TO_SOT_THT:TO-92_Inline` | TBD | Preferred; BC337-40/S8050 are equivalents — **verify pin order against datasheet before placing** |
| J7 | Pin header 1×2 | 2.54 mm pitch | `Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical` | — | IR LED cable connector (D3: was JST-XH B2B-XH-A 1x02 / C158012) |

### (b) Net connections

**IR RX (J6):** 3-pin at top edge, usually direct-soldered (HS0038(B) preferred —
best ambient-light rejection; VS1738 / VS1838B / LF1638B also on hand, all 38 kHz).
VCC = VCC3V3 (NOT 5 V — the S3's GP1 input max is 3.3 V; a receiver powered at 5 V
would idle its OUT at ~5 V into GP1, exceeding the absolute maximum).

**D10: pin order set to OUT·GND·VCC** (matches project KB and on-hand parts;
supersedes any earlier OUT/VCC/GND ordering). **Verify per module** before
soldering — some receivers use a different pinout.

| J6 Pin | Signal | Net | To |
|--------|--------|-----|----|
| 1 | OUT | `IR_RX` | Hier. pin → parent J1.10 / GP1 |
| 2 | GND | `GND` | Global GND; C9 − |
| 3 | VCC | `VCC3V3` | Global VCC3V3; C9 + |

**Decoupling for IR RX:** C9 (100 nF ceramic) spans VCC3V3/GND directly at J6.3/J6.2.
Buy from the same 100 nF lot as C4 and C7.

**IR TX circuit (GP37 → NPN → IR LED on pin header, D1+D2):**

```
VCC5 ──── R2 (33Ω) ──── J7.1 ──[IR LED anode]──[IR LED cathode]── J7.2 ──── Q1.collector
                                                                                    |
                                                                              Q1.emitter
                                                                                    |
                                                                                  GND
GP37 ──── R1 (470Ω) ──── Q1.base
```

The IR LED (D1, the LED — not to be confused with decision-tag "D1" above) is not
on the carrier — it lives on a short cable terminated by the mating plug. R2 is on
the carrier, so the cable side is just the LED.

| Net | From (ref.pin) | To (ref.pin) |
|-----|----------------|--------------|
| `IR_TX`     | Hier. pin (parent J2.5 / GP37) | R1.1 |
| `IR_TX_BASE`| R1.2 | Q1.base |
| `VCC5`      | Global | R2.1 |
| `IR_LED_A`  | R2.2 | J7.1 |
| `IR_LED_K`  | J7.2 | Q1.collector |
| `GND`       | Q1.emitter | Global GND |

**Operating point (updated per D2):** With VCC5 = 5 V, IR LED Vf ≈ 1.3 V,
Q1 Vce(sat) ≈ 0.2 V, R2 = 33 Ω:
- I_LED = (5 − 1.3 − 0.2) / 33 ≈ **~105 mA** (was ~16 mA at 220 Ω) — enough for
  useful shoot-back range. A **22 Ω** option gives ~150 mA if still more range is
  needed.
- I_base = (3.3 − 0.7) / 470 ≈ **5.5 mA**; hFE(2N2222A) ≥ 100 → I_sat_max well
  above 105 mA → Q1 is fully saturated. ✓
- Driver changed from the original Lolin32 GPIO13-pattern circuit (S8050/220Ω,
  16 mA) to this higher-current 2N2222A/33Ω/5V circuit for range.

### (c) External connectors

- J6 (1×3, 2.54 mm): IR RX module or direct-solder. Placed at **top edge** of
  the PCB for unobstructed 360° field of view. Pin order (facing component side,
  left to right): **OUT / GND / VCC** (D10) — matching the project KB and on-hand
  parts (HS0038(B) preferred; VS1738/VS1838B/LF1638B also on hand). **Verify against
  your specific receiver's datasheet before soldering** — pinouts vary between
  manufacturers. Add silkscreen label.
- J7 (pin header 1×2, 2.54 mm — D3): IR LED cable. Pin 1 = LED anode (+), Pin 2 =
  LED cathode (−).

### (d) Passives and why

| Ref | Value | Rail / Signal | Reason |
|-----|-------|--------------|--------|
| R1 | 470 Ω (D2) | GP37 → Q1 base | Base current limiter; I_base ≈ 5.5 mA, well within the S3's 40 mA per-pin max |
| R2 | 33 Ω (D2) | VCC5 → LED | IR LED series current limiter; sets I_LED ≈ 105 mA. 22 Ω alternative for ~150 mA. |
| C9 | 100 nF ceramic | VCC3V3/GND at J6.3/J6.2 | High-frequency decoupling for receiver IC. Placed as close to J6 VCC pin as possible. Buy same lot as C4 and C7. |

### (e) DNP / optional

J6 (IR RX) and J7 (IR TX) are independently DNP. Q1, R1, R2 are DNP if J7 is
unpopulated. The `irTxPin` in `BoardProfile` is now **GP37** on the S3 profile
(D1 — flashed and tested; no longer `-1`) when J7 is fitted and the shoot-back
feature (#5 in handoff) is enabled.

---

## Block 5 — `ws2812_out`

External WS2812 strip output header with series resistor on DATA and bulk cap on 5 V.
The onboard 8×8 matrix (GP14, internal to module) is **not** represented here.

**DATA pin assignment (decided here — not in pcb-design.md):** GP6 is assigned to
external WS2812 DATA. This is the first free-and-clean pin after GP4 (I2C SDA) and
GP5 (I2C SCL) are claimed by block 6. Firmware `BoardProfile` must add `extStripPin`
= GP6 (or NVS-configurable) when this block is fitted.

### (a) Components

| Ref | Part | Value / Spec | KiCad Footprint | LCSC | Notes |
|-----|------|-------------|-----------------|------|-------|
| J8 | Pin header 1×3 | 2.54 mm pitch | `Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical` | — | Strip output: 5V / DATA / GND (D3: was JST-XH B3B-XH-A 1x03 / C144394) |
| R3 | Resistor, axial | 220 Ω | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` | — | Series on DATA |
| C6 | Electrolytic cap | 1000 µF / 10 V | `Capacitor_THT:CP_Radial_D10mm_P5.00mm` | — | Bulk bypass at strip input |

### (b) Net connections

| Net | From (ref.pin) | To (ref.pin) |
|-----|----------------|--------------|
| `WS_DATA`      | Hier. pin (parent J1.5 / GP6) | R3.1 |
| `WS_DATA_OUT`  | R3.2 | J8.2 |
| `VCC5`         | Global | J8.1; C6 + |
| `GND`          | Global | J8.3; C6 − |

### (c) External connector

J8 (pin header 1×3, 2.54 mm — D3). Pinout: **Pin 1 = 5V, Pin 2 = DATA, Pin 3 = GND.**  
Match to the mating XH plug on the WS2812 strip input or to the level-shifter board
(block 7) input connector when a long strip is used.

### (d) Passives and why

| Ref | Value | Position | Reason |
|-----|-------|----------|--------|
| R3 | 220 Ω | Between GP6 and J8.2 | Textbook series resistor at the first pixel of a WS2812 strip: damps ringing and protects the GPIO from capacitive loads. Placed on the carrier side of the pin header, not the strip side. |
| C6 | 1000 µF / 10 V electrolytic | VCC5/GND at J8.1/J8.3 | Bulk reservoir for the strip's inrush when powering on; prevents rail collapse on the first frame. The textbook recommendation for WS2812 strips. In-stock. |

**Note on level-shifting:** many WS2812B strips accept 3.3 V data and will work
directly from GP6. For long runs or strips that require V_IH > 3.3 V, insert the
optional level-shifter board (block 7) between this connector and the strip.

### (e) DNP / optional

J8, R3, C6 are all DNP if no external strip is fitted.

---

## Block 6 — `i2c_oled_xh`

I2C OLED or LCD connector (0.1″ pin header, 4-pin — D3, was JST-XH). The S3-Matrix `BoardProfile` has
`sdaPin = -1`, `sclPin = -1` today; this block assigns **SDA = GP4, SCL = GP5**
(the first two free, clean pins after IR and I2S claims — confirmed in pcb-design.md
"free & clean" list).

### (a) Components

| Ref | Part | Value / Spec | KiCad Footprint | LCSC | Notes |
|-----|------|-------------|-----------------|------|-------|
| J9 | Pin header 1×4 | 2.54 mm pitch | `Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical` | — | 3V3 / GND / SDA / SCL (D3: was JST-XH B4B-XH-A 1x04 / C144395) |
| R4 | Resistor, axial | 4.7 kΩ | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` | — | SDA pull-up — **DNP** (see below) |
| R5 | Resistor, axial | 4.7 kΩ | same | — | SCL pull-up — **DNP** (see below) |

**Note on pull-ups:** SSD1306 breakout modules (LCSC C5261074 module or typical
Adafruit/clone) already carry 4.7 kΩ pull-ups to VCC on the module PCB. Fitting R4/R5
in addition would halve the pull-up resistance and is unnecessary. Leave R4/R5 DNP
unless the bare OLED glass (no pull-ups on the cable end) is used directly.
4.7 kΩ resistors are **not in stock** (only 220 Ω on hand); purchase if installing.

### (b) Net connections

| Net | From (ref.pin) | To (ref.pin) |
|-----|----------------|--------------|
| `VCC3V3`  | Global | J9.1; R4.1; R5.1 |
| `GND`     | Global | J9.2 |
| `I2C_SDA` | Hier. pin (parent J1.7 / GP4) | J9.3; R4.2 |
| `I2C_SCL` | Hier. pin (parent J1.6 / GP5) | J9.4; R5.2 |

### (c) External connector

J9 (pin header 1×4, 2.54 mm — D3). Pinout: **Pin 1 = 3V3, Pin 2 = GND, Pin 3 = SDA, Pin 4 = SCL.**  
Matches Qwiic/STEMMA-QT convention minus the connector type (those are JST-SH 4-pin).
Note this on the silkscreen. I2C address for SSD1306 = 0x3C (default).

### (d) Passives and why

| Ref | Value | Position | Reason |
|-----|-------|----------|--------|
| R4 | 4.7 kΩ | VCC3V3 → I2C_SDA | Pull-up to guarantee HIGH state on idle bus; DNP if module already has them (it usually does) |
| R5 | 4.7 kΩ | VCC3V3 → I2C_SCL | Same as R4 |

### (e) DNP / optional

J9 is DNP if no OLED/LCD fitted. R4 and R5 are **always DNP unless the specific
OLED module has no onboard pull-ups** — verify before populating. Update
`BoardProfile.sdaPin = 4`, `sclPin = 5`, `screen = ScreenKind::Ssd1306` in firmware
when fitted.

---

## Block 7 — `level_shifter` (Separate Small Board)

Stand-alone 0.1″-pitch board; **not** part of the carrier. 74AHCT125 quad buffer
running on 5 V, using only buffer 1 to convert 3.3 V WS2812 data to 5 V. The
74AHCT125's TTL-compatible input thresholds (VIH = 2.0 V) safely recognise 3.3 V
logic as HIGH while the output swings to VCC = 5 V.

### (a) Components

| Ref | Part | Value / Spec | KiCad Footprint | LCSC | Notes |
|-----|------|-------------|-----------------|------|-------|
| J10 | Pin header 1×3 | 2.54 mm pitch | `Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical` | — | Input: 5V / DATA_IN(3V3) / GND (D3: was JST-XH B3B-XH-A 1x03 / C144394) |
| J11 | Pin header 1×3 | 2.54 mm pitch | `Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical` | — | Output: 5V / DATA_OUT(5V) / GND (D3: was JST-XH B3B-XH-A 1x03 / C144394) |
| U1 | 74AHCT125, SOIC-14 | — | `Package_SO:SOIC-14_3.9x8.7mm_P1.27mm` | C7466 | Quad tri-state buffer |
| R6 | Resistor, axial | 220 Ω | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` | — | Series resistor on DATA_OUT |
| C7 | Ceramic cap | 100 nF / 10 V | `Capacitor_THT:C_Disc_D4.7mm_W2.5mm_P5.00mm` | — | VCC decoupling for U1; **BUY** |
| C8 | Electrolytic cap | 1000 µF / 10 V | `Capacitor_THT:CP_Radial_D10mm_P5.00mm` | — | Bulk bypass for strip; in-stock |

### (b) Net connections

74AHCT125 SOIC-14 pin assignments (only buffer 1 is used):

| U1 Pin | 74AHCT125 Signal | Net | Connection |
|--------|-----------------|-----|------------|
| 1  | /OE1 (enable, active LOW) | `GND`     | Tied to GND → buffer 1 always enabled |
| 2  | A1 (input)                | `DATA_IN` | J10.2 (3.3 V data from carrier) |
| 3  | Y1 (output)               | `DATA_BUF`| R6.1 |
| 4  | /OE2 (unused)             | `VCC5`    | Tied HIGH → buffer 2 output Hi-Z |
| 5  | A2 (unused)               | `GND`     | Tied to GND (CMOS inputs must not float) |
| 6  | Y2 (unused)               | NC        | Float is safe: /OE2 is HIGH → Hi-Z |
| 7  | GND                       | `GND`     | Power; C7 − |
| 8  | Y3 (unused)               | NC        | Float is safe: /OE3 is HIGH → Hi-Z |
| 9  | A3 (unused)               | `GND`     | Tied to GND |
| 10 | /OE3 (unused)             | `VCC5`    | Tied HIGH → buffer 3 output Hi-Z |
| 11 | Y4 (unused)               | NC        | Float is safe: /OE4 is HIGH → Hi-Z |
| 12 | A4 (unused)               | `GND`     | Tied to GND |
| 13 | /OE4 (unused)             | `VCC5`    | Tied HIGH → buffer 4 output Hi-Z |
| 14 | VCC                       | `VCC5`    | Power; C7 + |

Full net table:

| Net | From (ref.pin) | To (ref.pin) |
|-----|----------------|--------------|
| `VCC5`    | J10.1 | U1.14; U1.4; U1.10; U1.13; C7 +; C8 +; J11.1 |
| `GND`     | J10.3 | U1.7; U1.1; U1.5; U1.9; U1.12; C7 −; C8 −; J11.3 |
| `DATA_IN` | J10.2 | U1.2 |
| `DATA_BUF`| U1.3  | R6.1 |
| `DATA_OUT`| R6.2  | J11.2 |

### (c) External connectors

- **J10 (input):** Pin header 1×3, 2.54 mm (D3). Pin 1 = 5V, Pin 2 = DATA_IN
  (3.3 V from carrier J8.2), Pin 3 = GND. Cable from carrier J8.
- **J11 (output):** Pin header 1×3, 2.54 mm (D3). Pin 1 = 5V, Pin 2 = DATA_OUT
  (5 V shifted), Pin 3 = GND. Cable to WS2812 strip.

The 5 V rail passes through: carrier → J10.1 → board → J11.1 → strip. The strip
is powered from the same 5 V supply as the carrier.

### (d) Passives and why

| Ref | Value | Position | Reason |
|-----|-------|----------|--------|
| C7 | 100 nF ceramic | VCC5/GND at U1.14 | High-frequency decoupling directly at the IC supply pins, per AHCT125 application note. **Must buy.** |
| C8 | 1000 µF / 10 V electrolytic | VCC5/GND at J11 | Bulk reservoir at the strip input, identical function to C6 on the carrier — placed here so a long cable between carrier and strip doesn't starve the first pixel on the level-shifter output side. In-stock. |
| R6 | 220 Ω | DATA_BUF → J11.2 | Series resistor on the 5 V output data line, dampens ringing. Same textbook role as R3 on the carrier. |

### (e) DNP / optional

The level-shifter board is entirely optional. Short strips (<1 m) generally work
with 3.3 V data directly from the carrier J8 connector. Fit the level-shifter only
for long runs or strips requiring V_IH > 3.3 V.

---

## Pin Contention and Spare Pin Summary

| Pin | Assignment | Block |
|-----|-----------|-------|
| GP1 | IR RX | 4 |
| GP2 | **role selector** (default none; touch JP5 recommended, or button JP4 / audio-mute JP1) | 1(f) |
| GP3 | Strapping — **no carrier assignment; do not use for SPI or level-sensitive logic** | — |
| GP4 | I2C SDA | 6 |
| GP5 | I2C SCL | 6 |
| GP6 | WS2812 external DATA | 5 |
| GP7 | Act LED (onboard module LED, no carrier circuit) | — |
| GP33 | SD SCK | 3 |
| GP34 | SD MOSI | 3 |
| GP35 | SD MISO | 3 |
| GP36 | SD CS | 3 |
| GP37 | IR TX | 4 |
| GP38 | I2S BCLK | 2 |
| GP39 | I2S WS (LRC) | 2 |
| GP40 | I2S DIN | 2 |
| GP43 / TX | UART0 console — **reserved, no carrier assignment** | — |
| GP44 / RX | UART0 console — **reserved, no carrier assignment** | — |
| GP14 | Onboard matrix (internal, not on header) | — |

**D1 (2026-07-04): IR TX moved GP2 → GP37**, flashed and tested on hardware. This
frees GP2. **The board is now fully packed** — every broken-out pin is claimed by
exactly one block when blocks 2–6 are all populated. GP2 is the only slack, exposed
as the **role selector** (D11, Block 1 (f)): default none (spare/test-point), or one
of touch (JP5, recommended) / button (JP4) / audio SD hard-mute (JP1). Close at most
one — these are mutually exclusive board variants, not simultaneous functions.

**Warning — GP3:** This is a strapping pin sampled at boot. Driving it as a GPIO
output during or immediately before boot reset can alter boot mode. If GP2 is
insufficient and GP3 must be used, add a 10 kΩ pull-down resistor to hold it LOW
through power-on; avoid SPI CS usage (level glitches at power-on).

---

## Assumptions

1. **External WS2812 DATA = GP6.** This assignment is made here; it is not in
   `pcb-design.md`. Rationale: GP4 and GP5 are taken by I2C; GP6 is the next
   free-and-clean pin. Firmware must add `extStripPin = 6` to `BoardProfile` when
   this block is fitted.

2. **IR RX VCC = VCC3V3 (3.3 V).** A 5 V-powered VS1838-class receiver idles its
   OUT line near 5 V, which exceeds the ESP32-S3's 3.3 V GPIO absolute maximum.
   The VS1838 and compatible receivers operate correctly at 3.3 V. If a 5 V-only
   receiver must be used, a voltage divider or level translator is required on the
   OUT line — not specified here.

3. **IR TX circuit values (D2, updated 2026-07-04): 470 Ω base, 33 Ω LED series,
   driven from VCC5.** This replaces the earlier 220 Ω/220 Ω values. They yield
   I_LED ≈ 105 mA (up from ~16 mA) and I_base ≈ 5.5 mA — enough headroom for
   useful shoot-back range. A **22 Ω** option is available for ≈150 mA if still
   more range is needed; 33 Ω/470 Ω/22 Ω are all covered by the resistor pack the
   user has ordered (also covers 220 Ω/330 Ω/1 kΩ).

4. **NPN transistor = 2N2222A (D2, updated 2026-07-04; was S8050), LCSC TBD.**
   2N2222A is the primary part for the higher-current (~105 mA) IR-TX driver;
   BC337-40/S8050 are equivalents. Verify TO-92 pin order (EBC/CBE varies by
   manufacturer) before placing the footprint. Confirm the LCSC part number on
   lcsc.com before BOM submission.

5. **GP2 role selector (D7 + D11, updated 2026-07-04).** GP2 feeds a solder-jumper
   selector (Block 1 (f)) offering, per build, exactly one of: **none** (default —
   spare/test-point), **touch sensor** (recommended, JP5), **button** (JP4), or
   **audio SD hard-mute** (JP1 → MAX98357A SD). With all jumpers open (default) the
   amp's SD floats HIGH via its internal 100 kΩ pull-up → amp always enabled
   (unchanged behaviour). Each role needs a matching `BoardProfile` setting in
   firmware; none is required for normal operation. Touch VCC must be **3V3**.

5a. **Power LED always populated (D6).** D2 + R7 (330 Ω default, up to 1 kΩ
    acceptable for lower quiescent draw on battery) sit across VCC5(switched)/GND
    on the parent sheet and are NOT DNP — every board gets a power indicator.

5b. **Power-input terminal block J0 (D5).** A 2-pin terminal block feeds VCC5/GND
    directly; the module's 5V pin (J1.1) is powered from VCC5. The power switch and
    any battery/charger circuitry are explicitly off-board, upstream of J0 — not
    part of this carrier's scope.

6. **I2C pull-ups DNP.** SSD1306 breakout modules carry 4.7 kΩ pull-ups onboard.
   R4/R5 footprints are present but marked DNP. 4.7 kΩ resistors must be purchased
   to populate them (not in stock).

7. **100 nF ceramics must be purchased.** Smallest cap in stock is 0.47 µF, which
   is suitable for bulk bypass but not for high-frequency IC decoupling. Three are
   required: C4 (microSD VCC), C7 (74AHCT125 VCC), C9 (IR RX VCC). One strip of
   100 nF / 10 V through-hole ceramics covers all three uses; buy ≥10.

8. **microSD socket LCSC part TBD.** No push-push microSD socket is listed in the
   pcb-design.md parts table. Use a standard Hirose DM3AT-SF-PEJM5 or equivalent;
   source from LCSC/JLCPCB SMT catalogue or buy through-hole via Mouser.

9. **GP7 / ACT_LED — onboard LED status unconfirmed.** The firmware drives GPIO7
   as an activity LED (`activityLedPin = 7` in `BoardProfile`). The Waveshare
   ESP32-S3-Matrix's headline indicator is the 8×8 WS2812 matrix on GPIO14.
   Whether the module also has a discrete LED connected to GPIO7 is not verified
   from the datasheet. If no onboard LED exists on GPIO7, the firmware's activity
   pulse is invisible without an external circuit. To handle both cases: **the
   carrier should provide an LED + 220 Ω series resistor on the J1.4 (GP7) net as
   a DNP option**, giving visibility when the module has no built-in LED. Verify
   against the Waveshare hardware schematic before finalising block 1.

10. **SPK_P / SPK_N are off-board nets.** These nets originate at J4 but terminate
    at the MAX98357A module's solder pads (not on any other carrier net). In KiCad
    mark both as `No-Connect` at the J4 boundary on the carrier so ERC does not
    flag them as dangling. Add a schematic note: "Connect via wire to module SPK±."

---

## Aggregated BOM

"In stock" = in the on-hand passive kit described in `pcb-design.md`.  
"Buy" = must purchase.

| Ref(s) | Qty | Part | Value | KiCad Footprint | LCSC | Status |
|--------|-----|------|-------|-----------------|------|--------|
| J0 | 1 | Terminal block, 2-pin (D5) | 2.54/5.08 mm | `TerminalBlock_Phoenix:TerminalBlock_Phoenix_MPT-0,5-2-2.54_1x02` | — | Buy |
| J1, J2 | 2 | PinSocket 1×10 | 2.54 mm | `Connector_PinSocket_2.54mm:PinSocket_1x10_P2.54mm_Vertical` | — | Buy |
| J3 | 1 | PinSocket 1×7 | 2.54 mm | `Connector_PinSocket_2.54mm:PinSocket_1x07_P2.54mm_Vertical` | — | Buy |
| J4, J7 | 2 | Pin header 1×2 (D3; was JST-XH B2B-XH-A 1x02 / C158012) | 2.54 mm | `Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical` | — | Buy |
| J8, J10, J11 | 3 | Pin header 1×3 (D3; was JST-XH B3B-XH-A 1x03 / C144394) | 2.54 mm | `Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical` | — | Buy |
| J9 | 1 | Pin header 1×4 (D3; was JST-XH B4B-XH-A 1x04 / C144395) | 2.54 mm | `Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical` | — | Buy |
| J5 | 1 | PinSocket 1×6 (D4 — **primary**, breakout module; was push-push microSD) | 2.54 mm | `Connector_PinSocket_2.54mm:PinSocket_1x06_P2.54mm_Vertical` | — | Buy |
| J5-alt | 1 | microSD push-push socket (D4 — **demoted to alternative**) | 9-pin | `Connector_Card:microSD_HC_Hirose_DM3AT-SF-PEJM5` | TBD | Buy only if using this variant |
| J6 | 1 | Pin header 1×3 | 2.54 mm | `Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical` | — | Buy (or use cut-down strip) |
| SW1 | 1 | Tactile micro-switch, THT (D11 — GP2 button role) | 6 mm | `Button_Switch_THT:SW_PUSH_6mm_H5mm` | — | Optional (button variant) |
| J12 | 1 | Pin header 1×2 (D11 — external button, ‖ SW1) | 2.54 mm | `Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical` | — | Optional (button variant) |
| J13 | 1 | Pin header 1×3 (D11 — touch header SIG/VCC3V3/GND) | 2.54 mm | `Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical` | — | Optional (touch variant, recommended) |
| R8 | 1 | Resistor (D11 — GP2 pull-up) | 10 kΩ | Same axial footprint | — | **DNP** (internal pull-up default) |
| C12 | 1 | Ceramic cap (D11 — button debounce) | 100 nF | `Capacitor_THT:C_Disc_D4.7mm_W2.5mm_P5.00mm` | — | **DNP** (fit if bounce) |
| C1, C2, C5 | 3 | Electrolytic cap | 10–100 µF / 10 V | `Capacitor_THT:CP_Radial_D6.3mm_P2.50mm` or D5mm | — | In stock |
| C3 | 1 | Electrolytic cap | 470 µF / 10 V | `Capacitor_THT:CP_Radial_D10mm_P5.00mm` | — | In stock |
| C6, C8 | 2 | Electrolytic cap | 1000 µF / 10 V | `Capacitor_THT:CP_Radial_D10mm_P5.00mm` | — | In stock |
| C4, C7, C9 | 3 | Ceramic cap | **100 nF / 10 V** | `Capacitor_THT:C_Disc_D4.7mm_W2.5mm_P5.00mm` | — | **BUY** — not in stock (C4=microSD VCC, C7=74AHCT125 VCC, C9=IR RX VCC) |
| C10, C11 | 2 | Ceramic/electrolytic cap (D9 — optional, U5 fitted only) | 10 µF | `Capacitor_THT:CP_Radial_D5mm_P2.50mm` or SMD per U5 footprint | — | DNP unless U5 fitted; buy if fitting |
| R1 | 1 | Resistor (D2; was 220 Ω) | **470 Ω** | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` | — | In stock (resistor pack covers 470 Ω) |
| R2 | 1 | Resistor (D2; was 220 Ω) | **33 Ω** | Same axial footprint | — | In stock (resistor pack covers 33 Ω; 22 Ω alt also covered) |
| R3, R6 | 2 | Resistor | 220 Ω | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` | — | In stock |
| R4, R5 | 2 | Resistor | **4.7 kΩ** | Same axial footprint | — | **DNP** (buy only if bare OLED, no pull-ups) |
| R7 | 1 | Resistor (D6 — always populated) | **330 Ω** (up to 1 kΩ acceptable) | Same axial footprint | — | In stock (resistor pack covers 330 Ω/1 kΩ) |
| D2 | 1 | LED, THT (D6 — always populated) | Power indicator | `LED_THT:LED_D5.0mm` | — | Buy |
| Q1 | 1 | NPN transistor (D2; was S8050) | **2N2222A**, TO-92 | `Package_TO_SOT_THT:TO-92_Inline` | TBD — verify on LCSC | Buy |
| U1 | 1 | 74AHCT125, SOIC-14 | — | `Package_SO:SOIC-14_3.9x8.7mm_P1.27mm` | C7466 | Buy (level-shifter board only) |
| U5 | 1 | LDO regulator, 3.3 V (D9 — optional) | AP2112-3.3 / MCP1826 / AMS1117-3.3 | Per chosen part | TBD | DNP by default; buy if fitting the microSD dedicated-LDO option |

**Single most important purchase:** a strip of 100 nF through-hole ceramic capacitors
(at least 3, buy 10) — these are the one item the on-hand kit is missing that
affects correctness (C4 microSD VCC bypass, C7 74AHCT125 VCC bypass, C9 IR RX
VCC bypass).

**Note (2026-07-04):** the user has ordered a resistor pack covering 33 Ω, 220 Ω,
330 Ω, 470 Ω, 1 kΩ, etc. — R1 (470 Ω), R2 (33 Ω), and R7 (330 Ω) are expected to be
in stock once that pack arrives, in addition to the existing 220 Ω stock (R3, R6).
