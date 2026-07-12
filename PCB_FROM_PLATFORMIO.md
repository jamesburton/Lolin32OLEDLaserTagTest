# From PlatformIO code to a fabbed PCB

How this project went from firmware + a hand-wired perf-board prototype to
Gerbers submitted to PCBWay — the tools, the pipeline, and the gotchas, so the
next board takes an afternoon instead of a week.

The result: `hardware/lasertag-carrier/` — a 100×80 mm 2-layer THT carrier for
the ESP32-S3-Matrix (socketed), with MAX98357A audio, microSD, IR RX/TX driver,
WS2812 out, OLED header, GP2 role selector, power switch and M3 mounting holes.

---

## Toolchain

| Tool | Role | Why it was chosen |
|---|---|---|
| [KiCad](https://www.kicad.org/) 10 | The hub: board editor, DRC, Gerber export, Python API | Open S-expression text formats, real CLI (`kicad-cli`), scriptable end to end |
| [atopile](https://atopile.io/) | Authored the initial netlist + board from `.ato` code | Circuit-as-code fits a firmware repo; generated a valid `.kicad_pcb` from the spec |
| [Freerouting](https://github.com/freerouting/freerouting) | Autorouting (headless) | Routed all ~70 connections of this board in ~10 s, zero unrouted |
| [Wokwi](https://wokwi.com/) | Firmware+circuit simulation (optional) | Runs the actual PlatformIO firmware against a virtual circuit |
| PCBWay | Fabrication | ≤100×100 mm boards are in the cheapest tier (~$5/5 pcs) |

The whole flow ran **scripted on Windows** — no GUI steps were required to get
from spec to a DRC-clean, fully routed, labelled board. (A human review pass in
the KiCad GUI is still sensible before ordering.)

## The pipeline

1. **Spec first.** Write the circuit as markdown (`.docs/pcb-blocks.md`): nets,
   BOM, footprints, design decisions. Everything downstream implements this.
2. **Author the board.** atopile compiles `.ato` modules to a KiCad netlist +
   board. Alternatively clone footprint blocks directly in the `.kicad_pcb` —
   it's a text file and safe to edit with balanced-parenthesis surgery.
3. **Fix atopile's output gaps.** With offline custom components you get
   `U`-prefixed placeholder designators, blank values and an empty BOM. All
   three are fixable mechanically: every footprint carries an
   `atopile_address` property whose leaf name *is* the real designator
   (`ir.q1` → `Q1`).
4. **Place by script.** Measure real bounding boxes (pads + graphics), model
   keep-outs for the socketed module and elevated breakout bodies, then place
   to a floor plan. Verify with an independent overlap/bounds checker — never
   trust the placing script's own success message.
5. **Route.** `pcbnew` Python exports a Specctra DSN → Freerouting headless
   (`-de in.dsn -do out.ses -mp 20 -mt 1`) → import the SES. Add a bottom-layer
   GND pour **after** routing (see gotchas), widen power nets, and narrow back
   any segment DRC flags — loop until clean.
6. **Silk.** Pin-1 labels + signal names at every connector, values inside
   resistor bodies and beside caps, function names for jumpers, socket names
   placed *under the breakout bodies* (visible when empty, covered by the very
   module they name once populated).
7. **Verify + export.** `kicad-cli pcb drc --severity-all`, then
   `kicad-cli pcb export gerbers` (7 layers) + `export drill` (Excellon), zip.

## Ordering from PCBWay

- **[PCBWay](https://www.pcbway.com/)** — upload the Gerber zip manually via
  *Quote* → *Add Gerber file*, or, if you have KiCad with the
  **[PCBWay Plug-In for KiCad](https://www.pcbway.com/blog/News/PCBWay_Plug_In_for_KiCad_3ea6219c.html)**
  installed, push the board straight from the PCB editor with one click.
- **Verify the zip first** with PCBWay's
  [Online Gerber Viewer](https://www.pcbway.com/project/OnlineGerberViewer.html)
  — it renders every layer plus the drill file and catches missing layers or
  misaligned drills before you pay for them.
- Form settings for a board like this: 2 layers, 1.6 mm, HASL, 1 oz copper,
  min hole **0.3 mm tier** (that field is the *capability tier* you require,
  not your smallest hole — ours is 0.4 mm, comfortably inside standard).
- Protel-style filename extensions (`.gtl/.gbl/.gts/.gbs/.gto/.gbo/.gm1`) are
  what `kicad-cli` emits by default and what PCBWay expects.

The ready-to-upload archive lives at
`hardware/lasertag-carrier/fab/lasertag-carrier-rev1-gerbers.zip` and is also
attached to the `pcb-carrier-rev1` GitHub release.

## Gotchas (each one cost real time)

**atopile**
- Pin the version. 0.12.5 on Python 3.13 is the known-good combo here; 0.15.x
  silently **emptied the board layout** (its layout-sync needs a GUI plugin)
  and Python 3.14 breaks the CLI outright.
- Windows needs `PYTHONUTF8=1 PYTHONIOENCODING=utf-8 NO_COLOR=1` on every call
  or it crashes printing emoji.
- Its footprint parser rejects KiCad-10 footprints containing
  `allow_soldermask_bridges` (all stock solder jumpers) — keep locally patched
  copies, then **restore the attribute afterwards in the board and library**
  or DRC reports every jumper as a solder-mask bridge.
- After any manual edit to the generated board, **never run `ato build`
  again** — it regenerates the layout and destroys the work. The `.ato` files
  become documentation.

**Freerouting**
- Match the jar to your Java: 2.2.4 needs Java 25, 2.1.0 runs on Java 21
  (Android Studio bundles one). Older releases ship installers, not jars.
- `-mt 1` always — the multithreaded optimizer is known-broken.
- **Export the DSN without copper pours.** A GND plane in the DSN makes
  Freerouting skip routing GND entirely, and the other bottom-layer tracks
  then slice the pour into disconnected islands. Route GND as traces, add the
  pour after import.
- It leaves exact duplicate segments (same net, same endpoints, stacked up to
  3 deep across layers). Deduplicate by grouping on (net, endpoint-pair) —
  not by chasing DRC "dangling" reports, which chain and eventually delete
  load-bearing copper.

**KiCad scripting (`pcbnew` Python + text surgery)**
- The `.kicad_pcb` is an S-expression text file; balanced-paren block
  extraction + targeted regex is often *more* reliable than the API.
- `BOARD.Remove(footprint)` can silently no-op — print success, save, change
  nothing. **Verify every removal by re-counting in the saved file.**
- SWIG memory-leak spam on stderr is harmless, but redirecting it to
  `/dev/null` also hides real tracebacks; log to a file and filter.
- Long multi-pass scripts segfault unpredictably — run one pass per process.
- Footprint text angles are stored **absolute**: rotating a connector rotates
  its refdes too, and fixing it means setting angle 0, not counter-rotating.
- kicad-cli 10.0.3 DRC has a phantom `silk_overlap` bug pairing reference
  fields with items 10–70 mm away. If the geometry is impossible, verify the
  silk Gerber/SVG directly and move on.
- No schematic exists in this flow — **never let KiCad create one**: "Update
  PCB from Schematic" with an empty sheet deletes every footprint.

**Process**
- Git-commit between every scripted pass; recovery is one `git checkout`.
- ≤100×100 mm costs the same as any smaller board at the cheap-tier fabs —
  when 75×50 proved cramped (silk collisions, necked traces), growing to
  100×80 was free and fixed both.
- DRC-clean ≠ reviewed: the rule checker can't see that a module is mirrored
  or a connector faces the wrong edge. Hold the physical parts against the
  render before paying.
