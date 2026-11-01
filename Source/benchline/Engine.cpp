#include "Engine.h"

#include <algorithm>
#include <complex>
#include <cstdarg>
#include <cstdio>

namespace benchline::dsp
{
const double kBandNominal[kBands] = { 20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
                                      1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000 };
const char* const kRegionNames[kRegions] = { "Sub", "Low", "Low-mid", "Mid", "Presence", "Air" };

namespace
{
// norm.ppf of kPercentileList / 100 (the plain slope target's Gaussian spread)
constexpr double kNormQuantile[kPercentiles] = { -1.6448536269514729, -1.2815515655446004, -1.0364333894937898,
                                                 -0.8416212335729143, -0.6744897501960817, 0.0, 0.6744897501960817,
                                                 0.8416212335729143, 1.0364333894937898, 1.2815515655446004,
                                                 1.6448536269514729 };

std::string format (const char* fmt, ...) __attribute__ ((format (printf, 1, 2)));
std::string format (const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start (ap, fmt);
    std::vsnprintf (buf, sizeof (buf), fmt, ap);
    va_end (ap);
    return buf;
}
} // namespace

// ------------------------------------------------------------------ engine
void Engine::prepare (double sampleRate)
{
    fs = sampleRate;
    subLen = (int) std::lround (kSubSeconds * fs);
    crestLen = (int) std::lround (kCrestSeconds * fs);
    bank.prepare (fs);
    for (int r = 0; r < kRegions; ++r)
        regionM[(size_t) r].n = designBandpass (bandLo (kRegionFirst[r]), bandHi (kRegionLast[r]), fs, kRegionOrder, regionM[(size_t) r].s.data());
    kM.prepare (fs);
    kS.prepare (fs);
    reset();
}

void Engine::reset()
{
    bank.reset();
    for (auto& c : regionM) c.reset();
    kM.reset();
    kS.reset();
    pos = 0; subIndex = 0; inSub = 0; inWin = 0;
    acc = SubBlock {};
    winPeak.fill (0.0);
    winSq.fill (0.0);
    completed = 0;
    nextFrame = kFrameSub - 1;
    zeroRun = 0;
    idle = false;
    lastShortTerm = lastMomentary = -300;
}

void Engine::process (const float* left, const float* right, int n, const std::function<void (const Frame&)>& onFrame)
{
    const int64_t idleAfter = (int64_t) std::llround (kIdleZeroSeconds * fs);
    for (int i = 0; i < n; ++i)
    {
        const double L = left[i], R = right != nullptr ? right[i] : left[i];
        if (L == 0.0 && R == 0.0)
        {
            if (++zeroRun >= idleAfter && ! idle)
            {
                // three seconds of exact zeros: every filter has rung out; reset and stop filtering (Q5)
                bank.resetFilters();
                for (auto& c : regionM) c.reset();
                kM.reset(); kS.reset();
                idle = true;
            }
        }
        else
        {
            zeroRun = 0;
            idle = false;
        }
        const double M = 0.5 * (L + R), S = 0.5 * (L - R);
        if (! idle)
        {
            const double km = kM.process (M), ks = kS.process (S);
            acc.zM += km * km;
            acc.zS += ks * ks;
        }
        bank.process (M, S, idle);
        // crest windows (region band-passes on M at the full rate)
        for (int r = 0; r < kRegions; ++r)
        {
            const double y = idle ? 0.0 : regionM[(size_t) r].process (M);
            winPeak[(size_t) r] = std::max (winPeak[(size_t) r], std::abs (y));
            winSq[(size_t) r] += y * y;
        }
        if (++inWin == crestLen)
        {
            for (int r = 0; r < kRegions; ++r)
            {
                const double rms = std::sqrt (winSq[(size_t) r] / crestLen);
                if (rms > 1e-10)
                {
                    acc.crestSum[(size_t) r] += 20.0 * std::log10 (std::max (winPeak[(size_t) r], 1e-30) / std::max (rms, 1e-30));
                    acc.crestCnt[(size_t) r] += 1.0;
                }
                winPeak[(size_t) r] = 0.0;
                winSq[(size_t) r] = 0.0;
            }
            inWin = 0;
        }
        ++pos;
        if (++inSub == subLen)
            endSubBlock (onFrame);
    }
}

void Engine::endSubBlock (const std::function<void (const Frame&)>& onFrame)
{
    SubBlock sb;
    sb.zM = acc.zM / subLen;
    sb.zS = acc.zS / subLen;
    bank.takeSubBlock (sb.PM.data(), sb.PS.data());
    sb.crestSum = acc.crestSum;
    sb.crestCnt = acc.crestCnt;
    ring[(size_t) (completed % kRing)] = sb;
    zRing[(size_t) (completed % kRing)] = 2.0 * (sb.zM + sb.zS);
    ++completed;
    acc = SubBlock {};
    inSub = 0;
    ++subIndex;
    // the Output meter: short-term (last 30, fewer at the start) and momentary (last 4)
    {
        double sum = 0.0;
        const int64_t n30 = std::min<int64_t> (kShortTermSub, completed);
        for (int64_t i = completed - n30; i < completed; ++i) sum += zRing[(size_t) (i % kRing)];
        lastShortTerm = lufsOf (sum / (double) n30);
        sum = 0.0;
        const int64_t n4 = std::min<int64_t> (kFrameSub, completed);
        for (int64_t i = completed - n4; i < completed; ++i) sum += zRing[(size_t) (i % kRing)];
        lastMomentary = lufsOf (sum / (double) n4);
    }
    while (nextFrame + kGateAhead <= completed - 1)
    {
        emitFrame (nextFrame, nextFrame + kGateAhead, onFrame);
        ++nextFrame;
    }
}

void Engine::flush (const std::function<void (const Frame&)>& onFrame)
{
    while (nextFrame <= completed - 1)
    {
        emitFrame (nextFrame, completed - 1, onFrame);
        ++nextFrame;
    }
}

void Engine::emitFrame (int64_t j, int64_t hi, const std::function<void (const Frame&)>& onFrame)
{
    Frame f;
    f.j = j;
    f.readySeconds = (double) (hi + 1) * kSubSeconds;
    const int64_t lo = std::max<int64_t> (0, j - kGateBack);
    double zsum = 0.0;
    for (int64_t i = lo; i <= hi; ++i) zsum += zRing[(size_t) (i % kRing)];
    f.shortTerm = lufsOf (zsum / (double) (hi + 1 - lo));
    double zf = 0.0, zmid = 0.0;
    std::array<double, kBands> PT {}, PSd {};
    std::array<double, kRegions> cs {}, cc {};
    for (int64_t i = j - kFrameSub + 1; i <= j; ++i)
    {
        const auto& s = sub (i);
        zf += zRing[(size_t) (i % kRing)];
        zmid += s.zM;
        for (int b = 0; b < kBands; ++b)
        {
            PT[(size_t) b] += s.PM[(size_t) b] + s.PS[(size_t) b];
            PSd[(size_t) b] += s.PS[(size_t) b];
        }
        for (int r = 0; r < kRegions; ++r)
        {
            cs[(size_t) r] += s.crestSum[(size_t) r];
            cc[(size_t) r] += s.crestCnt[(size_t) r];
        }
    }
    zf /= kFrameSub;
    zmid /= kFrameSub;
    f.gated = f.shortTerm >= kGateLufs && zf > 0.0 && lufsOf (zf) >= kGateLufs;
    if (f.gated)
    {
        const double lt = lufsOf (zf), lm = lufsOf (std::max (zmid, 1e-30));
        for (int b = 0; b < kBands; ++b)
        {
            f.tone[(size_t) b] = std::clamp (10.0 * std::log10 (PT[(size_t) b] / kFrameSub + 1e-30) - lt, kLevelMin, kLevelMax);
            f.side[(size_t) b] = std::clamp (10.0 * std::log10 (PSd[(size_t) b] / kFrameSub + 1e-30) - lm, kLevelMin, kLevelMax);
        }
        for (int r = 0; r < kRegions; ++r)
            f.punch[(size_t) r] = std::clamp (cc[(size_t) r] > 0 ? cs[(size_t) r] / std::max (cc[(size_t) r], 1.0) : 0.0, kCrestMin, kCrestMax);
    }
    onFrame (f);
}

// ------------------------------------------------------------------ targets
void percentileTable (std::vector<std::vector<double>>& values, std::array<double, kPercentiles>* out)
{
    for (size_t k = 0; k < values.size(); ++k)
    {
        auto& v = values[k];
        std::sort (v.begin(), v.end());
        const size_t n = v.size();
        for (int p = 0; p < kPercentiles; ++p)
        {
            if (n == 0) { out[k][(size_t) p] = 0.0; continue; }
            const double posn = kPercentileList[p] / 100.0 * (double) (n - 1);
            const size_t i = (size_t) std::floor (posn);
            const double frac = posn - (double) i;
            out[k][(size_t) p] = i + 1 < n ? v[i] + (v[i + 1] - v[i]) * frac : v[i];
        }
    }
}

Target slopeTarget()
{
    Target t;
    t.name = "Plain slope";
    t.hasTone = true;
    for (int b = 0; b < kBands; ++b)
    {
        const double med = kSlopeOffsetDb + kSlopeDbPerOct * std::log2 (bandCentre (b) / 1000.0);
        for (int p = 0; p < kPercentiles; ++p)
            t.tone[(size_t) b][(size_t) p] = med + kSlopeSigmaDb * kNormQuantile[p];
    }
    return t;
}

std::array<double, 3> edges (const std::array<double, kPercentiles>& row, double tolerance)
{
    auto interp = [&] (double x)
    {
        if (x <= kPercentileList[0]) return row[0];
        for (int i = 0; i + 1 < kPercentiles; ++i)
            if (x <= kPercentileList[i + 1])
            {
                const double t = (x - kPercentileList[i]) / (kPercentileList[i + 1] - kPercentileList[i]);
                return row[(size_t) i] + (row[(size_t) i + 1] - row[(size_t) i]) * t;
            }
        return row[kPercentiles - 1];
    };
    return { interp (50.0 - tolerance / 2.0), row[kMedianIndex], interp (50.0 + tolerance / 2.0) };
}

void TargetBuilder::add (const Frame& f)
{
    if (! f.gated) return;
    for (int b = 0; b < kBands; ++b)
    {
        tone[(size_t) b].push_back (f.tone[(size_t) b]);
        side[(size_t) b].push_back (f.side[(size_t) b]);
    }
    for (int r = 0; r < kRegions; ++r)
        punch[(size_t) r].push_back (f.punch[(size_t) r]);
}

void TargetBuilder::merge (const TargetBuilder& o)
{
    for (size_t k = 0; k < tone.size(); ++k) { tone[k].insert (tone[k].end(), o.tone[k].begin(), o.tone[k].end()); side[k].insert (side[k].end(), o.side[k].begin(), o.side[k].end()); }
    for (size_t k = 0; k < punch.size(); ++k) punch[k].insert (punch[k].end(), o.punch[k].begin(), o.punch[k].end());
}

bool TargetBuilder::build (const std::string& name, Target& out)
{
    if (frames() == 0) return false;
    out = Target {};
    out.name = name;
    out.frames = frames();
    out.hasTone = out.hasSide = out.hasPunch = true;
    percentileTable (tone, out.tone.data());
    percentileTable (side, out.side.data());
    percentileTable (punch, out.punch.data());
    return true;
}

// ------------------------------------------------------------------ histogram and tracker
void Histogram::configure (int kk, double lo_, double hi_)
{
    k = kk;
    lo = lo_;
    nbins = (int) std::lround ((hi_ - lo_) / kBinDb);
    h.assign ((size_t) (k * nbins), 0.0);
}

void Histogram::clear() { std::fill (h.begin(), h.end(), 0.0); }

void Histogram::add (const double* values, double alpha)
{
    for (auto& x : h) x *= alpha;
    for (int i = 0; i < k; ++i)
    {
        const int idx = std::clamp ((int) std::floor ((values[i] - lo) / kBinDb), 0, nbins - 1);
        h[(size_t) (i * nbins + idx)] += 1.0;
    }
}

void Histogram::median (double* out) const
{
    for (int i = 0; i < k; ++i)
    {
        const double* row = h.data() + (size_t) (i * nbins);
        double W = 0.0;
        for (int b = 0; b < nbins; ++b) W += row[b];
        if (W <= 0.0) { out[i] = std::nan (""); continue; }
        double c = 0.0, before = 0.0;
        int b = 0;
        for (; b < nbins; ++b)
        {
            before = c;
            c += row[b];
            if (c >= W / 2.0) break;
        }
        b = std::min (b, nbins - 1);
        out[i] = lo + ((double) b + (W / 2.0 - before) / row[b]) * kBinDb;
    }
}

Tracker::Tracker()
{
    hist[kTone].configure (kBands, kLevelMin, kLevelMax);
    hist[kSide].configure (kBands, kLevelMin, kLevelMax);
    hist[kPunch].configure (kRegions, kCrestMin, kCrestMax);
    reset();
}

void Tracker::reset()
{
    for (auto& hh : hist) hh.clear();
    for (int v = 0; v < 3; ++v)
    {
        views[(size_t) v] = ViewState {};
        views[(size_t) v].count = v == kPunch ? kRegions : kBands;
        views[(size_t) v].median.fill (std::nan (""));
        outsideActive[(size_t) v].fill (false);
        outsideSince[(size_t) v].fill (0.0);
        lockedUntil[(size_t) v].fill (-1e9);
        shownSlot[(size_t) v].fill (-1);
    }
    lastGated = -1e9;
    everGated = false;
    isSilent = true;
    gated = 0;
}

void Tracker::push (const Frame& f, double now, double song)
{
    if (f.gated)
    {
        const double alpha = std::exp (-kSubSeconds / averageS);
        hist[kTone].add (f.tone.data(), alpha);
        hist[kSide].add (f.side.data(), alpha);
        hist[kPunch].add (f.punch.data(), alpha);
        lastGated = now;
        everGated = true;
        ++gated;
    }
    refresh (now, song);
}

void Tracker::refresh (double now, double song)
{
    isSilent = ! everGated || now - lastGated > kSilentAfterS;
    for (int v : { (int) kTone, (int) kSide, (int) kPunch })
        evaluate (v, now, song);
}

void Tracker::evaluate (int v, double now, double song)
{
    auto& st = views[(size_t) v];
    hist[(size_t) v].median (st.median.data());
    st.verdict.fill (Verdict::none);
    st.devDb.fill (0.0);
    st.numSuggestions = 0;
    const int k = st.count;
    const std::array<std::array<double, kPercentiles>, kBands>* tab = nullptr;
    const std::array<std::array<double, kPercentiles>, kRegions>* ptab = nullptr;
    if (target != nullptr)
    {
        if (v == kTone && target->hasTone) tab = &target->tone;
        if (v == kSide && target->hasSide) tab = &target->side;
        if (v == kPunch && target->hasPunch) ptab = &target->punch;
    }
    bool allNan = true;
    for (int i = 0; i < k; ++i) allNan = allNan && std::isnan (st.median[(size_t) i]);
    if ((tab == nullptr && ptab == nullptr) || isSilent || allNan)
        return;
    std::array<std::array<double, 3>, kBands> e {};
    for (int i = 0; i < k; ++i)
        e[(size_t) i] = edges (tab != nullptr ? (*tab)[(size_t) i] : (*ptab)[(size_t) i], tolerance);
    std::array<double, kRegions> excess {};
    for (int r = 0; r < kRegions; ++r)
    {
        const int i0 = v == kPunch ? r : kRegionFirst[r], i1 = v == kPunch ? r : kRegionLast[r];
        double dr = 0, up = 0, dn = 0;
        for (int i = i0; i <= i1; ++i)
        {
            dr += st.median[(size_t) i] - e[(size_t) i][1];
            up += e[(size_t) i][2] - e[(size_t) i][1];
            dn += e[(size_t) i][1] - e[(size_t) i][0];
        }
        const double n = (double) (i1 - i0 + 1);
        dr /= n; up /= n; dn /= n;
        const Verdict verdict = dr > up ? Verdict::above : (-dr > dn ? Verdict::below : Verdict::inside);
        st.verdict[(size_t) r] = verdict;
        st.devDb[(size_t) r] = dr;
        if (verdict == Verdict::inside)
        {
            outsideActive[(size_t) v][(size_t) r] = false;
            if (shownSlot[(size_t) v][(size_t) r] >= 0)
            {
                shownSlot[(size_t) v][(size_t) r] = -1;
                lockedUntil[(size_t) v][(size_t) r] = now + kSuggestLockoutS;
            }
        }
        else
        {
            excess[(size_t) r] = verdict == Verdict::above ? dr - up : -dr - dn;
            if (! outsideActive[(size_t) v][(size_t) r])
            {
                outsideActive[(size_t) v][(size_t) r] = true;
                outsideSince[(size_t) v][(size_t) r] = now;
            }
        }
    }
    std::array<int, kRegions> cand {};
    int nc = 0;
    for (int r = 0; r < kRegions; ++r)
        if (excess[(size_t) r] > 0 && now - outsideSince[(size_t) v][(size_t) r] >= kSuggestAfterS
            && (shownSlot[(size_t) v][(size_t) r] >= 0 || now >= lockedUntil[(size_t) v][(size_t) r]))
            cand[(size_t) nc++] = r;
    std::stable_sort (cand.begin(), cand.begin() + nc, [&] (int a, int b) { return excess[(size_t) a] > excess[(size_t) b]; });
    const int keep = std::min (nc, 2);
    for (int r = 0; r < kRegions; ++r)
    {
        bool kept = false;
        for (int i = 0; i < keep; ++i) kept = kept || cand[(size_t) i] == r;
        if (! kept) shownSlot[(size_t) v][(size_t) r] = -1;
    }
    for (int i = 0; i < keep; ++i)
    {
        const int r = cand[(size_t) i];
        const int sign = st.verdict[(size_t) r] == Verdict::above ? 1 : -1;
        Suggestion s = suggest (v, r, st.median.data(), e.data(), sign, now, song);
        if (shownSlot[(size_t) v][(size_t) r] >= 0)
        {
            s.sinceSeconds = shown[(size_t) v][(size_t) r].sinceSeconds;
            s.sinceSong = shown[(size_t) v][(size_t) r].sinceSong;
        }
        shown[(size_t) v][(size_t) r] = s;
        shownSlot[(size_t) v][(size_t) r] = i;
        st.suggestions[(size_t) i] = s;
    }
    st.numSuggestions = keep;
}

std::string hzLabel (double hz)
{
    if (hz < 1000.0) return format ("%.0f Hz", hz);
    auto s = format ("%.1f kHz", hz / 1000.0);
    const auto at = s.find (".0 kHz");
    if (at != std::string::npos) s.replace (at, 6, " kHz");
    return s;
}

double halfDb (double v) { return std::copysign (std::floor (std::abs (v) * 2.0 + 0.5) / 2.0, v); }

Suggestion Tracker::suggest (int v, int r, const double* med, const std::array<double, 3>* e, int sign, double now, double song) const
{
    Suggestion out;
    out.region = r;
    out.sinceSeconds = now;
    out.sinceSong = song;
    const char* aboveBelow = sign > 0 ? "above" : "below";
    if (v == kPunch)
    {
        const double d = med[r] - e[r][1];
        const double beyond = sign > 0 ? d - (e[r][2] - e[r][1]) : -d - (e[r][1] - e[r][0]);
        out.text = format ("%s: %s, crest %.1f dB %s the band; %s.", kRegionNames[r], sign > 0 ? "spiky" : "squashed", beyond, aboveBelow,
                           sign > 0 ? "could take more compression or a little saturation" : "try less compression or a slower attack");
        return out;
    }
    auto dev = [&] (int i) { return med[i] - e[i][1]; };
    int b = kRegionFirst[r];
    for (int i = kRegionFirst[r]; i <= kRegionLast[r]; ++i)
        if (sign * dev (i) > sign * dev (b)) b = i;
    const double beyond = sign * dev (b) - (sign > 0 ? e[b][2] - e[b][1] : e[b][1] - e[b][0]);
    auto outside = [&] (int i) { return sign > 0 ? dev (i) > e[i][2] - e[i][1] : -dev (i) > e[i][1] - e[i][0]; };
    int k0 = b, k1 = b;
    while (k0 - 1 >= 0 && outside (k0 - 1)) --k0;
    while (k1 + 1 < kBands && outside (k1 + 1)) ++k1;
    const double w = (double) (k1 - k0 + 1) / 3.0;
    out.q = std::floor (10.0 * std::sqrt (std::pow (2.0, w)) / (std::pow (2.0, w) - 1.0) + 0.5) / 10.0;
    double gain = -sign * halfDb (kSuggestFraction * sign * dev (b));
    if (gain == 0.0) gain = -sign * 0.5;
    out.gainDb = gain;
    out.hz = kBandNominal[b];
    const auto hz = hzLabel (out.hz);
    if (v == kSide)
    {
        const auto move = r <= 1 ? format ("try about %+.1f dB in the side around %s (mid/side EQ), or mono the bass below it", gain, hz.c_str())
                                 : format ("try about %+.1f dB in the side around %s (mid/side EQ)", gain, hz.c_str());
        out.text = format ("%s, side: %.1f dB %s the band around %s; %s.", kRegionNames[r], beyond, aboveBelow, hz.c_str(), move.c_str());
    }
    else if (r == 0 || r == 5)
        out.text = format ("%s: %.1f dB %s the band around %s; try a %s shelf, %+.1f dB at %s.", kRegionNames[r], beyond, aboveBelow, hz.c_str(),
                           r == 0 ? "low" : "high", gain, hz.c_str());
    else
        out.text = format ("%s: %.1f dB %s the band around %s; try about %+.1f dB, Q %.1f.", kRegionNames[r], beyond, aboveBelow, hz.c_str(), gain, out.q);
    return out;
}
} // namespace benchline::dsp
