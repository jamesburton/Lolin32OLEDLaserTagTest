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
