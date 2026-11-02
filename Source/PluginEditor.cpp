#include "PluginEditor.h"
#include "BuildInfo.h"
#include "Params.h"
#include "State.h"

#include <ProductData.h>

using namespace benchline::ui;
namespace sizes = oberli::ui::Tokens;   // the shared sizes; tokens() are the colours

namespace
{
constexpr int kBodyWidth = 904, kBodyHeight = 416;   // 960 x 520 with the header and the padding
constexpr double kPushInterval = 1.0 / 30.0;         // the display reads the analyser at 30 Hz

/** A document compiled into the plugin (CMakeLists.txt: ProductData), by its file name. */
juce::String embedded (const char* fileName)
{
    int size = 0;
    for (int i = 0; i < ProductData::namedResourceListSize; ++i)
        if (juce::String (ProductData::getNamedResourceOriginalFilename (ProductData::namedResourceList[i])) == fileName)
            if (const auto* data = ProductData::getNamedResource (ProductData::namedResourceList[i], size))
                return juce::String::fromUTF8 (data, size);
    return {};
}

oberli::ui::StripEditor::Info infoFor()
{
    oberli::ui::StripEditor::Info info;
    info.productName = JucePlugin_Name;
    info.version = PRODUCT_VERSION;
    info.what = "Compares your mix's tonal balance to your references. Bench Line never changes your audio.";
    // the home folder as ~, so the path fits About's line
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
    auto targets = benchline::TargetIO::targetsFolder().getFullPathName();
    if (targets.startsWith (home)) targets = "~" + targets.substring (home.length());
    info.ownLine = "Targets: " + targets;
    info.licences = embedded ("LICENSE") + "\n\n" + embedded ("THIRD_PARTY_NOTICES.md");
    info.soundVersion = benchline::State::kDspVersion;
    return info;
}

void setUp (oberli::ui::Knob& k, juce::RangedAudioParameter& p)
{
    k.bindParameter (p, p.getName (32));
    const juce::String tip (Params::tooltipFor (p.paramID.toRawUTF8()));
    k.setTooltip (tip);
    k.setHelpText (tip);
    k.syncDisplay();
}
} // namespace

//==============================================================================
BenchLineAudioProcessorEditor::BenchLineAudioProcessorEditor (BenchLineAudioProcessor& p)
    : StripEditor (p, p.presetHost(), infoFor()),
      audioProcessor (p),
      targets (p),
      averageAttach (*p.parameters().getParameter (Params::average), average),
      toleranceAttach (*p.parameters().getParameter (Params::tolerance), tolerance),
      holdAttach (p.parameters(), Params::hold, hold)
{
    setWantsKeyboardFocus (false);   // the space bar keeps reaching the host's transport

    for (auto* c : std::initializer_list<juce::Component*> { &targets, &display, &regions, &suggestions,
                                                             &average, &tolerance, &viewControl, &hold })
        addAndMakeVisible (*c);

    setUp (average, *p.parameters().getParameter (Params::average));
    setUp (tolerance, *p.parameters().getParameter (Params::tolerance));

    viewControl.attach (*p.parameters().getParameter (Params::view));
    viewControl.setTooltip (Params::tooltipFor (Params::view));
    hold.setTooltip (Params::tooltipFor (Params::hold));
    hold.setTitle ("Hold");

    setBodySize (kBodyWidth, kBodyHeight);
    audioProcessor.presetLibrary().writeFactoryFolder();   // the bank as files, once per version
    refreshNow();
}

BenchLineAudioProcessorEditor::~BenchLineAudioProcessorEditor() = default;

//==============================================================================
void BenchLineAudioProcessorEditor::layoutBody (juce::Rectangle<int> area)
{
    const auto o = area.getTopLeft();
    auto at = [o] (int x, int y, int w, int h) { return juce::Rectangle<int> (x, y, w, h) + o; };

    // Target | Balance | Output (the output column last on the right, after a hairline)
    targets.setBounds (at (0, 0, 200, kBodyHeight));

    balanceTitle = at (228, 0, 80, 32);
    tolerance.setBounds (at (318, 0, sizes::knobSm, sizes::knobSm));
    toleranceText = at (358, 0, 96, 32);
    viewControl.setBounds (at (464, 2, 228, sizes::controlHeight));
    hold.setBounds (at (768 - hold.getPreferredWidth(), 2, hold.getPreferredWidth(), sizes::controlHeight));
    display.setBounds (at (228, 40, 540, 210));
    captionArea = at (228, 254, 540, 16);
    regions.setBounds (at (228, 278, 540, 48));
    suggestions.setBounds (at (228, 340, 540, 64));

    outputRule = at (784, 0, 1, kBodyHeight);
    outputTitle = at (808, 0, 96, 20);
    average.setBounds (at (824, 28, sizes::knobLg, sizes::knobLg));
    averageText = at (808, 96, 96, 36);
    meterArea = at (847, 148, 18, 196);
    lufsText = at (808, 354, 96, 36);
}

void BenchLineAudioProcessorEditor::advanceBody (double now, float dt)
{
    average.advance (now, dt);
    tolerance.advance (now, dt);
    if (lastPush < 0.0 || now - lastPush >= kPushInterval)
    {
        lastPush = now;
        refreshNow();
    }
}

void BenchLineAudioProcessorEditor::presetRecalled (double now)
{
    tolerance.beginWave (now, false);
    average.beginWave (now + 0.04, false);
}

void BenchLineAudioProcessorEditor::refreshNow()
{
    view = juce::jlimit (0, 2, viewControl.getSelected());
    snap = audioProcessor.analyser().snapshot();
    display.setSnapshot (snap, view);
    regions.setSnapshot (snap, view);
    suggestions.setSnapshot (snap, view);
    targets.setTolerance (tolerance.getValue());
    targets.timerTick();
    repaint (balanceTitle.getUnion (toleranceText));
    repaint (captionArea);
    repaint (outputTitle.getUnion (lufsText));
}

bool BenchLineAudioProcessorEditor::previewDrag (const juce::String& parameterId)
{
    if (parameterId == Params::average) { average.setDragPreview (true); return true; }
    if (parameterId == Params::tolerance) { tolerance.setDragPreview (true); return true; }
    return false;
}

juce::StringArray BenchLineAudioProcessorEditor::diagnostics()
{
    auto lines = StripEditor::diagnostics();
    lines.add ("Target: " + (audioProcessor.currentTargetName().isNotEmpty() ? audioProcessor.currentTargetName() : juce::String ("none")));
    lines.add ("References folder: " + benchline::TargetIO::referencesFolder().getFullPathName());
    return lines;
}

//==============================================================================
void BenchLineAudioProcessorEditor::paint (juce::Graphics& g)
{
    StripEditor::paint (g);
    const auto& t = tokens();

    // balance title row
    drawCaps (g, "Balance", balanceTitle.toFloat(), TextStyle::sectionCaps, t.inkMuted);
    auto tol = toleranceText.toFloat();
    drawCaps (g, "Tolerance", tol.removeFromTop (16), TextStyle::labelCaps, t.inkMuted);
    oberli::ui::type::draw (g, tolerance.textFor (tolerance.getValue()), tol, oberli::ui::type::value(), t.ink);

    // caption under the display
    juce::String caption;
    const bool hasTarget = snap.views[(size_t) view].hasTarget;
    if (view == benchline::dsp::kPunch) caption = "Crest factor per region against the target's median, 50 ms windows";
    else caption = hasTarget ? "dB against the target's median, 1/3 octave, level matched to the target"
                             : "dB against a -4.5 dB/oct slope, 1/3 octave, level matched";
    if (snap.holdActive)
    {
        const int s = (int) std::max (0.0, snap.heldSinceSong);
        caption += "; dotted: held at " + juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }
    oberli::ui::type::draw (g, caption, captionArea.toFloat(), oberli::ui::type::meta(), t.inkSubtle);

    // output column
    g.setColour (t.line);
    g.fillRect (outputRule);
    drawCaps (g, "Output", outputTitle.toFloat(), TextStyle::sectionCaps, t.inkMuted, juce::Justification::centred);
    auto avg = averageText.toFloat();
    drawCaps (g, "Average", avg.removeFromTop (16), TextStyle::labelCaps, t.inkMuted, juce::Justification::centred);
    // the table's note: 60 s reads "Song" (the host's value string stays "60 s"; Waiting for Björn)
    const auto avgText = average.getValue() >= 59.5 ? juce::String ("Song") : average.textFor (average.getValue());
    oberli::ui::type::draw (g, avgText, avg, oberli::ui::type::value(), t.ink, juce::Justification::centred);
    paintMeter (g, meterArea.toFloat());
    auto lufs = lufsText.toFloat();
    drawCaps (g, "LUFS S", lufs.removeFromTop (16), TextStyle::labelCaps, t.inkMuted, juce::Justification::centred);
    oberli::ui::type::draw (g, snap.shortTermLufs > -70.0 ? juce::String (snap.shortTermLufs, 1) : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")),
                            lufs, oberli::ui::type::value(), t.ink, juce::Justification::centred);
}

/** Short-term loudness (EBU Tech 3341, the analyser's), -30 to 0 LUFS: Bench Line's own meter;
    the shared LevelMeter shows sample peak, which is not what this column reads. */
void BenchLineAudioProcessorEditor::paintMeter (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& t = tokens();
    g.setColour (t.surfaceSunken);
    g.fillRoundedRectangle (r, sizes::radiusXs);
    g.setColour (t.line);
    g.drawRoundedRectangle (r.reduced (0.5f), sizes::radiusXs, 1.0f);
    const double lo = -30.0, hi = 0.0;
    auto yOf = [&] (double v) { return r.getBottom() - (float) ((juce::jlimit (lo, hi, v) - lo) / (hi - lo)) * r.getHeight(); };
    for (int v = 0; v >= -30; v -= 6)
        oberli::ui::type::draw (g, juce::String (v), { r.getX() - 34, yOf (v) - 7, 28, 14 }, oberli::ui::type::meta(), t.inkSubtle,
                                juce::Justification::centredRight);
    if (snap.shortTermLufs > lo)
    {
        const float y = yOf (snap.shortTermLufs);
        g.setColour (t.meterLevel);
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.getX() + 3, y, r.getRight() - 3, r.getBottom() - 3));
        if (snap.shortTermLufs > -6.0)
        {
            g.setColour (t.meterHot);
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.getX() + 3, y, r.getRight() - 3, yOf (-6.0)));
        }
    }
}
