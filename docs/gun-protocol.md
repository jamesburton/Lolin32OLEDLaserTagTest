# Laser-Tag Gun IR Protocol (observed)

Reverse-engineered from VS1838B captures (`gun-frames.log`). This is an
empirical description from one gun, not an official spec.

## Carrier

The VS1838B (a 38 kHz demodulating receiver) reads the gun cleanly and
consistently, so the gun's carrier is **~38 kHz**. A 56 kHz receiver is not
needed. The gun is **not** NEC protocol.

## Frame structure

A weapon shot is a **41-edge frame**: 21 IR bursts (LOW, the VS1838B pulls its
output low during a burst) interleaved with 20 gaps (HIGH), starting and ending
with a burst.

Two symbol widths are used throughout:

| Symbol | Duration | Bit |
| ------ | -------- | --- |
| short  | ~380 µs  | 0   |
| long   | ~800 µs  | 1   |

So each frame is 41 short/long symbols. Quantising at a 600 µs threshold gives a
stable bit signature; within a weapon every repeat is identical.

### Constant structure

- **Gap preamble**: gap bits 0–3 are short, 4–7 are long (`0000 1111`) on every
  weapon — a sync/header.
- **Burst markers**: burst bit 0 (leading) and burst bit 12 are always long.
- **Payload**: burst bits 13–20 and gap bits 8–19 carry the **team + damage**.
  Both bursts and gaps carry payload — decoding from gaps alone is insufficient
  (see below).

## The IR encodes damage, not weapon

The gun transmits the **team** and the **damage** of the shot, not which weapon
fired it. Per the manual, the weapons map to damage:

| Weapon | Damage |
| ------ | ------ |
| Pistol | 1 |
| Shotgun | 2 |
| SMG | 2 |
| MG | 3 |
| Rocket | 4 |

Because Shotgun and SMG both deal **damage 2**, they transmit an **identical IR
frame** — no receiver can tell them apart. Confirmed in the data: for Blue,
Shotgun and SMG produce the same gap signature (`…000000010001`) while MG
(damage 3) differs (`…000000010010`). So a gun emits at most **4 distinct
damage codes per team** (4 teams × 4 damage = up to 16 codes total).

> **Audio caveat (this unit):** the spoken weapon names for **SMG and MG are
> swapped** — the gun announces "Machine Gun" while firing SMG and vice-versa.
> Identify shots by their damage code, not the audio.

When labelling trained signatures, use the damage (e.g. `Blue/dmg2`) rather than
a weapon name, since Shotgun and SMG are indistinguishable on the wire.

## Decoded bit map (full 4-team × 4-damage matrix)

Quantising each 41-edge frame to bits (every edge, bursts and gaps interleaved
in order, `>600 µs` = 1) and comparing all 16 team×damage codes isolates the
fields cleanly:

| Field | Bit positions | Encoding |
| ----- | ------------- | -------- |
| Preamble / framing | 0–21 | constant |
| **Team** | 22–24 | MSB-first: Blue=`001`(1), Red=`010`(2), Green=`011`(3), White=`100`(4) |
| Separator | 25–29 | constant |
| **Damage** | 30–32 | MSB-first: `001`–`100` = damage 1–4 |
| Separator | 33–36 | constant `0000` |
| Checksum | 37–40 | function of team+damage (≈ T+D+4 with a small nonlinearity; symmetric in T and D — not a popcount) |

So a shot decodes directly: `team = bits[22..24]`, `damage = bits[30..32]`,
both as 3-bit MSB-first values. The checksum can validate a frame but isn't
needed to decode. (Weapon is not transmitted — see the damage section above.)

### Reference: the 16 codes

```
Blue (team 1)                              Red (team 2)
 d1 10000000010101010000000010000000100000110
 d2 10000000010101010000000010000001000000111
 d3 10000000010101010000000010000001100001000
 d4 10000000010101010000000010000010000001001
```

(Full set captured in the trained `signatures.json`; the only varying regions
are bits 22–24, 30–32, and the 37–40 checksum.)

## Captured codes (Blue team)

Firing all five Blue weapons produced exactly **4 distinct codes** (3 identical
repeats each) — because Shotgun and SMG share a damage-2 code:

```
   damage  weapon(s)        burst-bits (21)        gap-bits (20)
A    1     Pistol        100000000000100010010   00001111000000000001
B    2     Shotgun+SMG   100000000000100000011   00001111000000010001
C    3     MG            100000000000100010000   00001111000000010010
D    4     Rocket        100000000000100100001   00001111000000000010
```

Signature B appeared 6× (twice as often as the others): it is the combined
Shotgun + SMG damage-2 code. The full team-bit vs damage-bit mapping still needs
systematic captures across all 4 teams to pin down (gaps alone are degenerate
for some teams — see below).

## Why gap-only fingerprinting failed

The first trainer pass stored only the gap (HIGH) durations. In that view some
distinct shots are **identical** because they differ only in their **burst**
widths — e.g. for Red, damage 2 and damage 3 share gaps (`00001111000100010010`)
and separate only in the bursts. The fix is to fingerprint the full frame —
every edge, bursts and gaps — quantised to short/long bits, and match exactly.
This is robust to timing jitter (the gun holds ~380/~800 µs, far from the 600 µs
threshold) and unambiguous.

## Fragments

Between clean 41-edge frames the capture also shows `n=3`/`n=5` fragments. These
are inter-shot artefacts and are ignored (the trainer drops non-NEC events with
fewer than 4 gap marks).
