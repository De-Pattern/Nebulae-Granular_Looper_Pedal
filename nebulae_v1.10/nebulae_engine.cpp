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
void CatchPot::Init(float a, float b, float c) {
    val_[0]=a; val_[1]=b; val_[2]=c;
    for (int i=0;i<NP;++i){ caught_[i]=true; park_[i]=-1.0f; left_caught_[i]=false; }
    left_caught_[0]=true;                    // only the primary page starts live
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
    // Do NOT set have_last_ here. On the very first SetPage (booting with SHIFT already
    // up) no Update has run, so last_ is still 0.0 from Init -- and for any secondary
    // whose stored value is also 0.0, Update's crossing test (last_-val_)*d <= 0 fires
    // trivially and the parameter adopts the knob outright. Input Level could snap to 4x
    // at power-on. have_last_ must only become true after a real Update.
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
    lfo_.Init(sr);  lfo_.SetSkew(0.65f);
    lfo2_.Init(sr); lfo2_.SetSkew(0.50f);   // size LFO: plain sine, breathes both ways
    fsh_.Init(sr);
    dc_.Init(sr, 12.0f);
    for (int i = 0; i < 6; ++i) sm_[i].Init(sr, 512, 20.0f);

    //                 primary  shift   page3
    catch_[0].Init(0.0f,  0.0f,  0.5f);   // POT1: Start   / LFO Rate     / (spare)
    catch_[1].Init(1.0f,  0.0f,  0.0f);   // POT3: Size    / Grain Spray  / SIZE LFO DEPTH
    catch_[2].Init(0.55f, 0.5f,  0.5f);   // POT4: Density / Overlap      / (spare)
    catch_[3].Init(1.0f,  0.0f,  0.5f);   // POT6: Blend   / LFO Depth    / (spare)
    catch_[4].Init(0.5f,  0.5f,  0.0f);   // POT2: Speed   / Dub Level    / SIZE LFO RATE
                                          //       (dub 0.5 = unity on the 0.25x..4x curve)
    catch_[5].Init(0.5f,  0.0f,  0.5f);   // POT5: Pitch   / Output Level / FREQ SHIFT (0.5 = off)

    phase_=0.0; speed_=1.0f; pitch_=1.0f; last_shift_=false;
    held_start_=0.0f; held_size_=1.0f; held_dens_=0.55f; held_ovl_=0.5f;
    held_spray_=0.0f; held_blend_=1.0f;
    held_rate_=0.0f;  held_depth_=0.0f; held_speed_=0.5f; held_inlvl_=0.0f;
    held_pitch_=0.5f; held_outlvl_=0.0f;
    held_inlvl_=0.5f;   // unity overdub level
    held_rate2_=0.0f; held_depth2_=0.0f; held_fshift_=0.5f; lfo2_prev_=0.0f;
    lfo_prev_=0.0f; in_gain_=1.0f; out_gain_=1.0f;

    scratch_n_ = 512;
    tpos_=(float*)calloc(scratch_n_,sizeof(float));
    ppos_=(float*)calloc(scratch_n_,sizeof(float));
    voc_ =(float*)calloc(scratch_n_,sizeof(float));
    grn_ =(float*)calloc(scratch_n_,sizeof(float));
    ing_ =(float*)calloc(scratch_n_,sizeof(float));
    pre_ =(float*)calloc(scratch_n_,sizeof(float));
}

void Engine::Process(const float* in, float* out, int n, Controls& c) {
    if (n > scratch_n_) n = scratch_n_;
    rec_.SetSource(c.src_mix);
    for (int i = 0; i < 6; ++i) c.pot[i] = sm_[i].Process(c.pot[i]);

    const int page = c.page3 ? 2 : (c.shift ? 1 : 0);
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
    else if (page == 1) { held_rate_  = p1; held_inlvl_ = p2; held_spray_ = p3;
                          held_ovl_   = p4; held_outlvl_= p5; held_depth_ = p6; }
    else                { /* page 3: SHIFT up + FS1 held. No panel labels. */
                          held_rate2_ = p2; held_depth2_ = p3; held_fshift_ = p5; }

    // OVERDUB LEVEL (SHIFT+POT2). Scales ONLY the input as it is written to the buffer,
    // not what you monitor -- so you can balance a new layer against the existing loop
    // without changing what you hear. Centre = unity, CCW attenuates to 0.1x, CW boosts
    // to 4x. In MIX mode the existing loop arrives via `arec` and the new material via
    // `ing_`, so this is precisely the layer balance.
    in_gain_  = expf((held_inlvl_-0.5f)*2.0f*logf(4.0f)); // 0.25x .. 1x .. 4x, unity at CENTRE
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
    // Window now lives entirely on the EXPO toggle -- POT3's shift slot became Grain
    // Spray. EXPO off = Bartlett (index 3, moderately sharp). EXPO on = Expodec (index 2,
    // sharp attack with an exponential tail).
    memcpy(wmix_, bank_[c.expo ? W_EXPODEC : W_BARTLETT], sizeof(float)*(WIN_LEN+1));

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

    // SIZE LFO (page 3: POT2 rate, POT3 depth). Plain sine, bipolar. Multiplies loop
    // length by 2^(depth^2 * lfo), so full depth breathes between half and double length.
    // Multiplicative keeps the effect uniform across the Size knob's squared curve. The
    // phasor is normalised over the loop, so a changing length rescales rather than jumps.
    // Recording uses the UNMODULATED lsize: loop geometry for the buffer must not breathe.
    const float lfo2_hz  = 0.02f*expf(held_rate2_*logf(2.0f/0.02f));
    const float lfo2_amt = held_depth2_*held_depth2_;
    const float lfo2_a = lfo2_prev_;
    const float lfo2_b = lfo2_.Tick(lfo2_hz, n);
    lfo2_prev_ = lfo2_b;

    for (int i = 0; i < n; ++i) {
        float l2  = lfo2_a + (lfo2_b-lfo2_a)*((float)(i+1)/(float)n);
        float lsz = lsize*exp2f(lfo2_amt*l2);
        double finc = ((1.0/lsz)*speed_)/sr_;
        phase_ += finc;
        while (phase_ >= 1.0) phase_ -= 1.0;
        while (phase_ <  0.0) phase_ += 1.0;
        float mix = lfo_a + (lfo_b-lfo_a)*((float)(i+1)/(float)n);  // smooth across block
        float uni = (mix + 1.0f)*0.5f;                              // -1..1 -> 0..1
        float mp  = (float)phase_ + uni*lfo_amt;
        mp -= floorf(mp);                                           // wrap into [0,1)
        ppos_[i] = mp;
        tpos_[i] = lstart + mp*lsz;                // absolute seconds, for mincer
    }

    // Skip the engine whose blend coefficient is exactly zero. Constant-power crossfade
    // means one side hits 0 at each extreme, and computing a grain cloud purely to
    // multiply it by zero is waste that displaced the vocoder's frame.
    const bool need_voc   = !empty && voc_g   > 1e-4f;
    const bool need_grain = !empty && grain_g > 1e-4f;

    if (need_voc) pv_.Process(tpos_, B_, blen, pitch_, 1.0f, voc_, n);
    else          for (int i = 0; i < n; ++i) voc_[i] = 0.0f;

    if (need_grain) {
        gr_.Process(B_, blen, ppos_, gfreq, gsize, pitch_, wmix_, grn_, n, held_spray_);
        rms_.Process(grn_, n);
    } else          for (int i = 0; i < n; ++i) grn_[i] = 0.0f;

    for (int i = 0; i < n; ++i) {
        float d = dc_.Process(in[i]);            // DC removed BEFORE engine and recorder
        // Mix BEFORE output gain. The recorder must never see out_gain_ -- `out` feeds the
        // overdub path, so with output boosted every pass would write the loop back
        // louder and compound on itself. Output level is monitoring only.
        float pre = voc_[i]*voc_g + grn_[i]*grain_g + d*dry_g;
        // Soft-limit the recorded copy so the buffer itself cannot run away, and the
        // monitored copy separately so the codec cannot wrap. Constant-power crossfade
        // assumes uncorrelated sources; at unity speed/pitch the vocoder tracks dry
        // closely, so intermediate Blend can sum toward 1.41x.
        float ap = fabsf(pre);
        if (ap > 0.9f) pre = (pre > 0.0f ? 1.0f : -1.0f)*(0.9f + 0.1f*tanhf((ap-0.9f)/0.1f));
        pre_[i] = pre;

        float y = pre*out_gain_;
        float a = fabsf(y);
        if (a > 0.9f) y = (y > 0.0f ? 1.0f : -1.0f)*(0.9f + 0.1f*tanhf((a-0.9f)/0.1f));
        out[i]  = y;
        ing_[i] = d*in_gain_;                    // overdub level, recorded copy only
    }

    // FREQ SHIFT (page 3: POT5). OFF at centre with a dead zone, then EXPONENTIAL 2 Hz to
    // 2 kHz on each side: half travel lands near 57 Hz, three-quarters near 330 Hz, which
    // is where sidebands read as timbre. A linear range spent most of the knob in the
    // sub-20 Hz beating zone, which is the "wobble" rather than the shift. Applied to the
    // pre-gain mix so monitoring and recording get the same thing.
    {
        float r  = (held_fshift_ - 0.5f)*2.0f;             // -1 .. +1
        float ar = fabsf(r);
        float hz = 0.0f;
        if (ar > 0.03f) {
            float t = (ar - 0.03f)/0.97f;                   // 0 .. 1 outside the dead zone
            hz = 2.0f*expf(t*logf(1000.0f));               // 2 Hz .. 2000 Hz
            if (r < 0.0f) hz = -hz;
        }
        fsh_.Process(pre_, n, hz);
        // pre_ is already limited; re-apply output gain + limit for the monitored copy
        for (int i = 0; i < n; ++i) {
            float y = pre_[i]*out_gain_;
            float a = fabsf(y);
            if (a > 0.9f) y = (y > 0.0f ? 1.0f : -1.0f)*(0.9f + 0.1f*tanhf((a-0.9f)/0.1f));
            out[i] = y;
        }
    }

    if (c.record && !rec_.recording()) {
        // Punch-in overdub: begin at the current playhead, one loop pass, auto-stop.
        rec_.Start((int)((lstart + (float)phase_*lsize)*sr_), (int)(lsize*sr_));
    }
    if (!c.record && rec_.recording()) rec_.Stop();
    // Record source: existing loop playback + new input, INDEPENDENT of Blend.
    // Stock scales the dry contribution by the blend dry factor, which is 0.0 at both
    // Blend extremes -- so overdubbing at hard CW or CCW captured engine output only and
    // your playing never reached the buffer. Passing 1.0 makes MIX a proper sound-on-
    // sound: what you hear plus what you play, at the overdub level set on SHIFT+POT2.
    rec_.Process(pre_, ing_, 1.0f, n);
    rec_.ServiceCopy();

    if (c.bypass) for (int i = 0; i < n; ++i) out[i] = in[i];
}

} // namespace neb
