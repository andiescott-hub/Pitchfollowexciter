// Engine checks that need no JUCE. Build: g++ -O2 -std=c++17 -I../core core_test.cpp -o core_test
#include <cstdio>
#include <cmath>
#include <vector>
#include "Engine.h"

using namespace pfx;
static const double SR = 44100.0, TAU = 6.283185307179586;
static int failures = 0;
static void check (bool ok, const char* what) { std::printf ("  %s  %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++failures; }

struct Run { std::vector<float> in, wet, hz; };

static Run run (Engine& e, const std::vector<float>& in)
{
    Run r; r.in = in; r.wet.resize (in.size()); r.hz.resize (in.size());
    const int B = 256;
    for (size_t o = 0; o + B <= in.size(); o += B) {
        e.processMono (in.data() + o, r.wet.data() + o, B);
        for (int i = 0; i < B; ++i) r.hz[o + i] = e.trackedHz (0);
    }
    const size_t done = in.size() / B * B;      // drop the unprocessed tail
    r.in.resize (done); r.wet.resize (done); r.hz.resize (done);
    return r;
}

static std::vector<float> tone (double f1, double f2, double seconds, double switchAt, bool plucked)
{
    std::vector<float> x ((size_t) (seconds * SR));
    double ph = 0;
    for (size_t i = 0; i < x.size(); ++i) {
        const double t = i / SR;
        ph += TAU * (t < switchAt ? f1 : f2) / SR;
        const double env = plucked ? std::exp (-std::fmod (t, 0.3) * 6.0) : 1.0;
        x[i] = (float) (0.5 * env * (std::sin (ph) + 0.3 * std::sin (2 * ph) + 0.15 * std::sin (3 * ph)));
    }
    return x;
}

static double lowDb (const std::vector<float>& w, size_t from, double cutoff)
{
    double a = 0, b = 0, c = 0, d = 0, e = 0, l = 0;
    const double k = 1.0 - std::exp (-TAU * cutoff / SR);
    for (size_t i = 0; i < w.size(); ++i) {
        a += (w[i] - a) * k; b += (a - b) * k; c += (b - c) * k; d += (c - d) * k;
        if (i >= from) { e += w[i] * w[i]; l += d * d; }
    }
    return 10.0 * std::log10 (l / e + 1e-20);
}

static void setup (Engine& e, bool lookahead, float fc, float drive, int type)
{
    e.prepare (SR, 256);
    e.setLookahead (lookahead);
    e.params[0].enabled = true; e.params[0].fc = fc; e.params[0].drive = drive; e.params[0].type = type;
}

int main()
{
    std::printf ("1. Steady 220 Hz note, clean (drive 0), lookahead\n");
    {
        Engine e; setup (e, true, 233.0f, 0.0f, Tube);
        Run r = run (e, tone (220, 220, 2.0, 9, false));
        check (std::fabs (r.hz.back() - 220.0f) < 1.0f, "tracks 220 Hz within 1 Hz");
        // the added signal must line up with the (delayed) input fundamental: it is a boost, not a new tone
        double num = 0, d1 = 0, d2 = 0; const int L = e.latency();
        for (size_t i = (size_t) SR; i < r.wet.size(); ++i) {
            const double ref = 0.5 * std::sin (TAU * 220.0 * (double) ((long) i - L + 1) / SR);
            num += r.wet[i] * ref; d1 += r.wet[i] * r.wet[i]; d2 += ref * ref;
        }
        const double corr = num / std::sqrt (d1 * d2), gain = std::sqrt (d1 / d2);
        std::printf ("     correlation with input fundamental %.4f, level %.2f dB\n", corr, 20 * std::log10 (gain));
        check (corr > 0.98, "added signal is phase-locked to the note");
        check (std::fabs (20 * std::log10 (gain)) < 1.0, "clean copy is at unity level");
    }

    std::printf ("2. Note change 110 -> 146.8 Hz at 1.0 s: time until the band follows within 2%%\n");
    for (int look = 0; look < 2; ++look) {
        Engine e; setup (e, look == 1, 123.0f, 0.3f, Tube);
        Run r = run (e, tone (110, 146.83, 2.5, 1.0, false));
        // express on the output timeline: when does the note change reach the output, when has the band followed
        const double changeAt = 1.0 + e.latency() / SR;
        double locked = -1;
        for (size_t i = r.hz.size() - 1; i > 0; --i)
            if (std::fabs (r.hz[i] / 146.83f - 1.0f) > 0.02f) { locked = (i + 1) / SR; break; }
        std::printf ("     %-11s latency %5.1f ms, band follows %+6.1f ms relative to the note change\n",
                     look ? "lookahead:" : "low latency:", 1000.0 * e.latency() / SR, 1000.0 * (locked - changeAt));
        check (std::fabs (r.hz.back() - 146.83f) < 1.5f, "ends on the new note");
        if (look) check (locked - changeAt < 0.03, "lookahead: follows within 30 ms of the change");
    }

    std::printf ("3. Plucked 220 Hz notes, drive 0.8: energy the band adds below 30 Hz (dB relative to what it adds)\n");
    for (int type = 0; type < 3; ++type) {
        Engine e; setup (e, true, 220.0f, 0.8f, type);
        Run r = run (e, tone (220, 220, 3.0, 9, true));
        const double db = lowDb (r.wet, (size_t) SR, 30.0);
        std::printf ("     %-11s %.1f dB\n", type == 0 ? "Tube" : type == 1 ? "Tape" : "Transistor", db);
        check (db < -60.0, "no low-frequency content generated");
    }

    std::printf ("%s\n", failures ? "FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
