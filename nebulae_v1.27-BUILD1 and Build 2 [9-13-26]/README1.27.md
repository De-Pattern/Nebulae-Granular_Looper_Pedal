# Nebulae - Granular Looper Pedal

_**A port of the Qu-Bit Nebulae v2 granular looper to a guitar pedal, running on an
Electrosmith Daisy Seed3 in a PedalPCB Terrarium platform**_

Phase vocoder and granular processor running in parallel off a shared 120-second live
buffer, with a blend between them

<img src="Nebulae1.9.7Decal_9-8-26.png" width="320" alt="Nebulae faceplate">

---

## Credits

| | |
|---|---|
| **Original engine** | Stephen Hensley / Qu-Bit Electronix, `a_granularlooper.instr` (Csound, 2017), [QB_Nebulae_V2](https://github.com/Qu-Bit-Electronix/QB_Nebulae_V2) |
| **Blend rework** | [alex-thibodeau](https://github.com/alex-thibodeau/QB_Nebulae_V2) , a 2-way engine blend with independent dry/wet |
| **Hardware platform** | [PedalPCB Terrarium](https://www.pedalpcb.com/product/terrarium/) |
| **DSP module** | [Electrosmith Daisy Seed3](https://electro-smith.com/) |

This is a transcription from the original Csound source, not a reimplementation from
descriptions. Every sound-producing constant (the tanh/cubic density curve, the 0.20 RMS
ceiling, the seven grain windows, the constant-power blend laws, `mincer`'s FFT size and
phase-locking) was taken from the instrument file and validated numerically against a
Python reference before any firmware was written.

**License: MIT.** The upstream Qu-Bit firmware is MIT (Copyright © 2019 Qu-Bit Electronix,
Inc.), so this port carries that forward alongside its own copyright. See [LICENSE](LICENSE).
MIT permits use, modification, distribution and commercial use; the only obligation is to
keep the copyright notice and permission text with the source. libDaisy is likewise MIT.

---

## Architecture

```
                       live input
                           |
                    +------+------+
                    |  DC block   |
                    +------+------+
                           |
          +----------------+----------------+
          |           record buffer         |   120 s mono float32, A/B double-buffered
          |        (volatile, SDRAM)        |   46.1 MB of 64 MB
          +----------------+----------------+
                           |
              +------------+------------+
              |        syncphasor       |<---- LFO 1  read position
              |                         |<---- LFO 2  loop length
              +--+-------------------+--+
                 |                   |
        +--------+------+   +--------+---------+
        | mincer        |   | partikkel        |
        | phase vocoder |   | granular         |
        | FFT 2048      |   | max 10 grains    |
        | 4x overlap    |   | spray, pitch rnd |
        | phase-locked  |   | RMS limiter 0.20 |
        +--------+------+   +--------+---------+
                 +---------+---------+
                     BLEND  constant power
                           |
                     freq shifter   page 3
                           |
                       bandpass     page 3  <---- LFO 2 (optional destination)
                           |
          record tap ------+------ output gain ---- soft limiter ---- out
```

The record tap is taken **before** output gain, so monitoring level never changes what
lands in the buffer. Everything on page 3 sits before that tap, so overdubs capture it.

---

## Hardware

### Required

| Item | Notes |
|---|---|
| PedalPCB Terrarium (PCB351) Rev 2 | |
| **Daisy Seed3** | 64 MB SDRAM, USB-C. Seed 1 also works |
| 125B enclosure | |
| 6 x B10K 16 mm pots | **POT2 and POT5 must be CENTER DETENT** (Speed and Pitch) |
| 4 x SPDT ON/ON toggle, 6 mm | standard solder-lug pedal toggle, same footprint as every other PedalPCB board |
| 2 x SPST **momentary** footswitch | not 3PDT latching |
| 2 x LED + resistors | |
| 2 x 20-pin female header | |

Passives per the Terrarium build doc. Note C2/C3/C6 are specified **MLCC**, not film. At
1 µF the footprint is 2.5 mm lead pitch, which does not exist in film.

### Pin map (Terrarium Rev 2)

PedalPCB labels the header pins `GPIO n`; libDaisy calls the same pin `D(n−1)`.

| Control | PedalPCB | libDaisy | | Control | PedalPCB | libDaisy |
|---|---|---|---|---|---|---|
| POT1 | GPIO17 | D16 / A1 | | SW1 | GPIO11 | D10 |
| POT2 | GPIO18 | D17 / A2 | | SW2 | GPIO10 | D9 |
| POT3 | GPIO19 | D18 / A3 | | SW3 | GPIO9 | D8 |
| POT4 | GPIO20 | D19 / A4 | | SW4 | GPIO8 | D7 |
| POT5 | GPIO21 | D20 / A5 | | LED1 | GPIO23 | D22 |
| POT6 | GPIO22 | D21 / A6 | | LED2 | GPIO24 | D23 |
| Audio in | pin 16 | AUDIO_IN_L | | FS1 | GPIO26 | D25 |
| Audio out | pin 18 | AUDIO_OUT_L | | FS2 | GPIO27 | D26 |

---


## Controls

Three pages of knob assignments, selected by SW1 and FS1.

| Page | How to reach it | Labelled on the panel |
|---|---|---|
| **1, primary** | SW1 down | yes |
| **2, shift** | SW1 up | yes |
| **3, tertiary** | SW1 up **and** FS1 held | no |

Page 3 lasts only as long as you hold FS1. Release and you are back on page 2.

### Full matrix

| Pot | 1 Primary | 2 SHIFT | 3 TERTIARY |
|---|---|---|---|
| **POT1** | START, loop start | LFO 1 RATE | BANDPASS freq |
| **POT2** | SPEED (detent) | DUB LVL | LFO 2 RATE (and shape) |
| **POT3** | SIZE, loop length | SPRAY | LFO 2 DEPTH |
| **POT4** | DENSITY | OVERLAP | SHIFT MIX |
| **POT5** | PITCH (detent) | OUTPUT | FREQ SHIFT |
| **POT6** | BLEND | LFO 1 DEPTH | GRAIN PITCH RAND |

| Switch | 1 Primary / 2 SHIFT | 3 TERTIARY |
|---|---|---|
| **SW1** | SHIFT, selects page 1 or 2 | |
| **SW2** | SRC, MIX or IN | **flip = toggle LFO 2 to bandpass** |
| **SW3** | WINDOW, expodec or bartlett | (free) |
| **SW4** | FREEZE, held or run | (free) |
| **FS1** | tap = bypass, hold 1 s = reset pages 2 and 3 | hold = page 3 |
| **FS2** | tap = record, hold 1 s = clear buffer | |

Page-3 defaults: bandpass centre (off), LFO 2 rate 0.02 Hz, LFO 2 depth 0, shift mix full
wet, freq shift centre (off), pitch rand 0.

---

## Page 1, primary

| | |
|---|---|
| **START** | Loop start position within the recording |
| **SPEED** | -4x to +4x. Centre detent is unity. Negative is reverse |
| **SIZE** | Loop length, squared curve, scaling the space between Start and the end |
| **DENSITY** | Grain trigger rate, 0.12 to 983 Hz, hybrid tanh and cubic curve |
| **PITCH** | -3 to +2 octaves. Centre detent is unity |
| **BLEND** | Constant power across vocoder, dry and granular |

POT2 and POT5 are centre detent, and the detent lands exactly on unity for both. The
module uses encoders here; the knob curve is remapped so a pot behaves the same way.

---

## Page 2, shift

| | |
|---|---|
| **LFO 1 RATE** | 0.02 to 2 Hz, exponential. Default 0.02 Hz, one cycle every 50 seconds |
| **DUB LVL** | Input level as written to the buffer. Unity at centre, 0.25x to 4x |
| **SPRAY** | Grain position randomisation, squared curve |
| **OVERLAP** | Grain length as a multiple of the grain period |
| **OUTPUT** | Monitoring level only, never reaches the recorder |
| **LFO 1 DEPTH** | 0 to a full loop, squared curve. Default 0 |

**SPRAY** is the module's Start secondary, restored. At zero every grain spawns from the
playhead, so concurrent grains all read identical audio and differ only in envelope phase.
The result is successive stutters rather than a cloud. Turning it up spawns grains from
random positions across the loop. This is the control that makes it sound granular.

**DUB LVL** scales the input as it is written to the buffer, not what you monitor. It
applies to every recording, not just overdubs. In MIX mode the existing loop arrives via
the engine and new material via this path, so it is the layer balance.

**OUTPUT** never reaches the recorder, so boosting it does not make overdubs compound on
themselves.

---

## Page 3, tertiary

Hold **FS1** while SW1 is up. There are no panel labels for these.

| | |
|---|---|
| **BANDPASS** | Centre is off. CCW sweeps down to 120 Hz, CW up to 6 kHz |
| **LFO 2 RATE** | 0.02 to 2 Hz. Also sets LFO 2's shape, see below |
| **LFO 2 DEPTH** | 0 to plus or minus one octave of loop length, squared curve |
| **SHIFT MIX** | Blends shifted against dry. Default full wet |
| **FREQ SHIFT** | Centre is off, then 0.5 Hz to 100 Hz either side |
| **GRAIN PITCH RAND** | Per-grain detune, 0 to plus or minus 120 cents |

### The page-3 toggle latch

**Hold page 3 and flip SW2 either direction.** LED2 blinks to confirm, and LFO 2 now
modulates the bandpass as well as loop length. Flip again to turn it off.

The firmware reads the **flip, not the position**. Entering page 3 takes a reference and
never acts on it, so the page cannot adopt wherever the lever happens to be sitting. Which
way you flip is irrelevant; only the transition counts.

SW2 keeps doing its normal SRC job throughout, including during page 3, so the lever never
lies about its own state. That means a page-3 flip also changes SRC, which is the
deliberate trade: an extra flip is better than a switch whose position means nothing.

SW3 and SW4 are free on page 3. A third or fourth LFO destination later costs one flip
each and no knobs.

---

## Catch and pickup

**No secondary ever jumps to wherever the shared knob happens to sit.** Nothing adopts on
a page change, on the first visit, or at power-on.

On **page 1** this is pure catch: the parameter waits indefinitely until the knob crosses
its stored value. The panel legend means something there, so knob position stays honest.

On **pages 2 and 3** catch has a **give-up threshold**. If you turn a knob more than about
15% of its travel without crossing the stored value, it stops waiting and takes over where
you are, crossfading over 25 ms so there is no click. That fixes the case where a stored
value sits at an extreme: without it, a parameter defaulting to 0.0 needs a full
counter-clockwise sweep before it responds at all, and reads as dead.

Merely changing pages never causes a takeover. The travel counter resets on every page
change, so a jump only ever follows a deliberate turn.

---

## Footswitches and LEDs

| | |
|---|---|
| **FS1** | tap (on release) = bypass, **hold 1 s = reset pages 2 and 3** |
| **FS2** | tap = record, **hold 1 s = clear buffer** |
| **LED1** | lit = engaged |
| **LED2** | lit = recording, blinks = buffer cleared, blinks = page-3 latch changed |

### FS1 in detail

FS1 does three different jobs depending on SW1 and how long you hold it.

| SW1 | Action | Result |
|---|---|---|
| down | tap | bypass toggles, on release |
| down | hold 1 s | resets every page 2 and page 3 parameter to its default |
| **up** | **hold** | **page 3, for as long as it is held** |
| **up** | **tap** | **nothing** |

> **With SW1 up, FS1 will not bypass.** While SW1 is up, FS1 is the page-3 modifier, so it
> neither toggles bypass nor starts the reset timer. Whatever bypass state you were in is
> held, so if the pedal was engaged, LED1 stays lit and you cannot switch it off.
> **Flip SW1 down first, then tap FS1.** The reset is likewise only reachable with SW1 down.

Bypass fires on **release**, not press, so that holding FS1 for a reset does not also flip
bypass on the way in. That costs a few hundred milliseconds, imperceptible by hand.

### The reset

Holding FS1 for one second with SW1 down returns all nine page-2 and page-3 parameters to
their power-on defaults: LFO 1 rate and depth, dub level, spray, overlap, output, LFO 2
rate and depth, bandpass, shift mix, freq shift and pitch rand.

**Page 1 is deliberately untouched.** Resetting it would leave all six knobs uncaught and
the pedal would ignore every control until each was swept. After a reset the affected pots
are uncaught, which is the point: the hidden pages go back to a known state.

It is kept separate from the FS2 buffer clear on purpose. Clearing audio while hunting for
a parameter default, or the reverse, would be irritating.

---

## Effects (page 3)

Three processors, all on the pre-gain mix so overdubs record them. Chain order is freq
shifter, then bandpass.

### Freq shift

Bode-style single-sideband shifter: Hilbert transform through two 4-section allpass chains
(Niemitalo coefficients), then quadrature modulation against a quadrature oscillator,
**one sideband only**. That detail is what makes it inharmonic rather than ring mod. Image
rejection measured at 46 dB.

Dead zone at centre, then exponential **0.5 Hz to 100 Hz** each side, so most of the travel
sits in the 1 to 30 Hz zone where sidebands colour the tone without destroying pitch.

```
1:00  ->   1.3 Hz     slow beating
2:00  ->   3.8 Hz
3:00  ->  11.2 Hz     sidebands start reading as timbre
4:00  ->  33.5 Hz
5:00  -> 100.0 Hz
```

An earlier version ran 2 Hz to 2 kHz. Everything past about 3:30 read as a resonant sweep
rather than a shift, so two thirds of the knob were unusable.

**SHIFT MIX** (POT4) blends shifted against dry. Hearing both together is the classic Bode
usage: the two beat against each other, which reads as movement rather than the flat
detune of a fully wet shift. Around halfway is where the beating is strongest.

### Bandpass

TPT state-variable filter, 12 dB/oct, Q of 1.0, last in the chain. Centre is off. The wet
amount fades in over the first 15% of travel either side, so there is no jump leaving the
dead zone, and the frequency mapping is exponential so the knob tracks pitch.

```
0.00 ->  120 Hz
0.25 ->  339 Hz
0.50 ->  OFF
0.75 -> 2257 Hz
1.00 -> 6000 Hz
```

A bandpass throws away everything outside its passband, so perceived level drops even
though the peak itself is unity: measured -14 dB at 200 Hz against -0.7 dB at 6 kHz before
compensation. Makeup gain scales with wet amount, reaching about +5 dB fully engaged.

24 dB/oct was considered and rejected. A steeper filter removes more energy, so it gets
quieter, not louder. Makeup gain is the right lever.

With the page-3 latch on, LFO 2 sweeps it bipolar and exponential, plus or minus 2 octaves
at full depth, centred on whatever the knob is set to. Clamped to 40 Hz and 12 kHz, so
extreme settings narrow the sweep rather than wrapping.

```
knob sets     lfo -1     lfo +1     span
   339 Hz        85       1356      4.0 octaves
  2257 Hz       564       9026      4.0 octaves
  6000 Hz      1500      12000      3.0 octaves
```

### Grain pitch randomisation

The module's Pitch secondary, restored. Each grain gets its own detune, assigned once at
launch and held for that grain's life. Per grain, not per sample: per sample would be FM.

Stock is `rand(amt * 1200)`, bipolar cents, so plus or minus an octave at full. That is far
too wide to stay musical, so this uses a squared curve to **plus or minus 120 cents**.

```
knob   max cents   rms cents
0.25      7.5         4.3
0.50     30.0        17.3
0.75     67.5        39.0
1.00    120.0        69.1
```

Quarter travel is chorus territory. Half is audible detune. Most of the knob stays subtle.
RMS runs about 58% of the maximum because the distribution is uniform, so typical grains
sit well inside the extreme.

Pair it with SPRAY. Spray decorrelates **where** grains read, pitch rand decorrelates **how
fast**. Together they are what makes the grain cloud sound like a cloud.

---

## LFO 1, read position

An addition, not part of the original module. It modulates read position only.

### Shape

A sine with its phase warped before the sine is taken:

```
warp(p) = p / (2s)                    for p < s
          0.5 + (p - s) / (2(1 - s))  for p >= s
out     = sin(2 * pi * warp(p))
```

`s` is the skew, fixed at **0.65**. At `s = 0.5` this reduces to an ordinary sine.

The warp stretches the first 65% of the cycle across the first half of the sine and
compresses the remaining 35% into the second half, so the rise is slow and the fall is
quick. It stays smooth throughout, with no corners or discontinuities.

One cycle sampled in 24 steps, against a pure sine:

```
skew 0.65  +0.20 +0.39 +0.57 +0.72 +0.85 +0.94 +0.99 +1.00 +0.97 +0.90 +0.80 +0.66
           +0.50 +0.32 +0.12 -0.15 -0.50 -0.78 -0.96 -1.00 -0.90 -0.68 -0.37  0.00

sine       +0.26 +0.50 +0.71 +0.87 +0.97 +1.00 +0.97 +0.87 +0.71 +0.50 +0.26 -0.00
           -0.26 -0.50 -0.71 -0.87 -0.97 -1.00 -0.97 -0.87 -0.71 -0.50 -0.26  0.00
```

The rise takes 15 of 24 steps; the fall takes 9.

<img src="lfo.png" width="320" alt="lfo visual">

### How it moves the sound

**It offsets read position, not loop geometry.** Loop start and loop length are computed
before the LFO is applied and are never touched by it, so playback rate and loop boundaries
stay fixed. Only the point being read within the loop moves.

**It is unipolar.** The sine's -1 to +1 range is mapped to 0 to +1, so the offset only ever
pushes forward from the playhead, never behind it. The sweep therefore anchors at the Start
position.

```
uni  = (sin_out + 1) * 0.5          ->  0 to 1
read = phasor + uni * depth^2
```

Depth is squared. At full depth the offset covers an entire loop, so the read position
scans forward through the whole buffer and returns.

**Both engines follow it together**, since they share the read position. The vocoder
time-warps as the read point moves and grains spawn from the moving position. At low depth
this is tape-like wobble; at high depth it becomes a slow scan through the loop.

**Rate is 0.02 to 2 Hz, exponential** (50 seconds per cycle at the slow end).

The asymmetry is what reads as motion rather than oscillation. A symmetric sine feels like
rocking in place; this drifts forward and resets.

### Controls

| | |
|---|---|
| **SHIFT + POT1** | LFO 1 RATE |
| **SHIFT + POT6** | LFO 1 DEPTH (0 = off, output is bit-identical to no LFO) |

---

## LFO 2, global

Page 3. Rate on POT2, depth on POT3. It is **normalled to loop length** and can be routed
to the bandpass as a second destination by flipping SW2 while page 3 is held.

One rate, one depth, one shape, shared across every destination. Depth at zero leaves them
all still, so routing costs nothing until you use it.

### Loop length, the normalled destination

Bipolar, multiplying loop length by `2^(depth^2 * lfo)`, so full depth breathes between
half and double length while the playhead rate follows inversely. Multiplicative, so it
feels uniform across Size's squared curve. **Recording uses the unmodulated length**, so
the buffer itself never breathes, only playback.

### Shape follows its own rate

Where LFO 1's skew is fixed, LFO 2's tracks its rate:

```
rate 0.00  ->  skew 0.350    quick rise, long sink
rate 0.50  ->  skew 0.425
rate 1.00  ->  skew 0.500    plain sine
```

At the slow end the asymmetry is audible as motion, so the shape is worth having. As the
rate climbs, that shape stops reading as shape and just colours the wobble, so it eases to
symmetric. One knob sets both.

An output curve of 0.6 is applied on top, fixed, which pushes the LFO toward its extremes.
It spends about 80% of each cycle beyond half depth, so it snaps away from centre,
blossoms, then eases back rather than rocking evenly through the middle.

---

## How to use it

### Load your first loop

1. **SW2 to IN**
2. Tap **FS2**, recording starts
3. Play
4. Tap **FS2**, recording stops

In IN mode nothing else affects what is recorded. Loop length is set by where you stop.

### Hear it

5. **BLEND** sets the mix. Fully CCW is the phase vocoder, centre is dry, fully CW is
   granular
6. **SPRAY** (SHIFT+POT3) is what turns stuttering into a grain cloud
7. **DENSITY** and **OVERLAP** shape the texture. Under FREEZE, grain length is the
   stutter length, so Overlap makes it longer and Density retriggers it faster

### Overdub

8. **SW2 to MIX**
9. Tap **FS2** once

Recording **punches in at the current playhead, runs exactly one loop pass, and stops
itself.** You do not tap FS2 again. Loop length is locked, so an overdub cannot shorten
the loop.

The record source is engine output plus your input, independent of BLEND. Use **DUB LVL**
(SHIFT+POT2) to balance the new layer, unity at centre.

### Reach page 3

With SW1 up, **hold FS1**. Release to return to page 2. Every page-3 control needs
catching, but the give-up threshold means about a sixth of a turn is enough.

### Clear and reset

Hold **FS2** one second to clear the buffer. Hold **FS1** one second, with SW1 down, to
reset pages 2 and 3 to defaults. LED2 blinks either way.

### Considerations

- The buffer is **volatile**. Power down loses it
- Layers are mixed destructively into one buffer. There is no undo and no way to adjust a
  layer after it is recorded
- With SIZE below maximum an overdub covers only that fraction of the loop and stops
  early. That is stock behaviour, since the module ties overdub length to loop size the
  same way
- At unity speed and unity pitch with SPRAY and PITCH RAND at zero, every concurrent grain
  reads identical audio and differs only in envelope phase. That is why it sounds stuttery
  rather than granular. Raise either one, or move PITCH off its detent
- A page-3 flip of SW2 also changes SRC, by design. The lever never lies about its own physical toggle
  position

---

## Versions

**Current: `nebulae_v1.26-BUILD1.bin`**

Two hardware variants build from one source tree. **BUILD1** is the toggle-SW1 pedal.
**BUILD2** adds the illuminated page-3 button and bi-colour LED2, built with
`make EXTRA_DEFS=-DBUILD2=1`. A BUILD1 binary only ships when build 1 actually changes.

| Version | Change |
|---|---|
| **1.27** | **Current.** LED2 now confirms the parameter reset |
| 1.26 | Page-3 LFO latch moved to SW2, which keeps its normal SRC job throughout |
| 1.25 | LFO 2 routable to the bandpass by flipping a toggle while page 3 is held, LED2 confirms |
| 1.24 | Grain pitch randomisation on page 3. LFO 2 skew now follows its own rate. Phaser removed |
| 1.23 | Catch gains a 15% give-up takeover on pages 2 and 3. Page 1 stays pure catch |
| 1.22 | Phaser deepened to 3 chains and 10 stages. Bandpass makeup gain |
| 1.21 | Page 3 rearranged. Phaser speed tracked the LFO 2 rate |
| 1.20 | Sweepable bandpass, last in the chain |
| 1.18 to 1.19 | Barber-pole phaser, later removed |
| 1.17 | Size LFO skew exposed, later folded into rate |
| 1.16 | Size LFO shape: mirrored skew plus an output curve |
| 1.15 | Freq shift dry/wet mix |
| 1.14 | Freq shift range cut to 0.5 Hz to 100 Hz. The old 2 kHz top read as a resonant sweep |
| 1.13 | Reset moved to FS1 hold. Bypass moved to release |
| 1.12 | Parameter reset for pages 2 and 3 |
| 1.11 | Build 2 variant gated behind a single `BUILD2` define |
| **1.10** | Third page on SW1 up plus FS1 held. Size LFO and freq shifter. Catch generalised to three pages |
| 1.9.7 | OUTPUT no longer reaches the recorder. With output boosted, every overdub had been writing the loop back louder and compounding |
| 1.9.6 | DUB LVL boots at unity. The init still meant unity on the old input-level curve but -12 dB on the new one |
| 1.9.5 | Record source is engine output plus input, independent of BLEND. At either Blend extreme the dry factor is zero, so overdubs had captured engine output only |
| 1.9.4 | SPRAY on SHIFT+POT3, ported from the Csound. Window moved onto the SW3 toggle |
| 1.9.3 | Record fixed. The overdub latch check ran before the engine had acted on a fresh tap, so it cleared the latch on the block that set it |
| 1.9.1 | Punch-in overdub. Also fixed a v1.9 bug where booting with SHIFT up made the shift page adopt knob positions outright |
| **1.9** | Catch resumes without a re-catch only if the parameter was live when leaving the page |
| 1.8 | RMS limiter moved to control rate. Fixed the grain crackle at high Overlap |
| 1.7 | Skip the engine whose blend coefficient is exactly zero |
| 1.6 | Output level on SHIFT+POT5 |
| 1.5 | Unipolar LFO depth. Catch tolerance widened from 0.2% to 2% |
| 1.1 to 1.4 | Catch adopt, expodec floor, LFO curve, density taper. All reverted |
| **1.0** | **Audio block size 64 to 512.** The critical fix, see below |
| 0.1 to 0.9 | Bring-up: clear, LFO, heartbeat, input level, pot smoothing, FTZ, DC block, soft limit |

**Two branches.** The `1.x` line is the maintained one. `2.x-ALEXTBLEND` keeps the 2-way
engine blend with dry on its own knob, but has no SPRAY, no page 3 and no effects.

<details>
<summary><b>Controls for the 2.x-ALEXTBLEND branch (click to expand)</b></summary>

<br>

The 2.x branch replaces the stock 3-way blend with a 2-way engine crossfade and moves dry
onto its own control. Under the stock law the vocoder and grains never overlap, since voc
fades out by centre and grain fades in after it. The 2.x law lets both sound together.

| | Primary | SHIFT |
|---|---|---|
| **POT1** | START | LFO RATE |
| **POT2** | SPEED (detent) | DRY/WET |
| **POT3** | SIZE | WINDOW |
| **POT4** | DENSITY | OVERLAP |
| **POT5** | PITCH (detent) | OUTPUT |
| **POT6** | BLEND, vocoder to granular only | LFO DEPTH |

BLEND becomes purely the balance between the two engines. Centre gives 0.707 of each
rather than pure dry. DRY/WET on SHIFT+POT2 is the main output mix and boots fully wet.

This branch has no SPRAY, no page 3 and no effects section, and its Window is still on a
knob. It is far behind the 1.x line and is kept only for anyone who prefers that blend
behaviour.

</details>

---

## Deviations from stock

| | |
|---|---|
| **Mono** | Terrarium is single in / single out. Loses grain stereo spread |
| **120 s buffer** | vs 5 min. Volatile, with no file, USB, SD or sample loading anywhere |
| **Secondaries mostly dropped** | Most `_alt` params fixed at 0, which *is* stock-at-defaults. SPRAY and grain pitch rand are restored |
| **Speed/Pitch remap** | Centre detent lands on unity. Endpoints preserved exactly. The module uses encoders, we use pots |
| **SPRAY, DUB LVL, OUTPUT** | SPRAY is the module's Start secondary, restored. DUB LVL and OUTPUT are additions, since the Terrarium is unity gain throughout |
| **Overdub ignores BLEND** | Stock scales the recorded dry by the blend dry factor, which is zero at both extremes. Correct for the module, wrong for a looper |
| **Catch mode** | One pot serving two parameters requires it |
| **Expodec window** | Replaces the stock linear ramp-down, on a toggle so it can be A/B'd |
| **LFO 1** | Not stock. Modulates read position only, so loop geometry is untouched |
| **LFO 2, freq shift, phaser, bandpass** | Not stock. Page 3 additions. Freq shift uses the Hilbert core from kuttor's SuperNova firmware with a re-mapped range. The barber-pole phaser is an original implementation of a well-documented algorithm class |
| **Input/output level** | Terrarium is unity gain; a guitar sits ~20 dB below where the grain RMS limiter engages |
| **DC block, output soft limit, block size 512** | Platform necessities |

Freeze and Reset are not on footswitches: Freeze is literally `kspeed = 0` in the original,
so a toggle does it, and Reset only mattered for CV sync.

---


## Notes from the port

**Block size was the one serious bug.** `mincer` computes an entire frame (two forward
FFTs, one inverse, two 1025-bin loops, and a 2048-sample interpolated SDRAM read) inside a
single audio callback. At 64 samples that's 1.33 ms of budget against ~1.4 ms of work, so
one block in eight overran: a dropout 94x/second that sounded like digital garble. Nebulae
itself runs Csound on a Pi with a ~42 ms output buffer, so its frame work is invisible.

**Sample-exact diffing is the wrong test for a phase vocoder.** `div = 1/(hypot(prev)+1e-20)`
reaches ~1e20 at empty bins, so float rounding compounds through the feedback path. Frame 0
matches the reference to 7.4e-08; after that the waveforms diverge while remaining
spectrally identical to −58 dB. Acceptance criteria are frame-0 agreement, unity gain, exact
pitch ratios and magnitude spectrum. The granular side, being non-recursive, is held to
sample-exact and passes at 2e-07.

**`giHamming` is not Hamming.** The instrument declares `ftgen 20, 9, 1` and comments it
"Hamming Window". GEN20 type 9 is **Sinc**. This matters: sinc reaches exactly 0 at both
endpoints, a real Hamming bottoms at 0.08 and would click at every grain boundary.

**The grain RMS stage is a limiter, not a leveler.** Identity below 0.20, attenuation above.
It never boosts, which is why input level matters on a guitar-level platform.

**Density never reaches its stated 2500 Hz.** The hybrid tanh/cubic scalar tops out at
0.906, so the real maximum is ~983 Hz.

---


## Building from source

```bash
git clone --recursive https://github.com/electro-smith/libDaisy.git
cd libDaisy && make -j
cd .. && mkdir nebulae && cd nebulae
# copy in: main.cpp terrarium.h nebulae_dsp.{h,cpp} nebulae_engine.{h,cpp} Makefile
make                          # -> build/nebulae.bin  (build 1)
make EXTRA_DEFS=-DBUILD2=1    # -> build 2, button and bi-colour LED
```

```make
TARGET = nebulae
CPP_SOURCES = main.cpp nebulae_dsp.cpp nebulae_engine.cpp
LIBDAISY_DIR = ../libDaisy
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
```

On Windows, Electrosmith's Daisy Toolchain installer bundles `arm-none-eabi-gcc`, `make` and
`dfu-util`.

---


## License

MIT. See [LICENSE](LICENSE).

- Original engine © 2019 Qu-Bit Electronix, Inc. (MIT)
- Blend rework derived from alex-thibodeau's fork (inherits MIT)
- This port © 2026 Kenny Rakentine (MIT)
- libDaisy © Electrosmith (MIT)

If you fork this, keep all four attributions.

---


## Build 2 plans

Second build once the Terrarium PCB is back in stock. Same source tree, gated behind
`BUILD2`.

### Indication

| | |
|---|---|
| **LED1** | blue or violet, engaged |
| **LED2** | 3 mm red/green bi-colour, common cathode. Red = recording, green = playback, blinks = cleared |

### Panel

**Keep SW1 as a toggle** and add a seventh hole for an illuminated momentary button
dedicated to page 3. Putting shift on the button instead costs the tactile and visual
state of a lever, makes the tap fire on release rather than press, and means a dead button
loses the whole shift page.

The firmware already supports the button-replaces-toggle arrangement under `BUILD2`, in
which case the button wires across the SW1 pads and behaves as:

| Gesture | Result |
|---|---|
| tap | toggles secondary, **green** while latched |
| hold | page 3 while held, **red** |

With a dedicated hole instead, the button is hold-only for page 3 and SW1 stays a toggle.

### Wiring

The bi-colour LED's second anode and the button's LED lines flywire to free Seed pins
(D0 to D6, D11 to D14, D24, D27 to D30) via the underside of the Terrarium's female header,
so the Seed stays removable. Each LED leg needs its own 1K. **Do this before the board goes
in the enclosure.**

### Optional: input boost

Non-inverting op-amp stage (OPA2134) between the input jack and the Terrarium IN pad, gain
4 to 6x, 100K trimmer so it is set by ear. Powered from the board's existing +5 V and VREF.
Shielded cable from the jack, routed away from the Seed.

This is the obly gain in the chain that can improve signal to noise, because everything
in firmware is applied after the converter and lifts signal and noise identically. A guitar
currently hits the ADC around 25 dB below full scale, wasting that much converter range.

Gain above roughly 6x clips IC1.1 before the converter sees it, since the audio rail is
+5 V with VREF at 2.5 V, giving about 3.5 Vpp of swing.


---


## Known limitations

- **Noise floor.** The Terrarium's analog path is unity gain, so a guitar hits the ADC
  20 to 30 dB below full scale. DUB LVL and OUTPUT are applied *after* the converter and
  cannot improve SNR. The only real fix is analog gain ahead of the pedal. See the input
  boost under Build 2 plans
- **No stereo.** Seed pins 17/19 are unused; a second output buffer would give dual mono.
  True stereo needs a second vocoder and grain cloud. CPU is fine, memory is the limit
- **Bypass is buffered, not true bypass.** Audio always passes through the codec. Loss of
  power means loss of signal
- **~43 ms vocoder latency** against the dry path. Inherent to a 2048-point FFT, and stock
  behaviour
- The module locks Speed/Start/Size during recording and snaps Speed/Pitch to unity after an
  overdub. Neither is implemented, and the second is impossible with pots
