# RF (2.4 GHz) Protocol Analysis — Design

Status: approved 2026-07-28. Sub-project: reverse-engineer the Vatos kit's
2.4 GHz link, the way `docs/gun-protocol.md` documented its IR link.

## 1. Why

The Vatos kit is dual-radio. Hits travel over **IR** — already decoded, see
[gun-protocol.md](../../gun-protocol.md) — but the vendor also advertises
"2.4 GHz Data SYNC" between guns and vests, used to keep life/score displays in
sync. That link is undocumented. Understanding it would let our ESP32 targets
see (and eventually participate in) the kit's native game state rather than only
its shots.

Scope for this spec is **detect, capture, document**. Whether we go on to
interoperate (decode into the host) or transmit (spoof shots, act as a base
station) is deliberately deferred until we have real captures — that decision
needs data we do not yet have.

## 2. What we know, and how confident we are

| Claim | Confidence |
| ----- | ---------- |
| Hits are IR; the 2.4 GHz link carries life/score sync | High — vendor listings, consistent with our IR captures |
| The 2.4 GHz chipset is nRF24L01(+) | **Unconfirmed.** Plausible for this product class; no teardown or FCC filing found |
| The kit transmits continuously vs only on events | Unknown — a capture question |

The unconfirmed chipset is the project's central risk. The toy market widely
uses **XN297** and **BK2425**, which resemble the nRF24L01+ but scramble the
preamble and are **not bit-compatible**. An nRF24-based sniffer hears nothing
from them — and "hears nothing" is indistinguishable from "the kit is silent"
unless we check the silicon. Hence Phase 0.

## 3. Hardware

The LC Technology `NRF24L01-TTL_V2` adaptor (CH340T + Nuvoton MS51FB9AE 8051 +
socketed radio) is **not usable as a sniffer**: its firmware owns the radio and
exposes at best AT-style config with a fixed 5-byte address width, no way to
disable address matching, and no raw PHY access. Its value is as a *known-good
transmitter* for validating our own receiver against ground truth.

The radio module is socketed, so it moves to an MCU we control:

| nRF24 pin | ESP8266 GPIO | Silk | Notes |
| --------- | ------------ | ---- | ----- |
| GND | GND | G | |
| VCC | 3V3 | 3V3 | 3.3 V only |
| CE | GPIO4 | D2 | |
| CSN | GPIO5 | D1 | |
| SCK | GPIO14 | D5 | HSPI |
| MOSI | GPIO13 | D7 | HSPI |
| MISO | GPIO12 | D6 | HSPI |
| IRQ | — | — | unconnected; the probe polls |

CE/CSN avoid GPIO15 and GPIO2 on purpose: both are boot-strapping pins, and
CSN idles high, which can stop the board booting. A 10 µF capacitor sits across
the module's supply (fitted) — without it the radio browns out on transmit and
produces phantom packets even in receive-only use.

The probe board is an ESP8266 (CP210x, currently COM6). This costs us the
IR-and-RF-on-one-timebase correlation described in §5, which the Lolin32 could
provide; the probe is therefore written as a **self-contained library** so
moving it to the Lolin32 — or onto a future combined IR+RF unit — is a wiring
and pin-map change, not a rewrite.

## 4. Phases

**Phase 0 — confirm the silicon (no code).** Scan for BLE advertisements while
the kit is powered, to rule BLE in or out. Open a gun and a vest and read the
2.4 GHz chip marking. If it reads XN297 or BK2425, stop and re-plan; an nRF24
sniffer cannot decode those. This is a hard gate: it is the cheapest hour in the
project and it invalidates everything downstream if skipped.

**Phase 1 — RF probe firmware.** Register-level nRF24 driver plus a serial
command surface:

- `selftest` — write and read back config registers to prove the SPI wiring.
  All-`0x00` or all-`0xFF` reads mean MISO/MOSI swapped or the module unpowered.
- `scan [secs]` — sweep all 126 channels polling the Received Power Detector,
  report per-channel hit counts. This is the "are there signals at all"
  deliverable. The RPD trips at roughly −64 dBm, so the kit must be within a few
  metres.
- `sniff ch=<n> rate=<250k|1m|2m>` — promiscuous capture using the Goodspeed
  technique: address width set to an illegal 2 bytes, pseudo-address `0x00AA`,
  CRC disabled, so any burst resembling that preamble is captured and the real
  address falls into the payload.

**Phase 2 — .NET capture and analysis.** Record labelled sessions, then
bit-shift realign, recover candidate 5-byte addresses by byte-frequency
analysis, and validate by recomputing CRC16. Only about 1 capture in 20 is a
genuine packet; CRC validation is the filter that makes the rest tractable.

**Phase 3 — document** in `docs/rf-protocol.md`, in the style of
`gun-protocol.md`: what is constant, what varies, what each field means, and
what remains unknown. Then decide interoperate vs transmit.

## 5. Architecture

```
nRF24L01+ ──SPI──> ESP8266 (rf_probe) ──USB serial──> .NET RfTrainer ──> capture.jsonl
                                                            │
                                                     LaserTag.Rf (pure)
                                              realign / address recovery / CRC16
```

**`lib/Nrf24Raw`** — a thin register-level driver: `readReg`/`writeReg`, channel,
air data rate, RPD, FIFO reads, and the promiscuous configuration. Deliberately
*not* the RF24 Arduino library, which clamps address width to 3–5 bytes and so
cannot express the 2-byte trick at all. Keeping it register-level also keeps it
portable to the ESP32 boards later.

**`src/rf_probe.cpp`** — the serial command loop and line-protocol emitter.
Output follows the existing firmware convention (`FRAME n=… data=…`):

```
RF ch=76 rate=1m ts=1234567 n=32 data=AABBCC…
SCAN ch=76 hits=41
```

**`LaserTag.Rf`** — pure .NET analysis: line parsing, bit-shift realignment,
CRC16 (poly 0x1021), address-candidate scoring. No I/O, so it is unit-testable
against recorded fixtures, matching how `ControlProto` and the UDP parsers are
tested today.

**`tools/RfTrainer`** — the interactive capture app, mirroring `IrSignalTrainer`:
label a scenario ("gun A fires, red, damage 2", "vest powers on", "pairing"),
capture, and diff labelled captures to isolate varying fields. Labelled diffing
is what cracked the IR protocol; the same method applies here.

Later, when the probe moves onto a board that also decodes IR, both event
streams share one microsecond timebase and a single trigger pull ties an IR
frame to its RF packets. That correlation is the strongest available lever for
identifying fields, which is why the combined IR+RF unit is worth building once
Phase 0 confirms compatible signals.

## 6. Error handling

The probe must fail loudly and specifically, because silent failure is
indistinguishable from "no traffic":

- **No radio**: `selftest` reports the read-back registers, not a bare pass/fail,
  so a wiring fault is visible rather than inferred.
- **Empty scan**: reported as "no channel exceeded the RPD threshold", with the
  reminder that RPD needs roughly −64 dBm — i.e. a range problem, not proof of
  silence.
- **Capture noise**: the .NET side reports total-captured vs CRC-valid counts, so
  a low yield reads as expected behaviour rather than a bug.

## 7. Testing

`LaserTag.Rf` gets xUnit tests over recorded fixtures: realignment, CRC16
against known-good nRF24 packets, and address recovery over a synthetic stream
with a known planted address. Firmware is bench-verified: `selftest` against the
real module, `scan` with the LC Tech board transmitting on a known channel as
ground truth (this is where that board earns its place), and `sniff` validated
by capturing the LC board's own traffic, whose address and payload we set.

Ground-truth-first matters: proving the sniffer against a radio we control means
that when we point it at the Vatos kit, silence is evidence about the kit rather
than about our code.

## 8. Out of scope

Transmitting, spoofing, or pairing with the kit; any RF work against hardware we
do not own; changes to the existing IR pipeline or the V2 control plane. The
combined IR+RF unit is a follow-on, gated on Phase 0.

## 9. Success criteria

1. `selftest` reads back written registers on the real module.
2. `scan` shows a clear channel-occupancy difference between the kit powered off
   and powered on/firing — or Phase 0 explains why it cannot.
3. At least one CRC-valid packet is captured from the kit and its 5-byte address
   recovered.
4. `docs/rf-protocol.md` records the findings, explicitly separating confirmed
   structure from speculation.
