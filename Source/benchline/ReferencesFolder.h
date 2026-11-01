#pragma once

/*  STAND-IN for Source/oberli/ References (shared code, in progress in bob-skills): where the
    reference folder shared by A B see and Bench Line lives. Replace the body by the shared
    module when it lands (StandIns.md). BENCHLINE_REFERENCES overrides it (tests, harness). */

#include <juce_core/juce_core.h>

namespace benchline
{
inline juce::File referencesFolder()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("BENCHLINE_REFERENCES", {});
    if (env.isNotEmpty()) return juce::File (env);
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Oberli Audio").getChildFile ("References");
}
} // namespace benchline
