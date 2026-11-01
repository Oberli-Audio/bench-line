// Filters, the Loudness stand-in and the BandBank stand-in (StandIns.md). Ported from the
// model's bandpass_sections, lowpass_sections, k_weighting_sos and the multirate part of analyse().
#include "BandBank.h"
#include "Loudness.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <utility>
#include <vector>

namespace benchline
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
using cplx = std::complex<double>;
cplx blt (cplx s, double r) { return (2.0 * r + s) / (2.0 * r - s); }
} // namespace

// ------------------------------------------------------------------ Filters.h
int designBandpass (double f1, double f2, double r, int N, Biquad* out)
{
    f2 = std::min (f2, kTopEdge * r);
    const double W1 = 2.0 * r * std::tan (kPi * f1 / r);
    const double W2 = 2.0 * r * std::tan (kPi * f2 / r);
    const double w0sq = W1 * W2, B = W2 - W1;
    std::vector<cplx> allp;
    for (int k = 0; k < N; ++k)
    {
        const cplx p = std::exp (cplx (0.0, kPi * (2.0 * k + N + 1) / (2.0 * N)));
        const cplx disc = std::sqrt (p * p * B * B - 4.0 * w0sq);
        allp.push_back ((p * B + disc) / 2.0);
        allp.push_back ((p * B - disc) / 2.0);
    }
    const double tol = 1e-9 * std::sqrt (w0sq);
    std::vector<std::pair<cplx, cplx>> pairs;
    std::vector<double> reals;
    for (auto s : allp)
        if (s.imag() > tol) pairs.push_back ({ s, std::conj (s) });
        else if (std::abs (s.imag()) <= tol) reals.push_back (s.real());
    std::sort (reals.begin(), reals.end());
    for (size_t i = 0; i + 1 < reals.size(); i += 2)
        pairs.push_back ({ cplx (reals[i]), cplx (reals[i + 1]) });
    const double wc = 2.0 * std::atan (std::sqrt (w0sq) / (2.0 * r));
    const cplx ejw = std::exp (cplx (0.0, wc));
    int n = 0;
    for (auto& pr : pairs)
    {
        if (n >= N) break;
        const cplx z1 = blt (pr.first, r), z2 = blt (pr.second, r);
        const double a1 = -(z1 + z2).real(), a2 = (z1 * z2).real();
        const cplx h = (1.0 - 1.0 / (ejw * ejw)) / (1.0 + a1 / ejw + a2 / (ejw * ejw));
        const double g = 1.0 / std::abs (h);
        out[n++] = Biquad { g, 0.0, -g, a1, a2 };
    }
    return n;
}

int designLowpass (double fc, double r, int N, Biquad* out)
{
    const double W = 2.0 * r * std::tan (kPi * fc / r);
    int n = 0;
    for (int k = 0; k < N; ++k)
    {
        const cplx p = W * std::exp (cplx (0.0, kPi * (2.0 * k + N + 1) / (2.0 * N)));
        if (p.imag() > 1e-12)
        {
            const cplx z = blt (p, r);
            const double a1 = -2.0 * z.real(), a2 = std::norm (z);
            const double g = (1.0 + a1 + a2) / 4.0;
            out[n++] = Biquad { g, 2.0 * g, g, a1, a2 };
        }
    }
    return n;
}

// ------------------------------------------------------------------ Loudness.h (stand-in)
void KWeighting::prepare (double fs) noexcept
{
    {   // stage 1: high shelf
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (kPi * f0 / fs);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        c.s[0] = Biquad { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                          2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
    }
    {   // stage 2: RLB high-pass
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (kPi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;
        c.s[1] = Biquad { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
    }
    c.n = 2;
}

double lufsOf (double power) noexcept { return power > 0.0 ? -0.691 + 10.0 * std::log10 (power) : -300.0; }

// ------------------------------------------------------------------ BandBank.h (stand-in)
double BandBank::centre (int b) { return 1000.0 * std::pow (10.0, (double) (b - 17) / 10.0); }
double BandBank::lo (int b) { return centre (b) * std::pow (10.0, -1.0 / 20.0); }
double BandBank::hi (int b) { return centre (b) * std::pow (10.0, 1.0 / 20.0); }

void BandBank::prepare (double fs)
{
    nStages = 1;
    for (int b = 0; b < kBands; ++b)
    {
        int s = 0;
        while (fs / std::pow (2.0, s + 1) >= kStageRatio * centre (b) && s + 1 < kMaxStages)
            ++s;
        stage[(size_t) b] = s;
        nStages = std::max (nStages, s + 1);
        const double r = fs / std::pow (2.0, s);
        bandM[(size_t) b].n = designBandpass (lo (b), hi (b), r, kBandOrder, bandM[(size_t) b].s.data());
        bandS[(size_t) b] = bandM[(size_t) b];
        // the clamped 20 kHz band at 44.1 kHz: restore the power of the missing width (pink-flat band)
        const double f2 = std::min (hi (b), kTopEdge * r);
        gain[(size_t) b] = std::log (hi (b) / lo (b)) / std::log (f2 / lo (b));
    }
    for (int s = 0; s + 1 < nStages; ++s)
    {
        const double r = fs / std::pow (2.0, s);
        decimM[(size_t) s].n = designLowpass (kDecimCutoff * r, r, kDecimOrder, decimM[(size_t) s].s.data());
        decimS[(size_t) s] = decimM[(size_t) s];
    }
    reset();
}

void BandBank::resetFilters() noexcept
{
    for (auto& c : bandM) c.reset();
    for (auto& c : bandS) c.reset();
    for (auto& c : decimM) c.reset();
    for (auto& c : decimS) c.reset();
}

void BandBank::reset() noexcept
{
    resetFilters();
    stageCount.fill (0);
    sumM.fill (0.0); sumS.fill (0.0); cnt.fill (0.0);
}

void BandBank::process (double m, double s, bool idle) noexcept
{
    double xm = m, xs = s;
    for (int st = 0; st < nStages; ++st)
    {
        const int64_t k = stageCount[(size_t) st]++;
        for (int b = 0; b < kBands; ++b)
        {
            if (stage[(size_t) b] != st) continue;
            if (! idle)
            {
                const double ym = bandM[(size_t) b].process (xm), ys = bandS[(size_t) b].process (xs);
                sumM[(size_t) b] += ym * ym;
                sumS[(size_t) b] += ys * ys;
            }
            cnt[(size_t) b] += 1.0;
        }
        if (st + 1 >= nStages) break;
        double dm = 0.0, ds = 0.0;
        if (! idle)
        {
            dm = decimM[(size_t) st].process (xm);
            ds = decimS[(size_t) st].process (xs);
        }
        if ((k & 1) != 0) break;   // only the even samples of stage st go on
        xm = dm; xs = ds;
    }
}

void BandBank::takeSubBlock (double* PM, double* PS) noexcept
{
    for (int b = 0; b < kBands; ++b)
    {
        const double c = std::max (cnt[(size_t) b], 1.0);
        PM[b] = gain[(size_t) b] * sumM[(size_t) b] / c;
        PS[b] = gain[(size_t) b] * sumS[(size_t) b] / c;
    }
    sumM.fill (0.0); sumS.fill (0.0); cnt.fill (0.0);
}
} // namespace benchline
