#include "Look.h"

namespace benchline::ui
{
const Tokens& tokens()
{
    static const Tokens t = []
    {
        const auto& p = oberli::ui::front();
        Tokens k;
        k.ground = p.ground; k.surfaceRaised = p.surfaceRaised; k.surfaceSunken = p.surfaceSunken;
        k.surfacePanel = p.surfacePanel; k.hover = p.hover;
        k.line = p.line; k.lineStrong = p.lineStrong; k.lineControl = p.lineControl;
        k.ink = p.ink; k.inkMuted = p.inkMuted; k.inkSubtle = p.inkSubtle; k.accentInk = p.accentInk;
        k.meterLevel = p.meterLevel; k.meterHot = p.meterHot; k.clip = p.clip;
        k.accent = oberli::ui::Tokens::accent(); k.onAccent = oberli::ui::Tokens::onAccent();
        return k;
    }();
    return t;
}

juce::Font font (TextStyle s)
{
    namespace type = oberli::ui::type;
    switch (s)
    {
        case TextStyle::pluginName:    return type::pluginName();
        case TextStyle::sectionCaps:   return type::section();
        case TextStyle::labelCaps:     return type::label();
        case TextStyle::value:         return type::value();
        case TextStyle::control:       return type::control();
        case TextStyle::controlStrong: return type::controlStrong();
        case TextStyle::meta:          return type::meta();
        case TextStyle::body:          return type::small();          // Nunito 13, never below 13 px
        case TextStyle::bodyStrong:    return type::text (true, 13.0f);
    }
    return type::control();
}

void drawCaps (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, TextStyle s, juce::Colour c, juce::Justification j)
{
    oberli::ui::type::draw (g, text.toUpperCase(), r, font (s), c, j);
}
} // namespace benchline::ui
