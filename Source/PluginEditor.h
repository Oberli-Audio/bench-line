#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "benchline/ui/BalanceView.h"
#include "benchline/ui/Look.h"
#include "oberli/Buttons.h"
#include "oberli/Knob.h"
#include "oberli/StripEditor.h"

/*  Bench Line - the editor (design plan, docs/sketch.md section 2; brand sheet, Bone, peacock).

    The family's Strip frame (Source/oberli/StripEditor.h: header with the mark, the name, the
    preset pill and the menu; the preset drawer; Save preset; About on the back; the GUI scale as a
    machine-local preference) around Bench Line's body, 904 x 416 at 100 %:
    Target list | Balance (Tolerance, View, Hold, the display, the region marks, the suggestions) |
    Output (Average, LUFS S). The body's parts are Bench Line's own (benchline/ui/BalanceView.h). */
class BenchLineAudioProcessorEditor final : public oberli::ui::StripEditor
{
public:
    explicit BenchLineAudioProcessorEditor (BenchLineAudioProcessor&);
    ~BenchLineAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;

    /** The snapshot tool and tests: one display update without waiting for a frame. */
    void refreshNow();

    bool previewDrag (const juce::String& parameterId) override;

private:
    void layoutBody (juce::Rectangle<int> area) override;
    void advanceBody (double now, float dt) override;
    void presetRecalled (double now) override;
    juce::StringArray diagnostics() override;

    void paintMeter (juce::Graphics&, juce::Rectangle<float>);

    BenchLineAudioProcessor& audioProcessor;   // not `processor`: AudioProcessorEditor has one (-Wshadow-field)

    benchline::ui::TargetList targets;
    benchline::ui::BalanceDisplay display;
    benchline::ui::RegionMarks regions;
    benchline::ui::SuggestionLines suggestions;
    oberli::ui::Knob average, tolerance;
    juce::SliderParameterAttachment averageAttach, toleranceAttach;
    oberli::ui::SegmentedControl viewControl { { "Tone", "Punch", "Side" } };
    oberli::ui::PillToggle hold { "Hold" };
    juce::AudioProcessorValueTreeState::ButtonAttachment holdAttach;

    benchline::Snapshot snap;
    int view = 0;
    double lastPush = -1.0;

    // body coordinates (editor space), set in layoutBody
    juce::Rectangle<int> balanceTitle, toleranceText, captionArea, outputRule, outputTitle, averageText, meterArea, lufsText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BenchLineAudioProcessorEditor)
};
