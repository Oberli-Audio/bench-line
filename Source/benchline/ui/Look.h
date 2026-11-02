#pragma once

/*  Bench Line's names for the family look (Source/oberli/OberliLookAndFeel.h): the Bone palette
    under the names the Bench Line parts use (BalanceView.h), and their text styles mapped onto the
    shared type scale (Rubik; Nunito for running text). The editor's LookAndFeel is the Strip
    frame's own; nothing here draws a widget. */

#include "../../oberli/OberliLookAndFeel.h"

namespace benchline::ui
{
struct Tokens
{
    juce::Colour ground, surfaceRaised, surfaceSunken, surfacePanel, hover;
    juce::Colour line, lineStrong, lineControl;
    juce::Colour ink, inkMuted, inkSubtle, accentInk;
    juce::Colour meterLevel, meterHot, clip;
    juce::Colour accent, onAccent;
};
/** The plugin's theme (oberli::ui::front(): Bone) and its accent (OBERLI_ACCENT: peacock). */
const Tokens& tokens();

enum class TextStyle { pluginName, sectionCaps, labelCaps, value, control, controlStrong, meta, body, bodyStrong };
juce::Font font (TextStyle s);
/** Caps text (the style's tracking comes with the font), centred in its line box as CSS does. */
void drawCaps (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, TextStyle s, juce::Colour c,
               juce::Justification j = juce::Justification::centredLeft);
} // namespace benchline::ui
