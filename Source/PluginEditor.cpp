#include "PluginEditor.h"
#include "BuildInfo.h"
#include "Params.h"
#include "State.h"
#include "benchline/FactoryPresets.h"

using namespace benchline::ui;

namespace
{
constexpr int kWidth = 960, kHeight = 520;

juce::PropertiesFile& settings()
{
    static juce::PropertiesFile file ([]
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Bench Line";
        o.filenameSuffix = ".settings";
        o.folderName = "Oberli Audio";
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }());
    return file;
}

/** The logo's knob as the header mark (stand-in until the shared frame brings the SVG). */
void drawMark (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& t = tokens();
    g.setColour (t.surfacePanel);
    g.fillEllipse (r);
    juce::Path p;
    const float c = r.getWidth();
    p.addRoundedRectangle (-c * 0.085f, -c * 0.5f + c * 0.075f, c * 0.17f, c * 0.34f, c * 0.085f);
    g.setColour (juce::Colour (0xFFF09300));   // the brand's mango pointer (the logo never takes a plugin's accent)
    g.fillPath (p, juce::AffineTransform::rotation (juce::degreesToRadians (38.0f)).translated (r.getCentre()));
}

class Knob : public juce::Slider
{
public:
    Knob (juce::RangedAudioParameter& p) : param (p), attachment (p, *this)
    {
        setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        setDoubleClickReturnValue (true, (double) p.convertFrom0to1 (p.getDefaultValue()));
        setVelocityBasedMode (false);
        setMouseDragSensitivity (200);
        setTooltip (Params::tooltipFor (p.paramID.toRawUTF8()));
        setHelpText (Params::tooltipFor (p.paramID.toRawUTF8()));
        setTitle (p.getName (32));
        setMouseClickGrabsKeyboardFocus (false);
        setWantsKeyboardFocus (false);
    }
    juce::String valueText() const { return param.getCurrentValueAsText(); }
private:
    juce::RangedAudioParameter& param;
    juce::SliderParameterAttachment attachment;
};
} // namespace

//==============================================================================
class BenchLineAudioProcessorEditor::Content : public juce::Component
{
public:
    explicit Content (BenchLineAudioProcessor& p)
        : proc (p), targets (p),
          average (*p.parameters().getParameter (Params::average)),
          tolerance (*p.parameters().getParameter (Params::tolerance)),
          viewAttach (*p.parameters().getParameter (Params::view), [this] (float v) { setView ((int) std::lround (v)); }),
          holdAttach (*p.parameters().getParameter (Params::hold), hold)
    {
        addAndMakeVisible (targets);
        addAndMakeVisible (display);
        addAndMakeVisible (regions);
        addAndMakeVisible (suggestions);
        addAndMakeVisible (average);
        addAndMakeVisible (tolerance);
        const char* names[] = { "Tone", "Punch", "Side" };
        for (int i = 0; i < 3; ++i)
        {
            auto& b = viewButtons[(size_t) i];
            b.setButtonText (names[i]);
            b.setClickingTogglesState (false);
            b.getProperties().set ("segment", true);
            b.getProperties().set ("ghost", true);
            b.setMouseClickGrabsKeyboardFocus (false);
            b.setWantsKeyboardFocus (false);
            b.setTooltip (Params::tooltipFor (Params::view));
            b.setTitle (juce::String ("View ") + names[i]);
            b.onClick = [this, i] { viewAttach.setValueAsCompleteGesture ((float) i); };
            addAndMakeVisible (b);
        }
        viewAttach.sendInitialUpdate();
        hold.setButtonText ("Hold");
        hold.setClickingTogglesState (true);
        hold.getProperties().set ("pill", true);
        hold.setTooltip (Params::tooltipFor (Params::hold));
        hold.setTitle ("Hold");
        hold.setMouseClickGrabsKeyboardFocus (false);
        hold.setWantsKeyboardFocus (false);
        addAndMakeVisible (hold);
        holdAttach.sendInitialUpdate();

        for (auto* b : { &prev, &next, &menu })
        {
            b->getProperties().set ("ghost", true);
            b->setMouseClickGrabsKeyboardFocus (false);
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (*b);
        }
        prev.setButtonText (juce::CharPointer_UTF8 ("\xe2\x80\xb9"));
        next.setButtonText (juce::CharPointer_UTF8 ("\xe2\x80\xba"));
        menu.setButtonText (juce::CharPointer_UTF8 ("\xe2\x89\xa1"));
        prev.setTitle ("Previous preset");
        next.setTitle ("Next preset");
        menu.setTitle ("Menu");
        prev.onClick = [this] { stepPreset (-1); };
        next.onClick = [this] { stepPreset (1); };
        menu.onClick = [this] { showMenu(); };
        presetIndex = 0;
    }

    void setView (int v)
    {
        view = juce::jlimit (0, 2, v);
        for (int i = 0; i < 3; ++i) viewButtons[(size_t) i].setToggleState (i == view, juce::dontSendNotification);
        pushSnapshot();
    }

    void stepPreset (int d)
    {
        const auto& bank = benchline::FactoryPresets::list();
        if (bank.isEmpty()) return;
        presetIndex = (presetIndex + d + bank.size()) % bank.size();
        benchline::FactoryPresets::load (proc.parameters(), presetIndex);
        repaint();
    }

    bool presetModified() const
    {
        return ! benchline::FactoryPresets::matches (proc.parameters(), presetIndex);
    }

    void showMenu()
    {
        juce::PopupMenu m, scales;
        const float now = (float) settings().getDoubleValue ("guiScale", 1.0);
        for (float s : { 0.75f, 1.0f, 1.5f, 2.0f })
            scales.addItem (juce::String (juce::roundToInt (s * 100)) + " %", true, std::abs (now - s) < 0.01f, [this, s]
            {
                settings().setValue ("guiScale", (double) s);
                settings().saveIfNeeded();
                if (onScale) onScale (s);
            });
        m.addSubMenu ("GUI scale", scales);
        m.addSeparator();
        m.addItem ("Report a problem...", [] { juce::URL ("https://github.com/Oberli-Audio/bench-line/issues/new").launchInDefaultBrowser(); });
        m.addItem ("About Bench Line", [this] { setAbout (true); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menu));
    }

    void setAbout (bool b) { about = b; for (auto* c : getChildren()) c->setVisible (! b); repaint(); }
    void mouseUp (const juce::MouseEvent&) override { if (about) setAbout (false); }

    void pushSnapshot()
    {
        snap = proc.analyser().snapshot();
        display.setSnapshot (snap, view);
        regions.setSnapshot (snap, view);
        suggestions.setSnapshot (snap, view);
        targets.setTolerance (tolerance.getValue());
        targets.timerTick();
        repaint (outputArea);
        repaint (headerArea);
        repaint (balanceTitle);
    }

    void resized() override
    {
        headerArea = { 0, 0, kWidth, 48 };
        next.setBounds (690, 10, 28, 28);
        prev.setBounds (520, 10, 28, 28);
        menu.setBounds (912, 10, 28, 28);
        targets.setBounds (28, 76, 200, 416);
        balanceTitle = { 256, 72, 540, 32 };
        tolerance.setBounds (346, 72, 32, 32);
        for (int i = 0; i < 3; ++i) viewButtons[(size_t) i].setBounds (492 + i * 76, 72, 76, 32);
        hold.setBounds (728, 72, 68, 32);
        display.setBounds (256, 112, 540, 214);
        captionArea = { 256, 328, 540, 16 };
        regions.setBounds (256, 352, 540, 48);
        suggestions.setBounds (256, 416, 540, 64);
        outputArea = { 812, 48, 148, 472 };
        average.setBounds (854, 100, 64, 64);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = tokens();
        g.fillAll (t.ground);
        if (about) { paintAbout (g); return; }
        // header
        g.setColour (t.surfaceRaised);
        g.fillRect (headerArea);
        g.setColour (t.line);
        g.drawHorizontalLine (47, 0.0f, (float) kWidth);
        drawMark (g, juce::Rectangle<float> (28, 15, 18, 18));
        g.setColour (t.ink);
        g.setFont (font (TextStyle::pluginName));
        g.drawText ("Oberli", 50, 14, 50, 20, juce::Justification::centredLeft);
        g.setColour (t.line);
        g.drawVerticalLine (108, 14.0f, 34.0f);
        g.setColour (t.ink);
        g.drawText ("Bench Line", 120, 14, 140, 20, juce::Justification::centredLeft);
        // preset pill
        auto pill = juce::Rectangle<float> (516, 8, 206, 32);
        g.setColour (t.lineControl);
        g.drawRoundedRectangle (pill.reduced (0.5f), 16.0f, 1.0f);
        const auto& bank = benchline::FactoryPresets::list();
        const auto name = bank.isEmpty() ? juce::String ("Init") : bank[presetIndex]->name;
        g.setColour (presetModified() ? t.inkSubtle : t.ink);
        g.setFont (font (TextStyle::control));
        g.drawText (name + (presetModified() ? " *" : ""), pill.reduced (34, 0), juce::Justification::centred);
        g.setColour (t.inkSubtle);
        g.setFont (font (TextStyle::meta));
        g.drawText (juce::String (PRODUCT_VERSION), 840, 14, 64, 20, juce::Justification::centredRight);

        // balance title row
        drawCaps (g, "Balance", juce::Rectangle<float> (256, 72, 80, 20), TextStyle::sectionCaps, t.ink);
        drawCaps (g, "Tolerance", juce::Rectangle<float> (384, 74, 100, 12), TextStyle::labelCaps, t.inkMuted);
        g.setColour (t.ink);
        g.setFont (font (TextStyle::value));
        g.drawText (tolerance.valueText(), 384, 88, 100, 14, juce::Justification::centredLeft);
        if (hold.getToggleState())
        {
            g.setColour (t.accent);
            g.fillEllipse (juce::Rectangle<float> (8, 8).withCentre ({ (float) hold.getX() + 14.0f, (float) hold.getBounds().getCentreY() }));
        }
        else
        {
            g.setColour (t.lineControl);
            g.drawEllipse (juce::Rectangle<float> (8, 8).withCentre ({ (float) hold.getX() + 14.0f, (float) hold.getBounds().getCentreY() }), 1.0f);
        }
        // caption under the display
        g.setColour (t.inkSubtle);
        g.setFont (font (TextStyle::meta));
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
        g.drawText (caption, captionArea, juce::Justification::centredLeft);

        // output column
        g.setColour (t.line);
        g.drawVerticalLine (812, 76.0f, 492.0f);
        drawCaps (g, "Output", juce::Rectangle<float> (836, 72, 100, 20), TextStyle::sectionCaps, t.ink);
        drawCaps (g, "Average", juce::Rectangle<float> (836, 168, 100, 12), TextStyle::labelCaps, t.inkMuted, juce::Justification::centred);
        g.setColour (t.ink);
        g.setFont (font (TextStyle::value));
        const auto avg = average.getValue() >= 59.5 ? juce::String ("Song") : average.valueText();
        g.drawText (avg, 836, 182, 100, 16, juce::Justification::centred);
        paintMeter (g, juce::Rectangle<float> (877, 214, 18, 196));
        drawCaps (g, "LUFS S", juce::Rectangle<float> (836, 420, 100, 12), TextStyle::labelCaps, t.inkMuted, juce::Justification::centred);
        g.setColour (t.ink);
        g.setFont (font (TextStyle::value));
        g.drawText (snap.shortTermLufs > -70.0 ? juce::String (snap.shortTermLufs, 1) : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")), 836, 434, 100, 16, juce::Justification::centred);
    }

    void paintMeter (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto& t = tokens();
        g.setColour (t.surfaceSunken);
        g.fillRoundedRectangle (r, 4.0f);
        const double lo = -30.0, hi = 0.0;
        auto yOf = [&] (double v) { return r.getBottom() - (float) ((juce::jlimit (lo, hi, v) - lo) / (hi - lo)) * r.getHeight(); };
        g.setFont (font (TextStyle::meta));
        for (int v = 0; v >= -30; v -= 6)
        {
            g.setColour (t.inkSubtle);
            g.drawText (juce::String (v), juce::Rectangle<float> (r.getX() - 34, yOf (v) - 7, 28, 14), juce::Justification::centredRight);
        }
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

    void paintAbout (juce::Graphics& g)
    {
        // the About page is the other theme: charcoal behind an effect (brand sheet)
        g.fillAll (juce::Colour (0xFF1C1B19));
        const auto ink = juce::Colour (0xFFEDE8DF), muted = juce::Colour (0xFFBDB6AA);
        g.setColour (ink);
        g.setFont (juce::Font (juce::FontOptions (28.0f, juce::Font::bold)));
        g.drawText ("Bench Line", 64, 96, 600, 36, juce::Justification::centredLeft);
        g.setFont (font (TextStyle::body));
        g.setColour (muted);
        g.drawText ("Compares your mix's tonal balance to your references. Bench Line never changes your audio.", 64, 140, 800, 20, juce::Justification::centredLeft);
        g.drawText ("Turn it and listen.", 64, 164, 600, 20, juce::Justification::centredLeft);
        g.setColour (ink);
        juce::StringArray lines {
            "Version " + juce::String (PRODUCT_VERSION) + " (" + juce::String (BUILD_SHA) + ")",
            "Analysis version " + juce::String (benchline::State::kDspVersion) + ", state version " + juce::String (benchline::State::kStateVersion),
            "Targets: " + benchline::TargetIO::referencesFolder().getFullPathName(),
            "Third-party notices: THIRD_PARTY_NOTICES.md in the download (JUCE, VST 3 SDK)",
            "Oberli Audio, Bjoern Oberli" };
        int y = 220;
        for (auto& l : lines) { g.drawText (l, 64, y, 820, 20, juce::Justification::centredLeft); y += 24; }
        g.setColour (muted);
        g.drawText ("Click anywhere to go back.", 64, 460, 400, 20, juce::Justification::centredLeft);
    }

    std::function<void (float)> onScale;
    int view = 0;

private:
    BenchLineAudioProcessor& proc;
    TargetList targets;
    BalanceDisplay display;
    RegionMarks regions;
    SuggestionLines suggestions;
    Knob average, tolerance;
    std::array<juce::TextButton, 3> viewButtons;
    juce::TextButton hold, prev, next, menu;
    juce::ParameterAttachment viewAttach;
    juce::ButtonParameterAttachment holdAttach;
    benchline::Snapshot snap;
    juce::Rectangle<int> headerArea, outputArea, captionArea, balanceTitle;
    int presetIndex = 0;
    bool about = false;
};

//==============================================================================
BenchLineAudioProcessorEditor::BenchLineAudioProcessorEditor (BenchLineAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&look);
    setWantsKeyboardFocus (false);   // the space bar keeps reaching the host's transport
    content = std::make_unique<Content> (p);
    content->onScale = [this] (float s) { setGuiScale (s); };
    addAndMakeVisible (*content);
    content->setBounds (0, 0, kWidth, kHeight);
    setGuiScale ((float) settings().getDoubleValue ("guiScale", 1.0));
    startTimerHz (30);
}

BenchLineAudioProcessorEditor::~BenchLineAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void BenchLineAudioProcessorEditor::setGuiScale (float s)
{
    scale = juce::jlimit (0.75f, 2.0f, s);
    content->setTransform (juce::AffineTransform::scale (scale));
    setSize (juce::roundToInt (kWidth * scale), juce::roundToInt (kHeight * scale));
}

void BenchLineAudioProcessorEditor::showAbout (bool b) { content->setAbout (b); }
void BenchLineAudioProcessorEditor::refreshNow() { content->pushSnapshot(); }

void BenchLineAudioProcessorEditor::resized() {}

void BenchLineAudioProcessorEditor::timerCallback()
{
    content->pushSnapshot();
}
