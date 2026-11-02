#pragma once

/*  Where the reference folder shared by A B see and Bench Line lives: the family's
    (oberli::folders::referencesFolder(): ~/Music/Oberli Audio/References, PRESET_ROOT moves it).
    BENCHLINE_REFERENCES overrides it (tests, harness, the measurement scripts). The targets and
    the analysis cache below it stay Bench Line's (TargetLibrary.h; brief Q10). */

#include <juce_core/juce_core.h>

#include "../oberli/Folders.h"

namespace benchline
{
inline juce::File referencesFolder()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("BENCHLINE_REFERENCES", {});
    if (env.isNotEmpty()) return juce::File (env);
    return oberli::folders::referencesFolder();
}
} // namespace benchline
