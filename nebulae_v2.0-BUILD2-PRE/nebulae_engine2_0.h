// nebulae v2.0-BUILD2-PRE  (nebulae_engine.h)
// nebulae v1.32  (nebulae_engine.h)
// Nebulae v2 granular looper -- engine top level.
// NO hardware dependencies. Buffers are injected by the caller so this compiles on
// host and on Daisy (where they live in SDRAM).
#pragma once
#include "nebulae_dsp.h"

namespace neb {

constexpr int   TAIL_SAMPLES  = 12000;   // kmaxdelay, 250 ms @ 48k
constexpr float COPY_SECTION  = 4.0f;    // instr 2 copies in 4-second sections

// ---------------- phase-skewed sine LFO ----------------
// s > 0.5 stretches the rise so the fall squeezes near the end of phase.
// s = 0.5 is a plain sine.
class Lfo {
public:
    void  Init(float sr) { sr_=sr; ph_=0.0; }
    void  SetSkew(float s) { skew_ = s<0.05f?0.05f:(s>0.95f?0.95f:s); }
    // curve < 1 pushes the output toward its extremes, so the LFO leaves centre quickly
    // and lingers near the peaks: a swell rather than a rocking motion. curve = 1 is the
    // plain warped sine. Zero crossings and the +/-1 peaks are preserved either way, so
    // depth still means what it says.
    void  SetCurve(float g) { curve_ = g<0.2f?0.2f:(g>3.0f?3.0f:g); }
    float Tick(float hz, int n);     // advance n samples, return value at the END
    float Value() const { return last_; }
private:
    float sr_=48000.f, skew_=0.65f, curve_=1.0f, last_=0.f; double ph_=0.0;
};

// ---------------- LFO 2: skewed triangle ----------------
// A different animal from Lfo above, deliberately. Lfo is a phase-warped sine: smooth at
// every skew, no corners, character rather than obviousness. TriLfo is the plain thing a
// sampler gives you. Skew is literally where the peak sits, so 0.5 is a triangle, toward 0
// a ramp-DOWN saw (quick rise, long fall) and toward 1 a ramp-UP saw. Real corner at the
// peak, which is what reads as a saw rather than a soft wobble. Reverse saw for trilling is
// skew toward 0.
//
// Computed PER SAMPLE, not per block. At trill rates a block-rate LFO puts corners up to
// one block late: at 15 Hz that is 17% of the period, which wobbles audibly on a saw. A
// triangle is an add and a fold, so per-sample costs almost nothing and corners land exact.
class TriLfo {
public:
    void  Init(float sr) { sr_ = sr; ph_ = 0.0; }
    void  SetSkew(float s) { skew_ = s < 0.02f ? 0.02f : (s > 0.98f ? 0.98f : s); }
    void  SetPhase(double p) { ph_ = p - (double)(long)p; if (ph_ < 0.0) ph_ += 1.0; }
    double Phase() const { return ph_; }
    // advance one sample, return the new value, bipolar -1..+1
    inline float Step(float hz) {
        ph_ += (double)hz/(double)sr_;
        if (ph_ >= 1.0) ph_ -= 1.0;
        float p = (float)ph_;
        float v = (p < skew_) ? (p/skew_) : (1.0f - (p-skew_)/(1.0f-skew_));
        return v*2.0f - 1.0f;
    }
private:
    float sr_ = 48000.f, skew_ = 0.5f; double ph_ = 0.0;
};

// ---------------- one-pole pot smoother ----------------
// The module uses encoders on Speed and Pitch, which have no jitter; a pot does.
// ~20 Hz removes ADC noise for about 8 ms of lag, imperceptible by hand.
// NO deadband -- that would add stiction the real module does not have.
class PotSmoother {
public:
    void  Init(float sr, int block, float hz = 20.0f);
    float Process(float raw);
    void  Reset(float v) { z_ = v; primed_ = true; }
private:
    float a_ = 0.2f, z_ = 0.0f; bool primed_ = false;
};

// ---------------- DC blocker ----------------
// The Terrarium biases audio to VREF at IC1.1 and drives the Seed input directly, with
// no coupling cap in that path. Passthrough hides it (output cap C3 blocks DC on the way
// out) but the recorder stores it, and a DC term through a 2048-point FFT and through
// grain windows is catastrophic. Must be removed before the buffer.
class DcBlock {
public:
    void Init(float sr, float hz = 12.0f) { R_ = 1.0f - (6.2831853f*hz/sr); x1_=y1_=0.0f; }
    inline float Process(float x) { float y = x - x1_ + R_*y1_; x1_ = x; y1_ = y; return y; }
private:
    float R_ = 0.9985f, x1_ = 0.f, y1_ = 0.f;
};

// ---------------- catch / pickup pot (deviation D8) ----------------
class CatchPot {
public:
    void  Init(float a, float b, float c = 0.5f, float d = 0.5f, float e = 0.5f);
    void  SetPage(int page);   // 0 primary, 1 shift, 2 tertiary, 3 LFO 1, 4 LFO 2
    float Update(float raw);
    int   CatchDir() const { return dir_; }  // -1 turn CCW, +1 turn CW, 0 caught
    // Restore pages 1 and 2 to their init values and mark them uncaught. Page 0 is left
    // alone: resetting it would strand all six knobs and the pedal would ignore every
    // control until each was physically swept. The LFO pages (3 and 4) are left alone too,
    // since they are reached by a different control and resetting them from a footswitch
    // gesture aimed at the hidden knob pages would be a surprise.
    void  ResetAlt();
private:
    static constexpr int NP = 5;
    float val_[NP], init_[NP], last_, park_[NP];
    bool  caught_[NP], have_last_, left_caught_[NP];
    int   page_, dir_;
    // Give-up takeover, pages 1 and 2 only. Catch alone strands a parameter whose stored
    // value sits at an extreme: the knob has to travel all the way there to pick it up,
    // so the control reads as dead. Tracking how far the knob has moved since the page
    // change lets it stop waiting after a modest nudge and take over where it is, ramped
    // so there is no click. Page 0 keeps pure catch, because the panel legend makes knob
    // position meaningful there and a jump would break that.
    float entry_, moved_;          // knob at page entry, and total travel since
    float ramp_;                   // 0..1 crossfade from old value to knob on takeover
    bool  gaveup_;
};

// ---------------- double-buffered recorder ----------------
class Recorder {
public:
    void Init(float* bufA, float* bufB, int capacity, float sr);
    void SetSource(bool mix) { src_mix_ = mix; }   // SW2
    // Fresh buffer: records from 0, length set by where you stop.
    // Existing buffer: PUNCH-IN OVERDUB -- begins at the current playhead, wraps, and
    // auto-stops after exactly one loop pass. Loop length never changes.
    void Start(int playhead = 0, int looplen = 0);
    void Stop();
    void Clear();
    bool recording()   const { return recording_; }
    bool overdubbing() const { return overdub_; }
    int  length()      const { return reclength_; }
    void Process(const float* arec, const float* ain, float blenddry, int n);
    void ServiceCopy();
private:
    float *A_, *B_; int cap_, ndx_, reclength_, tail_, copypos_, copyblk_;
    float sr_; bool recording_, overlapping_, copying_, src_mix_, overdub_;
    int   remain_;
};

// ---------------- control surface state ----------------
struct Controls {
    float pot[6];    // raw 0..1, POT1..POT6
    bool  shift;     // SW1  SHIFT
    bool  page3;     // SHIFT up + FS1 held : tertiary page
    bool  lfo2_div;  // page-3 SW4 latch: LFO 2 rate becomes loop divisions, retriggered
    int   lfo_page;  // 0 none, 1 = LFO 1 page, 2 = LFO 2 page
    bool  src_mix;   // SW2  SRC    : true = MIX (records output mix) / false = IN
    bool  expo;      // SW3  EXPO   : true = expodec window / false = stock RampDown
    bool  freeze;    // SW4  FREEZE : true = playhead held (kspeed = 0)
    bool  record;    // FS2
    bool  bypass;    // FS1
};

// ---------------- engine ----------------
class Engine {
public:
    void Init(float sr, float* bufA, float* bufB, int capacity);
    void Process(const float* in, float* out, int n, Controls& c);
    Recorder& rec() { return rec_; }
    void ClearBuffer() { rec_.Clear(); phase_ = 0.0; }
    void ResetAltParams();          // secondary + tertiary back to power-on defaults
    int  CatchDir(int which) const { return catch_[which].CatchDir(); }
    float speed()   const { return speed_; }
    float pitch()   const { return pitch_; }
    float inGain()  const { return in_gain_; }
    float outGain() const { return out_gain_; }
    float lfo()     const { return lfo_prev_; }
    float depth()   const { return held_depth_; }
private:
    float sr_; float *A_, *B_; int cap_;
    Mincer      pv_;
    Granular    gr_;
    RmsLimiter  rms_;
    Recorder    rec_;
    Lfo         lfo_;           // LFO 1: warped sine, always smooth
    TriLfo      lfo2_;          // LFO 2: skewed triangle, saw at the extremes
    FreqShift   fsh_;
    BarberPhaser bph_;
    Bandpass     bpf_;
    PotSmoother sm_[6];
    DcBlock     dc_;
    // [0]=POT1 start/rate  [1]=POT3 size/window  [2]=POT4 density/overlap
    // [3]=POT6 blend/depth [4]=POT2 speed/inlvl  [5]=POT5 pitch/outlvl
    CatchPot    catch_[6];
    float bank_[W_BANK][WIN_LEN+1];
    float wmix_[WIN_LEN+1];
    double phase_;
    float held_start_, held_size_, held_dens_, held_ovl_, held_spray_, held_blend_;
    float held_rate_, held_depth_, held_speed_, held_inlvl_, held_pitch_, held_outlvl_;
    float held_rate2_, held_depth2_, held_skew2_, held_skew_;
    float held_bpq_ = 0.0f, held_win_ = 0.0f, held_tape_ = 0.5f, held_windec_ = 0.0f;               // page 3: size LFO rate / depth
    float held_bpf_, held_prand_;                  // page 3: bandpass freq, grain pitch rand
    float held_fshift_, held_fsmix_;               // page 3: freq shift amount / mix
    float lfo2_prev_;
    // LFO depth routing. Four bipolar attenuverters per LFO: read position, loop length,
    // bandpass, blend. Centre of each knob is zero, so a depth left anywhere is within
    // reach and no depth knob ever defaults to an extreme.
    float d_rd1_=0, d_sz1_=0, d_bpf1_=0, d_bl1_=0;    // LFO 1 to each destination
    float d_rd2_=0, d_sz2_=0, d_bpf2_=0, d_bl2_=0;    // LFO 2 to each destination
    float bpf_mod_=0;
    float built_decay_=0.0f;      // window bank is rebuilt only when this changes                                  // octaves of bandpass offset
    float lfo_prev_;
    float speed_, pitch_, in_gain_, out_gain_;
    bool  last_shift_;
    float *tpos_, *ppos_, *voc_, *grn_, *ing_, *pre_;
    int    scratch_n_;
};

} // namespace neb
