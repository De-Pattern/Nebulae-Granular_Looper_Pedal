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

static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out, size_t size)
{
    cpu.OnBlockStart();
    for (int i = 0; i < 4; ++i) sw[i].Debounce();
    for (int i = 0; i < 2; ++i) fs[i].Debounce();
    const float blk_ms = 1000.0f * (float)size / 48000.0f;

    // ---- FS1: bypass toggle ----

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

    ctrl.shift   = sw[0].Pressed();
    // FS1: with SHIFT down, a tap toggles bypass. With SHIFT up, HOLDING it exposes the
    // tertiary page (no panel labels) for as long as it is held, and does not touch bypass.
    if (ctrl.shift) { ctrl.page3 = fs[0].Pressed(); }
    else            { ctrl.page3 = false; if (fs[0].RisingEdge()) bypass = !bypass; }
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
    if (clear_blink > 0) { led[1].Write((clear_blink/6) & 1); clear_blink--; }
    // LED2 = RECORDING ONLY (plus the clear-confirm blink above).
    // v1.9 had `recording || (shift && CatchDir(0) != 0)`, so LED2 lit on every SHIFT
    // flip -- LFO Rate defaults to 0.0 and is uncaught on entry, so the catch indicator
    // fired immediately. Useful information, but it reads as "recording" at a glance.
    // A dedicated SHIFT indicator goes on the RGB LED in build 2.
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

    engine.Init(48000.0f, bufA, bufB, BUF_LEN);
    cpu.Init(48000.0f, 512);
    hw.StartAudio(AudioCallback);
    for (;;) {}
}
