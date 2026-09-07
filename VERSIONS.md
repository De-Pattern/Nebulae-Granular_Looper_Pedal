# Nebulae Terrarium — Firmware Versions

**Stable: `nebulae_v1.9.bin`**  ·  **Branch: `nebulae_v2.0-ALEXTBLEND.bin`**

All four builds are archived here so you can pin or revert to any of them.
Everything before v1.3 was originally named `nebulae.bin`, so a file downloaded earlier
cannot be identified by name — see "Which one am I on?" below.

Flashing: hold BOOT, tap RESET, release BOOT → Chrome/Edge →
`https://electro-smith.github.io/Programmer/` → select "DFU in FS Mode" → Connect →
**File Upload** tab → choose the .bin → Program → tap RESET.
Windows needs the WinUSB driver via Zadig first (Options → List All Devices → pick
"DFU in FS Mode" → WinUSB → Replace Driver).

---

| Ver | Change | Why |
|---|---|---|
| 0.1 | First build | — |
| 0.2 | `Clear()` no longer memsets 46 MB | Ran inside the audio callback; would have blown the deadline the instant you held FS2 |
| 0.3 | Final panel layout, SW2/3/4 assigned, Speed+Pitch to centre column | Matches the waterslide artwork |
| 0.4 | LFO on read position, SHIFT+POT1 rate / SHIFT+POT6 depth | Phase-skewed sine, offsets playhead only so loop geometry is untouched |
| 0.5 | Heartbeat on the Seed's onboard LED | Bring-up diagnostic — proves clocks, SDRAM and codec all came up |
| 0.6 | Input level on SHIFT+POT2 | Terrarium is unity gain; guitar sits ~20 dB under the grain limiter's absolute 0.20 ceiling |
| 0.7 | Catch mode only engages if the knob actually moved | Was stranding all five primary controls after any trip to the shift page |
| 0.8 | Pot smoothing (20 Hz) + FPU flush-to-zero | Pitch jitter 0.236 → 0.068 cents; libDaisy never sets FPSCR |
| 0.9 | DC blocker on input, output soft-limit above 0.9 | Terrarium feeds the Seed with VREF bias and no coupling cap |
| **1.0** | **Audio block size 64 → 512** | **The real bug.** mincer computes a whole frame in one callback; 64 samples gave 1.33 ms of budget for ~1.4 ms of work, so one block in eight overran — a dropout 94×/sec |
| 1.1 | Catch adopts the knob on first visit to a page | Four of five shift params default to 0.0, so secondaries appeared dead unless you swept fully CCW |
| **2.0-ALEXTBLEND** | Blend becomes a 2-way voc↔grain crossfade (the stock FILE-mode law, already ported and previously unused). Dry moves to its own control on **SHIFT+POT2**, replacing Input Level | Adopts alex-thibodeau's blend rework. Under the stock live law the two engines NEVER overlap — voc fades out by centre, grain fades in after it — so time-stretched and granular material could not sound together. This fixes that and gives a real dry/wet control |
| 1.9 | Catch resumes without re-catch ONLY if the parameter was live when leaving the page | Second and later visits to the shift page were adopting the knob position outright — the exact jump catch exists to prevent |
| 1.8 | RMS limiter moved to control rate, gain ramped across the block | Csound's rms/gain pair is k-rate. Per-sample recomputation let a 1.6 ms follower track individual grain envelopes, modulating gain at grain rate — the crackle at high Overlap |
| 1.7 | Skip the engine whose blend coefficient is exactly zero | (superseded — see 2.0 note below) |
| 1.6 | CPU overload indicator on LED2; output level on SHIFT+POT5 | Diagnostic showed NO overload, ruling out CPU as the cause of the Overlap crackle |
| **1.5** | **= v1.0 with ONE change: LFO depth is unipolar, 0..+1.0 loop.** Reverts the v1.1 catch adopt, the v1.2 expodec floor, the v1.3 LFO rate/curve and the v1.4 density taper | v1.4 changes were rejected in testing: density lost its top end, expodec became less pronounced, LFO rate compressed into the last 10% of travel, and first-visit adopt made secondaries snap to the shared knob's position. v1.0 was the good build; unipolar depth is the only improvement kept |
| 1.4 | Density re-tapered above 3 o'clock: knob 0.75→1.0 now reaches 150 Hz instead of 983 Hz | Stock crams a 35× jump into the last quarter, turning the grain rate into an audible pitched buzz. **Below 3 o'clock the curve is bit-exact to stock**, so the Density/Overlap relationship is untouched |
| 1.3 | LFO rate low end 0.02 → 0.005 Hz; depth unipolar 0..+1.0 loop on a `depth^1.5` curve (was bipolar ±0.5 on `depth²`) | Slower drift available; sweep now anchored at Start and moves forward only, never behind the playhead; upper half of the depth knob is usable instead of jumping |
| 1.2 | Expodec decay floor 0.001 → 0.010 | Gentler droplet tail (decay to −20 dB at 53% vs 38%), and mean 0.2119 lands within 0.14 dB of Gaussian — level-matched with no artificial gain |

---

## Known open items

- **Slight HF ring at Blend hard CCW.** Measured clean: no spurious tones above −70 dBFS on a sine, and all frame-rate harmonics at or below the local noise floor on transient-rich material. Almost certainly the fixed hardware noise floor becoming audible as the vocoder output is quieter than dry — not DSP-generated.
- **Low output.** Analog path is unity gain. Input level raises loudness but is applied post-ADC so it does not improve SNR. Real fix is analog gain ahead of the pedal.
- **Clicking on transients.** Not yet characterised — needs testing at the Gaussian end of Window vs near Expodec and Rectangle. Rectangle clicks by design.
- **CMSIS FFT swap.** `RFFT` is a naive radix-2 doing a full complex transform on real input, ~2× wasteful. Held in reserve if CPU headroom is ever needed.


---

## Which one am I on?

Pre-1.3 builds were all called `nebulae.bin`. Identify by behavior:

| Test | Result | Version |
|---|---|---|
| Flip SHIFT, turn POT3 a little | nothing happens until swept fully CCW | **v1.0** |
| | responds immediately | **v1.1 or later** |
| Window at ~33% CW, SW3 on vs off | large level drop (~10 dB) with EXPO on | **v1.2 or earlier** |
| | level roughly matched | **v1.2 or later** |
| SHIFT + POT6 (LFO depth) fully CW | wobbles around the playhead, both directions | **v1.2 or earlier** |
| | scans forward from Start only | **v1.3** |

## Reverting

Each version is cumulative — reverting to v1.0 also gives up the v1.1 and v1.2 fixes:

- **back to v1.2** — loses only the LFO rate/depth changes
- **back to v1.1** — also loses the gentler, level-matched expodec
- **back to v1.0** — also loses first-visit catch adopt, so the secondary page appears dead
  unless you sweep each knob fully counter-clockwise

If v1.3 has a problem, **try v1.2 before going all the way back to v1.0.** It isolates the
LFO change while keeping everything else.

## Checksums

```
43aa9d73a7015371ed1729ffd0c657d2  nebulae_v1.0.bin
75076e4091e0036bfa343e26bfa77bba  nebulae_v1.1.bin
3bee31dd3e5e9adcb6413cc9685ee378  nebulae_v1.2.bin
9333b7ba1e118134c79e02295d233236  nebulae_v1.3.bin
```

## Density taper (v1.4)

Constants live in `nebulae_dsp.cpp`:

```
static constexpr float DENS_KNEE = 0.75f;   // where the re-taper starts
static constexpr float DENS_MAX  = 150.0f;  // Hz at full CW  (stock: 983)
```

| knob | v1.4 | stock | clock |
|---|---|---|---|
| 0.75 | 27.9 Hz | 27.9 Hz | 3:00 — identical up to here |
| 0.83 | 47.8 Hz | 60.4 Hz | 3:45 |
| 0.92 | 87.6 Hz | 220 Hz | 4:30 |
| 1.00 | **150 Hz** | 983 Hz | 5:00 |

Raise `DENS_MAX` toward 983 to restore stock reach, or lower it to tame further.
Moving `DENS_KNEE` changes where stock behaviour ends.


---

## v2.0-ALEXTBLEND vs v1.9

Branch build. v1.9 remains the stable reference.

### Blend knob

| Blend | v1.9 | 2.0-ALEXTBLEND |
|---|---|---|
| full CCW | voc 1.00 | voc 1.00 (same) |
| 9 o'clock | voc 0.71 + dry 0.71 | voc 0.87 + grain 0.50 |
| **centre** | **dry 1.00 — effect off** | **voc 0.71 + grain 0.71** |
| 3 o'clock | dry 0.71 + grain 0.71 | voc 0.50 + grain 0.87 |
| full CW | grain 1.00 | grain 1.00 (same) |

Extremes identical. The middle is where it differs: dry signal becomes both engines at once.

### SHIFT + POT2 = DRY/WET (replaces Input Level)

Main output mix, active at all times — not recording-specific.
**CW = wet = more processed engine. CCW = dry = more live input.** Constant power.
**Boots fully wet.** Sweep POT2 with SHIFT up to catch it.

### Gained
- Vocoder and grains together at any ratio — impossible in v1.9 at any setting
- Independent dry/wet, so a faint grain haze over mostly-dry signal is possible
- Recording no longer depends on Blend position (the REC-1 trap disappears)
- With SRC on MIX, DRY/WET becomes the overdub mix

### Lost
- Blend centre is no longer "effect off"; the 0.48–0.52 snap zone is meaningless and gone
- Input Level

### Watch for
Soft limiter engaging more around Blend centre. Both engines at 0.707 reading the same
buffer at unity speed/pitch are highly correlated and sum toward 1.41× — the F6 overshoot,
now at the middle of the knob rather than off to the sides.

### Decal
Requires a reprint: SHIFT+POT2 label changes from INPUT to DRY/WET.
