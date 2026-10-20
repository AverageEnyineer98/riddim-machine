// Renders Riddim Forge presets offline and measures them, for levelling the factory set,
// checking it, and training Magic Sound.
//
//   ForgePresetLab <presets.json> <analysis.json> [--wav <folder>] [--bars N]
//   ForgePresetLab --vectors <presets.json> <vectors.json>
//   ForgePresetLab --magic <count> <presets.json> [style]
//   ForgePresetLab --serum <wav folder> <file.SerumPreset>...
//
// --vectors skips rendering and writes every preset as normalised parameter values, plus
// each parameter's kind (continuous, or the number of choices), for Magic Sound training.
//
// Each preset plays a riddim bar (bass sounds) or a short phrase (leads, pads, plucks) at
// 150 BPM. The analysis lists, per preset: loudness, peak, clipping, whether the output stayed
// finite, and a timbre fingerprint (log-band spectrum, centroid, flatness, low-end share and
// how much the level moves), used to compare presets with each other.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "../Source/forge/ForgeAudition.h"
#include "../Source/PresetManager.h"
#include "../Source/forge/ForgeMagic.h"
#include "../Source/forge/SerumImport.h"

using namespace ForgeAudition;

namespace
{
    void writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio)
    {
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        juce::WavAudioFormat wav;
        const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (16);
        if (auto writer = wav.createWriterFor (stream, options))
            writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }
}

static int writeVectors (const juce::File& in, const juce::File& out)
{
    const auto presets = parseForgePresets (in.loadFileAsString());
    Host host;
    PresetManager manager (host.parameters, presets, "Forge Presets Lab");

    juce::Array<juce::var> params, rows;
    for (auto* p : host.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);
        auto* item = new juce::DynamicObject();
        item->setProperty ("id", ranged->getParameterID());
        int kind = 0; // continuous
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p))
            kind = choice->choices.size();
        else if (dynamic_cast<juce::AudioParameterBool*> (p) != nullptr)
            kind = 2;
        item->setProperty ("choices", kind);
        item->setProperty ("default", ranged->getDefaultValue());
        params.add (juce::var (item));
    }

    for (int i = 0; i < (int) presets.size(); ++i)
    {
        manager.loadPreset (i);
        juce::Array<juce::var> values;
        for (auto* p : host.getParameters())
        {
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p))
                values.add (choice->getIndex());
            else if (auto* toggle = dynamic_cast<juce::AudioParameterBool*> (p))
                values.add (toggle->get() ? 1 : 0);
            else
                values.add (p->getValue());
        }
        auto* row = new juce::DynamicObject();
        row->setProperty ("name", presets[(size_t) i].name);
        row->setProperty ("category", presets[(size_t) i].category);
        row->setProperty ("v", values);
        rows.add (juce::var (row));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("params", params);
    root->setProperty ("presets", rows);
    out.replaceWithText (juce::JSON::toString (juce::var (root), true));
    std::printf ("wrote %d presets x %d parameters\n", (int) presets.size(), params.size());
    return 0;
}

// Makes Magic Sound patches the way the plugin does, and writes them as a preset file.
static int writeMagic (int count, const juce::File& out, const juce::String& style)
{
    juce::SharedResourcePointer<WavetableBank> bank;
    ForgeMagic::Generator generator;
    if (! generator.isReady())
    {
        std::printf ("Magic Sound model failed to load\n");
        return 1;
    }

    juce::Random random (1234);
    juce::Array<juce::var> list;
    int failed = 0;
    const auto startMs = juce::Time::getMillisecondCounterHiRes();

    for (int i = 0; i < count; ++i)
    {
        const auto preset = generator.make (random, style);
        if (preset.values.empty())
        {
            ++failed;
            continue;
        }

        auto* values = new juce::DynamicObject();
        for (const auto& [id, value] : preset.values)
            values->setProperty (id, value);
        auto* text = new juce::DynamicObject();
        for (const auto& [key, value] : preset.text)
            text->setProperty (key, value);

        auto* item = new juce::DynamicObject();
        item->setProperty ("name", preset.name);
        item->setProperty ("category", preset.category);
        item->setProperty ("values", juce::var (values));
        item->setProperty ("text", juce::var (text));
        list.add (juce::var (item));
        std::printf ("%-22s %-8s score %.2f\n", preset.name.toRawUTF8(), preset.category.toRawUTF8(), generator.getLastStats().score);
    }

    out.replaceWithText (juce::JSON::toString (juce::var (list)));
    std::printf ("made %d, failed %d, %.2f s each\n", list.size(), failed,
                 (juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0 / juce::jmax (1, count));
    return 0;
}

// Imports Serum 2 presets the way the plugin does, then plays and measures each one.
static int importSerum (const juce::File& wavFolder, const juce::Array<juce::File>& files)
{
    juce::SharedResourcePointer<WavetableBank> bank;
    wavFolder.createDirectory();
    int failed = 0;

    for (const auto& file : files)
    {
        SerumImport::Result result;
        juce::String error;
        if (! SerumImport::convert (file, wavFolder.getChildFile ("tables"), result, error))
        {
            std::printf ("%s: %s\n", file.getFileName().toRawUTF8(), error.toRawUTF8());
            ++failed;
            continue;
        }

        Host host;
        std::vector<FactoryPreset> none;
        PresetManager manager (host.parameters, none, "Forge Presets Lab");
        manager.loadUnlisted (result.preset);
        const auto audio = render (host.parameters, false, 2);
        const auto features = analyse (audio);
        writeWav (wavFolder.getChildFile (juce::File::createLegalFileName (result.preset.name) + ".wav"), audio);

        std::printf ("%-40s %3d values  rms %6.1f dB  peak %.2f  %s\n", result.preset.name.toRawUTF8(), (int) result.preset.values.size(),
                     features.rmsDb, features.peak, features.finite ? "" : "NOT FINITE");
        for (const auto& line : result.notes)
            std::printf ("    - %s\n", line.toRawUTF8());
    }
    return failed == 0 ? 0 : 1;
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;
    if (argc >= 4 && juce::String (argv[1]) == "--serum")
    {
        juce::Array<juce::File> files;
        for (int i = 3; i < argc; ++i)
            files.add (juce::File::getCurrentWorkingDirectory().getChildFile (argv[i]));
        return importSerum (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]), files);
    }
    if (argc >= 4 && juce::String (argv[1]) == "--magic")
        return writeMagic (juce::String (argv[2]).getIntValue(), juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]),
                           argc > 4 ? juce::String (argv[4]) : juce::String());
    if (argc == 4 && juce::String (argv[1]) == "--vectors")
        return writeVectors (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]),
                             juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]));
    if (argc < 3)
    {
        std::printf ("usage: ForgePresetLab <presets.json> <analysis.json> [--wav folder] [--bars N]\n");
        return 2;
    }

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto presets = parseForgePresets (cwd.getChildFile (argv[1]).loadFileAsString());
    juce::File wavFolder;
    int bars = 2;
    for (int i = 3; i + 1 < argc; i += 2)
    {
        if (juce::String (argv[i]) == "--wav") wavFolder = cwd.getChildFile (argv[i + 1]);
        if (juce::String (argv[i]) == "--bars") bars = juce::jmax (1, juce::String (argv[i + 1]).getIntValue());
    }
    if (wavFolder != juce::File())
        wavFolder.createDirectory();

    juce::SharedResourcePointer<WavetableBank> bank; // built once, not per render
    Host host;
    PresetManager manager (host.parameters, presets, "Forge Presets Lab");
    juce::Array<juce::var> results;
    const auto startMs = juce::Time::getMillisecondCounterHiRes();

    for (int i = 0; i < (int) presets.size(); ++i)
    {
        manager.loadPreset (i);
        const auto& preset = presets[(size_t) i];
        const auto audio = render (host.parameters, isMelodic (preset.category), bars);

        auto entry = toVar (analyse (audio));
        entry.getDynamicObject()->setProperty ("name", preset.name);
        entry.getDynamicObject()->setProperty ("category", preset.category);
        results.add (entry);

        if (wavFolder != juce::File())
            writeWav (wavFolder.getChildFile (juce::File::createLegalFileName (preset.category + " - " + preset.name) + ".wav"), audio);
    }

    cwd.getChildFile (argv[2]).replaceWithText (juce::JSON::toString (juce::var (results)));
    std::printf ("analysed %d presets in %.1f s\n", (int) presets.size(), (juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0);
    return 0;
}
