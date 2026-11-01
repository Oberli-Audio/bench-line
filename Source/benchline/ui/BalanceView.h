#pragma once

/*  Bench Line's own editor parts (never shared): the balance display (the hero), the region
    marks, the suggestion lines, the target list. They read the analyser's Snapshot only. */

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Analyser.h"
#include "../TargetLibrary.h"
#include "Look.h"

class BenchLineAudioProcessor;

namespace benchline::ui
{
/** The hero (design plan 2): the mix curve inside the target's tolerance band, the parts outside
    the band in the accent. With a target the axis is the deviation from the target's median, so
    "on target" reads flat (G1 decision 8); without one, the mix on the -4.5 dB/oct axis. */
class BalanceDisplay : public juce::Component
{
public:
    void setSnapshot (const Snapshot& s, int view);
    void paint (juce::Graphics&) override;
private:
    Snapshot snap;
    int view = 0;
    float yFor (double db, juce::Rectangle<float> plot) const;
    float xForBand (int b, juce::Rectangle<float> plot) const;
};

/** Six region marks: the band as a tint, the mix as a mark; outside: the mark in the accent. */
class RegionMarks : public juce::Component
{
public:
    void setSnapshot (const Snapshot& s, int view);
    void paint (juce::Graphics&) override;
private:
    Snapshot snap;
    int view = 0;
};

/** At most two suggestion lines: region caps, the sentence, since when. */
class SuggestionLines : public juce::Component
{
public:
    void setSnapshot (const Snapshot& s, int view);
    void paint (juce::Graphics&) override;
private:
    Snapshot snap;
    int view = 0;
};

/** The target list (left column): Mine, then Built in; Build from files, with progress. */
class TargetList : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit TargetList (BenchLineAudioProcessor& p);
    void refresh();
    void setTolerance (double t) { tolerance = t; repaint(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void startBuildFrom (const juce::Array<juce::File>& files);
    void timerTick();

private:
    struct Row { TargetFile t; double spread = 0; bool header = false; juce::String title; juce::Rectangle<int> bounds; };
    BenchLineAudioProcessor& proc;
    juce::Array<Row> rows;
    juce::TextButton build { "Build from files..." };
    std::unique_ptr<juce::FileChooser> chooser;
    double tolerance = 60.0;
    int hover = -1;
    bool dragOver = false;
    bool wasBuilding = false;
    juce::String lastMessage;
};
} // namespace benchline::ui
