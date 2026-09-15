#include "nebulae_engine.h"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace neb {

// ============================ PotSmoother ============================
void PotSmoother::Init(float sr, int block, float hz) {
    float fs = sr/(float)block;                       // control rate
    a_ = 1.0f - expf(-2.0f*(float)M_PI*hz/fs);
    if (a_ > 1.0f) a_ = 1.0f;
    z_ = 0.0f; primed_ = false;
}
float PotSmoother::Process(float raw) {
    if (!primed_) { z_ = raw; primed_ = true; return z_; }  // snap on first read
    z_ += a_*(raw - z_);
    return z_;
}

// ============================ Lfo ============================
float Lfo::Tick(float hz, int n) {
    ph_ += (double)hz*n/sr_;
    while (ph_ >= 1.0) ph_ -= 1.0;
    float p = (float)ph_, s = skew_;
    float w = (p < s) ? (p/(2.0f*s)) : (0.5f + (p-s)/(2.0f*(1.0f-s)));
    last_ = sinf(2.0f*(float)M_PI*w);
    return last_;
}

// ============================ CatchPot ============================
void CatchPot::Init(float a, float b) {
    val_[0]=a; val_[1]=b; caught_[0]=true; caught_[1]=true;
    park_[0]=park_[1]=-1.0f;
    left_caught_[0]=true; left_caught_[1]=false;
    have_last_=false; last_=0.0f; page_=0; dir_=0;
}
void CatchPot::SetPage(int p) {
    if (p == page_) return;
    park_[page_]        = have_last_ ? last_ : -1.0f;  // where the knob sat when we left
    left_caught_[page_] = caught_[page_];              // ...and whether it was live
    page_ = p;
    // Resume without a re-catch ONLY if this page's parameter was already live when we
    // left it AND the knob has not moved since. Without the left_caught_ test, the second
    // and every later visit to the shift page would adopt the knob position outright --
    // the exact jump catch exists to prevent. Page 0 starts live, so flipping SHIFT and
    // back with untouched knobs still resumes the primaries instantly.
    bool unmoved = left_caught_[p] && (park_[p] >= 0.0f) && have_last_
                   && (fabsf(last_ - park_[p]) <= 0.01f);
    caught_[p] = unmoved;
    if (caught_[p]) val_[p] = last_;
    have_last_ = true;
}
float CatchPot::Update(float raw) {
    const int p = page_;
    if (!caught_[p]) {
        // Tolerance is 2% of travel, not 0.2%. The pot smoother lags during a sweep, so a
        // stored value sitting at an extreme (Window and LFO depth both default to 0.0) is
        // approached from above and never actually reached -- and the crossing test below
        // cannot fire either, because you cannot cross 0.0 from above.
        float d = raw - val_[p];
        if (fabsf(d) < 0.02f) caught_[p] = true;
        else if (have_last_ && ((last_-val_[p]) * d) <= 0.0f) caught_[p] = true;
        dir_ = caught_[p] ? 0 : (d > 0.0f ? -1 : +1);
    } else dir_ = 0;
    if (caught_[p]) val_[p] = raw;
    last_ = raw; have_last_ = true;
    return val_[p];
}

// ============================ Recorder ============================
void Recorder::Init(float* a, float* b, int cap, float sr) {
    A_=a; B_=b; cap_=cap; sr_=sr;
    ndx_=0; reclength_=0; tail_=0; copypos_=0; copyblk_=0;
    recording_=false; overlapping_=false; copying_=false; src_mix_=true;
    overdub_=false; remain_=0;
    memset(A_,0,sizeof(float)*cap); memset(B_,0,sizeof(float)*cap);
}
void Recorder::Start(int playhead, int looplen) {
    if (reclength_ > 0 && looplen > 0) {
        overdub_ = true;
        ndx_     = playhead % reclength_;  if (ndx_ < 0) ndx_ += reclength_;
        remain_  = looplen < reclength_ ? looplen : reclength_;
    } else {
        overdub_ = false; ndx_ = 0; remain_ = 0;
    }
    recording_ = true; overlapping_ = false; tail_ = 0;
}
void Recorder::Stop() {
    if (!recording_) return;
    recording_ = false;
    if (overdub_) {                 // loop length preserved; no tail to write
        overdub_=false; overlapping_=false; copying_=true; copypos_=0; copyblk_=0;
    } else {
        reclength_ = ndx_; overlapping_ = true; tail_ = 0;
    }
}
void Recorder::Clear() {
    // O(1). Do NOT memset here -- this runs in the audio callback, and zeroing 46 MB of
    // SDRAM would blow the deadline by three orders of magnitude. reclength_ == 0 means
    // "empty"; nothing reads past it, so stale data is unreachable.
    recording_=false; overlapping_=false; copying_=false; overdub_=false;
    ndx_=0; reclength_=0; tail_=0; copypos_=0; copyblk_=0; remain_=0;
}
void Recorder::Process(const float* arec, const float* ain, float blenddry, int n) {
    for (int i = 0; i < n; ++i) {
        if (recording_ && overdub_) {
            // wrap within the existing loop, auto-stop after exactly one pass
            A_[ndx_] = src_mix_ ? (arec[i] + ain[i]*blenddry) : ain[i];
            if (++ndx_ >= reclength_) ndx_ = 0;
            if (--remain_ <= 0) {
                recording_=false; overdub_=false;
                copying_=true; copypos_=0; copyblk_=0;
            }
        } else if (recording_) {
            if (ndx_ < cap_-1) {
                // SW2 SRC. MIX = BUF-1 faithful: output mix + scaled dry input, which is
                // what produces sound-on-sound. IN = clean input only, always, regardless
                // of Blend position.
                A_[ndx_] = src_mix_ ? (arec[i] + ain[i]*blenddry) : ain[i];
                ndx_++;
            } else { recording_=false; reclength_=ndx_; overlapping_=true; tail_=0; }
        } else if (overlapping_) {
            // 250 ms of live input past the loop end, into BOTH buffers (anti-click)
            int w = reclength_ + tail_;
            if (tail_ < TAIL_SAMPLES && w < cap_) { A_[w]=ain[i]; B_[w]=ain[i]; tail_++; }
            else { overlapping_=false; copying_=true; copypos_=0; copyblk_=0; }
        }
    }
}
void Recorder::ServiceCopy() {
    if (!copying_) return;
    // Stock instr 2 copies 4 s per (ksmps/4) k-cycles -- ~768 kB of memcpy inside one
    // callback, which would fail every time. Bounded chunk instead: same staged
    // behaviour, deadline-safe.
    const int CHUNK = 4096;                       // 16 kB per audio block
    int total = std::min(reclength_ + TAIL_SAMPLES, cap_);
    int end   = std::min(copypos_ + CHUNK, total);
    if (end > copypos_) memcpy(B_+copypos_, A_+copypos_, sizeof(float)*(end-copypos_));
    copypos_ = end;
    if (copypos_ >= total) copying_ = false;
}

// ============================ Engine ============================
void Engine::Init(float sr, float* a, float* b, int cap) {
    sr_=sr; A_=a; B_=b; cap_=cap;
    pv_.Init(sr); gr_.Init(sr); rms_.Init(sr);
    rec_.Init(a,b,cap,sr);
    BuildWindows(bank_);
    WinMix(bank_, 0.0f, true, wmix_);
    lfo_.Init(sr); lfo_.SetSkew(0.65f);
    dc_.Init(sr, 12.0f);
    for (int i = 0; i < 6; ++i) sm_[i].Init(sr, 512, 20.0f);

    catch_[0].Init(0.0f,  0.0f);    // POT1: Start   / LFO Rate
    catch_[1].Init(1.0f,  0.0f);    // POT3: Size    / Window
    catch_[2].Init(0.55f, 0.5f);    // POT4: Density / Overlap
    catch_[3].Init(1.0f,  0.0f);    // POT6: Blend   / LFO Depth
    catch_[4].Init(0.5f,  0.0f);    // POT2: Speed   / Input Level  (0 = unity)
    catch_[5].Init(0.5f,  0.0f);    // POT5: Pitch   / Output Level (0 = unity)

    phase_=0.0; speed_=1.0f; pitch_=1.0f; last_shift_=false;
    held_start_=0.0f; held_size_=1.0f; held_dens_=0.55f; held_ovl_=0.5f;
    held_win_=0.0f;   held_blend_=1.0f;
    held_rate_=0.0f;  held_depth_=0.0f; held_speed_=0.5f; held_inlvl_=0.0f;
    held_pitch_=0.5f; held_outlvl_=0.0f;
    lfo_prev_=0.0f; in_gain_=1.0f; out_gain_=1.0f;

    scratch_n_ = 512;
    tpos_=(float*)calloc(scratch_n_,sizeof(float));
    ppos_=(float*)calloc(scratch_n_,sizeof(float));
    voc_ =(float*)calloc(scratch_n_,sizeof(float));
    grn_ =(float*)calloc(scratch_n_,sizeof(float));
    ing_ =(float*)calloc(scratch_n_,sizeof(float));
}

void Engine::Process(const float* in, float* out, int n, Controls& c) {
    if (n > scratch_n_) n = scratch_n_;
    rec_.SetSource(c.src_mix);
    for (int i = 0; i < 6; ++i) c.pot[i] = sm_[i].Process(c.pot[i]);

    const int page = c.shift ? 1 : 0;
    for (int i = 0; i < 6; ++i) catch_[i].SetPage(page);
    float p1 = catch_[0].Update(c.pot[0]);   // POT1: Start   / LFO Rate
    float p3 = catch_[1].Update(c.pot[2]);   // POT3: Size    / Window
    float p4 = catch_[2].Update(c.pot[3]);   // POT4: Density / Overlap
    float p6 = catch_[3].Update(c.pot[5]);   // POT6: Blend   / LFO Depth
    float p2 = catch_[4].Update(c.pot[1]);   // POT2: Speed   / Input Level
    float p5 = catch_[5].Update(c.pot[4]);   // POT5: Pitch   / Output Level

    // Panel: POT1 START | POT2 SPEED | POT3 SIZE(window)
    //        POT4 DENSITY(overlap) | POT5 PITCH | POT6 BLEND
    if (page == 0) { held_start_ = p1; held_speed_ = p2; held_size_ = p3;
                     held_dens_  = p4; held_pitch_ = p5; held_blend_ = p6; }
    else           { held_rate_  = p1; held_inlvl_ = p2; held_win_  = p3;
                     held_ovl_   = p4; held_outlvl_= p5; held_depth_ = p6; }

    // INPUT LEVEL (SHIFT+POT2). Terrarium is unity gain, so a guitar sits ~20 dB below
    // where the grain RMS limiter's absolute 0.20 ceiling engages.
    in_gain_  = expf(held_inlvl_*logf(16.0f));           // 1x .. 16x  (0 .. +24 dB)
    // OUTPUT LEVEL (SHIFT+POT5). Makeup only -- lifts signal and noise together and
    // cannot improve SNR. Its value is downstream gain staging.
    out_gain_ = expf(held_outlvl_*logf(8.0f));           // 1x .. 8x   (0 .. +18 dB)

    const float g_start   = held_start_;
    const float g_speed   = held_speed_;
    const float g_pitch   = held_pitch_;
    const float g_blend   = held_blend_;
    const float g_density = held_dens_;

    speed_ = LawSpeed(KnobSpeed(g_speed), c.freeze);
    pitch_ = LawPitch(KnobPitch(g_pitch));
    float gfreq = LawDensity(g_density);
    float gsize = LawGrainSizeMs(held_ovl_, gfreq);
    float voc_g, dry_g, grain_g;
    LawBlendLive(g_blend, &voc_g, &dry_g, &grain_g);
    WinMix(bank_, held_win_, c.expo, wmix_);

    const bool empty = (rec_.length() <= 0);
    int   blen     = empty ? cap_ : rec_.length();
    float blen_sec = (float)blen/sr_;
    float lstart, lsize;
    LawLoop(g_start, held_size_, blen_sec, &lstart, &lsize);

    // LFO -> READ POSITION only. Loop start/size are computed BEFORE this, so loop
    // geometry and playback rate are untouched. At depth 0 output is bit-identical.
    const float lfo_hz  = 0.02f*expf(held_rate_*logf(2.0f/0.02f));   // 0.02 .. 2 Hz
    // UNIPOLAR 0..+1.0 of the loop, squared curve. Bipolar had the same peak-to-peak span
    // but pulled the read position behind the playhead; unipolar anchors the sweep at
    // Start and moves forward only.
    const float lfo_amt = held_depth_*held_depth_;
    const float lfo_a = lfo_prev_;
    const float lfo_b = lfo_.Tick(lfo_hz, n);
    lfo_prev_ = lfo_b;

    double finc = ((1.0/lsize)*speed_)/sr_;
    for (int i = 0; i < n; ++i) {
        phase_ += finc;
        while (phase_ >= 1.0) phase_ -= 1.0;
        while (phase_ <  0.0) phase_ += 1.0;
        float mix = lfo_a + (lfo_b-lfo_a)*((float)(i+1)/(float)n);  // smooth across block
        float uni = (mix + 1.0f)*0.5f;                              // -1..1 -> 0..1
        float mp  = (float)phase_ + uni*lfo_amt;
        mp -= floorf(mp);                                           // wrap into [0,1)
        ppos_[i] = mp;
        tpos_[i] = lstart + mp*lsize;              // absolute seconds, for mincer
    }

    // Skip the engine whose blend coefficient is exactly zero. Constant-power crossfade
    // means one side hits 0 at each extreme, and computing a grain cloud purely to
    // multiply it by zero is waste that displaced the vocoder's frame.
    const bool need_voc   = !empty && voc_g   > 1e-4f;
    const bool need_grain = !empty && grain_g > 1e-4f;

    if (need_voc) pv_.Process(tpos_, B_, blen, pitch_, 1.0f, voc_, n);
    else          for (int i = 0; i < n; ++i) voc_[i] = 0.0f;

    if (need_grain) {
        gr_.Process(B_, blen, ppos_, gfreq, gsize, pitch_, wmix_, grn_, n);
        rms_.Process(grn_, n);
    } else          for (int i = 0; i < n; ++i) grn_[i] = 0.0f;

    for (int i = 0; i < n; ++i) {
        float d = dc_.Process(in[i])*in_gain_;   // DC removed BEFORE engine and recorder
        float y = (voc_[i]*voc_g + grn_[i]*grain_g + d*dry_g)*out_gain_;
        // Constant-power crossfade assumes uncorrelated sources; at unity speed/pitch the
        // vocoder tracks dry closely, so intermediate Blend can sum toward 1.41x. Left
        // unclamped the codec WRAPS rather than clips, which is the classic "blown out
        // digital" sound. Soft-limit above 0.9 instead: transparent below, no wrap above.
        float a = fabsf(y);
        if (a > 0.9f) y = (y > 0.0f ? 1.0f : -1.0f)*(0.9f + 0.1f*tanhf((a-0.9f)/0.1f));
        out[i]  = y;
        ing_[i] = d;
    }

    if (c.record && !rec_.recording()) {
        // Punch-in overdub: begin at the current playhead, one loop pass, auto-stop.
        rec_.Start((int)((lstart + (float)phase_*lsize)*sr_), (int)(lsize*sr_));
    }
    if (!c.record && rec_.recording()) rec_.Stop();
    rec_.Process(out, ing_, dry_g, n);
    rec_.ServiceCopy();

    if (c.bypass) for (int i = 0; i < n; ++i) out[i] = in[i];
}

} // namespace neb
