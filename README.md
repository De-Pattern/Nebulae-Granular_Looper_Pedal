# Nebulae - Granular Looper Pedal

A port of the **Qu-Bit Nebulae v2** granular looper to a guitar pedal, running on an
**Electrosmith Daisy Seed3** in a **PedalPCB Terrarium** platform.

Phase vocoder and granular processor running in parallel off a shared 120-second live
buffer, with a blend between them

<img src="Nebulae%20Decal%202.0.png" width="320" alt="Nebulae faceplate">

---

## Credits

| | |
|---|---|
| **Original engine** | Stephen Hensley / Qu-Bit Electronix, `a_granularlooper.instr` (Csound, 2017) — [QB_Nebulae_V2](https://github.com/Qu-Bit-Electronix/QB_Nebulae_V2) |
| **Blend rework** | [alex-thibodeau](https://github.com/alex-thibodeau/QB_Nebulae_V2) — 2-way engine blend with independent dry/wet |
| **Hardware platform** | [PedalPCB Terrarium](https://www.pedalpcb.com/product/terrarium/) |
| **DSP module** | [Electrosmith Daisy Seed3](https://electro-smith.com/) |

This is a **transcription from the original Csound source**, not a reimplementation from
descriptions. Every sound-producing constant — the tanh/cubic density curve, the 0.20 RMS
ceiling, the seven grain windows, the constant-power blend laws, `mincer`'s FFT size and
phase-locking — was taken from the instrument file and validated numerically against a
Python reference before any firmware was written.

**License: MIT.** The upstream Qu-Bit firmware is MIT (Copyright © 2019 Qu-Bit Electronix,
Inc.), so this port carries that forward alongside its own copyright — see [LICENSE](LICENSE).
MIT permits use, modification, distribution and commercial use; the only obligation is to
keep the copyright notice and permission text with the source. libDaisy is likewise MIT.

---

## Architecture

```
                       live input
                           │
                    ┌──────┴──────┐
                    │  DC block   │
                    └──────┬──────┘
                           │
          ┌────────────────┴────────────────┐
          │           record buffer          │   120 s mono float32, A/B double-buffered
          │        (volatile, SDRAM)         │   46.1 MB of 64 MB
          └────────────────┬────────────────┘
                           │
              ┌────────────┴────────────┐
              │        syncphasor        │◄──── LFO (read position only)
              └──┬───────────────────┬──┘
                 │                   │
        ┌────────┴──────┐   ┌────────┴─────────┐
        │ mincer        │   │ partikkel        │
        │ phase vocoder │   │ granular         │
        │ FFT 2048      │   │ max 10 grains    │
        │ 4× overlap    │   │ 7 windows        │
        │ phase-locked  │   │ RMS limiter 0.20 │
        └────────┬──────┘   └────────┬─────────┘
                 └─────────┬─────────┘
                     BLEND (constant power)
                           │
                    DRY/WET ── soft limiter ── out
```

---

## Hardware

### Required

| Item | Notes |
|---|---|
| PedalPCB Terrarium (PCB351) Rev 2 | |
| **Daisy Seed3** | 64 MB SDRAM, USB-C. Seed 1 also works |
| 125B enclosure | |
| 6 × B10K 16 mm pots | **POT2 and POT5 must be CENTER DETENT** (Speed and Pitch) |
| 4 × SPDT ON/ON toggle, **PCB pin** | not solder lug |
| 2 × SPST **momentary** footswitch | not 3PDT latching |
| 2 × LED + resistors | |
| 2 × 20-pin female header | |

Passives per the Terrarium build doc. Note C2/C3/C6 are specified **MLCC**, not film — at
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

### Knobs

| | Primary | SHIFT (SW1 up) |
|---|---|---|
| **POT1** | START — loop start position | LFO RATE |
| **POT2** | SPEED ● — −4× to +4×, detent = 1× | DRY/WET |
| **POT3** | SIZE — loop length | WINDOW |
| **POT4** | DENSITY — grain rate, 0.12–983 Hz | OVERLAP |
| **POT5** | PITCH ● — −3 to +2 oct, detent = unity | OUTPUT level |
| **POT6** | BLEND — vocoder ↔ granular | LFO DEPTH |

● = center detent. Detent lands on unity for both.

**Shift pots use catch/pickup.** A secondary does not jump to wherever the shared knob sits
— sweep the knob through the stored value to pick it up. If a parameter seems dead, sweep
it fully counter-clockwise; several default to 0.0.

### Toggles

| | Up | Down |
|---|---|---|
| **SW1 SHIFT** | alt page | primary |
| **SW2 SRC** | **MIX** — records engine output + dry. Enables overdub | **IN** — records clean input only |
| **SW3 EXPO** | expodec grain window (sharp attack, exponential tail) | stock linear ramp-down |
| **SW4 FREEZE** | playhead held | run |

Toggle polarity depends on wiring; up/down may be inverted on your build.

### Footswitches and LEDs

| | |
|---|---|
| **FS1** | Bypass toggle |
| **FS2** | tap = Record on/off · **hold 1 s = Clear buffer** |
| **LED1** | lit = engaged |
| **LED2** | lit = recording · blinks = buffer cleared |

---

## How to use it

### Load your first loop

1. **SW2 → IN**
2. Tap **FS2** — recording starts
3. Play
4. Tap **FS2** — recording stops

In IN mode nothing else affects what's recorded. Loop length is set by where you stop.

### Hear it

5. **SHIFT up**, sweep **POT2** to catch DRY/WET, set it wet
6. **SHIFT down**
7. **BLEND** picks the engine balance — CCW vocoder, center both together, CW grains

### Overdub

8. **SW2 → MIX**
9. Set **DRY/WET** to taste — this is your layer mix
10. Tap **FS2**

**Recording punches in at the current playhead, runs exactly one loop pass, and stops
itself.** You do not tap FS2 again. Loop length is preserved.

### Clear

Hold **FS2** for one second. LED2 blinks.

### Gotchas

- **In MIX with DRY/WET fully wet, recording captures silence.** Dry is zeroed, so only
  engine output goes in — nothing on an empty buffer. Use IN, or keep DRY/WET off full wet
- The buffer is **volatile**. Power down loses it
- Under **FREEZE**, grain length *is* the stutter length — Overlap makes it longer, Density
  makes it retrigger faster

---

## Flashing

Firmware is a `.bin`. No toolchain needed.

Enter DFU: **hold BOOT → hold RESET → release RESET → release BOOT.**

### Daisy Web Programmer

1. USB-C to the Seed (a **data** cable — charge-only cables won't enumerate)
2. Enter DFU
3. Chrome or Edge → https://electro-smith.github.io/Programmer/ *(WebUSB; Firefox and
   Safari will not work)*
4. **Connect** → select **"DFU in FS Mode"** in the dialog → Connect
5. **File Upload** tab → choose the `.bin` → **Program**
6. Tap **RESET**

The onboard Seed LED should blink about once per second. That confirms clocks, SDRAM and
codec all came up.

### Windows: WinUSB driver via Zadig

Required before the web programmer will see the device.

1. Put the Seed in DFU **first**
2. Run [Zadig](https://zadig.akeo.ie/) → Options → **List All Devices**
3. Select **"DFU in FS Mode"** — confirm USB ID reads **0483:DF11**
4. Target driver **WinUSB** → **Install/Replace Driver**
5. Unplug, replug, re-enter DFU

> Zadig must target the DFU entry. Replacing the driver on the running-firmware entry breaks
> a working setup.

### STM32CubeProgrammer (most reliable)

ST's own tool. Talks to the bootloader directly — no WebUSB, no browser permissions.

1. Enter DFU, USB connected
2. Top-right dropdown → **USB** → Refresh. `USB1` with 0483:DF11 should appear
3. **Connect**
4. Download panel → browse to the `.bin`
5. Start address **`0x08000000`**
6. **Start Programming**
7. **Disconnect** (leave USB plugged in) → tap **RESET**

CubeProgrammer installs its own DFU driver and may supersede the WinUSB binding. If the web
programmer stops working afterwards, that's why — just keep using CubeProgrammer.

---

## Versions

| Version | Change |
|---|---|
| **2.1-ALEXTBLEND** | **Current.** Punch-in overdub: recording an existing loop begins at the playhead, runs one pass, auto-stops, preserves length |
| 2.0-ALEXTBLEND | Blend becomes a 2-way voc↔grain crossfade; dry moves to its own control on SHIFT+POT2, replacing input level |
| **1.9** | **Stock-blend stable.** Catch resumes without re-catch only if the parameter was live when leaving the page |
| 1.8 | RMS limiter moved to control rate — fixed grain crackle at high Overlap |
| 1.7 | Skip the engine whose blend coefficient is exactly zero |
| 1.6 | Output level on SHIFT+POT5; CPU load diagnostic |
| 1.5 | Unipolar LFO depth, 0 to +1.0 loop |
| 1.4 | Density top-end taper *(reverted)* |
| 1.3 | LFO rate/curve changes *(reverted)* |
| 1.2 | Expodec decay floor *(reverted)* |
| 1.1 | Catch adopt on first page visit *(reverted)* |
| **1.0** | **Audio block size 64 → 512.** The critical fix — see below |
| 0.1–0.9 | Bring-up: clear, LFO, heartbeat, input level, pot smoothing, FTZ, DC block, soft limit |

**Two branches.** `1.9` keeps the stock 3-way live blend where voc → dry → grain never
overlap. `2.x-ALEXTBLEND` lets both engines sound together with dry on its own knob.

---

## Deviations from stock

| | |
|---|---|
| **Mono** | Terrarium is single in / single out. Loses grain stereo spread |
| **120 s buffer** | vs 5 min. Volatile — no file, USB, SD or sample loading anywhere |
| **Secondaries dropped** | All `_alt` params fixed at 0, which *is* stock-at-defaults |
| **Speed/Pitch remap** | Center detent lands on unity. Endpoints preserved exactly. The module uses encoders; we use pots |
| **Catch mode** | One pot serving two parameters requires it |
| **Expodec window** | Replaces the stock linear ramp-down, on a toggle so it can be A/B'd |
| **LFO** | Not stock. Modulates read position only — loop geometry untouched |
| **Input/output level** | Terrarium is unity gain; a guitar sits ~20 dB below where the grain RMS limiter engages |
| **DC block, output soft limit, block size 512** | Platform necessities |

Freeze and Reset are not on footswitches: Freeze is literally `kspeed = 0` in the original,
so a toggle does it, and Reset only mattered for CV sync.

---

## Notes from the port

**Block size was the one serious bug.** `mincer` computes an entire frame — two forward
FFTs, one inverse, two 1025-bin loops, and a 2048-sample interpolated SDRAM read — inside a
single audio callback. At 64 samples that's 1.33 ms of budget against ~1.4 ms of work, so
one block in eight overran: a dropout 94×/second that sounded like digital garble. Nebulae
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
It never boosts — which is why input level matters on a guitar-level platform.

**Density never reaches its stated 2500 Hz.** The hybrid tanh/cubic scalar tops out at
0.906, so the real maximum is ~983 Hz.

---

## Building from source

```bash
git clone --recursive https://github.com/electro-smith/libDaisy.git
cd libDaisy && make -j
cd .. && mkdir nebulae && cd nebulae
# copy in: main.cpp terrarium.h nebulae_dsp.{h,cpp} nebulae_engine.{h,cpp} Makefile
make          # -> build/nebulae.bin
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

MIT — see [LICENSE](LICENSE).

- Original engine © 2019 Qu-Bit Electronix, Inc. (MIT)
- Blend rework derived from alex-thibodeau's fork (inherits MIT)
- This port © 2026 Kenneth Rakentine (MIT)
- libDaisy © Electrosmith (MIT)

If you fork this, keep all four attributions. That is the entire obligation MIT imposes,
and it costs nothing.

---

## Known limitations

- **Noise floor.** The Terrarium's analog path is unity gain, so a guitar hits the ADC
  20–30 dB below full scale. Input and output level are applied *after* the converter and
  cannot improve SNR. The real fix is analog gain ahead of the pedal — a clean boost, or a
  preamp stage between the input jack and the Terrarium IN pad. Note the audio rail is +5 V
  with VREF at 2.5 V, so usable swing is ~3.5 Vpp and gain above roughly 4–6× clips the
  input buffer
- **No stereo.** Seed pins 17/19 are unused; a second output buffer would give dual mono.
  True stereo needs a second vocoder and grain cloud — CPU is fine, memory is the limit
- **Bypass is buffered, not true bypass.** Audio always passes through the codec. Loss of
  power means loss of signal
- **~43 ms vocoder latency** against the dry path. Inherent to a 2048-point FFT, and stock
  behaviour
- The module locks Speed/Start/Size during recording and snaps Speed/Pitch to unity after an
  overdub. Neither is implemented — the second is impossible with pots
