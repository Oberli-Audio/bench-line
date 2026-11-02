#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

#include "benchline/Analyser.h"
#include "Params.h"
#include "benchline/TargetLibrary.h"

/*  Bench Line - the processor.

    The audio path is a wire (structural decision 1): processBlock never writes the buffer, so
    the output is the input bit for bit and the latency is 0. It copies the block into the
    analyser's FIFO and reads the play-head; nothing else (rule 1). Everything else - the
    analysis, targets, the build - runs off the audio thread (Analyser, TargetLibrary).
    State: parameters (State.h) plus <Extra> with the selected target and the Hold snapshot. */
class BenchLineAudioProcessor final : public juce::AudioProcessor
{
public:
    BenchLineAudioProcessor();
    ~BenchLineAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    juce::AudioProcessorParameter* getBypassParameter() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    bool loadState (const void* data, int sizeInBytes);

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }
    benchline::Analyser& analyser() { return analysis; }

    // ---- targets (message thread)
    /** Selects a target by its file (empty file and builtIn: the plain slope; both empty: none). */
    bool selectTarget (const benchline::TargetFile& t, juce::String* error = nullptr);
    void clearTarget();
    juce::String currentTargetName() const;
    juce::File currentTargetFile() const;
    bool currentTargetIsSlope() const { return slopeSelected; }

    // ---- building a target from files (a background thread; the editor polls)
    struct BuildStatus { bool active = false; int done = 0, total = 0; juce::String message; bool lastOk = false; juce::File lastFile; };
    void startBuild (const juce::Array<juce::File>& files, const juce::String& name);
    void cancelBuild();
    BuildStatus buildStatus() const;

private:
    class BuildThread;
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* bypassParam = nullptr;
    benchline::Analyser analysis;
    bool slopeSelected = false;
    std::unique_ptr<BuildThread> builder;
    mutable juce::CriticalSection buildLock;
    BuildStatus status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BenchLineAudioProcessor)
};
