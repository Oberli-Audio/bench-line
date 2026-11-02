#pragma once

/*  Biquads and the Butterworth designs of the model ("sound design/model/benchline_model.py":
    bandpass_sections, lowpass_sections). Used by the stand-ins (Loudness.h, BandBank.h) and by
    the crest regions, which are Bench Line's own. Double precision, transposed direct form II
    (as scipy's sosfilt). */

#include <array>
#include <cstddef>

namespace benchline
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0; }
};

template <int N>
struct Cascade
{
    std::array<Biquad, N> s {};
    int n = 0;
    inline double process (double x) noexcept
    {
        for (int i = 0; i < n; ++i)
            x = s[(size_t) i].process (x);
        return x;
    }
    void reset() noexcept { for (auto& q : s) q.reset(); }
};

inline constexpr double kTopEdge = 0.49;   // band upper edges clamp to 0.49 x rate (20 kHz band at 44.1 kHz)

/** Butterworth band-pass, prototype order N (band-pass order 2N), as N sections in `out`;
    zeros at z = +1 and -1, unity gain at the centre; f2 clamped to kTopEdge x rate. */
int designBandpass (double f1, double f2, double rate, int order, Biquad* out);
/** Butterworth low-pass of even order N as N/2 sections, unity DC gain. */
int designLowpass (double fc, double rate, int order, Biquad* out);
} // namespace benchline
