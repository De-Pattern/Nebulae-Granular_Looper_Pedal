// Nebulae v2 granular looper -- portable DSP core.
// NO Daisy/hardware dependencies. Host-compilable for validation against
// the Python reference oracle. See Nebulae_Engine_Port_Notes.md.
#pragma once
#include <cstdint>
#include <cstddef>

namespace neb {

constexpr int   WIN_LEN    = 512;          // giwindowtablesize
constexpr int   FFT_N      = 2048;         // ifftlivesize
constexpr int   DECIM      = 4;            // idecimlive
constexpr int   HOP        = FFT_N / DECIM;// 512
constexpr int   MAX_GRAINS = 10;           // imax_grains
constexpr float GRAIN_AMP  = 0.45f;        // iamp
constexpr float RMS_CEIL   = 0.20f;
constexpr float LN32       = 3.4657359028f;

// ---------- window bank (index order = Window knob sweep, CCW -> CW) ----------
enum WinId { W_GAUSSIAN=0, W_BLKHARRIS, W_EXPODEC, W_BARTLETT, W_RAMPUP, W_SINC, W_RECT,
             W_COUNT, W_RAMPDOWN = W_COUNT, W_BANK };   // slot 2 is expodec OR rampdown
// Builds all 8 tables: the 7 sweep positions plus the stock RampDown alternative
// for slot 2. Switching EXPO at runtime is then a table pick, not a rebuild.
void  BuildWindows(float bank[W_BANK][WIN_LEN+1]);
void  WinMix(const float bank[W_BANK][WIN_LEN+1], float knob, bool use_expodec,
             float out[WIN_LEN+1]);

// ---------- real FFT, Csound packing: [DC, Nyq, re1, im1, ... ] ----------
class RFFT {
public:
    void Init();
    void Forward(float* d);   // in-place, length N -> Csound packing
    void Inverse(float* d);   // in-place, Csound packing -> length N
private:
    float cs_[FFT_N], sn_[FFT_N];
    int   rev_[FFT_N];
    float re_[FFT_N], im_[FFT_N];
    void  Cfft(float* re, float* im, bool inverse);
};

// ---------- phase vocoder (Csound mincer, mono variant) ----------
class Mincer {
public:
    void  Init(float sr);
    void  Process(const float* time_sec, const float* buf, int buflen,
                  float pitch, float amp, float* out, int n);
private:
    float sr_, win_[FFT_N];
    float fwin_[FFT_N+2], bwin_[FFT_N+2], prev_[FFT_N+2];
    float outframe_[DECIM*FFT_N];
    int   framecnt_[DECIM], cnt_, curframe_;
    RFFT  fft_;
};

// ---------- granular (partikkel subset) ----------
struct Grain {
    double phase, delta, envphase, envinc;
    int    stop; uint32_t order; bool active;
};
class Granular {
public:
    void Init(float sr);
    void Process(const float* buf, int buflen, const float* samplepos,
                 float grainfreq, float grainsize_ms, float pitch,
                 const float* window, float* out, int n, float spray = 0.0f);
private:
    float    sr_; double grainphase_; uint32_t order_, rng_;
    inline float Rnd();   // uniform -1..1
    Grain    g_[MAX_GRAINS];
    void KillOldest();
};

// RMS limiter: identity below RMS_CEIL, attenuation above. NOT a leveler.
class RmsLimiter {
public:
    void Init(float sr, float hp = 10.0f);
    void Process(float* sig, int n);
private:
    float b_, gprev_; double acc_;
};

// ---------- control laws ----------
float KnobSpeed(float pot);     // D5 remap: detent -> +1x
float KnobPitch(float pot);     // D6 remap: detent -> unity
float LawSpeed(float g, bool freeze);
float LawPitch(float g);
float LawDensity(float g);
float LawGrainSizeMs(float overlap, float grainfreq);
void  LawLoop(float g_start, float g_size, float buflen_sec, float* start, float* size);
void  LawBlendLive(float g, float* voc, float* dry, float* grain);

} // namespace neb
