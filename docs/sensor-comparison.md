# IR Sensor Comparison — KEYES comparator board vs VS1838B

Two IR sensors were trialled for reading laser-tag / IR-remote signals on the
Lolin32 OLED board.

## Sensors

| | KEYES module (LM393) | VS1838B |
| --- | --- | --- |
| Type | Photodiode + LM393 comparator | Integrated 38 kHz demodulating receiver |
| Output | D0 (thresholded raw light), A0 (analog) | Single digital data pin |
| Demodulation | None — passes raw light level | Internal 38 kHz band-pass + AGC |
| Idle level | Either (threshold dependent) | HIGH; pulses LOW on a detected burst |
| GPIO used | D0 → 16, A0 → 36 | OUT → 25 |
| Tuning | Sensitivity potentiometer (fiddly) | None |

## Results (TV remote, 38 kHz NEC)

**KEYES comparator board:** required potentiometer tuning and never fully
stabilised. Frames suffered two failure modes depending on range/threshold:

- **Fragmenting** (weak signal): marks broke into 9–15 spurious glitch edges.
- **Merging** (strong/close signal): gaps filled in, collapsing a 6-mark frame
  to 1–2 marks.

Best observed: ~29% clean frames at the sweet-spot pot setting; the rest needed
the "total frame duration" heuristic (robust at ±3%) to be identifiable at all.

**VS1838B:** clean, textbook NEC decode with no tuning. From 21 captured frames:

- 20 of 21 full-length and structurally identical (1 runt = NEC repeat fragment).
- Clean bimodal bit timing:
  - bit mark (LOW): ~560 µs
  - "0" space (HIGH): mean **572 µs** (tight)
  - "1" space (HIGH): mean **1684 µs**, range 1668–1698 µs (extremely tight)
- Leader ~4.5 ms + 4.5 ms, ~560 µs bit marks → standard NEC.

## Decision

**Adopt the VS1838B** as the sensor. It removes the entire pot-tuning problem,
produces decode-grade timings, and gives proper ambient-light immunity and
range. The KEYES board remains usable only as a crude "hit happened" detector.

The firmware reads the IR pin via `IR_PIN` (now GPIO 25) and is otherwise
unchanged; the A0 carrier sampler is disabled (`ENABLE_CARRIER_SAMPLER 0`) since
the VS1838B has no analog output.

## Notes / follow-ups

- **Carrier frequency of the laser-tag gun is still unmeasured.** The VS1838B is
  a 38 kHz part; if it reads the gun cleanly, the gun is ~38 kHz. If it is deaf
  to the gun while still reading TV remotes, the gun is likely 56 kHz (MilesTag)
  or uncarriered — would then need a 56 kHz receiver (e.g. TSOP4856). Test when
  the gun is to hand.
- **`FRAME_GAP_US` (50 ms)** was chosen for the gun, whose full-auto weapons
  showed 6–12 ms gaps *within* a single frame. At 50 ms the NEC repeat code
  (~48 ms later) gets appended to the remote frame as a deterministic tail.
  Harmless for fingerprinting; revisit if cleaner per-press frames are wanted.
- VS1838B output is **active-low**: idles HIGH, pulses LOW on a burst.
