#pragma once

/*  STAND-IN for Source/oberli/ LookAndFeel, Strip frame and meters (shared code, in progress in
    bob-skills; StandIns.md). The Bone tokens of the brand sheet, the knob of the logo and the
    type sizes, drawn plainly so the Bench Line parts (BalanceView.h) have something to sit in.
    When the shared module lands, BenchLineLook becomes a thin subclass of it (or goes) and the
    Bench Line components take their colours from it through tokens(). The font is JUCE's sans
    until the shared LookAndFeel brings Rubik (Waiting for Björn in docs/status.md). */

#include <juce_gui_basics/juce_gui_basics.h>

namespace benchline::ui
{
struct Tokens
{
    // Bone (effects), brand sheet "Palette"
    juce::Colour ground { 0xFFEDE8DF }, surfaceRaised { 0xFFF6F2EB }, surfaceSunken { 0xFFE6E0D6 }, surfacePanel { 0xFF1C1B19 };
    juce::Colour line { 0xFFD3CCC0 }, lineStrong { 0xFFBDB6AA }, lineControl { 0xFF77726A };
    juce::Colour ink { 0xFF1C1B19 }, inkMuted { 0xFF45423E }, inkSubtle { 0xFF5C574F };
    juce::Colour meterLevel { 0xFF1C1B19 }, meterHot { 0xFF7D5C00 }, clip { 0xFFB02A24 };
    juce::Colour accent { 0xFF039BE5 };   // peacock: the category accent for analysis (CLAUDE.md, 2026-11-01)
    juce::Colour onAccent { 0xFF1C1B19 };
};
const Tokens& tokens();

enum class TextStyle { pluginName, sectionCaps, labelCaps, value, control, meta, body };
juce::Font font (TextStyle s);
/** Draws caps text with the brand's letter spacing. */
void drawCaps (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, TextStyle s, juce::Colour c,
               juce::Justification j = juce::Justification::centredLeft);

class BenchLineLook : public juce::LookAndFeel_V4
{
public:
    BenchLineLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (TextStyle::control); }
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Font getPopupMenuFont() override { return font (TextStyle::control); }
};
} // namespace benchline::ui
