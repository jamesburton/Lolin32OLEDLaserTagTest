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

## RF probe (2.4 GHz capture and diagnostics)

Tooling from the RF sub-project, which investigated whether the Vatos kit uses a
2.4 GHz link alongside its IR. **Nothing was detected and the work is parked**
(see [docs/rf-protocol.md](../docs/rf-protocol.md)); this section is here so the
tools can be picked up again without rediscovering how they work.

### Hardware

An nRF24L01+ module wired to an ESP8266 over SPI. The module was pulled from an
LC Technology `NRF24L01-TTL_V2` adaptor — that board's own firmware is a
transparent serial bridge and **cannot** capture foreign traffic, so only its
socketed radio is useful.

| nRF24 pin | ESP8266 GPIO | Silk |
| --------- | ------------ | ---- |
| GND / VCC | GND / 3V3 | 3.3 V only, never 5 V |
| CE | GPIO4 | D2 |
| CSN | GPIO5 | D1 |
| SCK | GPIO14 | D5 |
| MOSI | GPIO13 | D7 |
| MISO | GPIO12 | D6 |
| IRQ | — | unconnected; the probe polls |

CE and CSN deliberately avoid GPIO15 and GPIO2 — both are boot-strapping pins
and CSN idles high, which can stop the board booting. Fit a **10 µF capacitor
across the module's supply**: without it the radio browns out and produces
phantom captures. Keep leads under about 10 cm.

```sh
pio run -e esp8266-rfprobe -t upload --upload-port COM6
pio device monitor -e esp8266-rfprobe
```

### Commands

| Command | What it does |
| ------- | ------------ |
| `selftest` | Writes then reads back `RF_CH`/`SETUP_AW`. Proves the SPI wiring before anything else is believed. All `0x00`/`0xFF` means MISO/MOSI swapped, CSN miswired, or no 3V3. |
| `scan [sweeps]` | Sweeps all 126 channels reading the power detector. Fast, low sensitivity — finds continuous carriers, misses bursts. |
| `watch from= to= ms=` | Dwells `ms` per channel over a range. ~100× the listening time of `scan`, so it can catch short bursts. Reports `high/samples` and a percentage. |
| `dwell ch= secs=` | Camps on one channel reporting 100 ms buckets. This is how you correlate activity with an event (a trigger pull, a power-on). |
| `sniff ch= rate= secs=` | Promiscuous capture at `250k`, `1m` or `2m`. Emits `RF ch= rate= ts= n= data=<hex>` lines for offline analysis. |

### Method that actually works

1. `selftest` — never trust a session that hasn't passed this.
2. `scan` for orientation, then `watch` over the band **with the target off** to
   get a control, and again with it active. Run the control close in time: WiFi
   occupancy swings by more than most signals of interest.
3. **Confirm any candidate with `dwell` before believing it.** Three candidates
   died at this step during the 2026-07-28 session; a sweep visits each channel
   briefly, so a passing WiFi burst can look like a 37% spike.
4. `sniff` on a confirmed channel, at each data rate in turn — a 2 Mbps
   transmitter is completely invisible to a 1 Mbps listener.
5. Analyse offline. **Only a CRC-valid packet counts as a detection.**

Interpreting occupancy: WiFi appears as broad humps roughly 20 MHz wide centred
on 2412, 2437 and 2462 MHz. A narrow 1–2 MHz spike with quiet neighbours is
interesting; a broad hump is not. Note also that the power detector only trips
above about −64 dBm, so work within a couple of metres, and that nRF24 channels
above 83 (2483 MHz) are outside the ISM band, where certified products won't be.

### Analysing captures

Save `sniff` output to a file, then run the analyser — a .NET 10 file-based app,
so no project scaffolding is needed:

```sh
dotnet run tools/rf-analyse.cs docs/captures/rf-captures-2026-07-28-two-guns-firing.txt
```

It reports, per channel and rate, how many captures survive CRC validation as
Enhanced ShockBurst packets (trying address widths 3–5), and ranks recurring
byte sequences as candidate addresses. Sequences like `AAAAAAAAAA` or
`A0A0A0A0A0` are alternating-bit noise, not addresses — promiscuous capture is
roughly nineteen parts noise to one part signal, so a low yield is normal and a
**zero** yield is the meaningful result.

The parsing, CRC16, bit realignment, packet validation and address recovery live
in **`dotnet/LaserTag.Rf`** (16 xUnit tests, including a round trip that builds
an ESB packet, bit-shifts it and recovers it), so any new tool can reuse them.

### Limits worth knowing before trusting a negative

The nRF24 can only decode nRF24-compatible traffic. **XN297 and BK2425** — both
common in toys — look similar but scramble the preamble and are not
bit-compatible, so they read as silence. Silence is therefore never proof of
absence on its own; it has to be combined with evidence about what the target
actually contains.

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
reports its IP — use that **IP** for OTA (mDNS `lasertag-<board>.local`
resolves fine for ping/REST on this host, but espota is unreliable with mDNS
names, so OTA should always target the current IP).

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
