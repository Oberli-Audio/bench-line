#pragma once

/*  Bench Line - the analysis engine (G4 port of "sound design/model/benchline_model.py").
    Bench Line's own part; the K-weighting and the band bank come through the stand-ins
    Loudness.h and BandBank.h (StandIns.md).

    Every function here has a counterpart of the same name in the model, and every constant is
    the model's number (docs/analysis.md explains them once). The engine never touches audio: it
    runs on the worker thread (live) or a build thread (targets), never in processBlock.

    Structure (G1, docs/status.md):
      L/R -> M = (L+R)/2, S = (L-R)/2
      K-weighting of M and S (BS.1770, redesigned per rate)
      31 third-octave bands on M and S, order-3 Butterworth band-pass, multirate octave stages
      100 ms sub-blocks -> 400 ms frames at a 100 ms hop -> gate -> normalised band levels
      crest per region (50 ms windows) on M
      Tracker: exponentially weighted median per band (decaying histogram), region verdicts,
      suggestions; Target: percentile tables.
    Double precision throughout (the low bands, the long sums). No allocation after prepare(). */

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "BandBank.h"
#include "Loudness.h"

namespace benchline::dsp
{
// ------------------------------------------------------------------ constants (the model's)
inline constexpr int kBands = BandBank::kBands;
inline constexpr int kRegions = 6;
inline constexpr int kPercentiles = 11;
inline constexpr double kPercentileList[kPercentiles] = { 5, 10, 15, 20, 25, 50, 75, 80, 85, 90, 95 };
inline constexpr int kMedianIndex = 5;
inline constexpr int kRegionOrder = 2;
inline constexpr double kSubSeconds = 0.1;
inline constexpr int kFrameSub = 4;
inline constexpr int kShortTermSub = 30;
inline constexpr double kGateLufs = -40.0;
inline constexpr int kGateBack = 16;
inline constexpr int kGateAhead = 13;
inline constexpr double kCrestSeconds = 0.05;
inline constexpr double kLevelMin = -80.0, kLevelMax = 20.0;
inline constexpr double kCrestMin = 0.0, kCrestMax = 40.0;
inline constexpr double kBinDb = 0.1;
inline constexpr double kSuggestAfterS = 8.0;
inline constexpr double kSuggestLockoutS = 30.0;
inline constexpr double kSuggestFraction = 2.0 / 3.0;
inline constexpr double kSilentAfterS = 2.0;
inline constexpr double kSlopeDbPerOct = -1.5;
inline constexpr double kSlopeSigmaDb = 2.0;
inline constexpr double kSlopeOffsetDb = -18.5;
inline constexpr double kIdleZeroSeconds = 3.0;   // exact zeros this long: filters reset, no filtering (Q5)

extern const double kBandNominal[kBands];
extern const char* const kRegionNames[kRegions];
inline constexpr int kRegionFirst[kRegions] = { 0, 5, 10, 15, 20, 25 };
inline constexpr int kRegionLast[kRegions] = { 4, 9, 14, 19, 24, 30 };   // inclusive

inline double bandCentre (int b) { return BandBank::centre (b); }
inline double bandLo (int b) { return BandBank::lo (b); }
inline double bandHi (int b) { return BandBank::hi (b); }

// ------------------------------------------------------------------ features
struct SubBlock
{
    double zM = 0, zS = 0;
    std::array<double, kBands> PM {}, PS {};
    std::array<double, kRegions> crestSum {}, crestCnt {};
};

struct Frame
{
    int64_t j = 0;
    double readySeconds = 0;
    bool gated = false;
    double shortTerm = -300;
    std::array<double, kBands> tone {}, side {};
    std::array<double, kRegions> punch {};
};

/** Streaming engine: samples in, frames out (analyse() + frames() of the model). */
class Engine
{
public:
    void prepare (double sampleRate);
    void reset();
    /** Feeds n samples. onFrame is called for every emitted frame (gated or not). */
    void process (const float* left, const float* right, int n, const std::function<void (const Frame&)>& onFrame);
    /** End of input (a file): emits the remaining frames with the look-ahead they have. */
    void flush (const std::function<void (const Frame&)>& onFrame);
    double sampleRate() const noexcept { return fs; }
    int64_t samplesSeen() const noexcept { return pos; }
    /** Short-term loudness (last 30 sub-blocks) after the latest sub-block, LUFS. */
    double shortTermLufs() const noexcept { return lastShortTerm; }
    double momentaryLufs() const noexcept { return lastMomentary; }

private:
    void endSubBlock (const std::function<void (const Frame&)>& onFrame);
    void emitFrame (int64_t j, int64_t hi, const std::function<void (const Frame&)>& onFrame);
    const SubBlock& sub (int64_t i) const noexcept { return ring[(size_t) (i % kRing)]; }

    static constexpr int kRing = 64;
    double fs = 48000;
    int subLen = 4800, crestLen = 2400;
    BandBank bank;
    KWeighting kM, kS;
    std::array<Cascade<kRegionOrder>, kRegions> regionM;

    // running state
    int64_t pos = 0;         // input samples consumed
    int64_t subIndex = 0;    // sub-block being filled
    int inSub = 0;
    SubBlock acc;
    std::array<double, kRegions> winPeak {}, winSq {};
    int inWin = 0;
    std::array<SubBlock, kRing> ring {};
    std::array<double, kRing> zRing {};
    int64_t completed = 0;   // number of completed sub-blocks
    int64_t nextFrame = kFrameSub - 1;
    int64_t zeroRun = 0;
    bool idle = false;
    double lastShortTerm = -300, lastMomentary = -300;
};

// ------------------------------------------------------------------ targets
struct Target
{
    std::string name;
    int64_t frames = 0;
    bool hasTone = false, hasSide = false, hasPunch = false;
    std::array<std::array<double, kPercentiles>, kBands> tone {}, side {};
    std::array<std::array<double, kPercentiles>, kRegions> punch {};
};

/** Percentile tables over pooled frames (numpy 'linear'). values: one vector per band. */
void percentileTable (std::vector<std::vector<double>>& values, std::array<double, kPercentiles>* out);
Target slopeTarget();
/** (lower, median, upper) for a Tolerance in % from one stored row. */
std::array<double, 3> edges (const std::array<double, kPercentiles>& row, double tolerance);

/** Collects gated frames for a target build. */
struct TargetBuilder
{
    std::vector<std::vector<double>> tone, side, punch;
    TargetBuilder() : tone (kBands), side (kBands), punch (kRegions) {}
    void add (const Frame& f);
    void merge (const TargetBuilder& other);
    int64_t frames() const noexcept { return tone.empty() ? 0 : (int64_t) tone[0].size(); }
    bool build (const std::string& name, Target& out);
};

// ------------------------------------------------------------------ the mix tracker
enum class Verdict { none, inside, above, below };
enum ViewIndex { kTone = 0, kPunch = 1, kSide = 2 };   // the View parameter's order

struct Suggestion
{
    int region = -1;
    double hz = 0, gainDb = 0, q = 0;
    double sinceSeconds = 0;   // engine time when it appeared
    double sinceSong = -1;     // song position when it appeared (play-head), -1 unknown
    std::string text;
};

struct ViewState
{
    std::array<double, kBands> median {};   // NaN where empty; punch uses the first 6
    int count = 0;                          // bands (31) or regions (6)
    std::array<Verdict, kRegions> verdict {};
    std::array<double, kRegions> devDb {};
    std::array<Suggestion, 2> suggestions {};
    int numSuggestions = 0;
};

class Histogram
{
public:
    void configure (int k, double lo, double hi);
    void clear();
    void add (const double* values, double alpha);
    void median (double* out) const;
    const std::vector<double>& data() const { return h; }
private:
    int k = 0, nbins = 0;
    double lo = 0;
    std::vector<double> h;
};

class Tracker
{
public:
    Tracker();
    void reset();
    void setTarget (const Target* t) noexcept { target = t; }
    void setAverage (double seconds) noexcept { averageS = seconds; }
    void setTolerance (double pct) noexcept { tolerance = pct; }
    /** One frame at engine time `now` (seconds); `song` is the play-head position or -1. */
    void push (const Frame& f, double now, double song);
    /** Recomputes verdicts and suggestions without a new frame (Tolerance or target changed). */
    void refresh (double now, double song);
    const ViewState& view (int v) const noexcept { return views[(size_t) v]; }
    bool silent() const noexcept { return isSilent; }
    int64_t gatedFrames() const noexcept { return gated; }

private:
    void evaluate (int v, double now, double song);
    Suggestion suggest (int v, int r, const double* med, const std::array<double, 3>* e, int sign, double now, double song) const;

    const Target* target = nullptr;
    double averageS = 10.0, tolerance = 60.0;
    std::array<Histogram, 3> hist;
    std::array<ViewState, 3> views {};
    std::array<std::array<double, kRegions>, 3> outsideSince {}, lockedUntil {};
    std::array<std::array<bool, kRegions>, 3> outsideActive {};
    std::array<std::array<int, kRegions>, 3> shownSlot {};   // -1 not shown
    std::array<std::array<Suggestion, kRegions>, 3> shown {};
    double lastGated = -1e9;
    bool everGated = false, isSilent = true;
    int64_t gated = 0;
};

std::string hzLabel (double hz);
double halfDb (double v);
} // namespace benchline::dsp
