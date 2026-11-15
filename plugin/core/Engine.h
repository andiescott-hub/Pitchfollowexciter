// Pitch-follow exciter engine. Plain C++, no JUCE, so it can be unit tested on its own.
//
// Signal flow per band:
//   mono in -> shared multi-resolution analysis -> strongest peak in the band's window (hysteresis, gate)
//           -> glide -> tracked state-variable bandpass  => s (the note, phase-locked) and q (same, a quarter cycle late)
//           -> generate(): saturator residual + octave  => everything the band adds beyond the clean copy
//           -> finish():   highpass on the generated part, then wet += gain * (s + generated)
//
// Two modes:
//   Low latency: no delay. The filter runs live; the pitch estimate arrives late, so a band needs a moment to lock.
//   Lookahead:   audio is delayed by latency() samples so the pitch estimate is already there when the note starts.
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>
#include "Fft.h"
#include "Rules.h"

namespace pfx {

struct BandParams {
    bool  enabled  = false;
    float fc       = 100.0f;   // centre of the window, Hz
    float amountDb = 0.0f;
    float drive    = 0.3f;     // 0..1
    float focus    = 0.5f;     // 0..1, 0.5 = automatic window
    float glide    = 0.5f;     // 0..1, 0.5 = automatic glide
    int   type     = Tube;
};

class Engine {
public:
    static constexpr int kBands = 4, kRes = 4;

    BandParams params[kBands];
    int  keyMask  = 0;         // bit n set = pitch class n is in the key; 0 = key off
    bool bassLink = false;     // upper bands prefer harmonics of the lowest band's note

    void prepare (double sampleRate, int maxBlockSize)
    {
        sr = sampleRate;
        mul = sr <= 50000.0 ? 1 : sr <= 100000.0 ? 2 : 4;
        static const int baseN[kRes] = { 8192, 4096, 2048, 1024 }, baseHop[kRes] = { 1024, 512, 256, 256 };
        for (int r = 0; r < kRes; ++r) {
            Res& x = res[r];
            x.N = baseN[r] * mul; x.hop = baseHop[r] * mul; x.count = 0;
            x.fft.init (x.N);
            x.win.resize ((size_t) x.N); x.re.resize ((size_t) x.N); x.im.resize ((size_t) x.N);
            x.mags.assign ((size_t) x.N / 2, 0.0f);
            for (int i = 0; i < x.N; ++i) x.win[(size_t) i] = 0.5 - 0.5 * std::cos (6.283185307179586 * i / x.N);
        }
        ring.assign ((size_t) (16384 * mul), 0.0f);
        mask = (int64_t) ring.size() - 1;
        maxBlock = maxBlockSize;
        dly.assign ((size_t) maxBlock, 0.0f);
        for (auto& b : bands) { b.s.assign ((size_t) maxBlock, 0.0f); b.q.assign ((size_t) maxBlock, 0.0f); b.reset(); }
        now = 0; writePos = 0;
        attack  = 1.0f - std::exp (-1.0f / (0.004f * (float) sr));
        release = 1.0f - std::exp (-1.0f / (0.050f * (float) sr));
        peakRel = std::exp (-1.0f / (3.0f * (float) sr));
    }

    void setLookahead (bool on) { delay = on ? 4096 * mul : 0; }
    int  latency() const        { return delay; }

    // Step 1. Feed a block of mono input. Fills delayed(), s(b) and q(b).
    void analyse (const float* mono, int n)
    {
        updateBlockParams();
        for (int i = 0; i < n; ++i) {
            ring[(size_t) (writePos & mask)] = mono[i];
            ++writePos; ++now;
            for (int r = 0; r < kRes; ++r)
                if (++res[r].count >= res[r].hop) { res[r].count = 0; if (resUsed[r]) frame (r); }
            const float x = ring[(size_t) ((writePos - 1 - delay) & mask)];
            dly[(size_t) i] = x;
            for (int b = 0; b < kBands; ++b)
                if (params[b].enabled) bandSample (bands[b], x, bands[b].s[(size_t) i], bands[b].q[(size_t) i]);
        }
    }

    const float* delayed() const   { return dly.data(); }
    const float* s (int b) const   { return bands[b].s.data(); }
    const float* q (int b) const   { return bands[b].q.data(); }

    // Step 2. What the saturator adds beyond the clean copy. Works at any sample rate, so the caller can
    // run it oversampled. `gen` may alias `sIn`.
    void generate (int b, const float* sIn, const float* qIn, float* gen, int n) const
    {
        const Band& B = bands[b];
        const Shaper& sh = B.shaper;
        const float pk = std::max (B.peak, 1.0e-4f), inv = sh.d / pk, out = pk / sh.den;
        for (int i = 0; i < n; ++i) {
            const float x = sIn[i], y = qIn[i];
            const float odd = out * sh.curve (x * inv);
            // 2*x*y is a pure octave above the note. Unlike a biased curve it has no envelope-rate term,
            // so it cannot thump. Dividing by the envelope keeps its level proportional to the note.
            const float oct = sh.even * 2.0f * x * y / std::sqrt (x * x + y * y + 1.0e-18f);
            gen[i] = odd - x + oct;
        }
    }

    // Step 3. Highpass the generated part below the band, then mix: wet += gain * (clean copy + generated).
    void finish (int b, const float* gen, float* wet, int n, float gainFrom, float gainTo)
    {
        Band& B = bands[b];
        const float* sIn = B.s.data();
        const float step = n > 1 ? (gainTo - gainFrom) / (float) (n - 1) : 0.0f;
        float g = gainFrom;
        for (int i = 0; i < n; ++i, g += step) {
            const float x = gen[i], y = B.hb0 * x + B.hz1;
            B.hz1 = B.hb1 * x - B.ha1 * y + B.hz2;
            B.hz2 = B.hb0 * x - B.ha2 * y;
            wet[i] += g * (sIn[i] + y);
        }
    }

    // Convenience for tests and offline use: steps 1 to 3 with no oversampling.
    void processMono (const float* in, float* wet, int n)
    {
        analyse (in, n);
        std::fill (wet, wet + n, 0.0f);
        std::vector<float>& tmp = scratch;
        tmp.resize ((size_t) n);
        for (int b = 0; b < kBands; ++b) {
            if (! params[b].enabled) continue;
            generate (b, s (b), q (b), tmp.data(), n);
            const float g = std::pow (10.0f, params[b].amountDb / 20.0f);
            finish (b, tmp.data(), wet, n, g, g);
        }
    }

    float trackedHz (int b) const { return bands[b].uiHz.load (std::memory_order_relaxed); }
    bool  gateOpen (int b) const  { return bands[b].uiOn.load (std::memory_order_relaxed); }

private:
    struct Res {
        int N = 0, hop = 0, count = 0;
        Fft fft;
        std::vector<double> win, re, im;
        std::vector<float> mags;
    };

    struct Event { int64_t at; float logHz; bool on; };

    struct Band {
        // tracker
        float prevHz = 0, runMax = 0, lastHz = 0, lastAmp = 0;
        bool  lastOn = false, wasEnabled = false;
        Event queue[128];
        int   qHead = 0, qCount = 0;
        // control
        float tgtLog = 0, cur = 0, gate = 0, glideCoef = 1, fixedLog = 0;
        bool  fixed = false;
        bool  tgtOn = false;
        // tracked filter (state-variable, stable under fast modulation)
        float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1, Q = 10;
        int   coefCount = 0;
        // saturation
        Shaper shaper;
        float peak = 0;
        // highpass on the generated part
        float hb0 = 1, hb1 = 0, ha1 = 0, ha2 = 0, hz1 = 0, hz2 = 0;
        float lo = 0, hi = 0;
        int   res = 0;
        std::vector<float> s, q;
        std::atomic<float> uiHz { 0.0f };
        std::atomic<bool>  uiOn { false };

        void reset()
        {
            prevHz = runMax = lastHz = lastAmp = 0; lastOn = false;
            qHead = qCount = 0; gate = 0; tgtOn = false;
            ic1 = ic2 = hz1 = hz2 = 0; peak = 0; coefCount = 0;
        }
    };

    void updateBlockParams()
    {
        for (int r = 0; r < kRes; ++r) resUsed[r] = false;
        bass = -1;
        for (int b = 0; b < kBands; ++b) {
            Band& B = bands[b];
            const BandParams& p = params[b];
            if (! p.enabled) { B.wasEnabled = false; B.uiOn.store (false, std::memory_order_relaxed); continue; }
            if (! B.wasEnabled) { B.reset(); B.cur = B.tgtLog = std::log (p.fc); B.wasEnabled = true; }
            B.res = resFor (p.fc);
            resUsed[B.res] = true;
            bandEdges (p.fc, p.focus, B.lo, B.hi);
            B.Q = autoQ (p.fc);
            B.fixed = glideFixed (p.glide);
            B.fixedLog = std::log (p.fc);
            B.glideCoef = 1.0f - std::exp (-1.0f / (glideMs (p.fc, p.glide) * 0.001f * (float) sr));
            B.shaper.set (p.type, p.drive);
            if (bass < 0 || p.fc < params[bass].fc) bass = b;
            // 2nd-order highpass at 0.4 x the window's lower edge
            const float w = 6.2831853f * std::min (0.4f * B.lo, 0.3f * (float) sr) / (float) sr;
            const float cs = std::cos (w), al = std::sin (w) / 1.41421356f, a0 = 1.0f + al;
            B.hb0 = (1.0f + cs) * 0.5f / a0; B.hb1 = -(1.0f + cs) / a0;
            B.ha1 = -2.0f * cs / a0;         B.ha2 = (1.0f - al) / a0;
        }
        if (! bassLink) bass = -1;
    }

    void frame (int r)
    {
        Res& x = res[r];
        const int N = x.N;
        for (int i = 0; i < N; ++i) {
            x.re[(size_t) i] = ring[(size_t) ((writePos - N + i) & mask)] * x.win[(size_t) i];
            x.im[(size_t) i] = 0.0;
        }
        x.fft.run (x.re.data(), x.im.data());
        const double sc = 4.0 / N;
        for (int k = 0; k < N / 2; ++k)
            x.mags[(size_t) k] = (float) (std::sqrt (x.re[(size_t) k] * x.re[(size_t) k] + x.im[(size_t) k] * x.im[(size_t) k]) * sc);
        // track the bass band first so the others can use its fresh note
        if (bass >= 0 && bands[bass].res == r) track (bass, x);
        for (int b = 0; b < kBands; ++b)
            if (params[b].enabled && bands[b].res == r && b != bass) track (b, x);
    }

    void track (int b, const Res& x)
    {
        Band& B = bands[b];
        const BandParams& p = params[b];
        const float bin = (float) sr / (float) x.N, hyst = 3.0f / focusScale (p.focus);
        const float* M = x.mags.data();
        const int b0 = std::max (2, (int) std::floor (B.lo / bin)), b1 = std::min (x.N / 2 - 2, (int) std::ceil (B.hi / bin));
        const float fb = (bass >= 0 && bass != b && bands[bass].lastOn) ? bands[bass].lastHz : 0.0f;

        int best = -1, near = -1;
        float bestScore = -1.0e9f, nearDist = 1.0e9f, nearScore = 0;
        for (int k = b0; k <= b1; ++k) {
            const float v = M[k];
            if (v > M[k - 1] && v >= M[k + 1]) {
                const float f = (float) k * bin;
                float score = 20.0f * std::log10 (v + 1.0e-12f);
                if (keyMask != 0) {                                       // prefer notes in the key
                    const int pc = (((int) std::lround (69.0f + 12.0f * std::log2 (f / 440.0f))) % 12 + 12) % 12;
                    if (keyMask & (1 << pc)) score += 6.0f;
                }
                if (fb > 0) {                                             // prefer harmonics of the bass note
                    const float h = std::round (f / fb);
                    if (h >= 1 && std::fabs (1200.0f * std::log2 (f / (h * fb))) < 45.0f) score += 6.0f;
                }
                if (score > bestScore) { bestScore = score; best = k; }
                if (B.prevHz > 0) {
                    const float d = std::fabs (std::log2 (f / B.prevHz));
                    if (d < nearDist) { nearDist = d; near = k; nearScore = score; }
                }
            }
        }

        // estimates are scheduled so they line up with the delayed audio
        const int64_t at = now + std::max (0, delay - x.N / 2);
        const float decay = std::exp (-(float) x.hop / (8.0f * (float) sr));
        B.runMax *= decay;

        if (best < 0) {
            B.lastOn = false;
            push (B, { at, std::log (B.prevHz > 0 ? B.prevHz : p.fc), false });
            return;
        }
        int k = best;
        if (near >= 0 && near != best && nearDist < 0.125f && bestScore - nearScore < hyst) k = near;   // stay on the current note
        const float a = std::log (M[k - 1] + 1.0e-12f), c0 = std::log (M[k] + 1.0e-12f), c = std::log (M[k + 1] + 1.0e-12f);
        const float den = a - 2.0f * c0 + c;
        const float off = std::clamp (std::fabs (den) > 1.0e-12f ? 0.5f * (a - c) / den : 0.0f, -0.5f, 0.5f);
        const float hz = ((float) k + off) * bin, amp = std::exp (c0 - 0.25f * (a - c) * off);
        B.prevHz = hz;
        B.runMax = std::max (B.runMax, amp);
        bool on = amp > std::max (B.runMax * 0.0126f, 1.0e-4f);         // automatic gate: 38 dB under the band's recent peak
        B.lastHz = hz; B.lastAmp = amp; B.lastOn = on;
        // no shared partials: if another band sits within a semitone and is stronger, this one yields
        for (int o = 0; o < kBands && on; ++o)
            if (o != b && params[o].enabled && bands[o].lastOn
                && std::fabs (std::log2 (hz / bands[o].lastHz)) < 1.0f / 12.0f && amp < bands[o].lastAmp)
                on = false;
        push (B, { at, std::log (hz), on });
    }

    static void push (Band& B, Event e)
    {
        if (B.qCount == 128) { B.qHead = (B.qHead + 1) & 127; --B.qCount; }
        B.queue[(B.qHead + B.qCount) & 127] = e;
        ++B.qCount;
    }

    void bandSample (Band& B, float x, float& sOut, float& qOut)
    {
        while (B.qCount > 0 && B.queue[B.qHead].at <= now) {
            const Event& e = B.queue[B.qHead];
            B.tgtLog = e.logHz; B.tgtOn = e.on;
            B.uiHz.store (std::exp (B.fixed ? B.fixedLog : e.logHz), std::memory_order_relaxed);
            B.uiOn.store (e.on, std::memory_order_relaxed);
            B.qHead = (B.qHead + 1) & 127; --B.qCount;
        }
        if (B.fixed) B.cur = B.fixedLog; else B.cur += (B.tgtLog - B.cur) * B.glideCoef;
        const float tg = B.tgtOn ? 1.0f : 0.0f;
        B.gate += (tg - B.gate) * (tg > B.gate ? attack : release);
        if ((B.coefCount++ & 15) == 0) {
            const float f = std::clamp (std::exp (B.cur), 20.0f, 0.45f * (float) sr);
            const float g = std::tan (3.14159265f * f / (float) sr);
            B.k = 1.0f / B.Q;
            B.a1 = 1.0f / (1.0f + g * (g + B.k)); B.a2 = g * B.a1; B.a3 = g * B.a2;
        }
        const float v3 = x - B.ic2, v1 = B.a1 * B.ic1 + B.a2 * v3, v2 = B.ic2 + B.a2 * B.ic1 + B.a3 * v3;
        B.ic1 = 2.0f * v1 - B.ic1; B.ic2 = 2.0f * v2 - B.ic2;
        sOut = B.k * v1 * B.gate;        // bandpass: the note itself, in phase with the input
        qOut = B.k * v2 * B.gate;        // lowpass output scaled: the same note a quarter cycle late
        const float mag = std::fabs (sOut);
        B.peak = mag > B.peak ? mag : B.peak * peakRel;
    }

    double sr = 44100.0;
    int mul = 1, delay = 0, maxBlock = 0, bass = -1;
    int64_t now = 0, writePos = 0, mask = 0;
    float attack = 1, release = 1, peakRel = 1;
    bool resUsed[kRes] = {};
    Res res[kRes];
    Band bands[kBands];
    std::vector<float> ring, dly, scratch;
};

} // namespace pfx
