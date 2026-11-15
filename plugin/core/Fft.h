// Small radix-2 complex FFT. No dependencies, allocation only in init().
#pragma once
#include <cmath>
#include <vector>

namespace pfx {

class Fft {
public:
    void init (int size)
    {
        n = size;
        int levels = 0;
        while ((1 << levels) < n) ++levels;
        rev.resize ((size_t) n);
        co.resize ((size_t) n / 2);
        si.resize ((size_t) n / 2);
        for (int i = 0; i < n; ++i) {
            int r = 0, x = i;
            for (int j = 0; j < levels; ++j) { r = (r << 1) | (x & 1); x >>= 1; }
            rev[(size_t) i] = r;
        }
        const double tau = 6.283185307179586;
        for (int i = 0; i < n / 2; ++i) {
            co[(size_t) i] = std::cos (tau * i / n);
            si[(size_t) i] = std::sin (tau * i / n);
        }
    }

    void run (double* re, double* im) const
    {
        for (int i = 0; i < n; ++i) {
            const int j = rev[(size_t) i];
            if (j > i) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
        }
        for (int size = 2; size <= n; size <<= 1) {
            const int half = size >> 1, step = n / size;
            for (int i = 0; i < n; i += size)
                for (int j = 0, k = 0; j < half; ++j, k += step) {
                    const int a = i + j, b = a + half;
                    const double tr = re[b] * co[(size_t) k] + im[b] * si[(size_t) k];
                    const double ti = im[b] * co[(size_t) k] - re[b] * si[(size_t) k];
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr;        im[a] += ti;
                }
        }
    }

private:
    int n = 0;
    std::vector<int> rev;
    std::vector<double> co, si;
};

} // namespace pfx
