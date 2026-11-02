/*  BenchLineGuiSnapshot - renders the real editor to a PNG (plugin-ui-bob audio-plugins.md 15).

    Links the processor and the editor (same LookAndFeel and code as the release), feeds audio
    through the processor with the analysis pumped synchronously, then paints the editor offscreen.

      --out=<png>                 the file (the scale goes into the name: x@1x.png, x@1.5x.png, x@2x.png)
      --scale=<0.75|1|1.5|2>      GUI scale (default 1)
      --preset=<slug>             a factory preset
      --param=<id>=<plain value>  repeatable, through the parameter
      --target=<file.json>|slope|none
      --in=<file.wav> | --signal=pink   input to analyse first (--seconds, default 12)
      --state=<none|about|silent|bypass|hold:<s>>   about page; no input; bypass; Hold switched on at s seconds
      --list                      print the states and exit
    Exit 0 written, 2 refused. */

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "benchline/FactoryPresets.h"

#include <iostream>
#include <random>

static int fail (const juce::String& m) { std::cerr << "error: " << m << std::endl; return 2; }

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::StringPairArray a;
    juce::StringArray params;
    for (int i = 1; i < argc; ++i)
    {
        juce::String s (argv[i]);
        if (! s.startsWith ("--")) return fail ("unexpected " + s);
        s = s.substring (2);
        const auto k = s.upToFirstOccurrenceOf ("=", false, false), v = s.fromFirstOccurrenceOf ("=", false, false);
        if (k == "param") params.add (v); else a.set (k, v);
    }
    if (a.containsKey ("list")) { std::cout << "states: none about silent bypass hold:<seconds>\n"; return 0; }
    if (a["out"].isEmpty()) return fail ("--out=<png> is required");

    BenchLineAudioProcessor proc;
    proc.analyser().setSynchronous (true);
    auto& apvts = proc.parameters();
    if (a["preset"].isNotEmpty())
    {
        int idx = -1;
        const auto& bank = benchline::FactoryPresets::list();
        for (int i = 0; i < bank.size(); ++i) if (bank[i]->slug == a["preset"]) idx = i;
        if (idx < 0) return fail ("no factory preset " + a["preset"]);
        benchline::FactoryPresets::load (apvts, idx);
    }
    for (auto& pv : params)
    {
        auto* p = apvts.getParameter (pv.upToFirstOccurrenceOf ("=", false, false));
        if (p == nullptr) return fail ("unknown parameter " + pv);
        p->setValueNotifyingHost (p->convertTo0to1 (pv.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
    }
    const auto tgt = a["target"];
    if (tgt == "slope") proc.selectTarget ({ "Plain slope", {}, true });
    else if (tgt.isNotEmpty() && tgt != "none")
    {
        juce::String err;
        if (! proc.selectTarget ({ {}, juce::File::getCurrentWorkingDirectory().getChildFile (tgt), false }, &err)) return fail ("--target: " + err);
    }

    const auto state = a["state"];
    double rate = 48000.0;
    juce::AudioBuffer<float> input;
    if (a["in"].isNotEmpty())
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File::getCurrentWorkingDirectory().getChildFile (a["in"])));
        if (r == nullptr) return fail ("cannot read --in");
        rate = r->sampleRate;
        input.setSize (2, (int) r->lengthInSamples);
        r->read (&input, 0, (int) r->lengthInSamples, 0, true, true);
    }
    else if (a["signal"] == "pink" || (a["signal"].isEmpty() && state != "silent"))
    {
        const double secs = a["seconds"].isEmpty() ? 12.0 : a["seconds"].getDoubleValue();
        input.setSize (2, (int) (secs * rate));
        std::mt19937 rng (1);
        std::normal_distribution<float> n (0.0f, 1.0f);
        float b0 = 0, b1 = 0, b2 = 0;
        for (int i = 0; i < input.getNumSamples(); ++i)
        {
            const float w = n (rng);
            b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
            const float pk = (b0 + b1 + b2 + w * 0.1848f) * 0.05f;
            input.setSample (0, i, pk);
            input.setSample (1, i, pk * 0.9f);
        }
    }
    proc.setRateAndBufferSizeDetails (rate, 512);
    proc.prepareToPlay (rate, 512);
    if (state == "bypass") apvts.getParameter (Params::bypass)->setValueNotifyingHost (1.0f);
    const double holdAt = state.startsWith ("hold:") ? state.fromFirstOccurrenceOf (":", false, false).getDoubleValue() : -1.0;
    juce::MidiBuffer midi;
    for (int pos = 0; pos < input.getNumSamples(); pos += 512)
    {
        if (holdAt >= 0 && pos >= (int) (holdAt * rate) && apvts.getParameter (Params::hold)->getValue() < 0.5f)
        {
            proc.analyser().pump();
            apvts.getParameter (Params::hold)->setValueNotifyingHost (1.0f);
            proc.analyser().pump();
            // after the hold, the mix changes: +3 dB around 250 Hz from here on (so the ghost differs)
        }
        const int n = juce::jmin (512, input.getNumSamples() - pos);
        juce::AudioBuffer<float> blk (2, n);
        for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, input, ch, pos, n);
        if (state == "bypass") proc.processBlockBypassed (blk, midi); else proc.processBlock (blk, midi);
        proc.analyser().pump();
    }
    proc.analyser().pump();

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditorIfNeeded());
    auto* editor = dynamic_cast<BenchLineAudioProcessorEditor*> (ed.get());
    if (editor == nullptr) return fail ("no editor");
    const float scale = a["scale"].isEmpty() ? 1.0f : a["scale"].getFloatValue();
    editor->setGuiScale (1.0f);
    if (state == "about") editor->showAbout (true);
    editor->refreshNow();
    const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (a["out"]);
    const auto tag = scale == 1.0f ? juce::String ("@1x") : (scale == 2.0f ? juce::String ("@2x") : "@" + juce::String (scale, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + "x");
    if (! out.getFileNameWithoutExtension().endsWith (tag))
        out = out.getSiblingFile (out.getFileNameWithoutExtension() + tag + ".png");
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat png;
    if (! os.openedOk() || ! png.writeImageToStream (img, os)) return fail ("cannot write " + out.getFullPathName());
    ed.reset();
    std::cout << out.getFullPathName() << std::endl;
    return 0;
}
