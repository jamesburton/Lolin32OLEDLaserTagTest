# ESP32-S3-Matrix Carrier PCB — Block-Level Schematic Spec

**Date:** 2026-06-28  
**Authority:** `.docs/pcb-design.md`, `.docs/handoff.md` §Next Steps #9,
`lib/Board/BoardProfile.cpp`  
**Purpose:** Netlist-level spec for KiCad hierarchical sheets. Do **not** re-litigate
decisions in `pcb-design.md`; this document turns those decisions into precise
pin-to-pin tables. KiCad file generation should be mechanical from these tables.

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
| J1–J2  | Module sockets (parent) |
| C1–C2  | Parent-sheet decoupling |
| J3–J4, C3 | `i2s_audio` sheet |
| J5, C4–C5 | `microsd_spi` sheet |
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
| J1  | PinSocket 1×10 | 2.54 mm pitch | `Connector_PinSocket_2.54mm:PinSocket_1x10_P2.54mm_Vertical` | — | Left module row |
| J2  | PinSocket 1×10 | 2.54 mm pitch | `Connector_PinSocket_2.54mm:PinSocket_1x10_P2.54mm_Vertical` | — | Right module row, 22.86 mm from J1 centre-to-centre |
| C1  | Electrolytic cap | 100 µF / 10 V | `Capacitor_THT:CP_Radial_D6.3mm_P2.50mm` | — | Bulk decoupling, VCC5 |
| C2  | Electrolytic cap | 10 µF / 10 V  | `Capacitor_THT:CP_Radial_D5mm_P2.50mm`   | — | Bulk decoupling, VCC3V3 |

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
| 9 | GP2 | `IR_TX` | Hierarchical pin → `ir` sheet |
| 10 | GP1 | `IR_RX` | Hierarchical pin → `ir` sheet |

#### J2 — Right row (pin 1 = top/GP33 end as installed)

| J2 Pin | Module pad | Net | Destination |
|--------|-----------|-----|-------------|
| 1 | GP33 | `SD_SCK`  | Hierarchical pin → `microsd_spi` sheet |
| 2 | GP34 | `SD_MOSI` | Hierarchical pin → `microsd_spi` sheet |
| 3 | GP35 | `SD_MISO` | Hierarchical pin → `microsd_spi` sheet |
| 4 | GP36 | `SD_CS`   | Hierarchical pin → `microsd_spi` sheet |
| 5 | GP37 | `GP37_SPARE` | No connection (test point recommended; only true spare after all optional blocks populated) |
| 6 | GP38 | `I2S_BCLK` | Hierarchical pin → `i2s_audio` sheet |
| 7 | GP39 | `I2S_WS`   | Hierarchical pin → `i2s_audio` sheet |
| 8 | GP40 | `I2S_DIN`  | Hierarchical pin → `i2s_audio` sheet |
| 9 | TX / GP43 | `UART_TX` | No connection on carrier (UART0 console — reserved) |
| 10 | RX / GP44 | `UART_RX` | No connection on carrier (UART0 console — reserved) |

### (c) External connector

The module itself is the "external connector" for this sheet. J1 and J2 are female
sockets; the ESP32-S3-Matrix module pins plug in. No additional external connector
on the parent sheet.

### (d) Passives and why

| Ref | Value | Rail | Reason |
|-----|-------|------|--------|
| C1 | 100 µF / 10 V electrolytic | VCC5 / GND | Bulk bypass at the 5 V entry point before distributing to MAX98357A, WS2812 strip, IR LED circuit |
| C2 | 10 µF / 10 V electrolytic | VCC3V3 / GND | Bulk bypass at the 3.3 V out before distributing to microSD, IR RX, I2C |

Both use values from on-hand stock.

### (e) DNP / optional

All child sheet hierarchical sheet symbols on the parent are themselves optional
(blocks 2–6); their connectors carry the individual DNP flags. C1 and C2 are always
populated.

---

## Block 2 — `i2s_audio`

MAX98357A I2S amplifier module on a 1×7 female socket; speaker via JST-XH.

### (a) Components

| Ref | Part | Value / Spec | KiCad Footprint | LCSC | Notes |
|-----|------|-------------|-----------------|------|-------|
| J3 | PinSocket 1×7 | 2.54 mm pitch | `Connector_PinSocket_2.54mm:PinSocket_1x07_P2.54mm_Vertical` | — | Accepts MAX98357A module header |
| J4 | JST-XH 2-pin | — | `Connector_JST:JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical` | C158012 | Speaker output |
| C3 | Electrolytic cap | 470 µF / 10 V | `Capacitor_THT:CP_Radial_D10mm_P5.00mm` | — | VIN bulk cap (Class-D spike suppression) |

### (b) Net connections

MAX98357A module 1×7 header pinout (left-to-right as labelled on the module PCB):

| J3 Pin | Module signal | Net | From (carrier hierarchical pin) |
|--------|--------------|-----|--------------------------------|
| 1 | LRC (Word Select) | `I2S_WS` | From parent J2.7 / GP39 |
| 2 | BCLK (Bit Clock)  | `I2S_BCLK` | From parent J2.6 / GP38 |
| 3 | DIN (Serial Data) | `I2S_DIN` | From parent J2.8 / GP40 |
| 4 | GAIN | `AUDIO_GAIN` | NC on carrier — left to module default (floating = 9 dB; pull to GND for 12 dB; see MAX98357A datasheet) |
| 5 | SD (Shutdown) | `AUDIO_SD` | NC on carrier — module's internal 100 kΩ pull-up to VIN holds HIGH → amp always enabled |
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
| `AUDIO_GAIN` | J3.4 | NC |
| `AUDIO_SD` | J3.5 | NC |
| `GND` | J3.6 | GND; C3 − |
| `VCC5` | J3.7 | VCC5; C3 + |
| `SPK_P` | J4.1 | Wire to module SPK+ |
| `SPK_N` | J4.2 | Wire to module SPK− |

### (c) External connector

J4 (JST-XH 2-pin) — speaker. Pin 1 = SPK+, Pin 2 = SPK−. Polarity is
speaker-amplifier convention (no fixed polarity for dynamic speakers, but keep
consistent with the MAX98357A module's pad labels).

### (d) Passives and why

| Ref | Value | Rail | Reason |
|-----|-------|------|--------|
| C3 | 470 µF / 10 V electrolytic | VCC5 / GND at J3.7 | MAX98357A is a Class-D amp; switching spikes 100–400 mA. A bulk cap directly at VIN suppresses rail droop. 470 µF is in-stock and sufficient; 100 µF is the minimum recommended. Place as close to J3.7 as possible. |

### (e) DNP / optional

J3, J4, C3 are all DNP if audio is not fitted. Mark J3 and J4 both DNP.
GAIN and SD pins (J3.4/J3.5) are NC; no carrier-side passive needed.

---

## Block 3 — `microsd_spi`

microSD card in SPI mode on GP33–36, powered from VCC3V3. Primary footprint is
a direct-solder push-push microSD socket; a JST-XH 6-pin breakout header is the
alternative if an external breakout module is preferred (footprint-swap, same nets).

### (a) Components

| Ref | Part | Value / Spec | KiCad Footprint | LCSC | Notes |
|-----|------|-------------|-----------------|------|-------|
| J5 | microSD push-push socket | 9-pin + 2-pin CD | `Connector_Card:microSD_HC_Hirose_DM3AT-SF-PEJM5` (or `Connector_Card:microSD_HC_Wuerth_693072010801`) | TBD — not in pcb-design.md table | Primary; **buy** |
| C4 | Ceramic cap | 100 nF / 10 V | `Capacitor_THT:C_Disc_D4.7mm_W2.5mm_P5.00mm` | — | **BUY** — smallest on-hand (0.47 µF) is too large for high-freq VCC decoupling |
| C5 | Electrolytic cap | 10 µF / 10 V | `Capacitor_THT:CP_Radial_D5mm_P2.50mm` | — | Bulk bypass, in-stock |

**Breakout alternative:** substitute J5 with JST-XH 6-pin (`Connector_JST:JST_XH_B6B-XH-A_1x06_P2.50mm_Vertical`; LCSC part TBD — not in the 2P/3P/4P table) wired CS/MOSI/MISO/SCK/VCC/GND. Same net names.

### (b) Net connections

microSD card SPI-mode pin mapping. Pad names below are the card-contact function
names (same across all microSD sockets); exact pad numbers reconcile to the chosen
footprint's datasheet (Hirose DM3AT-SF-PEJM5 or equivalent — **check pad order
before routing**).

| Card pad (function) | Card Signal (SPI mode) | Net | From (carrier hier. pin) |
|---------------------|----------------------|-----|--------------------------|
| DAT3 / CS | Chip Select | `SD_CS` | Parent J2.4 / GP36 |
| CMD / DI | MOSI | `SD_MOSI` | Parent J2.2 / GP34 |
| VSS (×2) | Ground | `GND` | Global GND |
| VDD | Supply | `VCC3V3` | Global VCC3V3; C4 +, C5 + |
| CLK | SCK | `SD_SCK` | Parent J2.1 / GP33 |
| DAT0 / DO | MISO | `SD_MISO` | Parent J2.3 / GP35 |
| DAT1 | NC | NC | SPI mode; card has internal pull-up |
| DAT2 | NC | NC | SPI mode; card has internal pull-up |
| CD / DET | Card detect A | NC | Optional: wire to GP37_SPARE for hot-swap; NC if unused |
| CD GND | Card detect B | `GND` | Only relevant if CD switch is wired |

Full net table (use the function-name column above to map pad numbers once the
footprint is confirmed):

| Net | From | To |
|-----|------|----|
| `SD_CS`   | Hier. pin (parent J2.4) | J5 DAT3/CS pad |
| `SD_MOSI` | Hier. pin (parent J2.2) | J5 CMD/DI pad |
| `GND`     | Global | J5 VSS pads; C4 −; C5 − |
| `VCC3V3`  | Global | J5 VDD pad; C4 +; C5 + |
| `SD_SCK`  | Hier. pin (parent J2.1) | J5 CLK pad |
| `SD_MISO` | Hier. pin (parent J2.3) | J5 DAT0/DO pad |

### (c) External connector

J5 is the microSD card socket itself (the card is the "external" connector).
If the breakout-module variant is used, J5 becomes a 6-pin JST-XH and the
breakout board's SPI pins (CS/MOSI/MISO/SCK) map 1:1.

### (d) Passives and why

| Ref | Value | Rail | Reason |
|-----|-------|------|--------|
| C4 | 100 nF ceramic | VCC3V3 / GND at J5.4 | High-frequency decoupling for SPI switching; the standard prescribes 100 nF directly at the card power pin. **Must buy** — smallest in-stock cap (0.47 µF) is too high an impedance at SPI frequencies to substitute. |
| C5 | 10 µF electrolytic | VCC3V3 / GND | Bulk reservoir for card initialisation inrush (~100 mA peak). In-stock. |

Note: optional 22–33 Ω series resistors on SCK/MOSI/MISO (between J2 and J5) are
standard practice to reduce ringing on long PCB traces. On a compact carrier with
short traces this is unlikely to matter; omit unless signal integrity problems arise.
Mark DNP if placed.

### (e) DNP / optional

J5, C4, C5 all DNP if microSD not fitted. `hasSdCard = false` in `BoardProfile`
achieves the same in firmware.

---

## Block 4 — `ir`

IR receiver (3-pin direct-solder header, VCC = 3V3) and IR TX emitter (NPN
transistor driver, LED on JST-XH, shoot-back circuit replicating the proven Lolin32
GPIO13 circuit).

### (a) Components

| Ref | Part | Value / Spec | KiCad Footprint | LCSC | Notes |
|-----|------|-------------|-----------------|------|-------|
| J6 | Pin header 1×3 | 2.54 mm pitch | `Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical` | — | IR RX — top PCB edge; direct-solder or header |
| C9 | Ceramic cap | 100 nF / 10 V | `Capacitor_THT:C_Disc_D4.7mm_W2.5mm_P5.00mm` | — | VCC3V3 decoupling for IR RX at J6.2/J6.3; **BUY** |
| R1 | Resistor, axial | 220 Ω | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` | — | NPN base resistor |
| R2 | Resistor, axial | 220 Ω | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` | — | IR LED current limiter |
| Q1 | NPN transistor | S8050, TO-92 | `Package_TO_SOT_THT:TO-92_Inline` | TBD | Preferred; BC547 (CBE TO-92) is a drop-in substitute — **verify pin order against datasheet before placing** |
| J7 | JST-XH 2-pin | — | `Connector_JST:JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical` | C158012 | IR LED cable connector |

### (b) Net connections

**IR RX (J6):** 3-pin at top edge, usually direct-soldered (VS1838 or compatible).
VCC = VCC3V3 (NOT 5 V — the S3's GP1 input max is 3.3 V; a receiver powered at 5 V
would idle its OUT at ~5 V into GP1, exceeding the absolute maximum).

| J6 Pin | Signal | Net | To |
|--------|--------|-----|----|
| 1 | OUT | `IR_RX` | Hier. pin → parent J1.10 / GP1 |
| 2 | VCC | `VCC3V3` | Global VCC3V3; C9 + |
| 3 | GND | `GND` | Global GND; C9 − |

**Decoupling for IR RX:** C9 (100 nF ceramic) spans VCC3V3/GND directly at J6.2/J6.3.
Buy from the same 100 nF lot as C4 and C7.

**IR TX circuit (GP2 → NPN → IR LED on JST-XH):**

```
VCC5 ──── R2 (220Ω) ──── J7.1 ──[IR LED anode]──[IR LED cathode]── J7.2 ──── Q1.collector
                                                                                     |
                                                                               Q1.emitter
                                                                                     |
                                                                                   GND
GP2 ──── R1 (220Ω) ──── Q1.base
```

The IR LED (D1) is not on the carrier — it lives on a short cable terminated by the
mating JST-XH plug. R2 is on the carrier, so the cable side is just the LED.

| Net | From (ref.pin) | To (ref.pin) |
|-----|----------------|--------------|
| `IR_TX`     | Hier. pin (parent J1.9 / GP2) | R1.1 |
| `IR_TX_BASE`| R1.2 | Q1.base |
| `VCC5`      | Global | R2.1 |
| `IR_LED_A`  | R2.2 | J7.1 |
| `IR_LED_K`  | J7.2 | Q1.collector |
| `GND`       | Q1.emitter | Global GND |

**Operating point (assumption — documented):** With VCC5 = 5 V, IR LED Vf ≈ 1.3 V,
Q1 Vce(sat) ≈ 0.2 V, R2 = 220 Ω:
- I_LED = (5 − 1.3 − 0.2) / 220 ≈ **16 mA** — modest; adequate for shoot-back
  (short distances). The only resistor value in stock is 220 Ω; a lower value
  (e.g. 68 Ω for ~51 mA) would extend range but requires a separate purchase.
- I_base = (3.3 − 0.7) / 220 ≈ **11.8 mA**; hFE(S8050) ≈ 200 → I_sat_max ≈ 2.4 A
  >> 16 mA → Q1 is fully saturated. ✓
- This replicates the proven Lolin32 GPIO13 IR-TX circuit (same driver pattern).

### (c) External connectors

- J6 (1×3, 2.54 mm): IR RX module or direct-solder. Placed at **top edge** of
  the PCB for unobstructed 360° field of view. Pin order (facing component side,
  left to right): OUT / VCC / GND — matching VS1838 family (verify against your
  specific receiver datasheet; some are GND/VCC/OUT). Add silkscreen label.
- J7 (JST-XH 2-pin): IR LED cable. Pin 1 = LED anode (+), Pin 2 = LED cathode (−).

### (d) Passives and why

| Ref | Value | Rail / Signal | Reason |
|-----|-------|--------------|--------|
| R1 | 220 Ω | GP2 → Q1 base | Base current limiter; ensures defined drive current and limits GPIO source current well within the S3's 40 mA per-pin max |
| R2 | 220 Ω | VCC5 → LED | IR LED series current limiter; sets I_LED ≈ 16 mA |
| C9 | 100 nF ceramic | VCC3V3/GND at J6.2/J6.3 | High-frequency decoupling for receiver IC. Placed as close to J6 VCC pin as possible. Buy same lot as C4 and C7. |

### (e) DNP / optional

J6 (IR RX) and J7 (IR TX) are independently DNP. Q1, R1, R2 are DNP if J7 is
unpopulated. The `irTxPin` in `BoardProfile` is currently `-1` on the S3 profile;
set it to GP2 when J7 is fitted and the shoot-back feature (#5 in handoff) lands.

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
| J8 | JST-XH 3-pin | — | `Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical` | C144394 | Strip output: 5V / DATA / GND |
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

J8 (JST-XH 3-pin). Pinout: **Pin 1 = 5V, Pin 2 = DATA, Pin 3 = GND.**  
Match to the mating XH plug on the WS2812 strip input or to the level-shifter board
(block 7) input connector when a long strip is used.

### (d) Passives and why

| Ref | Value | Position | Reason |
|-----|-------|----------|--------|
| R3 | 220 Ω | Between GP6 and J8.2 | Textbook series resistor at the first pixel of a WS2812 strip: damps ringing and protects the GPIO from capacitive loads. Placed on the carrier side of the JST-XH, not the strip side. |
| C6 | 1000 µF / 10 V electrolytic | VCC5/GND at J8.1/J8.3 | Bulk reservoir for the strip's inrush when powering on; prevents rail collapse on the first frame. The textbook recommendation for WS2812 strips. In-stock. |

**Note on level-shifting:** many WS2812B strips accept 3.3 V data and will work
directly from GP6. For long runs or strips that require V_IH > 3.3 V, insert the
optional level-shifter board (block 7) between this connector and the strip.

### (e) DNP / optional

J8, R3, C6 are all DNP if no external strip is fitted.

---

## Block 6 — `i2c_oled_xh`

I2C OLED or LCD connector (JST-XH 4-pin). The S3-Matrix `BoardProfile` has
`sdaPin = -1`, `sclPin = -1` today; this block assigns **SDA = GP4, SCL = GP5**
(the first two free, clean pins after IR and I2S claims — confirmed in pcb-design.md
"free & clean" list).

### (a) Components

| Ref | Part | Value / Spec | KiCad Footprint | LCSC | Notes |
|-----|------|-------------|-----------------|------|-------|
| J9 | JST-XH 4-pin | — | `Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical` | C144395 | 3V3 / GND / SDA / SCL |
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

J9 (JST-XH 4-pin). Pinout: **Pin 1 = 3V3, Pin 2 = GND, Pin 3 = SDA, Pin 4 = SCL.**  
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
| J10 | JST-XH 3-pin (or 2.54 mm header) | — | `Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical` | C144394 | Input: 5V / DATA_IN(3V3) / GND |
| J11 | JST-XH 3-pin | — | `Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical` | C144394 | Output: 5V / DATA_OUT(5V) / GND |
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

- **J10 (input):** JST-XH 3-pin. Pin 1 = 5V, Pin 2 = DATA_IN (3.3 V from carrier
  J8.2), Pin 3 = GND. Cable from carrier J8.
- **J11 (output):** JST-XH 3-pin. Pin 1 = 5V, Pin 2 = DATA_OUT (5 V shifted),
  Pin 3 = GND. Cable to WS2812 strip.

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
| GP2 | IR TX | 4 |
| GP3 | Strapping — **no carrier assignment; do not use for SPI or level-sensitive logic** | — |
| GP4 | I2C SDA | 6 |
| GP5 | I2C SCL | 6 |
| GP6 | WS2812 external DATA | 5 |
| GP7 | Act LED (onboard module LED, no carrier circuit) | — |
| GP33 | SD SCK | 3 |
| GP34 | SD MOSI | 3 |
| GP35 | SD MISO | 3 |
| GP36 | SD CS | 3 |
| **GP37** | **SPARE — only remaining free pin** | — |
| GP38 | I2S BCLK | 2 |
| GP39 | I2S WS (LRC) | 2 |
| GP40 | I2S DIN | 2 |
| GP43 / TX | UART0 console — **reserved, no carrier assignment** | — |
| GP44 / RX | UART0 console — **reserved, no carrier assignment** | — |
| GP14 | Onboard matrix (internal, not on header) | — |

**No pin contention** exists when blocks 2–6 are all populated: each pin is
claimed by exactly one block. The constraint is that GP4/GP5 (I2C) and GP6
(WS2812 external) are all consumed by blocks 5 and 6, leaving GP37 as the only
spare broken-out pin.

**Warning — GP3:** This is a strapping pin sampled at boot. Driving it as a GPIO
output during or immediately before boot reset can alter boot mode. If GP37 is
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

3. **IR TX circuit values (220 Ω base, 220 Ω LED series).** These are the only
   resistor values in stock. They yield I_LED ≈ 16 mA and I_base ≈ 11.8 mA. This
   is the minimum to drive 38 kHz-modulated IR effectively at short ranges (<5 m).
   Upgrade R2 to 68 Ω (buy) for ≈51 mA and longer range when available.

4. **NPN transistor = S8050, LCSC TBD.** S8050 is preferred for availability and
   high hFE; BC547 is a drop-in substitute but has the opposite TO-92 pin order
   (CBE vs EBC — verify before placing footprint). Confirm the LCSC part number
   on lcsc.com before BOM submission; do not use C2053 without verifying it maps
   to an S8050.

5. **MAX98357A SD pin unconnected.** The module has an internal 100 kΩ pull-up to
   VIN; SD floating HIGH = amp always enabled. This is the intended default per the
   handoff (no SD shutdown needed in normal operation). Add a pull-down to GND via a
   jumper or solder bridge if power-gate control is ever required.

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
| J1, J2 | 2 | PinSocket 1×10 | 2.54 mm | `Connector_PinSocket_2.54mm:PinSocket_1x10_P2.54mm_Vertical` | — | Buy |
| J3 | 1 | PinSocket 1×7 | 2.54 mm | `Connector_PinSocket_2.54mm:PinSocket_1x07_P2.54mm_Vertical` | — | Buy |
| J4, J7 | 2 | JST-XH 2-pin | — | `Connector_JST:JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical` | C158012 | Buy |
| J8, J10, J11 | 3 | JST-XH 3-pin | — | `Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical` | C144394 | Buy |
| J9 | 1 | JST-XH 4-pin | — | `Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical` | C144395 | Buy |
| J5 | 1 | microSD push-push socket | 9-pin | `Connector_Card:microSD_HC_Hirose_DM3AT-SF-PEJM5` | TBD | Buy |
| J6 | 1 | Pin header 1×3 | 2.54 mm | `Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical` | — | Buy (or use cut-down strip) |
| C1, C2, C5 | 3 | Electrolytic cap | 10–100 µF / 10 V | `Capacitor_THT:CP_Radial_D6.3mm_P2.50mm` or D5 | — | In stock |
| C3 | 1 | Electrolytic cap | 470 µF / 10 V | `Capacitor_THT:CP_Radial_D10mm_P5.00mm` | — | In stock |
| C6, C8 | 2 | Electrolytic cap | 1000 µF / 10 V | `Capacitor_THT:CP_Radial_D10mm_P5.00mm` | — | In stock |
| C4, C7, C9 | 3 | Ceramic cap | **100 nF / 10 V** | `Capacitor_THT:C_Disc_D4.7mm_W2.5mm_P5.00mm` | — | **BUY** — not in stock (C4=microSD VCC, C7=74AHCT125 VCC, C9=IR RX VCC) |
| R1, R2, R3, R6 | 4 | Resistor | 220 Ω | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` | — | In stock |
| R4, R5 | 2 | Resistor | **4.7 kΩ** | Same axial footprint | — | **DNP** (buy only if bare OLED, no pull-ups) |
| Q1 | 1 | NPN transistor | S8050, TO-92 | `Package_TO_SOT_THT:TO-92_Inline` | TBD — verify on LCSC | Buy |
| U1 | 1 | 74AHCT125, SOIC-14 | — | `Package_SO:SOIC-14_3.9x8.7mm_P1.27mm` | C7466 | Buy (level-shifter board only) |

**Single most important purchase:** a strip of 100 nF through-hole ceramic capacitors
(at least 3, buy 10) — these are the one item the on-hand kit is missing that
affects correctness (C4 microSD VCC bypass, C7 74AHCT125 VCC bypass, C9 IR RX
VCC bypass). All other parts use values already in stock.
