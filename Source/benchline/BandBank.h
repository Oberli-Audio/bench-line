#pragma once

/*  Bench Line's band analysis (StandIns.md: oberli::Spectrum is an FFT on base-2 bands, which G1
    decision 1 rejected; the G5 null test holds this bank to the model at every rate).

    31 base-10 third-octave bands (20 Hz-20 kHz) on two signals (M and S), order-3 Butterworth
    band-pass per band, multirate: a band runs at the lowest octave stage whose rate is >= 5 x
    its centre; stage s+1 is stage s through an 8th-order Butterworth low-pass at 0.2 x its rate,
    every second sample (G1 decision 1). Output: the mean square per band over a sub-block. */

#include "Filters.h"

#include <cstdint>

namespace benchline
{
class BandBank
{
public:
    static constexpr int kBands = 31;
    static constexpr int kMaxStages = 16;
    static double centre (int b);
    static double lo (int b);
    static double hi (int b);

    void prepare (double sampleRate);
    void reset() noexcept;
    /** Clears the filter states only (idle): counters and the decimation phase keep running. */
    void resetFilters() noexcept;
    /** One input sample of M and S. idle: the input has been exact zeros for long enough;
        counters advance, no filtering (the filters were reset). */
    void process (double m, double s, bool idle) noexcept;
    /** Mean squares per band since the last call (sub-block end), then clears. */
    void takeSubBlock (double* PM, double* PS) noexcept;

private:
    static constexpr double kStageRatio = 5.0, kDecimCutoff = 0.2;
    static constexpr int kDecimOrder = 8, kBandOrder = 3;
    int nStages = 1;
    std::array<int, kBands> stage {};
    std::array<double, kBands> gain {};
    std::array<Cascade<kBandOrder>, kBands> bandM, bandS;
    std::array<Cascade<kDecimOrder / 2>, kMaxStages> decimM, decimS;
    std::array<int64_t, kMaxStages> stageCount {};
    std::array<double, kBands> sumM {}, sumS {}, cnt {};
};
} // namespace benchline
