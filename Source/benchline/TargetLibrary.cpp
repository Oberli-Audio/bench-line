#include "TargetLibrary.h"

#include <cmath>

#include "ReferencesFolder.h"

namespace benchline
{
namespace
{
constexpr const char* kTargetSuffix = ".benchline-target.json";

double q2 (double v) { return std::round (v * 100.0) / 100.0; }       // frame features: 0.01 dB
double q4 (double v) { return std::round (v * 10000.0) / 10000.0; }   // stored percentiles: 0.0001 dB

juce::var rowsToVar (const double* data, int rows, int cols)
{
    juce::Array<juce::var> out;
    for (int r = 0; r < rows; ++r)
    {
        juce::Array<juce::var> row;
        for (int c = 0; c < cols; ++c) row.add (data[r * cols + c]);
        out.add (row);
    }
    return out;
}

template <size_t R>
bool varToRows (const juce::var& v, std::array<std::array<double, dsp::kPercentiles>, R>& out)
{
    auto* rows = v.getArray();
    if (rows == nullptr || rows->size() != (int) R) return false;
    for (int r = 0; r < (int) R; ++r)
    {
        auto* row = (*rows)[r].getArray();
        if (row == nullptr || row->size() != dsp::kPercentiles) return false;
        for (int c = 0; c < dsp::kPercentiles; ++c)
        {
            const auto& x = (*row)[c];
            if (! (x.isDouble() || x.isInt() || x.isInt64())) return false;
            const double d = (double) x;
            if (! std::isfinite (d)) return false;
            out[(size_t) r][(size_t) c] = d;
        }
        for (int c = 1; c < dsp::kPercentiles; ++c)
            if (out[(size_t) r][(size_t) c] < out[(size_t) r][(size_t) c - 1]) return false;   // percentiles ascend
    }
    return true;
}

juce::String cacheKey (const juce::File& f)
{
    const auto id = f.getFullPathName() + "|" + juce::String (f.getSize()) + "|" + juce::String (f.getLastModificationTime().toMilliseconds())
                  + "|" + juce::String (TargetIO::kAnalysisVersion);
    return f.getFileNameWithoutExtension().retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_").substring (0, 40)
           + "-" + juce::String::toHexString (id.hashCode64());
}
} // namespace

// ------------------------------------------------------------------ folders and files
juce::File TargetIO::referencesFolder() { return benchline::referencesFolder(); }
juce::File TargetIO::targetsFolder() { return referencesFolder().getChildFile ("Targets"); }
juce::File TargetIO::cacheFolder() { return referencesFolder().getChildFile (".analysis"); }

juce::File TargetIO::fileForName (const juce::String& name)
{
    auto safe = juce::File::createLegalFileName (name.trim());
    if (safe.isEmpty()) safe = "Mine";
    return targetsFolder().getChildFile (safe + kTargetSuffix);
}

juce::var TargetIO::toJson (const dsp::Target& t)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", "oberli-target");
    o->setProperty ("version", kFormatVersion);
    o->setProperty ("name", juce::String (t.name));
    o->setProperty ("frames", (juce::int64) t.frames);
    juce::Array<juce::var> pct, bands, regions;
    for (double p : dsp::kPercentileList) pct.add (p);
    for (double b : dsp::kBandNominal) bands.add (b);
    for (auto* r : dsp::kRegionNames) regions.add (juce::String (r));
    o->setProperty ("percentiles", pct);
    o->setProperty ("bands_hz", bands);
    o->setProperty ("regions", regions);
    o->setProperty ("tone", t.hasTone ? rowsToVar (t.tone[0].data(), dsp::kBands, dsp::kPercentiles) : juce::var());
    o->setProperty ("side", t.hasSide ? rowsToVar (t.side[0].data(), dsp::kBands, dsp::kPercentiles) : juce::var());
    o->setProperty ("punch", t.hasPunch ? rowsToVar (t.punch[0].data(), dsp::kRegions, dsp::kPercentiles) : juce::var());
    return juce::var (o);
}

bool TargetIO::fromJson (const juce::var& v, dsp::Target& out, juce::String& error)
{
    if (v.getProperty ("format", {}).toString() != "oberli-target") { error = "not a Bench Line target"; return false; }
    const int version = (int) v.getProperty ("version", 0);
    if (version < 1 || version > kFormatVersion) { error = "target format version " + juce::String (version) + " is newer than this Bench Line"; return false; }
    auto* pct = v.getProperty ("percentiles", {}).getArray();
    if (pct == nullptr || pct->size() != dsp::kPercentiles) { error = "wrong percentile list"; return false; }
    for (int i = 0; i < dsp::kPercentiles; ++i)
        if (std::abs ((double) (*pct)[i] - dsp::kPercentileList[i]) > 1e-9) { error = "wrong percentile list"; return false; }
    dsp::Target t;
    t.name = v.getProperty ("name", "Target").toString().toStdString();
    t.frames = (juce::int64) v.getProperty ("frames", 0);
    const auto tone = v.getProperty ("tone", {}), side = v.getProperty ("side", {}), punch = v.getProperty ("punch", {});
    if (! tone.isVoid() && ! tone.isUndefined()) { if (! varToRows (tone, t.tone)) { error = "bad tone table"; return false; } t.hasTone = true; }
    if (! side.isVoid() && ! side.isUndefined()) { if (! varToRows (side, t.side)) { error = "bad side table"; return false; } t.hasSide = true; }
    if (! punch.isVoid() && ! punch.isUndefined()) { if (! varToRows (punch, t.punch)) { error = "bad punch table"; return false; } t.hasPunch = true; }
    if (! t.hasTone) { error = "the target has no tone table"; return false; }
    out = t;
    return true;
}

bool TargetIO::save (const dsp::Target& t, const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    return file.replaceWithText (juce::JSON::toString (toJson (t), false, 6));
}

bool TargetIO::load (const juce::File& file, dsp::Target& out, juce::String& error)
{
    if (! file.existsAsFile()) { error = "no such file"; return false; }
    if (file.getSize() > 4 * 1024 * 1024) { error = "file too large for a target"; return false; }
    juce::var v;
    const auto res = juce::JSON::parse (file.loadFileAsString(), v);
    if (res.failed()) { error = res.getErrorMessage(); return false; }
    return fromJson (v, out, error);
}

juce::Array<TargetFile> TargetIO::list()
{
    juce::Array<TargetFile> out;
    out.add ({ "Plain slope", {}, true });
    auto files = targetsFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + kTargetSuffix);
    files.sort();
    for (auto& f : files)
    {
        dsp::Target t;
        juce::String err;
        if (load (f, t, err))
            out.add ({ juce::String (t.name), f, false });
    }
    return out;
}

// ------------------------------------------------------------------ one file
bool FileAnalysis::analyse (const juce::File& audio, dsp::TargetBuilder& out, juce::String& error, const std::atomic<bool>* cancel, bool useCache)
{
    const auto cache = TargetIO::cacheFolder().getChildFile (cacheKey (audio) + ".json");
    if (useCache && cache.existsAsFile())
    {
        juce::var v;
        if (juce::JSON::parse (cache.loadFileAsString(), v).wasOk() && v.getProperty ("format", {}).toString() == "oberli-analysis"
            && (int) v.getProperty ("version", 0) == TargetIO::kAnalysisVersion)
        {
            auto* frames = v.getProperty ("frames", {}).getArray();
            if (frames != nullptr)
            {
                dsp::TargetBuilder local;
                bool ok = true;
                for (auto& fv : *frames)
                {
                    auto* a = fv.getArray();
                    if (a == nullptr || a->size() != 2 * dsp::kBands + dsp::kRegions) { ok = false; break; }
                    dsp::Frame f;
                    f.gated = true;
                    for (int b = 0; b < dsp::kBands; ++b) { f.tone[(size_t) b] = (double) (*a)[b] / 100.0; f.side[(size_t) b] = (double) (*a)[dsp::kBands + b] / 100.0; }
                    for (int r = 0; r < dsp::kRegions; ++r) f.punch[(size_t) r] = (double) (*a)[2 * dsp::kBands + r] / 100.0;
                    local.add (f);
                }
                if (ok) { out.merge (local); return true; }
            }
        }
    }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (audio));
    if (reader == nullptr) { error = audio.getFileName() + ": not an audio file this build can read"; return false; }
    if (reader->sampleRate < 8000.0 || reader->sampleRate > 384000.0) { error = audio.getFileName() + ": unsupported sample rate"; return false; }

    juce::ScopedNoDenormals noDenormals;
    dsp::Engine engine;
    engine.prepare (reader->sampleRate);
    dsp::TargetBuilder local;
    juce::Array<juce::var> cached;
    auto onFrame = [&] (const dsp::Frame& fr)
    {
        if (! fr.gated) return;
        dsp::Frame f = fr;
        juce::Array<juce::var> row;
        for (int b = 0; b < dsp::kBands; ++b) { f.tone[(size_t) b] = q2 (f.tone[(size_t) b]); row.add ((int) std::lround (f.tone[(size_t) b] * 100.0)); }
        for (int b = 0; b < dsp::kBands; ++b) { f.side[(size_t) b] = q2 (f.side[(size_t) b]); row.add ((int) std::lround (f.side[(size_t) b] * 100.0)); }
        for (int r = 0; r < dsp::kRegions; ++r) { f.punch[(size_t) r] = q2 (f.punch[(size_t) r]); row.add ((int) std::lround (f.punch[(size_t) r] * 100.0)); }
        local.add (f);
        cached.add (row);
    };
    const int chunk = 1 << 15;
    juce::AudioBuffer<float> buf ((int) juce::jmax (1u, reader->numChannels), chunk);
    for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += chunk)
    {
        if (cancel != nullptr && cancel->load()) { error = "cancelled"; return false; }
        const int n = (int) juce::jmin ((juce::int64) chunk, reader->lengthInSamples - pos);
        reader->read (&buf, 0, n, pos, true, true);
        engine.process (buf.getReadPointer (0), buf.getNumChannels() > 1 ? buf.getReadPointer (1) : nullptr, n, onFrame);
    }
    engine.flush (onFrame);
    if (local.frames() == 0) { error = audio.getFileName() + ": nothing above -40 LUFS"; return false; }

    if (useCache)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("format", "oberli-analysis");
        o->setProperty ("version", TargetIO::kAnalysisVersion);
        o->setProperty ("file", audio.getFileName());
        o->setProperty ("sample_rate", reader->sampleRate);
        o->setProperty ("layout", "31 tone, 31 side (0.01 dB re frame loudness), 6 punch crest (0.01 dB)");
        o->setProperty ("frames", cached);
        cache.getParentDirectory().createDirectory();
        cache.replaceWithText (juce::JSON::toString (juce::var (o), true));
    }
    out.merge (local);
    return true;
}

// ------------------------------------------------------------------ the build
TargetBuild::Result TargetBuild::build (const juce::Array<juce::File>& files, const juce::String& name, const juce::File& outFile,
                                        std::function<void (int, int)> progress, const std::atomic<bool>* cancel, bool useCache)
{
    Result res;
    res.name = name.trim().isEmpty() ? juce::String ("Mine") : name.trim();
    res.files = files.size();
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    std::vector<dsp::TargetBuilder> parts ((size_t) files.size());
    std::vector<juce::String> errors ((size_t) files.size());
    std::vector<char> ok ((size_t) files.size(), 0);
    std::atomic<int> done { 0 };
    {
        const int threads = juce::jlimit (1, 8, juce::SystemStats::getNumCpus() - 1);
        juce::ThreadPool pool (juce::ThreadPoolOptions{}.withThreadName ("Bench Line build").withNumberOfThreads (threads));
        for (int i = 0; i < files.size(); ++i)
            pool.addJob ([&, i]
            {
                ok[(size_t) i] = FileAnalysis::analyse (files[i], parts[(size_t) i], errors[(size_t) i], cancel, useCache) ? 1 : 0;
                const int d = ++done;
                if (progress) progress (d, files.size());
            });
        while (pool.getNumJobs() > 0)
            juce::Thread::sleep (5);
    }
    dsp::TargetBuilder all;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (ok[i]) all.merge (parts[i]);
        else { ++res.filesFailed; if (res.error.isEmpty()) res.error = errors[i]; }
    }
    if (cancel != nullptr && cancel->load()) { res.error = "cancelled"; return res; }
    dsp::Target t;
    if (! all.build (res.name.toStdString(), t))
    {
        if (res.error.isEmpty()) res.error = "no usable audio in the files";
        return res;
    }
    for (auto* tab : { &t.tone, &t.side })
        for (auto& row : *tab) for (auto& x : row) x = q4 (x);
    for (auto& row : t.punch) for (auto& x : row) x = q4 (x);
    res.frames = t.frames;
    res.file = outFile == juce::File() ? TargetIO::fileForName (res.name) : outFile;
    if (! TargetIO::save (t, res.file)) { res.error = "cannot write " + res.file.getFullPathName(); return res; }
    res.ok = true;
    res.seconds = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    return res;
}
} // namespace benchline
