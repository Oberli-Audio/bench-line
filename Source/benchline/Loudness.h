#pragma once

/*  Bench Line's per-sample K-weighting (StandIns.md: oberli::Loudness measures windows and keeps
    its filters private; the engine filters every sample into its 400 ms frames).

    K-weighting (BS.1770) designed for any rate from the prototype, as dspkit.k_weighting_sos
    and libebur128 do: at 48 kHz the printed coefficients, at other rates the matched redesign
    (rule 17). Transposed direct form II in double. */

#include "Filters.h"

namespace benchline
{
class KWeighting
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept { c.reset(); }
    inline double process (double x) noexcept { return c.process (x); }
private:
    Cascade<2> c;
};

/** -0.691 + 10 log10(power); -300 for no power. */
double lufsOf (double power) noexcept;
} // namespace benchline
