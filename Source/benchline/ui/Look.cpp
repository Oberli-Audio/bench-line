#include "Look.h"

namespace benchline::ui
{
const Tokens& tokens()
{
    static const Tokens t;
    return t;
}

juce::Font font (TextStyle s)
{
    auto f = [] (float size, bool bold) { return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)); };
    switch (s)
    {
        case TextStyle::pluginName:  return f (15.0f, true);
        case TextStyle::sectionCaps: return f (11.0f, true);
        case TextStyle::labelCaps:   return f (10.0f, true);
        case TextStyle::value:       return f (12.0f, true);
        case TextStyle::control:     return f (12.0f, false);
        case TextStyle::meta:        return f (11.0f, false);
        case TextStyle::body:        return f (12.0f, false);
    }
    return f (12.0f, false);
}

void drawCaps (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, TextStyle s, juce::Colour c, juce::Justification j)
{
    auto f = font (s);
    const float spacing = s == TextStyle::sectionCaps ? 0.12f : 0.1f;
    f.setExtraKerningFactor (spacing);
    g.setFont (f);
    g.setColour (c);
    g.drawText (text.toUpperCase(), r, j, false);
}

BenchLineLook::BenchLineLook()
{
    const auto& t = tokens();
    setColour (juce::ResizableWindow::backgroundColourId, t.ground);
    setColour (juce::PopupMenu::backgroundColourId, t.surfaceRaised);
    setColour (juce::PopupMenu::textColourId, t.ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, t.surfaceSunken);
    setColour (juce::PopupMenu::highlightedTextColourId, t.ink);
    setColour (juce::TextButton::textColourOffId, t.ink);
    setColour (juce::TextButton::textColourOnId, t.onAccent);
    setColour (juce::Label::textColourId, t.ink);
    setColour (juce::TooltipWindow::backgroundColourId, t.surfacePanel);
    setColour (juce::TooltipWindow::textColourId, t.ground);
    setColour (juce::ScrollBar::thumbColourId, t.lineStrong);
}

// The knob of the logo (brand sheet "Knob"): cap 72 % of the box in surface-panel with a 1.5 px
// line-control ring, a rounded-bar pointer 17 % of the cap wide and 34 % long, tip 7.5 % in,
// 270 degrees from 7:30, track line-strong, value arc in the accent.
void BenchLineLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto& t = tokens();
    auto box = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float size = juce::jmin (box.getWidth(), box.getHeight());
    box = box.withSizeKeepingCentre (size, size);
    const auto c = box.getCentre();
    const float rArc = size * 0.5f - 2.0f;
    const float angle = start + pos * (end - start);
    juce::Path track, value;
    track.addCentredArc (c.x, c.y, rArc, rArc, 0.0f, start, end, true);
    value.addCentredArc (c.x, c.y, rArc, rArc, 0.0f, start, angle, true);
    const float stroke = juce::jmax (2.5f, size * 0.05f);
    g.setColour (t.lineStrong);
    g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (s.isEnabled() ? t.accent : t.lineStrong);
    g.strokePath (value, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float cap = size * 0.72f;
    auto capBox = juce::Rectangle<float> (cap, cap).withCentre (c);
    g.setColour (t.surfacePanel);
    g.fillEllipse (capBox);
    g.setColour (t.lineControl);
    g.drawEllipse (capBox, 1.5f);
    const float pw = cap * 0.17f, pl = cap * 0.34f, tip = cap * 0.075f;
    juce::Path pointer;
    pointer.addRoundedRectangle (-pw * 0.5f, -cap * 0.5f + tip, pw, pl, pw * 0.5f);
    g.setColour (t.accent);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c));
    if (s.hasKeyboardFocus (true))
    {
        g.setColour (t.ink);
        g.drawEllipse (box.expanded (3.0f), 2.0f);
    }
}

void BenchLineLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto& t = tokens();
    auto r = b.getLocalBounds().toFloat().reduced (0.75f);
    const float radius = b.getProperties().getWithDefault ("pill", false) ? r.getHeight() * 0.5f : 10.0f;
    if (b.getToggleState() && (bool) b.getProperties().getWithDefault ("segment", false))
    {
        g.setColour (t.accent);
        g.fillRoundedRectangle (r, radius);
        return;
    }
    if (over || down)
    {
        g.setColour (t.surfaceSunken);
        g.fillRoundedRectangle (r, radius);
    }
    if (! (bool) b.getProperties().getWithDefault ("ghost", false))
    {
        g.setColour (t.lineControl);
        g.drawRoundedRectangle (r, radius, 1.0f);
    }
}

void BenchLineLook::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const auto& t = tokens();
    g.setFont (font (TextStyle::control));
    g.setColour (b.getToggleState() && (bool) b.getProperties().getWithDefault ("segment", false) ? t.onAccent : t.ink);
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}

void BenchLineLook::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    const auto& t = tokens();
    g.setColour (t.surfacePanel);
    g.fillRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, 6.0f);
    g.setColour (t.ground);
    g.setFont (font (TextStyle::body));
    g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (10, 6), juce::Justification::centredLeft, 4);
}

juce::Rectangle<int> BenchLineLook::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    const int w = 280;
    juce::AttributedString s;
    s.append (text, font (TextStyle::body));
    juce::TextLayout tl;
    tl.createLayout (s, (float) w - 20.0f);
    const int h = (int) tl.getHeight() + 14;
    return juce::Rectangle<int> (pos.x + 12, pos.y + 16, w, h).constrainedWithin (parent);
}

void BenchLineLook::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    const auto& t = tokens();
    g.fillAll (t.surfaceRaised);
    g.setColour (t.line);
    g.drawRect (0, 0, w, h);
}
} // namespace benchline::ui
