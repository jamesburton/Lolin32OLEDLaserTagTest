# hardware/ — KiCad PCB project

Custom carrier/breakout PCB for the laser-tag platform. Starts with the
**ESP32-S3-Matrix carrier** (full scope: audio, microSD, IR RX/TX, I2C OLED,
external WS2812 out, power in; optional separate level-shifter board).

## Source of truth
- **`../.docs/pcb-blocks.md`** — netlist-level block spec (pin-to-pin tables, BOM).
  KiCad generation is mechanical from these tables.
- **`../.docs/pcb-design.md`** — higher-level decisions + toolchain rationale.
- **`../.docs/handoff.md`** §Board BOM — the discrete circuitry summary.

The circuit was agreed 2026-07-04 (see the "RECONCILED" note in `pcb-blocks.md`):
IR TX on **GP37** (2N2222A + 33Ω from 5V driver), 0.1″ connectors throughout,
power LED, optional microSD LDO, audio SD hard-mute on a GP2 solder-jumper.

## Toolchain (see `pcb-design.md` for the full rationale)
- **Schematic = file-based** via `kicad-skip` (edits `.kicad_sch` S-expr) or SKiDL
  (code → netlist). **KiCad v10 has no schematic API**, so the MCP can't do this.
- **Exports** (Gerbers / BOM / DRC / netlist) via `kicad-cli` (KiCad 10.0.3 at
  `C:\Program Files\KiCad\10.0\bin\kicad-cli.exe`).
- **`kicad-mcp` is PCB-layout only** — registered in `../.mcp.json` for the later
  routing phase; needs a session reload to activate. Not used for schematic.
- **Wokwi `diagram.json`** (in `../sim/wokwi/`) is generated one-way from the KiCad
  netlist for firmware/wiring simulation.

`KICAD_SEARCH_PATHS` (in `../.mcp.json`) points here.

## Status
Scaffold. Schematic capture pending (next step: `kicad-skip`/SKiDL from the
`pcb-blocks.md` tables). Layout/routing/fab gated on the pin freeze (handoff #1/#5,
now largely landed).
