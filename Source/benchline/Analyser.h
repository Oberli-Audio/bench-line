#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

#include "Engine.h"

/*  Bench Line - the analysis side of the processor (G1 decision 7, rule 1).

    Audio thread: push() copies the block into a pre-allocated single-producer FIFO
    (juce::AbstractFifo, 1 s at the prepared rate) and returns; it never waits, allocates or
    locks. Overflow is counted, never blocks.
    Worker thread (one per instance, normal priority): pump() drains the FIFO through the
    engine and the tracker and publishes a Snapshot under a mutex the audio thread never takes.
    The harness calls pump() itself after every block (synchronous mode), so its renders are
    deterministic. The editor reads snapshot() at 30 Hz. */
namespace benchline
{
struct Snapshot
{
    static constexpr int kTextLen = 200;
    struct Sugg { int region = -1; double hz = 0, gainDb = 0, q = 0, sinceSeconds = 0, sinceSong = -1; char text[kTextLen] {}; };
    struct View
    {
        std::array<double, dsp::kBands> median {}, lo {}, mid {}, hi {};   // target edges at the current Tolerance
        int count = 0;
        bool hasTarget = false;
        std::array<dsp::Verdict, dsp::kRegions> verdict {};
        std::array<double, dsp::kRegions> devDb {};
        std::array<Sugg, 2> sugg {};
        int numSugg = 0;
    };
    std::array<View, 3> views {};
    std::array<std::array<double, dsp::kBands>, 3> held {};
    bool holdActive = false;
    double heldSinceSong = -1, heldSinceSeconds = 0;
    double shortTermLufs = -300, momentaryLufs = -300;
    double engineSeconds = 0;
    double lastGatedSeconds = -1e9;
    bool silent = true, bypassed = false, idle = true;
    juce::int64 gatedFrames = 0, framesAnalysed = 0, overflowSamples = 0;
    double sampleRate = 0;
    char targetName[96] {};
    juce::uint32 serial = 0;
};

class Analyser : private juce::Thread
{
public:
    Analyser();
    ~Analyser() override;

    /** Message thread or prepareToPlay (never concurrent with push()). */
    void prepare (double sampleRate, int maxBlock);
    /** Audio thread. Real-time safe. right may be null (mono). */
    void push (const float* left, const float* right, int n) noexcept;
    /** Audio thread: the host's song position at the start of the block just pushed. */
    void setSongPosition (double seconds, bool playing) noexcept;
    void setBypassed (bool b) noexcept { bypassed.store (b, std::memory_order_relaxed); }

    /** Parameters, read by the worker every pump (the processor hands over its atomics). */
    void bindParameters (std::atomic<float>* average, std::atomic<float>* tolerance, std::atomic<float>* hold);

    /** Non-audio threads. A null target clears it. */
    void setTarget (std::shared_ptr<const dsp::Target> t, const juce::String& file);
    std::shared_ptr<const dsp::Target> getTarget() const;
    juce::String getTargetFile() const;

    /** Harness: no worker thread; the caller pumps. Call before prepare(). */
    void setSynchronous (bool sync);
    /** Drains the FIFO; returns the frames analysed. Worker or harness. */
    int pump();
    /** Called from pump() for every frame (harness: dump, timeline). */
    std::function<void (const dsp::Frame&, const dsp::Tracker&)> onFrame;

    Snapshot snapshot() const;
    /** Hold state for the session (message thread): the frozen medians and when. */
    struct HeldState { bool active = false; double sinceSong = -1; std::array<std::array<double, dsp::kBands>, 3> medians {}; };
    HeldState heldState() const;
    void restoreHeld (const HeldState& h);
    double workerSeconds() const noexcept { return workerTime; }

private:
    void run() override;
    void publish();

    // FIFO (audio thread writes, worker reads)
    juce::AbstractFifo fifo { 1 };
    juce::AudioBuffer<float> fifoData;
    std::atomic<juce::int64> overflow { 0 }, pushedSamples { 0 };
    std::atomic<double> songAt { -1.0 };
    std::atomic<juce::int64> songAtSample { 0 };
    std::atomic<bool> bypassed { false };
    std::atomic<bool> prepared { false };

    // worker side
    mutable std::mutex workMutex;   // engine, tracker, target (never taken by the audio thread)
    dsp::Engine engine;
    dsp::Tracker tracker;
    std::shared_ptr<const dsp::Target> target;
    juce::String targetFile;
    std::atomic<float>* averageParam = nullptr;
    std::atomic<float>* toleranceParam = nullptr;
    std::atomic<float>* holdParam = nullptr;
    float lastTolerance = -1.0f;
    bool lastHold = false, targetDirty = true;
    HeldState held;
    double lastNow = 0, lastSong = -1;
    juce::int64 framesAnalysed = 0;
    std::vector<float> scratchL, scratchR;
    double workerTime = 0;
    juce::int64 lastPublishedSamples = -1;
    bool lastPublishedBypass = false;
    bool synchronous = false;

    mutable std::mutex snapMutex;
    Snapshot snap;
};
} // namespace benchline
