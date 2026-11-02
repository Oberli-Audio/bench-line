#pragma once

/*  The factory bank inside the binary (presets.csv order, presets/<slug>.oberlipreset), for the
    header's preset pill. STAND-IN for the shared preset drawer of the Strip frame
    (StandIns.md); the files and their format stay Bench Line's (State.h). */

#include <juce_audio_processors/juce_audio_processors.h>

#include "BenchLinePresets.h"
#include "../State.h"

namespace benchline
{
struct FactoryPresets
{
    struct Entry { juce::String slug, name, category; std::unique_ptr<juce::XmlElement> xml; };

    static const juce::OwnedArray<Entry>& list()
    {
        static juce::OwnedArray<Entry> bank = []
        {
            juce::OwnedArray<Entry> out;
            auto find = [] (const juce::String& file) -> juce::String
            {
                for (int i = 0; i < BenchLinePresets::namedResourceListSize; ++i)
                    if (juce::String (BenchLinePresets::originalFilenames[i]) == file)
                    {
                        int size = 0;
                        const char* data = BenchLinePresets::getNamedResource (BenchLinePresets::namedResourceList[i], size);
                        return juce::String::fromUTF8 (data, size);
                    }
                return {};
            };
            auto lines = juce::StringArray::fromLines (find ("presets.csv"));
            for (int i = 1; i < lines.size(); ++i)
            {
                auto cells = juce::StringArray::fromTokens (lines[i], ",", "\"");
                if (cells.size() < 3) continue;
                auto e = std::make_unique<Entry>();
                e->slug = cells[0];
                e->name = cells[1];
                e->category = cells[2];
                e->xml = juce::parseXML (find (cells[0] + ".oberlipreset"));
                if (e->xml != nullptr) out.add (std::move (e));
            }
            return out;
        }();
        return bank;
    }

    static bool load (juce::AudioProcessorValueTreeState& apvts, int index)
    {
        const auto& bank = list();
        if (! juce::isPositiveAndBelow (index, bank.size())) return false;
        auto* state = State::stateOf (*bank[index]->xml);
        return state != nullptr && State::fromXml (*state, apvts);
    }

    /** The modified mark by comparison (plugin-ui-bob code checklist): every parameter but bypass. */
    static bool matches (juce::AudioProcessorValueTreeState& apvts, int index)
    {
        const auto& bank = list();
        if (! juce::isPositiveAndBelow (index, bank.size())) return true;
        auto* state = State::stateOf (*bank[index]->xml);
        auto* params = state != nullptr ? state->getChildByName ("PARAMETERS") : nullptr;
        if (params == nullptr) return true;
        for (auto* e : params->getChildWithTagNameIterator ("PARAM"))
        {
            const auto id = e->getStringAttribute ("id");
            if (id == "bypass") continue;
            auto* p = apvts.getParameter (id);
            if (p == nullptr) continue;
            const double now = p->convertFrom0to1 (p->getValue());
            if (std::abs (now - e->getDoubleAttribute ("value")) > 1e-3 * juce::jmax (1.0, std::abs (now))) return false;
        }
        return true;
    }
};
} // namespace benchline
