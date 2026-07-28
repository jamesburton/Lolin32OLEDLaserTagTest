# Vatos 2.4 GHz Link — Findings

Working notes for the RF sub-project. Design:
[spec](superpowers/specs/2026-07-28-rf-protocol-analysis-design.md) ·
[plan](superpowers/plans/2026-07-28-rf-protocol-analysis.md).

Written in the spirit of [gun-protocol.md](gun-protocol.md): confirmed
observations and inference are kept visibly separate.

## Status

| Phase | State |
| ----- | ----- |
| 0 — identify the silicon | **Pending** (blocking; see below) |
| 1 — probe firmware | `selftest` + `scan` done and bench-verified; `sniff` not yet written |
| 2 — .NET analysis | Not started |
| 3 — document | This file |

## Probe hardware (confirmed)

nRF24L01+ module pulled from an LC Technology `NRF24L01-TTL_V2` adaptor
(CH340T + Nuvoton MS51FB9AE), wired to an ESP8266 (CP210x, COM6). The adaptor's
own firmware is a transparent serial-to-RF bridge and cannot capture foreign
traffic, so only its socketed radio is used.

Wiring: CE=GPIO4 (D2), CSN=GPIO5 (D1), SCK=GPIO14 (D5), MOSI=GPIO13 (D7),
MISO=GPIO12 (D6), IRQ unconnected, VCC=3V3 with a 10 µF cap at the module.

`selftest` result (2026-07-28), confirming the SPI link:

```
SELFTEST rfch=0x2A(exp 0x2A) setupaw=0x03(exp 0x03) config=0x08 status=0x0E
SELFTEST ok - radio is responding
```

`config=0x08` is the power-on default (EN_CRC set, PWR_UP clear), which is what
a healthy, idle radio should report.

## Result so far: the kit was NOT detected on air

Across a full evening of measurements with one gun and with two guns firing at
each other, **no signal attributable to the kit was found**. Three candidate
channels appeared and all three failed verification. This is a negative result,
not a null one: the reasons it might be a false negative are listed below, and
they are worth more than the measurements.

### Candidates raised and retracted

| Candidate | Why it looked real | Why it failed |
| --------- | ------------------ | ------------- |
| 2446 & 2407 MHz | ~147 "trips" firing vs 2-3 with the gun off | Measured with an uncalibrated counter (see below). A dwell control on the same idle channel disagreed by ~100x. Artefact. |
| 2411 MHz | 56% occupancy firing vs 9% control; neighbour 2412 at 4%, too narrow for WiFi | Camping on it showed a flat ~13-15% floor in every 100 ms bucket with no trigger correlation, and 17% overall minutes later. The 56% was a passing WiFi burst. |
| 2464 MHz | 37% with two guns firing, neighbours at 0-2% — a 1 MHz spike, impossible for 20 MHz-wide WiFi | Dwelling on it during firing gave **7%, below its own 10% idle floor**. The sweep's 37% was one transient burst caught during that channel's 150 ms visit. |

### Instrument bug found and fixed (important)

The first `watch`/`dwell` implementation counted every SPI poll where the power
detector read high. That count scales with how fast the polling loop spins and
how often it re-arms, **not** with airtime, so figures from different commands
were never comparable and the first two candidates above were manufactured by
the measurement itself. Fixed by sampling on a fixed 500 µs cadence and
reporting `high/samples` as a percentage (commit `9d72355`). Every number in
this document from the 2411 MHz candidate onward uses the calibrated metric;
treat any earlier "trips" figure as meaningless.

Lesson worth keeping: an occupancy metric must be normalised by sample count
before any A/B comparison, and a candidate found by sweeping must be confirmed
by dwelling on it. Both retractions came from that second step.

### Why this may still be a false negative

1. **No vest.** The vendor describes the 2.4 GHz link as gun-to-vest data sync.
   With no vest in the house, the guns may have had nothing to sync with. Two
   guns firing at each other did not change the picture, but gun-to-gun sync is
   an assumption, not a documented behaviour.
2. **Power-on pairing not tested.** A gun with no partner most plausibly
   transmits at switch-on and then goes quiet. The planned test — repeated
   power-cycling during a sweep, so bursts accumulate — was not run.
3. **The chipset is still unidentified.** If it is XN297 or BK2425, this radio
   cannot decode it and would show exactly what we saw. See Phase 0 below.
4. **Energy detection is weak by nature.** The RPD reports only that power
   exceeded ~−64 dBm. Within the WiFi band, household traffic swings by more
   than the effect we are hunting, which is what defeated every candidate.

### Promiscuous capture: zero valid packets (confirmed)

With **two guns firing at each other**, 2901 promiscuous captures were recorded
and run through `LaserTag.Rf`:

| Channel | 1 Mbps | 2 Mbps |
| ------- | ------ | ------ |
| 2402 MHz (ch 2) | 369 captures, **0 CRC-valid** | 946 captures, **0 CRC-valid** |
| 2464 MHz (ch 64) | 142 captures, **0 CRC-valid** | 537 captures, **0 CRC-valid** |
| 2476 MHz (ch 76) | 228 captures, **0 CRC-valid** | 679 captures, **0 CRC-valid** |

Every address width from 3 to 5 bytes was tried on every capture. The most
frequent recurring 5-byte sequences were `AAAAAAAAAA` (105), `A0A0A0A0A0` (37)
and `2828282828` (33) — alternating-bit noise patterns, not addresses.

This is much stronger than the occupancy work: the validator is unit-tested
against a synthetic ESB packet it can build, bit-shift and recover, so a real
nRF24 packet on those channels at those rates would have been found.

Scope of the claim: 3 of 126 channels at 2 of 3 data rates. It does **not**
prove the kit is silent — 250 kbps was untested and 123 channels were not
swept — but combined with every energy candidate failing, it materially
increases the odds that the kit is either not nRF24-compatible or not
transmitting without a vest.

## Baseline scan — Vatos kit OFF (confirmed)

`scan 30`, 2026-07-28, 30 sweeps of all 126 channels reading the RPD
(trips above roughly −64 dBm). 51 channels showed at least one hit:

| Region | Channels (MHz) | Peak hits | Reading |
| ------ | -------------- | --------- | ------- |
| 2402–2445 | dense, near-continuous | 6 @ 2438 | Ambient WiFi (2.4 GHz channels 1–7) |
| 2402 / 2426 / 2480 | isolated peaks | 4 | The three BLE advertising channels |
| 2453–2470 | sparse, 1–3 hits | 3 | WiFi tail / intermittent traffic |
| 2482–2525 | **no hits** | 0 | Quiet — the useful search space |

The band above 2482 MHz being empty is the important result: if the kit's link
lives there, it will stand out unambiguously against this baseline. If it sits
inside the WiFi region, captures will need the difference between kit-off and
kit-on runs rather than raw occupancy.

**Not yet done:** the matching kit-ON scan. Run `scan 40` within two metres of a
powered gun and vest, firing, and diff against the table above.

## Phase 0 — hardware identification (BLOCKING, not yet done)

The chipset is **unconfirmed**. nRF24L01 is plausible for this product class but
no teardown or FCC filing was found to support it. The toy market widely uses
**XN297** and **BK2425**, which resemble the nRF24L01+ but scramble the preamble
and are **not bit-compatible** — an nRF24 sniffer hears nothing from them, and
that silence is indistinguishable from "the kit is quiet".

So before any capture work is trusted:

1. BLE-scan with the kit off, then on and firing. A new advertiser that tracks
   the kit means the link is BLE, and this whole approach changes.
2. Open a gun and a vest, and read the marking on the 2.4 GHz IC.

| Marking | Verdict |
| ------- | ------- |
| nRF24L01 / nRF24L01+ / SI24R1 / RFX24C01 | Proceed |
| XN297 / XN297L, BK2425 / BK2401 / BK5811 | Stop; re-plan around a different radio or an SDR |
| BLE/proprietary SoC with no separate radio | Stop; re-plan |

## Still unknown

- The chipset (Phase 0).
- Which channel(s) the kit uses, and its air data rate — a listener at the wrong
  rate is deaf, so 250 k, 1 M and 2 M must each be tried.
- Whether the link is continuous or event-driven (only on hits, pairing, or
  power-on).
- Packet structure, addressing, and whether the payload carries anything not
  already visible in the IR frames.
