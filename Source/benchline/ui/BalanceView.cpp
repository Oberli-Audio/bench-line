#include "BalanceView.h"

#include "../../PluginProcessor.h"

namespace benchline::ui
{
namespace
{
constexpr double kRangeDb = 9.0;   // +/-9 dB visible (sketch 4.1)
const char* const kRegionRange[dsp::kRegions] = { "20-50", "63-160", "200-500", "0.63-1.6k", "2-5k", "6.3-20k" };

double slopeAxis (int b) { return dsp::kSlopeOffsetDb + dsp::kSlopeDbPerOct * std::log2 (dsp::bandCentre (b) / 1000.0); }

juce::String sinceText (const Snapshot::Sugg& s)
{
    const double t = s.sinceSong >= 0 ? s.sinceSong : s.sinceSeconds;
    const int secs = (int) std::floor (t);
    return "since " + juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2);
}
} // namespace

// ------------------------------------------------------------------ the balance display
void BalanceDisplay::setSnapshot (const Snapshot& s, int v)
{
    snap = s;
    view = v;
    repaint();
}

float BalanceDisplay::yFor (double db, juce::Rectangle<float> plot) const
{
    const double c = juce::jlimit (-kRangeDb, kRangeDb, db);
    return plot.getCentreY() - (float) (c / kRangeDb) * plot.getHeight() * 0.5f;
}

float BalanceDisplay::xForBand (int b, juce::Rectangle<float> plot) const
{
    const auto& v = snap.views[(size_t) view];
    const int n = v.count;
    if (view == dsp::kPunch) return plot.getX() + plot.getWidth() * ((float) b + 0.5f) / (float) n;
    // log frequency, the 20 Hz band centre at the left edge, 20 kHz at the right
    return plot.getX() + plot.getWidth() * (float) b / (float) (n - 1);
}

void BalanceDisplay::paint (juce::Graphics& g)
{
    const auto& t = tokens();
    auto r = getLocalBounds().toFloat();
    g.setColour (t.surfaceSunken);
    g.fillRoundedRectangle (r, 4.0f);
    auto plot = r.reduced (28.0f, 12.0f).withTrimmedBottom (14.0f);
    const auto& v = snap.views[(size_t) view];
    const int n = v.count;
    const bool target = v.hasTarget;

    // grid: 3 dB lines, frequency lines at the decades
    g.setFont (font (TextStyle::meta));
    for (int db = -6; db <= 6; db += 3)
    {
        const float y = yFor (db, plot);
        g.setColour (db == 0 ? t.lineStrong : t.line);
        g.drawHorizontalLine ((int) std::round (y), plot.getX(), plot.getRight());
        g.setColour (t.inkSubtle);
        g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (r.getX() + 2, y - 7, 24, 14), juce::Justification::centredRight);
    }
    if (view != dsp::kPunch)
    {
        const int marks[] = { 3, 7, 10, 14, 17, 20, 24, 27 };   // 50, 100, 200, 500, 1k, 2k, 5k, 10k
        const char* labels[] = { "50", "100", "200", "500", "1k", "2k", "5k", "10k" };
        for (int i = 0; i < 8; ++i)
        {
            const float x = xForBand (marks[i], plot);
            g.setColour (t.line);
            g.drawVerticalLine ((int) std::round (x), plot.getY(), plot.getBottom());
            g.setColour (t.inkSubtle);
            g.drawText (labels[i], juce::Rectangle<float> (x - 20, plot.getBottom() + 2, 40, 12), juce::Justification::centred);
        }
    }
    else
    {
        for (int i = 0; i < dsp::kRegions; ++i)
        {
            const float x = xForBand (i, plot);
            g.setColour (t.inkSubtle);
            g.drawText (dsp::kRegionNames[i], juce::Rectangle<float> (x - 40, plot.getBottom() + 2, 80, 12), juce::Justification::centred);
        }
    }

    auto ref = [&] (int i) { return target ? v.mid[(size_t) i] : (view == dsp::kTone ? slopeAxis (i) : (view == dsp::kSide ? slopeAxis (i) - 10.0 : 12.0)); };

    // the tolerance band and the target's median
    if (target)
    {
        juce::Path band;
        for (int i = 0; i < n; ++i)
        {
            const float x = xForBand (i, plot);
            const float y = yFor (v.hi[(size_t) i] - v.mid[(size_t) i], plot);
            if (i == 0) band.startNewSubPath (view == dsp::kPunch ? x - 18 : x, y);
            else band.lineTo (x, y);
            if (view == dsp::kPunch) band.lineTo (x + 18, y);
        }
        for (int i = n - 1; i >= 0; --i)
        {
            const float x = xForBand (i, plot);
            const float y = yFor (v.lo[(size_t) i] - v.mid[(size_t) i], plot);
            if (view == dsp::kPunch) band.lineTo (x + 18, y);
            band.lineTo (view == dsp::kPunch ? x - 18 : x, y);
        }
        band.closeSubPath();
        g.setColour (t.accent.withAlpha (0.22f));
        g.fillPath (band);
        g.setColour (t.accent.withAlpha (0.55f));
        g.strokePath (band, juce::PathStrokeType (1.0f));
        juce::Path median;
        median.startNewSubPath (plot.getX(), yFor (0.0, plot));
        median.lineTo (plot.getRight(), yFor (0.0, plot));
        const float dash[] = { 4.0f, 3.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, median, dash, 2);
        g.setColour (t.inkMuted);
        g.fillPath (dashed);
    }

    // the held curve (Hold), a dashed ghost
    if (snap.holdActive)
    {
        juce::Path held;
        bool started = false;
        for (int i = 0; i < n; ++i)
        {
            const double m = snap.held[(size_t) view][(size_t) i];
            if (std::isnan (m)) continue;
            const float x = xForBand (i, plot), y = yFor (m - ref (i), plot);
            if (! started) { held.startNewSubPath (x, y); started = true; } else held.lineTo (x, y);
        }
        const float dash[] = { 2.0f, 3.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, held, dash, 2);
        g.setColour (t.inkSubtle);
        g.fillPath (dashed);
    }

    // the mix curve: smoothed across one band for display only (sketch 4.1); outside the band in the accent
    std::array<double, dsp::kBands> y {};
    bool any = false;
    for (int i = 0; i < n; ++i)
    {
        y[(size_t) i] = v.median[(size_t) i] - ref (i);
        any = any || ! std::isnan (v.median[(size_t) i]);
    }
    if (any)
    {
        std::array<double, dsp::kBands> s = y;
        if (view != dsp::kPunch)
            for (int i = 1; i + 1 < n; ++i)
                if (! std::isnan (y[(size_t) i - 1]) && ! std::isnan (y[(size_t) i + 1]))
                    s[(size_t) i] = 0.25 * y[(size_t) i - 1] + 0.5 * y[(size_t) i] + 0.25 * y[(size_t) i + 1];
        juce::Path curve;
        for (int i = 0; i < n; ++i)
        {
            const float x = xForBand (i, plot), yy = yFor (s[(size_t) i], plot);
            if (i == 0) curve.startNewSubPath (x, yy); else curve.lineTo (x, yy);
            if (view == dsp::kPunch) g.fillEllipse (juce::Rectangle<float> (7, 7).withCentre ({ x, yy }));
        }
        const auto curveColour = snap.silent ? t.inkSubtle : t.ink;
        if (target && ! snap.silent)
        {
            // segments outside the band: a wide accent stroke under the ink line (3:1 kept by the ink)
            for (int i = 0; i + 1 < n; ++i)
            {
                auto out = [&] (int k) { return y[(size_t) k] > v.hi[(size_t) k] - v.mid[(size_t) k] || y[(size_t) k] < v.lo[(size_t) k] - v.mid[(size_t) k]; };
                if (out (i) || out (i + 1))
                {
                    juce::Path seg;
                    seg.startNewSubPath (xForBand (i, plot), yFor (s[(size_t) i], plot));
                    seg.lineTo (xForBand (i + 1, plot), yFor (s[(size_t) i + 1], plot));
                    g.setColour (t.accent);
                    g.strokePath (seg, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
            }
        }
        g.setColour (curveColour);
        g.strokePath (curve, juce::PathStrokeType (1.75f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // states, in words
    juce::String note;
    if (snap.bypassed) note = "Bypassed: the analysis pauses.";
    else if (! target && view == dsp::kTone) note = "Choose a target or build one from your references.";
    else if (! target && snap.targetName[0] != 0) note = juce::String (view == dsp::kSide ? "This target has no side statistics." : "This target has no punch statistics.");
    else if (! target) note = "Choose a target or build one from your references.";
    else if (! any) note = "Play the mix: the curve appears after a second or two.";
    if (note.isNotEmpty())
    {
        g.setColour (t.inkMuted);
        g.setFont (font (TextStyle::body));
        g.drawText (note, plot.withHeight (20.0f).translated (0, 4), juce::Justification::centred);
    }
}

// ------------------------------------------------------------------ region marks
void RegionMarks::setSnapshot (const Snapshot& s, int v)
{
    snap = s;
    view = v;
    repaint();
}

void RegionMarks::paint (juce::Graphics& g)
{
    const auto& t = tokens();
    const auto& v = snap.views[(size_t) view];
    auto r = getLocalBounds().toFloat();
    const float gap = 12.0f;
    const float w = (r.getWidth() - gap * 5) / 6.0f;
    for (int i = 0; i < dsp::kRegions; ++i)
    {
        auto cell = juce::Rectangle<float> (r.getX() + (float) i * (w + gap), r.getY(), w, r.getHeight());
        auto bar = cell.removeFromTop (16.0f);
        g.setColour (t.surfaceSunken);
        g.fillRoundedRectangle (bar, 4.0f);
        if (v.hasTarget && ! snap.silent && v.verdict[(size_t) i] != dsp::Verdict::none)
        {
            // the band of the region (mean of its bands) around the centre, the mix as a mark
            const int i0 = view == dsp::kPunch ? i : dsp::kRegionFirst[i], i1 = view == dsp::kPunch ? i : dsp::kRegionLast[i];
            double up = 0, dn = 0;
            for (int k = i0; k <= i1; ++k) { up += v.hi[(size_t) k] - v.mid[(size_t) k]; dn += v.mid[(size_t) k] - v.lo[(size_t) k]; }
            up /= (i1 - i0 + 1); dn /= (i1 - i0 + 1);
            const double span = 6.0;
            auto xOf = [&] (double d) { return bar.getCentreX() + (float) (juce::jlimit (-span, span, d) / span) * bar.getWidth() * 0.5f; };
            g.setColour (t.accent.withAlpha (0.3f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (-dn), bar.getY() + 2, xOf (up), bar.getBottom() - 2));
            g.setColour (t.lineStrong);
            g.drawVerticalLine ((int) bar.getCentreX(), bar.getY() + 2, bar.getBottom() - 2);
            const bool outside = v.verdict[(size_t) i] != dsp::Verdict::inside;
            auto mark = juce::Rectangle<float> (5.0f, bar.getHeight() - 2).withCentre ({ xOf (v.devDb[(size_t) i]), bar.getCentreY() });
            if (outside)
            {
                g.setColour (t.accent);
                g.fillRoundedRectangle (mark.expanded (2.0f, 0.0f), 2.0f);
            }
            g.setColour (t.ink);
            g.fillRoundedRectangle (mark.reduced (outside ? 1.0f : 0.0f, 0.0f), 1.5f);
        }
        drawCaps (g, dsp::kRegionNames[i], cell.removeFromTop (16.0f).translated (0, 3), TextStyle::labelCaps, t.inkMuted, juce::Justification::centred);
        g.setColour (t.inkSubtle);
        g.setFont (font (TextStyle::meta));
        g.drawText (view == dsp::kPunch ? juce::String ("crest") : juce::String (kRegionRange[i]) + " Hz", cell.removeFromTop (14.0f), juce::Justification::centred);
    }
}

// ------------------------------------------------------------------ suggestions
void SuggestionLines::setSnapshot (const Snapshot& s, int v)
{
    snap = s;
    view = v;
    repaint();
}

void SuggestionLines::paint (juce::Graphics& g)
{
    const auto& t = tokens();
    const auto& v = snap.views[(size_t) view];
    auto r = getLocalBounds().toFloat();
    const float rowH = r.getHeight() / 2.0f;
    for (int i = 0; i < 2; ++i)
    {
        auto row = juce::Rectangle<float> (r.getX(), r.getY() + rowH * (float) i, r.getWidth(), rowH);
        g.setColour (t.line);
        g.drawHorizontalLine ((int) row.getY(), row.getX(), row.getRight());
        if (i < v.numSugg && ! snap.silent)
        {
            const auto& s = v.sugg[(size_t) i];
            juce::String text (juce::CharPointer_UTF8 (s.text));
            const auto colon = text.indexOfChar (':');
            const auto sentence = colon > 0 ? text.substring (colon + 1).trim() : text;
            drawCaps (g, dsp::kRegionNames[s.region], row.removeFromLeft (84.0f), TextStyle::labelCaps, t.inkMuted);
            auto since = row.removeFromRight (72.0f);
            g.setColour (t.inkSubtle);
            g.setFont (font (TextStyle::meta));
            g.drawText (sinceText (s), since, juce::Justification::centredRight);
            g.setColour (t.ink);
            g.setFont (font (TextStyle::body).boldened());
            g.drawFittedText (sentence.substring (0, 1).toUpperCase() + sentence.substring (1), row.toNearestInt(), juce::Justification::centredLeft, 1, 0.9f);
        }
        else if (i == 0 && v.hasTarget && ! snap.silent && v.numSugg == 0 && ! std::isnan (v.median[0]))
        {
            g.setColour (t.inkSubtle);
            g.setFont (font (TextStyle::body));
            g.drawText ("No suggestion: every region has stayed inside the band for 8 s or more, or only just left it.", row, juce::Justification::centredLeft);
        }
    }
    g.setColour (t.line);
    g.drawHorizontalLine ((int) r.getBottom() - 1, r.getX(), r.getRight());
}

// ------------------------------------------------------------------ target list
TargetList::TargetList (BenchLineAudioProcessor& p) : proc (p)
{
    build.setTooltip ("Analyse your reference songs into a target: the median and spread of their tonal balance.");
    build.setMouseClickGrabsKeyboardFocus (false);
    build.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose your reference songs", TargetIO::referencesFolder(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc) { if (fc.getResults().size() > 0) startBuildFrom (fc.getResults()); });
    };
    addAndMakeVisible (build);
    refresh();
}

void TargetList::startBuildFrom (const juce::Array<juce::File>& files)
{
    juce::Array<juce::File> audio;
    for (auto& f : files)
        if (f.existsAsFile()) audio.add (f);
        else if (f.isDirectory()) audio.addArray (f.findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg"));
    if (audio.isEmpty()) return;
    // a name: "Mine", or "Mine 2", ... when taken
    juce::String name = "Mine";
    for (int i = 2; TargetIO::fileForName (name).existsAsFile(); ++i) name = "Mine " + juce::String (i);
    auto* w = new juce::AlertWindow ("Build a target", "From " + juce::String (audio.size()) + (audio.size() == 1 ? " file. " : " files. ") + "Name it after the sound you want, for example \"Worship EP refs\".", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", name, "Name");
    w->addButton ("Build", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, audio] (int result)
    {
        const auto n = w->getTextEditorContents ("name").trim();
        if (result == 1) proc.startBuild (audio, n.isEmpty() ? juce::String ("Mine") : n);
        repaint();
    }), true);
}

void TargetList::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    juce::Array<juce::File> f;
    for (auto& s : files) f.add (juce::File (s));
    startBuildFrom (f);
}

void TargetList::refresh()
{
    rows.clear();
    juce::Array<Row> mine, builtIn;
    for (const auto& tf : TargetIO::list())
    {
        Row row;
        row.t = tf;
        dsp::Target t;
        juce::String err;
        if (tf.builtIn) t = dsp::slopeTarget();
        else TargetIO::load (tf.file, t, err);
        double s = 0;
        for (const auto& b : t.tone) { const auto e = dsp::edges (b, tolerance); s += 0.5 * (e[2] - e[0]); }
        row.spread = s / dsp::kBands;
        (tf.builtIn ? builtIn : mine).add (row);
    }
    Row h; h.header = true;
    h.title = "Mine";
    rows.add (h);
    if (mine.isEmpty()) { Row e; e.header = true; e.title = "-"; rows.add (e); }
    rows.addArray (mine);
    h.title = "Built in";
    rows.add (h);
    rows.addArray (builtIn);
    resized();
    repaint();
}

void TargetList::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (28);   // section title
    for (auto& row : rows)
        row.bounds = r.removeFromTop (row.header ? 26 : 32);
    r.removeFromTop (12);
    build.setBounds (r.removeFromTop (32).withWidth (juce::jmin (180, r.getWidth())));
}

void TargetList::paint (juce::Graphics& g)
{
    const auto& t = tokens();
    drawCaps (g, "Target", getLocalBounds().removeFromTop (20).toFloat(), TextStyle::sectionCaps, t.ink);
    const auto current = proc.currentTargetFile();
    const bool slope = proc.currentTargetIsSlope();
    for (int i = 0; i < rows.size(); ++i)
    {
        const auto& row = rows.getReference (i);
        auto b = row.bounds.toFloat();
        if (row.header)
        {
            if (row.title == "-")
            {
                g.setColour (t.inkSubtle);
                g.setFont (font (TextStyle::meta));
                g.drawText ("None yet: build one from your references.", b.withTrimmedLeft (6), juce::Justification::centredLeft);
            }
            else
                drawCaps (g, row.title, b.withTrimmedLeft (6).withTrimmedTop (6), TextStyle::labelCaps, t.inkMuted);
            continue;
        }
        const bool selected = (row.t.builtIn && slope) || (! row.t.builtIn && row.t.file == current && current != juce::File());
        if (selected)
        {
            g.setColour (t.surfacePanel);
            g.fillRoundedRectangle (b.reduced (0, 1), 4.0f);
        }
        else if (i == hover)
        {
            g.setColour (t.surfaceSunken);
            g.fillRoundedRectangle (b.reduced (0, 1), 4.0f);
        }
        g.setColour (t.line);
        g.drawHorizontalLine ((int) b.getBottom(), b.getX(), b.getRight());
        g.setColour (selected ? t.ground : t.ink);
        g.setFont (font (TextStyle::control).boldened());
        g.drawText (row.t.name, b.reduced (8, 0).withTrimmedRight (40), juce::Justification::centredLeft, true);
        g.setColour (selected ? t.ground : t.inkSubtle);
        g.setFont (font (TextStyle::meta));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String (row.spread, 1), b.reduced (8, 0), juce::Justification::centredRight);
    }
    // build progress (design plan 5: a hairline and "Analysing 3 of 6")
    const auto st = proc.buildStatus();
    auto info = juce::Rectangle<float> ((float) build.getX(), (float) build.getBottom() + 6, (float) getWidth(), 30);
    if (st.active)
    {
        g.setColour (t.line);
        g.fillRect (info.withHeight (2.0f));
        g.setColour (t.accent);
        g.fillRect (info.withHeight (2.0f).withWidth (info.getWidth() * (st.total > 0 ? (float) st.done / (float) st.total : 0.0f)));
    }
    g.setColour (t.inkSubtle);
    g.setFont (font (TextStyle::meta));
    const auto msg = st.active || st.message.isNotEmpty() ? st.message : juce::String ("Or drop audio files here: any songs you want to sound like.");
    g.drawFittedText (msg, info.withTrimmedTop (4).toNearestInt(), juce::Justification::topLeft, 2);
    if (dragOver)
    {
        g.setColour (t.accent);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1), 6.0f, 2.0f);
    }
}

void TargetList::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < rows.size(); ++i)
        if (! rows[i].header && rows[i].bounds.contains (e.getPosition())) h = i;
    if (h != hover) { hover = h; repaint(); }
}

void TargetList::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void TargetList::mouseUp (const juce::MouseEvent& e)
{
    for (const auto& row : rows)
        if (! row.header && row.bounds.contains (e.getPosition()))
        {
            if (e.mods.isPopupMenu() && ! row.t.builtIn)
            {
                juce::PopupMenu m;
                m.addItem ("Show in folder", [f = row.t.file] { f.revealToUser(); });
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
                return;
            }
            juce::String err;
            if (! proc.selectTarget (row.t, &err))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Bench Line", "This target does not load: " + err);
            repaint();
            return;
        }
}

void TargetList::timerTick()
{
    const auto st = proc.buildStatus();
    if (st.active || st.message != lastMessage) repaint();
    if (wasBuilding && ! st.active) refresh();
    wasBuilding = st.active;
    lastMessage = st.message;
}
} // namespace benchline::ui
