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

## set-wifi.ps1

Provisions WiFi credentials on a board over serial (TagNet stores them in NVS,
then connects). Works for both boards.

```powershell
./tools/set-wifi.ps1 -Port COM14 -Ssid "MyNetwork" -Password "s3cret"
```

Equivalent manual commands in any serial monitor (one per line):

```
ssid MyNetwork
pass s3cret
wifi-save
```

Other TagNet commands: `wifi-status`, `wifi-clear`. Once connected the board
reports its IP — use that **IP** for OTA and monitoring (mDNS
`lasertag-<board>.local` is best-effort and does not resolve on every host).

## TagMonitor

A .NET console app that listens for the boards' UDP telemetry broadcasts and
prints them raw with a timestamp + source IP. Devices emit the V2 control-plane
line-protocol (heartbeats `HB`, telemetry `EVT`); see the
[control-plane contract](../docs/superpowers/specs/2026-06-15-control-plane-contract.md).

```sh
dotnet run --project tools/TagMonitor        # listens on UDP 4210
```

Example output:

```
[15:30:01] 192.168.1.24   lasertag-matrix HB id=752b38 ip=192.168.1.24 fw=2.0.0 team=2 mode=idle hp=100 online=1
[15:30:05] 192.168.1.24   lasertag-matrix EVT hit victim=752b38 shooterTeam=2 dmg=2 proto=vatos hp=80 ts=12345
[15:30:05] 192.168.1.24   lasertag-matrix EVT state s=stunned hp=80 ts=12500
```

For typed access (parse into records, track a live roster, drive the REST API),
use the **`dotnet/LaserTag.Client`** library instead of parsing raw lines.

> **Nothing printed but REST works?** Missing inbound firewall rule for UDP 4210,
> or a lossy weak-RSSI link — see `setup-firewall.ps1`/`.sh` below.

## setup-firewall.ps1 / setup-firewall.sh

Checks (and optionally fixes) the host firewall so the UDP listener can receive
telemetry on port 4210. A missing inbound rule is one common cause of "REST
works but no heartbeats" (a lossy link is the other).

```powershell
./tools/setup-firewall.ps1            # Windows: check, then offer to add (self-elevates via UAC)
./tools/setup-firewall.ps1 -Check     # diagnose only (exit 0 = ok, 1 = missing)
./tools/setup-firewall.ps1 -Remove    # undo
```

```sh
./tools/setup-firewall.sh             # Linux (ufw/firewalld via sudo); macOS verify+advise
./tools/setup-firewall.sh --check     # diagnose only
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
