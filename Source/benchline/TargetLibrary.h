#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>

#include "Engine.h"

/*  Targets on disk: the shared reference folder (brief Q10, docs/references-folder.md).

      <Documents>/Oberli Audio/References/            the user's folder, shared with A B see
          Targets/<name>.benchline-target.json        target files (small, shareable)
          .analysis/<key>.json                        per-file frame cache (versioned)

    BENCHLINE_REFERENCES overrides the folder (tests, the harness). Nothing here runs on the
    audio thread: loading happens on the message thread, building on a background pool. */
namespace benchline
{
struct TargetFile
{
    juce::String name;
    juce::File file;        // empty for the built-in slope
    bool builtIn = false;
};

namespace TargetIO
{
inline constexpr int kFormatVersion = 1;
inline constexpr int kAnalysisVersion = 1;   // bump when the engine's frame features change
juce::File referencesFolder();
juce::File targetsFolder();
juce::File cacheFolder();
juce::var toJson (const dsp::Target& t);
bool fromJson (const juce::var& v, dsp::Target& out, juce::String& error);
bool save (const dsp::Target& t, const juce::File& file);
bool load (const juce::File& file, dsp::Target& out, juce::String& error);
juce::File fileForName (const juce::String& name);
/** Every target: the built-in slope first, then the folder's files by name. */
juce::Array<TargetFile> list();
} // namespace TargetIO

/** Analyses one audio file into gated frames, through the per-file cache. */
struct FileAnalysis
{
    static bool analyse (const juce::File& audio, dsp::TargetBuilder& out, juce::String& error,
                         const std::atomic<bool>* cancel = nullptr, bool useCache = true);
};

/** Builds a target from files on a thread pool; progress and the result reach the caller
    through callbacks on the message thread (or synchronously with build()). */
class TargetBuild
{
public:
    struct Result
    {
        bool ok = false;
        juce::String error, name;
        juce::File file;
        int files = 0, filesFailed = 0;
        juce::int64 frames = 0;
        double seconds = 0;
    };
    /** Synchronous: analyses the files (in parallel), writes <name>.benchline-target.json to
        `out` (or the targets folder), returns what happened. */
    static Result build (const juce::Array<juce::File>& files, const juce::String& name, const juce::File& out,
                         std::function<void (int done, int total)> progress = {},
                         const std::atomic<bool>* cancel = nullptr, bool useCache = true);
};
} // namespace benchline
