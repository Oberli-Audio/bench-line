#include "Analyser.h"

#include <cstring>

namespace benchline
{
Analyser::Analyser() : juce::Thread ("Bench Line analysis") {}

Analyser::~Analyser()
{
    stopThread (2000);
}

void Analyser::setSynchronous (bool sync)
{
    synchronous = sync;
    if (sync) stopThread (2000);
}

void Analyser::bindParameters (std::atomic<float>* average, std::atomic<float>* tolerance, std::atomic<float>* hold)
{
    averageParam = average;
    toleranceParam = tolerance;
    holdParam = hold;
}

void Analyser::prepare (double sampleRate, int maxBlock)
{
    {
        std::lock_guard<std::mutex> lock (workMutex);
        prepared.store (false);
        const int capacity = juce::jmax (2 * juce::jmax (1, maxBlock), (int) std::lround (sampleRate)) + 1;   // 1 s, in seconds (rule 12)
        fifoData.setSize (2, capacity, false, true, false);
        fifo.setTotalSize (capacity);
        fifo.reset();
        scratchL.assign (4096, 0.0f);
        scratchR.assign (4096, 0.0f);
        engine.prepare (sampleRate);
        tracker.reset();
        framesAnalysed = 0;
        lastNow = 0;
        overflow.store (0);
        pushedSamples.store (0);
        targetDirty = true;
        prepared.store (true);
    }
    if (! synchronous && ! isThreadRunning())
        startThread (juce::Thread::Priority::low);
}

void Analyser::push (const float* left, const float* right, int n) noexcept
{
    if (n <= 0 || ! prepared.load (std::memory_order_acquire)) return;
    int start1, size1, start2, size2;
    fifo.prepareToWrite (n, start1, size1, start2, size2);
    const int written = size1 + size2;
    if (size1 > 0)
    {
        fifoData.copyFrom (0, start1, left, size1);
        fifoData.copyFrom (1, start1, right != nullptr ? right : left, size1);
    }
    if (size2 > 0)
    {
        fifoData.copyFrom (0, start2, left + size1, size2);
        fifoData.copyFrom (1, start2, (right != nullptr ? right : left) + size1, size2);
    }
    fifo.finishedWrite (written);
    if (written < n) overflow.fetch_add (n - written, std::memory_order_relaxed);
    pushedSamples.fetch_add (n, std::memory_order_relaxed);
}

void Analyser::setSongPosition (double seconds, bool playing) noexcept
{
    songAt.store (playing ? seconds : -1.0, std::memory_order_relaxed);
    songAtSample.store (pushedSamples.load (std::memory_order_relaxed), std::memory_order_relaxed);
}

// The message thread's side: shareMutex only, so a read never waits for a pump (the worker
// takes up the change at the start of its next one).
void Analyser::setTarget (std::shared_ptr<const dsp::Target> t, const juce::String& file)
{
    std::lock_guard<std::mutex> lock (shareMutex);
    target = std::move (t);
    targetFile = file;
    targetChanged = true;
}

std::shared_ptr<const dsp::Target> Analyser::getTarget() const
{
    std::lock_guard<std::mutex> lock (shareMutex);
    return target;
}

juce::String Analyser::getTargetFile() const
{
    std::lock_guard<std::mutex> lock (shareMutex);
    return targetFile;
}

Analyser::HeldState Analyser::heldState() const
{
    std::lock_guard<std::mutex> lock (shareMutex);
    return held;
}

void Analyser::restoreHeld (const HeldState& h)
{
    std::lock_guard<std::mutex> lock (shareMutex);
    held = h;
    heldRestored = true;   // the hold parameter is restored with the same state, no new snapshot
}

int Analyser::pump()
{
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    std::lock_guard<std::mutex> lock (workMutex);
    if (! prepared.load()) return 0;
    juce::ScopedNoDenormals noDenormals;
    {
        std::lock_guard<std::mutex> share (shareMutex);
        if (targetChanged) { workTarget = target; targetChanged = false; targetDirty = true; }
        if (heldRestored) { workHeld = held; lastHold = held.active; heldRestored = false; targetDirty = true; }
    }
    tracker.setTarget (workTarget.get());
    if (averageParam != nullptr) tracker.setAverage (juce::jlimit (1.0, 60.0, (double) averageParam->load()));
    const float tol = toleranceParam != nullptr ? toleranceParam->load() : 60.0f;
    tracker.setTolerance (tol);
    const double fs = engine.sampleRate();
    int frames = 0;
    auto handle = [&] (const dsp::Frame& f)
    {
        // song position of the frame's centre: the play-head at the last block start, moved by
        // the samples between that block and this frame
        double song = -1.0;
        const double ph = songAt.load (std::memory_order_relaxed);
        if (ph >= 0.0)
            song = juce::jmax (0.0, ph + ((double) (f.j - 1) * dsp::kSubSeconds * fs - (double) songAtSample.load (std::memory_order_relaxed)) / fs);
        tracker.push (f, f.readySeconds, song);
        lastNow = f.readySeconds;
        lastSong = song;
        ++framesAnalysed;
        ++frames;
        if (onFrame) onFrame (f, tracker);
    };
    std::function<void (const dsp::Frame&)> cb = handle;
    // only what is there now: audio arriving faster than the worker drains it (a host rendering
    // ahead, pluginval) must not keep one pump, and workMutex, going for ever
    for (int budget = fifo.getNumReady(); budget > 0;)
    {
        int start1, size1, start2, size2;
        const int want = juce::jmin (budget, fifo.getNumReady(), (int) scratchL.size());
        if (want <= 0) break;
        budget -= want;
        fifo.prepareToRead (want, start1, size1, start2, size2);
        std::memcpy (scratchL.data(), fifoData.getReadPointer (0, start1), sizeof (float) * (size_t) size1);
        std::memcpy (scratchR.data(), fifoData.getReadPointer (1, start1), sizeof (float) * (size_t) size1);
        if (size2 > 0)
        {
            std::memcpy (scratchL.data() + size1, fifoData.getReadPointer (0, start2), sizeof (float) * (size_t) size2);
            std::memcpy (scratchR.data() + size1, fifoData.getReadPointer (1, start2), sizeof (float) * (size_t) size2);
        }
        fifo.finishedRead (size1 + size2);
        engine.process (scratchL.data(), scratchR.data(), size1 + size2, cb);
    }
    // Hold: the rising edge freezes the current medians, the falling edge drops them
    const bool hold = holdParam != nullptr && holdParam->load() >= 0.5f;
    if (hold != lastHold)
    {
        workHeld.active = hold;
        if (hold)
        {
            for (int v = 0; v < 3; ++v) workHeld.medians[(size_t) v] = tracker.view (v).median;
            workHeld.sinceSong = lastSong >= 0 ? lastSong : lastNow;
        }
        lastHold = hold;
        targetDirty = true;
        std::lock_guard<std::mutex> share (shareMutex);
        held = workHeld;
    }
    const bool changed = targetDirty || ! juce::exactlyEqual (tol, lastTolerance);
    if (frames == 0 && changed)
        tracker.refresh (lastNow, lastSong);   // Tolerance or target changed: edges only, no re-analysis
    lastTolerance = tol;
    targetDirty = false;
    const auto seen = engine.samplesSeen();
    const bool bypass = bypassed.load();
    // publish when something the editor shows moved: a frame, a sub-block (the meter), a change
    if (frames > 0 || changed || seen / 4096 != lastPublishedSamples / 4096 || bypass != lastPublishedBypass || snap.serial == 0)
    {
        publish();
        lastPublishedSamples = seen;
        lastPublishedBypass = bypass;
    }
    workerTime += (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    return frames;
}

void Analyser::publish()
{
    Snapshot s;
    const double tol = toleranceParam != nullptr ? (double) toleranceParam->load() : 60.0;
    for (int v = 0; v < 3; ++v)
    {
        const auto& src = tracker.view (v);
        auto& dst = s.views[(size_t) v];
        dst.count = src.count;
        dst.median = src.median;
        dst.verdict = src.verdict;
        dst.devDb = src.devDb;
        dst.numSugg = src.numSuggestions;
        for (int i = 0; i < src.numSuggestions; ++i)
        {
            const auto& a = src.suggestions[(size_t) i];
            auto& b = dst.sugg[(size_t) i];
            b.region = a.region; b.hz = a.hz; b.gainDb = a.gainDb; b.q = a.q;
            b.sinceSeconds = a.sinceSeconds; b.sinceSong = a.sinceSong;
            std::strncpy (b.text, a.text.c_str(), Snapshot::kTextLen - 1);
        }
        if (workTarget != nullptr)
        {
            const std::array<double, dsp::kPercentiles>* rows = nullptr;
            if (v == dsp::kTone && workTarget->hasTone) rows = workTarget->tone.data();
            if (v == dsp::kSide && workTarget->hasSide) rows = workTarget->side.data();
            if (v == dsp::kPunch && workTarget->hasPunch) rows = workTarget->punch.data();
            dst.hasTarget = rows != nullptr;
            if (rows != nullptr)
                for (int i = 0; i < dst.count; ++i)
                {
                    const auto e = dsp::edges (rows[i], tol);
                    dst.lo[(size_t) i] = e[0]; dst.mid[(size_t) i] = e[1]; dst.hi[(size_t) i] = e[2];
                }
        }
    }
    s.holdActive = workHeld.active;
    s.held = workHeld.medians;
    s.heldSinceSong = workHeld.sinceSong;
    s.shortTermLufs = engine.shortTermLufs();
    s.momentaryLufs = engine.momentaryLufs();
    s.engineSeconds = (double) engine.samplesSeen() / engine.sampleRate();
    s.silent = tracker.silent();
    s.bypassed = bypassed.load();
    s.gatedFrames = tracker.gatedFrames();
    s.framesAnalysed = framesAnalysed;
    s.overflowSamples = overflow.load();
    s.sampleRate = engine.sampleRate();
    if (workTarget != nullptr) std::strncpy (s.targetName, workTarget->name.c_str(), sizeof (s.targetName) - 1);
    std::lock_guard<std::mutex> lock (snapMutex);
    s.serial = snap.serial + 1;
    snap = s;
}

Snapshot Analyser::snapshot() const
{
    std::lock_guard<std::mutex> lock (snapMutex);
    return snap;
}

void Analyser::run()
{
    while (! threadShouldExit())
    {
        pump();
        wait (15);
    }
}
} // namespace benchline
