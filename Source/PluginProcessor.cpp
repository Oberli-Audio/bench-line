#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "State.h"

namespace
{
std::atomic<float>* param (juce::AudioProcessorValueTreeState& apvts, const char* id)
{
    auto* p = apvts.getRawParameterValue (id);
    jassert (p != nullptr);   // the table and the code disagree: parameters.csv is the source
    return p;
}

juce::String joinRow (const std::array<double, benchline::dsp::kBands>& row)
{
    juce::StringArray s;
    for (double v : row) s.add (std::isnan (v) ? juce::String ("nan") : juce::String (v, 4));
    return s.joinIntoString (" ");
}

bool parseRow (const juce::String& text, std::array<double, benchline::dsp::kBands>& row)
{
    auto parts = juce::StringArray::fromTokens (text, " ", "");
    if (parts.size() != benchline::dsp::kBands) return false;
    for (int i = 0; i < benchline::dsp::kBands; ++i)
    {
        const auto t = parts[i].trim();
        row[(size_t) i] = t == "nan" ? std::nan ("") : t.getDoubleValue();
        if (t != "nan" && ! std::isfinite (row[(size_t) i])) return false;
    }
    return true;
}
} // namespace

//==============================================================================
class BenchLineAudioProcessor::BuildThread : public juce::Thread
{
public:
    BuildThread (BenchLineAudioProcessor& p, juce::Array<juce::File> f, juce::String n)
        : juce::Thread ("Bench Line target build"), owner (p), files (std::move (f)), name (std::move (n)) {}
    ~BuildThread() override { cancel.store (true); stopThread (10000); }
    void run() override
    {
        auto res = benchline::TargetBuild::build (files, name, {}, [this] (int done, int total)
        {
            const juce::ScopedLock sl (owner.buildLock);
            owner.status.done = done;
            owner.status.total = total;
            owner.status.message = "Analysing " + juce::String (juce::jmin (done + 1, total)) + " of " + juce::String (total);
        }, &cancel);
        {
            const juce::ScopedLock sl (owner.buildLock);
            owner.status.active = false;
            owner.status.lastOk = res.ok;
            owner.status.lastFile = res.file;
            owner.status.message = res.ok ? (juce::String (res.files - res.filesFailed) + " of " + juce::String (res.files) + " files, "
                                             + juce::String (res.frames) + " frames, " + juce::String (res.seconds, 1) + " s"
                                             + (res.filesFailed > 0 ? "; skipped: " + res.error : juce::String()))
                                          : res.error;
        }
        if (res.ok)
        {
            auto file = res.file;
            juce::MessageManager::callAsync ([safe = juce::WeakReference<BuildThread> (this), file, &p = owner]
            {
                if (safe != nullptr) p.selectTarget ({ {}, file, false });
            });
        }
    }
    std::atomic<bool> cancel { false };
private:
    BenchLineAudioProcessor& owner;
    juce::Array<juce::File> files;
    juce::String name;
    JUCE_DECLARE_WEAK_REFERENCEABLE (BuildThread)
};

//==============================================================================
BenchLineAudioProcessor::BenchLineAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", Params::createParameterLayout())
{
    bypassParam = param (apvts, Params::bypassParameterId);
    analysis.bindParameters (param (apvts, Params::average), param (apvts, Params::tolerance), param (apvts, Params::hold));
    // init: the target called "Mine" when the library has one, else none (brief, init_patch)
    for (const auto& t : benchline::TargetIO::list())
        if (! t.builtIn && t.name == "Mine") { selectTarget (t); break; }
}

BenchLineAudioProcessor::~BenchLineAudioProcessor()
{
    builder.reset();
}

void BenchLineAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    analysis.prepare (sampleRate, samplesPerBlock);
}

void BenchLineAudioProcessor::releaseResources() {}

bool BenchLineAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return in == out;   // a wire: as many channels out as in
}

juce::AudioProcessorParameter* BenchLineAudioProcessor::getBypassParameter() const
{
    return apvts.getParameter (Params::bypassParameterId);
}

void BenchLineAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // a wire either way; the analysis pauses while bypassed
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
    analysis.setBypassed (true);
}

void BenchLineAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);
    if (numSamples == 0)   // a zero-length block is legal (harness contract, D.5.1)
        return;
    if (bypassParam->load (std::memory_order_relaxed) >= 0.5f)
    {
        processBlockBypassed (buffer, midi);
        return;
    }
    analysis.setBypassed (false);
    if (auto* ph = getPlayHead())
    {
        const auto pos = ph->getPosition();
        const bool playing = pos.hasValue() && pos->getIsPlaying();
        const double t = pos.hasValue() && pos->getTimeInSeconds().hasValue() ? *pos->getTimeInSeconds() : -1.0;
        analysis.setSongPosition (t, playing && t >= 0.0);
    }
    const int inputs = juce::jmin (2, getTotalNumInputChannels());
    if (inputs > 0)
        analysis.push (buffer.getReadPointer (0), inputs > 1 ? buffer.getReadPointer (1) : nullptr, numSamples);
}

juce::AudioProcessorEditor* BenchLineAudioProcessor::createEditor()
{
    return new BenchLineAudioProcessorEditor (*this);
}

//==============================================================================
bool BenchLineAudioProcessor::selectTarget (const benchline::TargetFile& t, juce::String* error)
{
    if (t.builtIn)
    {
        analysis.setTarget (std::make_shared<benchline::dsp::Target> (benchline::dsp::slopeTarget()), {});
        slopeSelected = true;
        return true;
    }
    if (t.file == juce::File())
    {
        clearTarget();
        return true;
    }
    auto target = std::make_shared<benchline::dsp::Target>();
    juce::String err;
    if (! benchline::TargetIO::load (t.file, *target, err))
    {
        if (error != nullptr) *error = err;
        return false;
    }
    analysis.setTarget (std::move (target), t.file.getFullPathName());
    slopeSelected = false;
    return true;
}

void BenchLineAudioProcessor::clearTarget()
{
    analysis.setTarget (nullptr, {});
    slopeSelected = false;
}

juce::String BenchLineAudioProcessor::currentTargetName() const
{
    auto t = const_cast<BenchLineAudioProcessor*> (this)->analysis.getTarget();
    return t != nullptr ? juce::String (t->name) : juce::String();
}

juce::File BenchLineAudioProcessor::currentTargetFile() const
{
    const auto f = const_cast<BenchLineAudioProcessor*> (this)->analysis.getTargetFile();
    return f.isEmpty() ? juce::File() : juce::File (f);
}

void BenchLineAudioProcessor::startBuild (const juce::Array<juce::File>& files, const juce::String& name)
{
    builder.reset();
    {
        const juce::ScopedLock sl (buildLock);
        status = BuildStatus {};
        status.active = true;
        status.total = files.size();
        status.message = "Analysing 1 of " + juce::String (files.size());
    }
    builder = std::make_unique<BuildThread> (*this, files, name);
    builder->startThread (juce::Thread::Priority::low);
}

void BenchLineAudioProcessor::cancelBuild()
{
    if (builder != nullptr) builder->cancel.store (true);
}

BenchLineAudioProcessor::BuildStatus BenchLineAudioProcessor::buildStatus() const
{
    const juce::ScopedLock sl (buildLock);
    return status;
}

//==============================================================================
void BenchLineAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto xml = benchline::State::toXml (apvts);
    if (auto* extra = xml->getChildByName ("Extra"))
    {
        auto* tgt = extra->createNewChildElement ("Target");
        tgt->setAttribute ("name", currentTargetName());
        tgt->setAttribute ("file", currentTargetFile().getFullPathName());
        tgt->setAttribute ("builtIn", slopeSelected);
        const auto h = analysis.heldState();
        if (h.active)
        {
            auto* hold = extra->createNewChildElement ("Hold");
            hold->setAttribute ("since", h.sinceSong);
            hold->setAttribute ("tone", joinRow (h.medians[benchline::dsp::kTone]));
            hold->setAttribute ("punch", joinRow (h.medians[benchline::dsp::kPunch]));
            hold->setAttribute ("side", joinRow (h.medians[benchline::dsp::kSide]));
        }
    }
    copyXmlToBinary (*xml, destData);
}

void BenchLineAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    loadState (data, sizeInBytes);
}

bool BenchLineAudioProcessor::loadState (const void* data, int sizeInBytes)
{
    // arbitrary bytes are refused, never crashed on (D.5.3): a foreign or newer blob leaves the state as it is
    if (data == nullptr || sizeInBytes <= 0)
        return false;
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return false;
    auto* state = benchline::State::stateOf (*xml);
    if (state == nullptr || ! benchline::State::fromXml (*state, apvts))
        return false;
    if (auto* extra = state->getChildByName ("Extra"))
    {
        if (auto* tgt = extra->getChildByName ("Target"))
        {
            const juce::File f (tgt->getStringAttribute ("file").isNotEmpty() && juce::File::isAbsolutePath (tgt->getStringAttribute ("file"))
                                    ? juce::File (tgt->getStringAttribute ("file")) : juce::File());
            if (tgt->getBoolAttribute ("builtIn")) selectTarget ({ "Plain slope", {}, true });
            else if (f != juce::File() && f.existsAsFile()) selectTarget ({ tgt->getStringAttribute ("name"), f, false });
            else
            {
                // the file moved: look the name up in this machine's library
                bool found = false;
                for (const auto& t : benchline::TargetIO::list())
                    if (! t.builtIn && t.name == tgt->getStringAttribute ("name")) { found = selectTarget (t); break; }
                if (! found) clearTarget();
            }
        }
        benchline::Analyser::HeldState h;
        if (auto* hold = extra->getChildByName ("Hold"))
        {
            h.active = parseRow (hold->getStringAttribute ("tone"), h.medians[benchline::dsp::kTone])
                    && parseRow (hold->getStringAttribute ("punch"), h.medians[benchline::dsp::kPunch])
                    && parseRow (hold->getStringAttribute ("side"), h.medians[benchline::dsp::kSide]);
            h.sinceSong = hold->getDoubleAttribute ("since", -1.0);
        }
        analysis.restoreHeld (h);
    }
    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BenchLineAudioProcessor();
}
