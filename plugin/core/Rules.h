// Automatic per-band settings derived from centre frequency, and the saturation types.
// These mirror prototype/index.html so the plugin and the web prototype sound alike.
#pragma once
#include <cmath>

namespace pfx {

struct Pt { float f, v; };

inline float interp (const Pt* p, int n, float f)
{
    if (f <= p[0].f) return p[0].v;
    for (int i = 1; i < n; ++i)
        if (f <= p[i].f) {
            const Pt a = p[i - 1], b = p[i];
            return a.v + (b.v - a.v) * std::log (f / a.f) / std::log (b.f / a.f);
        }
    return p[n - 1].v;
}

// Window half-width in semitones: wide for bass, narrow up high.
inline float autoWindowSt (float fc) { static const Pt p[] = { { 80, 7 }, { 250, 4 }, { 1000, 2.5f }, { 3000, 1.5f } }; return interp (p, 4, fc); }
// Glide in milliseconds.
inline float autoGlideMs (float fc)  { static const Pt p[] = { { 80, 60 }, { 250, 40 }, { 1000, 25 }, { 3000, 15 } };   return interp (p, 4, fc); }
// Tracked filter Q: lower in the bass so it settles quickly.
inline float autoQ (float fc)        { static const Pt p[] = { { 40, 6 }, { 250, 17 } };                                 return interp (p, 2, fc); }

inline float focusScale (float focus) { return std::pow (2.0f, (focus - 0.5f) * 1.4f); } // 0..1, 0.5 = automatic
// Glide knob, 0..1: far left = fixed (no following, the band sits at its centre), 0.5 = automatic, far right = 3 s.
inline bool  glideFixed (float glide) { return glide < 0.02f; }
inline float glideMs (float fc, float glide)
{
    const float a = autoGlideMs (fc);
    return glide <= 0.5f ? a * std::pow (2.0f, (glide - 0.5f) * 4.0f) : a * std::pow (3000.0f / a, (glide - 0.5f) * 2.0f);
}
inline int   resFor (float fc)        { return fc < 120 ? 0 : fc < 500 ? 1 : fc < 2000 ? 2 : 3; }

inline void bandEdges (float fc, float focus, float& lo, float& hi)
{
    const float w = autoWindowSt (fc) * focusScale (focus);
    lo = fc * std::pow (2.0f, -w / 12.0f);
    hi = fc * std::pow (2.0f,  w / 12.0f);
}

enum SatType { Tube = 0, Tape = 1, Transistor = 2 };

// Every curve is symmetric, so on its own it adds odd harmonics only and no low-frequency term.
// Even harmonics come from a separate octave signal (see Engine::generate), scaled by `even`.
struct Shaper {
    int type = Tube;
    float d = 0.02f, den = 0.02f, even = 0.0f;

    void set (int t, float drive)
    {
        static const float gain[3] = { 7.0f, 6.0f, 12.0f }, ev[3] = { 0.7f, 0.08f, 0.0f };
        type = t;
        d = 0.02f + gain[t] * std::pow (drive, 1.5f);
        den = curve (d);
        even = ev[t] * drive;
    }

    float curve (float u) const
    {
        switch (type) {
            case Tube: return std::tanh (u);                                  // soft
            case Tape: return u / std::sqrt (1.0f + u * u);                   // softer, slower knee
            default:   { const float u2 = u * u; return u / std::sqrt (std::sqrt (1.0f + u2 * u2)); } // flat, then hard
        }
    }
};

} // namespace pfx
