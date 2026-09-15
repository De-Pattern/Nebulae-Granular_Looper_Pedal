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
    float Tick(float hz, int n);     // advance n samples, return value at the END
    float Value() const { return last_; }
private:
    float sr_=48000.f, skew_=0.65f, last_=0.f; double ph_=0.0;
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
    void  Init(float initA, float initB);
    void  SetPage(int page);                 // 0 = primary, 1 = shift
    float Update(float raw);
    int   CatchDir() const { return dir_; }  // -1 turn CCW, +1 turn CW, 0 caught
private:
    float val_[2], last_, park_[2];
    bool  caught_[2], have_last_, left_caught_[2];
    int   page_, dir_;
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
    Lfo         lfo_;
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
    float lfo_prev_;
    float speed_, pitch_, in_gain_, out_gain_;
    bool  last_shift_;
    float *tpos_, *ppos_, *voc_, *grn_, *ing_, *pre_;
    int    scratch_n_;
};

} // namespace neb
