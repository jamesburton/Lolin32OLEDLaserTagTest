# Tools

## IrSignalTrainer

A .NET console app that reads the board's serial output, identifies IR signals,
and lets you tag them against named devices and buttons.

### Run

Close any other program holding the serial port first (and pause any captures),
then:

```sh
dotnet run --project tools/IrSignalTrainer            # COM14, signatures.json
dotnet run --project tools/IrSignalTrainer COM14 signatures.json
```

Arguments: `[serialPort] [libraryPath]` (defaults `COM14`, `signatures.json`).

### Commands (single keypress)

| Key | Action |
| --- | --- |
| `t` | Train: enter device + button names, then fire/press 4 times |
| `l` | List trained signatures |
| `q` | Quit |

Fire a device at any time to see live matches:

- **Green `HIT  <device> / <button>`** — recognised. NEC signals show the code
  (e.g. `0707:04`); raw signals show the fingerprint deviation.
- **Yellow `Unknown`** — no match; shows the NEC code or raw mark list.

### How matching works

- **NEC signals** (TV remotes, and any NEC-based gun): matched **exactly** on
  the decoded `addr:cmd` code. The address identifies the device, the command
  identifies the button. Robust and range-independent.
- **Non-NEC signals**: fall back to a raw pulse-timing fingerprint with a 25%
  per-mark tolerance. Less reliable; used only when no NEC decode is available.

Training stores whichever form applies. `signatures.json` is the library:

```json
[
  { "Device": "LivingRoomTV", "Button": "Power", "NecCode": "0707:04", "Samples": 4 }
]
```

## Firmware serial protocol

The firmware (`src/main.cpp`) emits, per received IR burst:

```
NEC addr=0x0707 cmd=0x04     # optional — only when the frame decodes as NEC
FRAME n=75 data=L4520,H4490,L570,H1669,...   # always — raw edge timings (µs)
```

The optional `NEC` line precedes its `FRAME` line so a consumer can attach the
decoded code to the frame that terminates the event. `H`/`L` mark whether the
line was HIGH or LOW for that duration (VS1838B idles HIGH, pulses LOW).
