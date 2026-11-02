#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "benchline/ui/BalanceView.h"
#include "benchline/ui/Look.h"

/*  Bench Line - the editor (design plan, docs/sketch.md section 2; brand sheet, Bone, peacock).

    960 x 520 at 100 %, scaled as a whole (75/100/150/200 %, a machine-local preference).
    Header: mark, name, preset pill, version, menu. Body: Target list | Balance (View, Hold,
    Tolerance, the display, the region marks, the suggestions) | Output (Average, LUFS S).
    The shared frame, knob and meter are stand-ins until Source/oberli/ lands (StandIns.md). */
class BenchLineAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BenchLineAudioProcessorEditor (BenchLineAudioProcessor&);
    ~BenchLineAudioProcessorEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

    /** Snapshot tool and tests: the content at a scale, and a state (about, menu). */
    void setGuiScale (float s);
    void showAbout (bool b);
    void refreshNow();   // one timer tick without waiting

private:
    class Content;
    void timerCallback() override;

    BenchLineAudioProcessor& processor;
    benchline::ui::BenchLineLook look;
    std::unique_ptr<Content> content;
    float scale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BenchLineAudioProcessorEditor)
};
