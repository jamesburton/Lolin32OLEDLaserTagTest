# Vatos 2.4 GHz Link — Findings

Working notes for the RF sub-project. Design:
[spec](superpowers/specs/2026-07-28-rf-protocol-analysis-design.md) ·
[plan](superpowers/plans/2026-07-28-rf-protocol-analysis.md).

Written in the spirit of [gun-protocol.md](gun-protocol.md): confirmed
observations and inference are kept visibly separate.

## Status

**Paused, pending evidence that the kit has a radio at all.** The tooling is
finished and proven; the target has not been shown to exist.

| Phase | State |
| ----- | ----- |
| 0 — identify the silicon | **Blocked, permanently by this route.** The units are a child's toys in daily use and cannot be disassembled. No model number on the units; packaging not retained. |
| 1 — probe firmware | Done: `selftest`, `scan`, `watch`, `dwell`, `sniff`, all bench-verified |
| 2 — .NET analysis | Done: `LaserTag.Rf` (parser, CRC16, realignment, ESB validation, address recovery), 16 xUnit tests green |
| 3 — document | This file |
| Remaining plan tasks | `RfTrainer` interactive capture app (plan Task 6) — deferred; there is no confirmed signal to capture |

### The constraint that changes the plan

The spec made Phase 0 — read the 2.4 GHz chip marking — a hard gate before
trusting any capture work. **That gate cannot be satisfied.** The guns cannot be
opened, carry no visible model number, and the packaging is gone. Every
conclusion below therefore rests on measurement alone, with no way to confirm
what silicon is inside.

This matters because the failure modes are indistinguishable from the outside:
a kit with no radio, a kit with an XN297/BK2425 radio our hardware cannot decode,
and a kit whose radio stays silent without a vest all produce exactly the
observations we recorded.

### Leading hypothesis: these guns have no radio

These are the **rechargeable, gun-only units, sold without vests**. The
"2.4GHz Data SYNC" claim that motivated this sub-project comes from vendor
listings for Vatos sets **bundled with LCD-equipped vests** — not from this
product. If the RF hardware ships only in the vest-bundled variants, then these
guns have nothing to transmit, and every measurement below is explained at once
without any of the more exotic explanations.

That would also make this the correct outcome rather than a failure: the
sub-project's job was to find out whether there is a protocol to reverse
engineer, and "there is no radio in this variant" is a legitimate answer.

### Product-line evidence (2026-07-28 research)

Across every Vatos SKU found, **"2.4GHz Data Sync" appears only on sets bundled
with vests or wearable receivers**:

| SKU | Vests/receivers | 2.4 GHz advertised |
| --- | --------------- | ------------------ |
| VL-BB8933A (B0C5JGFHSY) | 4 strap-on receivers | Yes |
| B092W1PMMK, B0CBBLGNRB, B0DZ6JC3VJ, B0FVWYGJ9C | Vests (LCD) | Yes |
| **VL-BB8933B (B0CZL4NCP3) — "No Vests Needed"** | **None; IR receiver built into each gun** | **No mention anywhere in the listing or vendor page** |

The one confirmed vest-free rechargeable line advertises no RF features at all,
describing hit detection as infrared receivers plus vibration inside each gun.
That matches this kit's description and matches everything measured. Treat as
strong circumstantial evidence, not proof — the unit is unlabelled.

No standalone Vatos vests or third-party compatible vests were found for sale,
and no documented procedure for retrofitting vests to a gun-only set. So "buy a
vest to test the link" is **not currently an available experiment**; the vests
appear only inside complete bundles.

### The one non-invasive check that would settle it

Vatos's manufacturer (Canhui Plastic Toys) holds FCC ID **`2A6LV-BB1550F`**, a
genuine intentional-radiator grant for a spread-spectrum digital transmission
system at **2407.0–2475.0 MHz**, ~1 mW. So RF-equipped units in this family do
exist and are labelled.

**Look on the underside, battery/charge door or grip for an "FCC ID: 2A…" label
and look it up at fccid.io.** A label proves a radio and names its band; no
label at all (US-market units must carry one for an intentional radiator) is
weak-but-real evidence against, though a UK/EU unit may show only CE/UKCA.

### Refinement if work ever resumes

That FCC grant covers **2407–2475 MHz**, and ~1 mW is very low power. Two of the
three channels sniffed on 2026-07-28 — 2402 and 2476 MHz — fall **outside** that
range; only 2464 MHz was inside it. Any future sweep should confine itself to
channels 7–75, include **250 kbps** (untested, and the most likely choice for a
1 mW link needing range), and be run with the probe within a metre of the unit.

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

### Power-cycle / pairing test (confirmed negative)

Hypothesis: the kit may choose a channel at pairing, so power-cycling would move
it and defeat fixed-channel tests. Tested by aggregating four full-band sweeps
(120 ms/channel, 960 samples/channel total) while a gun was repeatedly switched
off and on with a shot fired in between, since the team is not locked until the
first shot.

Top channels were 2411 (23.8%), 2435 (23.2%), 2465 (21.7%), 2436 (19.4%) and
2434 MHz (19.2%) — broad humps centred on WiFi channels 1, 6 and 11. **No narrow
isolated carrier appeared anywhere.** Hopping changes which channel is used, not
the narrowness of the carrier, so a hopping radio would still have shown a
1-2 MHz spike somewhere across four sweeps. It did not.

### Is this model even RF-equipped?

Worth resolving before any further RF work: the "2.4GHz Data SYNC" claim comes
from **vendor listings for Vatos sets with LCD-equipped vests**, not from this
unit. If these guns are an IR-only model, every observation above is explained
at once and there is no protocol to find. Check the model number on the box or
manual, and look inside for a radio module and antenna trace.

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

## Phase 0 — hardware identification (blocked; non-invasive routes only)

The chipset is **unconfirmed and, without disassembly, may stay that way**. The
toy market widely uses **XN297** and **BK2425**, which resemble the nRF24L01+
but scramble the preamble and are **not bit-compatible** — an nRF24 sniffer
hears nothing from them, and that silence looks identical to "no radio fitted".

Non-invasive checks that remain available, in order of value:

1. **Regulatory label.** Look on the underside, the battery/charge door, or
   moulded into the grip. In the US an intentional radiator must carry an
   **FCC ID**; finding one proves a radio exists and identifies it via the FCC
   database. A UK/EU unit may show only CE/UKCA, so absence is weak evidence —
   suggestive of no radio, but not proof.
2. **BLE scan** with a phone (nRF Connect or similar), kit off then on and
   firing. Rules the BLE case in or out without opening anything.
3. **Vendor listing archaeology.** Identify the SKU from external features
   (rechargeable, no vest, weapon-mode count) and check whether that specific
   listing advertises 2.4 GHz sync. See the "Open questions" section.
4. **Buy one vest.** The most decisive non-invasive test available: if the RF
   link is gun-to-vest, a vest gives it a reason to transmit, and the tooling
   here is ready to capture it the same evening. Only worth it if the listings
   confirm the feature exists for these guns.

## Still unknown

- **Whether these guns contain a 2.4 GHz radio at all.** This is now the primary
  question; everything else is downstream of it.
- The chipset, if one exists — not resolvable without disassembly.
- 250 kbps was never sniffed. Of the three air data rates, only 1 M and 2 M were
  tested, on 3 of 126 channels.
- Whether the link (if any) is event-driven, and on what event.
- Packet structure, addressing, and whether the payload carries anything not
  already visible in the IR frames.

## If work resumes

The tooling is complete and needs no further build-out to take a first capture.
The sequence would be: flash `pio run -e esp8266-rfprobe -t upload`, run
`scan` for a baseline, `watch from=0 to=83 ms=150` with the kit off and again
with it active, then `dwell` on any candidate to confirm it before believing it,
and `sniff ch= rate=` on a confirmed channel. Feed the captured `RF …` lines
through `LaserTag.Rf`; a non-zero CRC-valid count is the only result that counts
as detection.

Do not repeat the 2026-07-28 mistake: a candidate found by sweeping is not a
finding until dwelling on it reproduces the effect.
