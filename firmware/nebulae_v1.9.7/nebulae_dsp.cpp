#include "nebulae_dsp.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace neb {

// ============================ WINDOWS ============================
static void CosWin(float* ft, const double c[4]) {
    double arg = 2.0*M_PI/WIN_LEN;
    for (int i = 0; i <= WIN_LEN; ++i) {
        double x = i*arg;
        ft[i] = (float)(c[0] - c[1]*cos(x) + c[2]*cos(2.0*x) - c[3]*cos(3.0*x));
    }
}
static void Gen7(float* ft, const double* seg, int nseg) {
    int i = 0; double v = seg[0];
    for (int j = 1; j < nseg; j += 2) {
        int n = (int)lround(seg[j]); double nx = seg[j+1];
        n = std::min(n, WIN_LEN+1-i);
        for (int k = 0; k < n; ++k) ft[i+k] = (float)(v + (nx-v)*((double)k/n));
        i += n; v = nx;
    }
    for (; i <= WIN_LEN; ++i) ft[i] = (float)v;
}

void BuildWindows(float bank[W_BANK][WIN_LEN+1]) {
    const int h = WIN_LEN >> 1;
    // Gaussian: fgens.c gen20 case 6, varian = 1.0
    { double arg = 12.0/WIN_LEN; float* ft = bank[W_GAUSSIAN];
      double x = -6.0; int i = 0;
      for (; i < h; ++i, x += arg) ft[i] = (float)exp(-(x*x)/2.0);
      for (x = 0.0; i <= WIN_LEN; ++i, x += arg) ft[i] = (float)exp(-(x*x)/2.0); }
    { const double c[4] = {0.35878,0.48829,0.14128,0.01168}; CosWin(bank[W_BLKHARRIS], c); }
    // Bartlett: gen20 case 3
    { double arg = 2.0/WIN_LEN; float* ft = bank[W_BARTLETT]; int i = 0; double x = 0.0;
      for (; i < h; ++i, x += 1.0) ft[i] = (float)(x*arg);
      for (; i <= WIN_LEN; ++i, x += 1.0) ft[i] = (float)(2.0 - x*arg); }
    { const double s[] = {0, 15.0*(WIN_LEN/16.0), 1, (WIN_LEN/16.0), 0}; Gen7(bank[W_RAMPUP], s, 5); }
    // Sinc: gen20 case 9. NOTE: instrument names this giHamming -- it is NOT Hamming.
    { double arg = 2.0*M_PI/WIN_LEN; float* ft = bank[W_SINC];
      int i = 0; double x = -M_PI;
      for (; i < h; ++i, x += arg) ft[i] = (float)(sin(x)/x);
      ft[i++] = 1.0f;
      for (x = arg; i <= WIN_LEN; ++i, x += arg) ft[i] = (float)(sin(x)/x); }
    for (int i = 0; i <= WIN_LEN; ++i) bank[W_RECT][i] = 1.0f;
    // Slot 2 has two candidates: stock RampDown, and the D9 Expodec deviation.
    { const double s[] = {0, (WIN_LEN/16.0), 1, 15.0*(WIN_LEN/16.0), 0};
      Gen7(bank[W_RAMPDOWN], s, 5); }
    {
        float* ft = bank[W_EXPODEC]; int a = (int)(WIN_LEN/16.0);
        for (int i = 0; i < a; ++i)
            ft[i] = (float)(0.001*pow(1000.0, (double)i/(a-1)));
        int m = WIN_LEN+1-a;
        for (int i = 0; i < m; ++i)
            ft[a+i] = (float)(1.0*pow(0.001, (double)i/(m-1)));
        ft[0] = 0.0f; ft[WIN_LEN] = 0.0f;   // force true zero at both ends
    }
}

void WinMix(const float bank[W_BANK][WIN_LEN+1], float knob, bool use_expodec,
            float out[WIN_LEN+1]) {
    float k = std::min(std::max(knob, 0.0f), 1.0f)*6.0f;
    int sel = std::min((int)k, 6); float bl = k - sel;
    // EXPO off -> slot 2 resolves to the stock RampDown table instead
    auto tab = [&](int i) -> const float* {
        return bank[(i == W_EXPODEC && !use_expodec) ? W_RAMPDOWN : i];
    };
    if (sel >= 6) { memcpy(out, bank[W_RECT], sizeof(float)*(WIN_LEN+1)); return; }
    const float *a = tab(sel), *b = tab(sel+1);
    for (int i = 0; i <= WIN_LEN; ++i) out[i] = a[i]*(1.0f-bl) + b[i]*bl;
}

// ============================ RFFT ============================
void RFFT::Init() {
    for (int i = 0; i < FFT_N; ++i) {
        cs_[i] = (float)cos(2.0*M_PI*i/FFT_N);
        sn_[i] = (float)sin(2.0*M_PI*i/FFT_N);
    }
    int lg = 0; while ((1 << lg) < FFT_N) ++lg;
    for (int i = 0; i < FFT_N; ++i) {
        int r = 0, x = i;
        for (int b = 0; b < lg; ++b) { r = (r<<1) | (x&1); x >>= 1; }
        rev_[i] = r;
    }
}
void RFFT::Cfft(float* re, float* im, bool inverse) {
    for (int i = 0; i < FFT_N; ++i) {
        int j = rev_[i];
        if (j > i) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= FFT_N; len <<= 1) {
        int step = FFT_N/len, half = len>>1;
        for (int i = 0; i < FFT_N; i += len) {
            for (int j = 0, k = 0; j < half; ++j, k += step) {
                float wr = cs_[k], wi = inverse ? sn_[k] : -sn_[k];
                float ur = re[i+j],      ui = im[i+j];
                float vr = re[i+j+half], vi = im[i+j+half];
                float tr = vr*wr - vi*wi, ti = vr*wi + vi*wr;
                re[i+j] = ur+tr;      im[i+j] = ui+ti;
                re[i+j+half] = ur-tr; im[i+j+half] = ui-ti;
            }
        }
    }
}
void RFFT::Forward(float* d) {
    for (int i = 0; i < FFT_N; ++i) { re_[i] = d[i]; im_[i] = 0.0f; }
    Cfft(re_, im_, false);
    d[0] = re_[0];              // DC real
    d[1] = re_[FFT_N/2];        // Nyquist real
    for (int k = 1; k < FFT_N/2; ++k) { d[2*k] = re_[k]; d[2*k+1] = im_[k]; }
}
void RFFT::Inverse(float* d) {
    re_[0] = d[0]; im_[0] = 0.0f;
    re_[FFT_N/2] = d[1]; im_[FFT_N/2] = 0.0f;
    for (int k = 1; k < FFT_N/2; ++k) {
        re_[k] = d[2*k];  im_[k] = d[2*k+1];
        re_[FFT_N-k] = d[2*k]; im_[FFT_N-k] = -d[2*k+1];   // conjugate mirror
    }
    Cfft(re_, im_, true);
    for (int i = 0; i < FFT_N; ++i) d[i] = re_[i]/FFT_N;
}

// ============================ MINCER ============================
void Mincer::Init(float sr) {
    sr_ = sr; fft_.Init();
    for (int i = 0; i < FFT_N; ++i) win_[i] = (float)(0.5 - 0.5*cos(2.0*M_PI*i/FFT_N));
    memset(fwin_,0,sizeof fwin_); memset(bwin_,0,sizeof bwin_);
    memset(prev_,0,sizeof prev_); memset(outframe_,0,sizeof outframe_);
    for (int k = 0; k < DECIM; ++k) framecnt_[k] = k*FFT_N;
    cnt_ = HOP; curframe_ = 0;
}
void Mincer::Process(const float* time_sec, const float* buf, int size,
                     float pitch, float amp, float* out, int n) {
    const float scaling = (8.0f/DECIM)/3.0f;
    for (int s = 0; s < n; ++s) {
        if (cnt_ == HOP) {
            // F1: read position QUANTIZED to hop boundaries
            double tim = time_sec[s];
            long long spos = (long long)HOP * (long long)(tim*sr_/HOP);
            while (spos >  size) spos -= size;
            while (spos <= 0)    spos += size;
            double pos = (double)spos;
            for (int i = 0; i < FFT_N; ++i) {
                int post = (int)pos; float frac = (float)(pos - post);
                post %= size; if (post < 0) post += size;
                float in = (post+1 < size) ? buf[post] + frac*(buf[post+1]-buf[post]) : buf[post];
                fwin_[i] = in*win_[i];
                // F2: bwin deliberately reuses fwin's `frac`. Csound quirk, preserved.
                int pb = (int)(pos - (double)HOP*pitch);
                pb %= size; if (pb < 0) pb += size;
                in = (pb+1 < size) ? buf[pb] + frac*(buf[pb+1]-buf[pb]) : buf[pb];
                bwin_[i] = in*win_[i];
                pos += pitch;
            }
            fft_.Forward(bwin_); fft_.Forward(fwin_);
            bwin_[FFT_N] = bwin_[1]; bwin_[FFT_N+1] = 0.0f;
            fwin_[FFT_N] = fwin_[1]; fwin_[FFT_N+1] = 0.0f;
            for (int i = 0; i < FFT_N+2; i += 2) {           // phase diff vs prev
                float div = 1.0f/(hypotf(prev_[i], prev_[i+1]) + 1e-20f);
                float pr = prev_[i]*div, pi = prev_[i+1]*div;
                float tr = bwin_[i]*pr + bwin_[i+1]*pi;
                float ti = bwin_[i]*pi - bwin_[i+1]*pr;
                bwin_[i] = tr; bwin_[i+1] = ti;
            }
            for (int i = 0; i < FFT_N+2; i += 2) {           // phase lock (always on)
                float tr, ti;
                if (i > 0) {
                    if (i < FFT_N) { tr = bwin_[i]+bwin_[i-2]+bwin_[i+2];
                                     ti = bwin_[i+1]+bwin_[i-1]+bwin_[i+3]; }
                    else           { tr = bwin_[i]+bwin_[i-2]; ti = 0.0f; }
                } else             { tr = bwin_[i]+bwin_[i+2]; ti = 0.0f; }
                tr += 1e-15f;
                float div = 1.0f/hypotf(tr, ti);
                float pr = tr*div, pi = ti*div;
                float orr = fwin_[i]*pr - fwin_[i+1]*pi;
                float oi  = fwin_[i]*pi + fwin_[i+1]*pr;
                prev_[i] = fwin_[i] = orr; prev_[i+1] = fwin_[i+1] = oi;
            }
            fwin_[1] = fwin_[FFT_N];
            fft_.Inverse(fwin_);
            framecnt_[curframe_] = curframe_*FFT_N;
            int base = framecnt_[curframe_];
            for (int i = 0; i < FFT_N; ++i) outframe_[base+i] = win_[i]*fwin_[i];
            cnt_ = 0; curframe_ = (curframe_+1) % DECIM;
        }
        float acc = 0.0f;
        for (int i = 0; i < DECIM; ++i) { acc += outframe_[framecnt_[i]]; framecnt_[i]++; }
        out[s] = acc*amp*scaling;
        cnt_++;
    }
}

// ============================ GRANULAR ============================
inline float Granular::Rnd() {           // xorshift32 -> uniform -1..1
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return ((float)(rng_ & 0xFFFFFF) / 8388608.0f) - 1.0f;
}
void Granular::Init(float sr) {
    sr_ = sr; grainphase_ = 0.0; order_ = 0; rng_ = 0x1234567u;
    for (int i = 0; i < MAX_GRAINS; ++i) g_[i].active = false;
}
void Granular::KillOldest() {
    int best = -1; uint32_t o = 0xFFFFFFFFu;
    for (int i = 0; i < MAX_GRAINS; ++i)
        if (g_[i].active && g_[i].order < o) { o = g_[i].order; best = i; }
    if (best >= 0) g_[best].active = false;
}
void Granular::Process(const float* buf, int tablen, const float* samplepos,
                       float grainfreq, float grainsize_ms, float pitch,
                       const float* window, float* out, int n, float spray) {
    const double graininc = (double)grainfreq/sr_;
    const double wavfreq  = (double)pitch/((double)tablen/sr_);
    for (int s = 0; s < n; ++s) {
        grainphase_ += graininc;
        if (grainphase_ >= 1.0) {
            grainphase_ -= 1.0;
            double dur = sr_*grainsize_ms/1000.0;
            if (dur >= 1.0) {
                int slot = -1;
                for (int i = 0; i < MAX_GRAINS; ++i) if (!g_[i].active) { slot = i; break; }
                if (slot < 0) { KillOldest();
                    for (int i = 0; i < MAX_GRAINS; ++i) if (!g_[i].active) { slot = i; break; } }
                // offset always 0 -> phase_corr engages above ~150 Hz grain rate
                double pc = (graininc > 0.0032) ? grainphase_/graininc : 0.0;
                double ph = samplepos[s] + pc*wavfreq/sr_;
                // GRAIN POSITION SPRAY. Stock: krandposscalar = alt*alt; krandpos =
                // birnd(scalar); asamplepos = abs(agphs + krandpos), wrapped. The squared
                // curve keeps fine control near zero. samplepos here is normalised over
                // the RECORDING, so the stock live-scalar is already implicit.
                if (spray > 1e-4f) {
                    double sc = (double)spray*spray;
                    ph = fabs(ph + Rnd()*sc);
                    ph -= floor(ph);                            // wrap into [0,1)
                } else {
                    ph = ph > 1.0 ? 1.0 : (ph < 0.0 ? 0.0 : ph);
                }
                double envinc = 1.0/dur;
                g_[slot] = { ph*tablen, (wavfreq/sr_)*tablen, pc*envinc, envinc,
                             (int)(s + dur - pc)+1, ++order_, true };
            }
        }
        float acc = 0.0f;
        for (int i = 0; i < MAX_GRAINS; ++i) {
            Grain& g = g_[i];
            if (!g.active) continue;
            if (s >= g.stop || g.envphase >= 1.0) { g.active = false; continue; }
            double p = fmod(g.phase, (double)tablen); if (p < 0) p += tablen;
            int i0 = (int)p; float fr = (float)(p - i0);
            float sm = buf[i0] + fr*(buf[(i0+1)%tablen] - buf[i0]);
            double ei = g.envphase*WIN_LEN;
            int e0 = (int)ei; float ef = (float)(ei - e0);
            int e1 = std::min(e0+1, WIN_LEN);
            float en = window[e0] + ef*(window[e1]-window[e0]);
            acc += sm*GRAIN_AMP*en;
            g.phase += g.delta; g.envphase += g.envinc;
        }
        out[s] = acc;
    }
}

void RmsLimiter::Init(float sr, float hp) {
    b_ = expf(-2.0f*(float)M_PI*hp/sr); acc_ = 0.0; gprev_ = 1.0f;
}
void RmsLimiter::Process(float* sig, int n) {
    // Csound's rms/gain pair is K-RATE: the scalar updates once per control period and is
    // constant in between. Recomputing per sample let a 1.6 ms follower track individual
    // grain envelopes, modulating gain at grain rate -- and audio-rate gain modulation is
    // distortion. It worsened with Overlap because more concurrent grains means more
    // amplitude ripple: the crackle that "follows transients and accumulates".
    double acc = acc_;
    for (int i = 0; i < n; ++i) acc = b_*acc + (1.0-b_)*(double)sig[i]*sig[i];
    acc_ = acc;
    double r   = sqrt(acc_);
    double tgt = r > RMS_CEIL ? RMS_CEIL : r;
    float  g   = (float)(r > 1e-12 ? tgt/r : 1.0);
    const float dg = (g - gprev_)/(float)n;      // ramp across the block, never step
    float gc = gprev_;
    for (int i = 0; i < n; ++i) { gc += dg; sig[i] *= gc; }
    gprev_ = g;
}

// ============================ CONTROL LAWS ============================
float KnobSpeed(float p) { return p <= 0.5f ? p*1.25f : 0.625f + (p-0.5f)*0.75f; }
float KnobPitch(float p) { return p <= 0.5f ? p*1.20f : 0.600f + (p-0.5f)*0.80f; }

float LawSpeed(float g, bool freeze) {
    if (freeze) return 0.0f;
    float k = g*8.0f - 4.0f;
    float a = fabsf(k);
    if (a >= 0.975f && a <= 1.025f) k = (k > 0.0f) ? 1.0f : -1.0f;
    return k;
}
float LawPitch(float g) {
    float k = 0.125f*expf(g*LN32);
    return (k >= 0.995f && k <= 1.005f) ? 1.0f : k;
}
float LawDensity(float g) {
    float t = (tanhf(g*2.0f - 1.0f) + 0.8f)*0.4f;
    float u = g*2.0f - 1.0f;
    float c = (u*u*u + 1.0f)*0.5f;
    float s = t*0.25f + c*0.75f;
    return expf(s*(logf(2500.0f) - logf(0.12f)) + logf(0.12f));
}
float LawGrainSizeMs(float overlap, float gf) {
    float sc = overlap*overlap; sc = std::min(std::max(sc,0.0f),1.0f);
    float maxpw = (gf < 4.0f) ? 4.0f : 6.0f;
    float v = sc*((1.0f/gf)*maxpw*1000.0f) + 1.0f;
    return std::min(v, 8000.0f);
}
void LawLoop(float gs, float gz, float blen, float* start, float* size) {
    *start = gs*blen;
    float v = (gz*gz)*(blen - *start);
    *size = v < 0.000035f ? 0.000035f : v;
}
void LawBlendLive(float g, float* voc, float* dry, float* grain) {
    if (g >= 0.48f && g <= 0.52f) g = 0.5f;
    if (g < 0.01f) g = 0.0f;
    if (g > 0.99f) g = 1.0f;
    float df = fabsf(g*4.0f - 2.0f) - 1.0f; df = std::min(std::max(df,-1.0f),1.0f);
    float vf = std::min(g*4.0f - 1.0f, 1.0f);
    float gf = std::max(g*4.0f - 3.0f, -1.0f);
    *voc   = std::min(std::max(sqrtf(0.5f*(1.0f-vf)),0.0f),1.0f);
    *grain = std::min(std::max(sqrtf(0.5f*(1.0f+gf)),0.0f),1.0f);
    *dry   = sqrtf(0.5f*(1.0f-df));
}

} // namespace neb
