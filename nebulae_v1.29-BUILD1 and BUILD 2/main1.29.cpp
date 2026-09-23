// Nebulae v2 granular looper -- Daisy Seed3 / PedalPCB Terrarium Rev 2.
// Thin hardware layer. All DSP is in nebulae_dsp / nebulae_engine, validated on
// host against a Python oracle. No file/USB/SD functionality exists anywhere.
#include "daisy_seed.h"
#include "terrarium.h"
#include "nebulae_engine.h"

using namespace daisy;
using namespace terrarium;

static DaisySeed        hw;
static AdcChannelConfig adcCfg[6];
static Switch           sw[4], fs[2];
static GPIO             led[2];
static neb::Engine      engine;
static neb::Controls    ctrl;
static CpuLoadMeter     cpu;
static int              overload = 0;
static uint32_t         hb = 0;
static bool             led_state = false;

// Panel:  POT1 START | POT2 SPEED (detent) | POT3 SIZE (shift: Window)
//         POT4 DENSITY (shift: Overlap) | POT5 PITCH (detent) | POT6 BLEND
// 120 s mono float32 x2 (A = record target, B = playback source) = 46.1 MB of 64 MB
constexpr int BUF_LEN = 48000 * 120;
static float DSY_SDRAM_BSS bufA[BUF_LEN];
static float DSY_SDRAM_BSS bufB[BUF_LEN];

static bool  bypass = true, recording = false;
static bool  fs2_down = false, fs2_consumed = false;
static float fs2_ms = 0.0f;
static int   clear_blink = 0;
static const float HOLD_MS = 1000.0f;      // FS2 held this long = clear buffer

// ============================================================================
// SHIFT CONTROL: toggle (build 1) or illuminated momentary button (build 2).
//   0 = SW1 is an SPDT toggle. Up = shift page. Page 3 = SHIFT up + FS1 held.
//   1 = SW1 position holds a momentary tact button wired across the SW1 pads.
//       Tap (< SHIFT_TAP_MS) toggles the shift latch, green LED lit while latched.
//       Hold (>= SHIFT_TAP_MS) gives page 3 while held, red LED lit, no toggle on
//       release. Hold works from primary OR shift. FS1 is then pure bypass.
// A toggle held up would read as an eternal hold, so this MUST be 0 on build 1.
// ============================================================================
#ifndef BUILD2
#define BUILD2 0
#endif
#ifndef SHIFT_IS_BUTTON
#define SHIFT_IS_BUTTON BUILD2
#endif
static const float SHIFT_TAP_MS = 300.0f;   // past this, the press is a hold (page 3)
static bool  shift_latch = false, sb_down = false, sb_held = false;
static float sb_ms = 0.0f;
static int   reset_blink = 0;
// Page-3 toggle latches. Read the FLIP, never the position: entering page 3 must not
// adopt whatever the toggles happen to be sitting at. While page 3 is held, flipping
// SW2 either way toggles the bandpass LFO. SW2 keeps doing its normal SRC job at the
// same time, so the lever never lies about what it is set to.
static bool  bpf_lfo = false;
static bool  sw2_prev = false;
static bool  p3_prev  = false;
static int   p3_blink = 0;
// FS1: tap on RELEASE toggles bypass, hold 1 s resets secondary + tertiary. Bypass has to
// move to release, otherwise every bypass tap would also start a reset timer.
static bool  fs1_down = false, fs1_consumed = false;
static float fs1_ms = 0.0f;
#if SHIFT_IS_BUTTON
static GPIO  shift_led_r, shift_led_g;     // button's built-in red/green, via 1K each
static const Pin PIN_SHIFT_LED_R = seed::D0;   // free Seed pins; flywire from the
static const Pin PIN_SHIFT_LED_G = seed::D1;   // underside of the Terrarium header
#endif

static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out, size_t size)
{
    cpu.OnBlockStart();
    for (int i = 0; i < 4; ++i) sw[i].Debounce();
    for (int i = 0; i < 2; ++i) fs[i].Debounce();
    const float blk_ms = 1000.0f * (float)size / 48000.0f;

#if SHIFT_IS_BUTTON
    // ---- SW1 as momentary button: tap = shift latch, hold = page 3 while held ----
    if (sw[0].Pressed()) {
        if (!sb_down) { sb_down = true; sb_ms = 0.0f; sb_held = false; }
        sb_ms += blk_ms;
        if (!sb_held && sb_ms >= SHIFT_TAP_MS) sb_held = true;   // crossed into hold
    } else if (sb_down) {
        sb_down = false;
        if (!sb_held) shift_latch = !shift_latch;                // it was a tap
        sb_held = false;
    }
    ctrl.shift = shift_latch;
    ctrl.page3 = sb_held;
    if (reset_blink > 0) {                  // FS1 reset also confirms on the button's LED
        shift_led_r.Write((reset_blink/5) & 1);
        shift_led_g.Write((reset_blink/5) & 1);
    } else {
        shift_led_r.Write(sb_held);                 // red  = page 3, while held
        shift_led_g.Write(shift_latch && !sb_held); // green = secondary, latched
    }
#else
    // ---- SW1 as a toggle. Page 3 is SHIFT up + FS1 held. ----
    ctrl.shift = sw[0].Pressed();
    ctrl.page3 = ctrl.shift && fs[0].Pressed();
#endif

    // ---- FS1: tap (on release) = bypass, hold 1 s = reset secondary + tertiary ----
    // Bypass fires on release, not press, so holding for a reset does not also toggle it.
    // On build 1, FS1 doubles as the page-3 modifier while SHIFT is up, so it does neither
    // job in that state: a page-3 hold must not reset, and must not flip bypass on release.
    if (ctrl.page3) {
        fs1_down = false; fs1_consumed = false; fs1_ms = 0.0f;
    } else if (fs[0].Pressed()) {
        if (!fs1_down) { fs1_down = true; fs1_ms = 0.0f; fs1_consumed = false; }
        fs1_ms += blk_ms;
        if (!fs1_consumed && fs1_ms >= HOLD_MS) {
            engine.ResetAltParams();
            fs1_consumed = true;
            reset_blink = 40;
        }
    } else if (fs1_down) {
        fs1_down = false;
        if (!fs1_consumed) bypass = !bypass;
    }

    // ---- FS2: tap = record toggle (on release), hold 1 s = clear buffer ----
    if (fs[1].Pressed()) {
        if (!fs2_down) { fs2_down = true; fs2_ms = 0.0f; fs2_consumed = false; }
        fs2_ms += blk_ms;
        if (!fs2_consumed && fs2_ms >= HOLD_MS) {   // fires once at the threshold
            engine.ClearBuffer();
            recording = false;
            fs2_consumed = true;
            clear_blink = 40;
        }
    } else if (fs2_down) {
        fs2_down = false;
        if (!fs2_consumed) recording = !recording;   // it was a tap, not a hold
    }

    // Page-3 latch on SW2. Read the FLIP, never the position, so entering page 3 cannot
    // adopt wherever the lever happens to sit. SW2 goes on doing its normal SRC job
    // throughout, including during page 3: a lever that means one thing and reads another
    // is worse than an extra flip.
    {
        bool sw2 = sw[1].Pressed();
        if (ctrl.page3) {
            if (!p3_prev) sw2_prev = sw2;          // entering: take a reference only
            else if (sw2 != sw2_prev) { bpf_lfo = !bpf_lfo; sw2_prev = sw2; p3_blink = 25; }
        }
        p3_prev = ctrl.page3;
        ctrl.bpf_lfo = bpf_lfo;
    }

    ctrl.src_mix = sw[1].Pressed();
    ctrl.expo    = sw[2].Pressed();
    ctrl.freeze  = sw[3].Pressed();
    ctrl.record  = recording;
    ctrl.bypass  = bypass;
    for (int i = 0; i < 6; ++i) ctrl.pot[i] = hw.adc.GetFloat(i);

    static float ibuf[512], obuf[512];
    size_t n = size > 512 ? 512 : size;
    for (size_t i = 0; i < n; ++i) ibuf[i] = in[0][i];
    engine.Process(ibuf, obuf, (int)n, ctrl);

    // An overdub auto-stops itself after one pass. Without this the latch stays true and
    // Engine punches in AGAIN on the next block, overdubbing forever.
    // MUST be AFTER Process(): before it, the engine has not yet acted on a fresh tap, so
    // rec_.recording() is still false and this would clear the latch on the very block
    // that set it -- record could never start at all.
    if (recording && !engine.rec().recording()) recording = false;
    for (size_t i = 0; i < n; ++i) { out[0][i] = obuf[i]; out[1][i] = obuf[i]; }

    cpu.OnBlockEnd();
    // DIAGNOSTIC: LED2 latches on ~1 s whenever a block exceeds 90% of its deadline.
    if (cpu.GetMaxCpuLoad() > 0.90f) overload = 90;
    if (overload > 0) overload--;

    // Heartbeat on the Seed's onboard LED, ~1 Hz at 512 samples/48 kHz. If this blinks
    // steadily the callback is running at the right rate, which means clocks, SDRAM init
    // and codec setup all succeeded. First thing to check on a new build.
    if (++hb >= 47) { hb = 0; led_state = !led_state; hw.SetLed(led_state); }

    led[0].Write(!bypass);                                  // LED1: engaged
    // LED2 blink chain. Distinct periods so the three confirmations are tellable apart:
    // buffer clear is slowest, parameter reset is in between, page-3 latch is quickest.
    if (clear_blink > 0) { led[1].Write((clear_blink/6) & 1); clear_blink--; }
    else if (reset_blink > 0) { led[1].Write((reset_blink/9) & 1); reset_blink--; }
    // LED2 = RECORDING ONLY (plus the clear-confirm blink above).
    // v1.9 had `recording || (shift && CatchDir(0) != 0)`, so LED2 lit on every SHIFT
    // flip -- LFO Rate defaults to 0.0 and is uncaught on entry, so the catch indicator
    // fired immediately. Useful information, but it reads as "recording" at a glance.
    // A dedicated SHIFT indicator goes on the RGB LED in build 2.
    else if (p3_blink > 0) { led[1].Write((p3_blink/4) & 1); p3_blink--; }
    else led[1].Write(recording);
    (void)overload;   // CPU meter still runs; no longer drives an LED
}

int main(void)
{
    hw.Init();

    // Flush-to-zero. libDaisy enables the FPU (SCB->CPACR) but never touches FPSCR, so
    // denormals are live. The phase vocoder's prev_ array is recursive and decays toward
    // zero during silence -- exactly where subnormal arithmetic could cost cycles. Values
    // this small (~1e-38) are inaudible either way, so this cannot change the sound.
    __set_FPSCR(__get_FPSCR() | (1u << 24));   // FZ
    // BLOCK SIZE 512, NOT 64.
    // mincer computes an entire frame -- 2 forward FFTs, 1 inverse, two 1025-bin loops,
    // and a 2048-sample interpolated SDRAM read -- inside ONE callback, every HOP=512
    // samples. At 64 samples the budget is 1.33 ms against ~1.4 ms of work, so one block
    // in eight overruns: a dropout 94x/second, the "garbled digital" sound.
    hw.SetAudioBlockSize(512);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    for (int i = 0; i < 6; ++i) adcCfg[i].InitSingle(PIN_POT[i]);
    hw.adc.Init(adcCfg, 6);
    hw.adc.Start();

    for (int i = 0; i < 4; ++i) sw[i].Init(PIN_SW[i], hw.AudioCallbackRate());
    for (int i = 0; i < 2; ++i) fs[i].Init(PIN_FS[i], hw.AudioCallbackRate());
    for (int i = 0; i < 2; ++i) led[i].Init(PIN_LED[i], GPIO::Mode::OUTPUT);
#if SHIFT_IS_BUTTON
    shift_led_r.Init(PIN_SHIFT_LED_R, GPIO::Mode::OUTPUT);
    shift_led_g.Init(PIN_SHIFT_LED_G, GPIO::Mode::OUTPUT);
#endif

    engine.Init(48000.0f, bufA, bufB, BUF_LEN);
    cpu.Init(48000.0f, 512);
    hw.StartAudio(AudioCallback);
    for (;;) {}
}
