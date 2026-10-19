// Renders every Riddim Forge factory preset over a riddim pattern, plus an imported
// wavetable, writes them as WAVs and checks the output is finite, audible and not clipping.
//
//   ForgeRenderTest <output folder>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Source/forge/ForgeRenderer.h"
#include "../Source/Parameters.h"
#include "../Source/PresetManager.h"

namespace
{
    constexpr double sampleRate = 48000.0, bpm = 150.0;
    constexpr int blockSize = 256;

    // The same 1/8-triplet riddim bar as the synth's render test, without rate automation:
    // the hit sequencer changes the LFO rate per hit instead.
    struct Hit { int step, length, note; };
    const std::vector<Hit> riddimBar { { 0, 2, 30 }, { 3, 1, 30 }, { 4, 1, 30 }, { 6, 2, 30 }, { 9, 1, 30 }, { 10, 2, 33 } };
    constexpr int stepsPerBar = 12;

    // Just enough of a processor to own a parameter tree.
    struct ParameterHost : juce::AudioProcessor
    {
        ParameterHost() : parameters (*this, nullptr, "RiddimForge", createForgeParameterLayout()) {}

        const juce::String getName() const override { return "test"; }
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        double getTailLengthSeconds() const override { return 0; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}

        juce::AudioProcessorValueTreeState parameters;
    };

    void writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, int channels)
    {
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        juce::WavAudioFormat wav;
        const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate).withNumChannels (channels).withBitsPerSample (24);
        if (auto writer = wav.createWriterFor (stream, options))
            writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }

    // Four frames morphing saw to square, 2048 samples each, as another synth would export.
    juce::File writeTestWavetable (const juce::File& folder)
    {
        constexpr int size = 2048, frames = 4;
        juce::AudioBuffer<float> table (1, size * frames);
        for (int f = 0; f < frames; ++f)
            for (int i = 0; i < size; ++i)
            {
                const auto p = (float) i / size;
                const auto saw = 2.0f * p - 1.0f, square = p < 0.5f ? 1.0f : -1.0f;
                const auto t = (float) f / (frames - 1);
                table.setSample (0, f * size + i, 0.8f * (saw + t * (square - saw)));
            }

        auto file = folder.getChildFile ("test_wavetable.wav");
        writeWav (file, table, 1);
        return file;
    }

    struct Result { bool pass; };

    bool renderCase (const juce::String& name, ParameterHost& host, const UserWavetable* imported, const juce::File& outDir)
    {
        ForgeRenderer renderer (host.parameters);
        renderer.prepare (sampleRate, blockSize);
        renderer.setUserTable (0, imported);
        for (int l = 0; l < Forge::numLfos; ++l)
            renderer.setLfoCurve (l, Forge::LfoCurve::fromString (host.parameters.state.getProperty (ForgeID::lfoCurveProperty (l)).toString()));

        const auto stepSamples = (int) (sampleRate * 60.0 / bpm / 3.0);
        constexpr int bars = 4;
        const auto total = bars * stepsPerBar * stepSamples + (int) sampleRate / 2;

        juce::MidiBuffer all;
        for (int bar = 0; bar < bars; ++bar)
            for (const auto& hit : riddimBar)
            {
                const auto start = (bar * stepsPerBar + hit.step) * stepSamples;
                all.addEvent (juce::MidiMessage::noteOn (1, hit.note, 0.9f), start);
                all.addEvent (juce::MidiMessage::noteOff (1, hit.note), start + (int) (hit.length * stepSamples * 0.9));
            }

        juce::AudioBuffer<float> output (2, total), block (2, blockSize);
        const auto startMs = juce::Time::getMillisecondCounterHiRes();

        for (int pos = 0; pos < total; pos += blockSize)
        {
            const auto n = juce::jmin (blockSize, total - pos);
            juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, n);
            juce::MidiBuffer midi;
            for (const auto m : all)
                if (m.samplePosition >= pos && m.samplePosition < pos + n)
                    midi.addEvent (m.getMessage(), m.samplePosition - pos);

            renderer.process (view, midi, bpm, std::nullopt);

            for (int ch = 0; ch < 2; ++ch)
                output.copyFrom (ch, pos, view, ch, 0, n);
        }

        const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - startMs;
        bool finite = true;
        int clipped = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < total; ++i)
            {
                const auto v = output.getSample (ch, i);
                finite = finite && std::isfinite (v);
                clipped += std::abs (v) >= 0.999f ? 1 : 0;
            }

        const auto rms = output.getRMSLevel (0, 0, total);
        const auto clipPercent = 100.0 * clipped / (2.0 * total);
        const auto pass = finite && rms > 0.01f && clipPercent < 5.0;
        std::printf ("%-20s rms %.3f  peak %.3f  clipped %.2f%%  cpu %.1f%% of realtime  %s\n", name.toRawUTF8(), rms,
                     output.getMagnitude (0, total), clipPercent, 100.0 * elapsedMs / (total / sampleRate * 1000.0), pass ? "OK" : "FAIL");

        writeWav (outDir.getChildFile ("forge_" + name.toLowerCase().replaceCharacter (' ', '_') + ".wav"), output, 2);
        return pass;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce; // the parameter tree needs a message manager
    const juce::File outDir (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                      : juce::File::getCurrentWorkingDirectory());
    outDir.createDirectory();

    ParameterHost host;
    PresetManager presets (host.parameters, forgeFactoryPresets(), "Forge Presets");
    bool ok = true;

    for (int i = 0; i < presets.getNumFactoryPresets(); ++i)
    {
        presets.loadPreset (i);
        ok = renderCase (presets.getCurrentName(), host, nullptr, outDir) && ok;
    }

    // An imported wavetable through the "Hit Seq Riddim" patch.
    juce::String error;
    const auto imported = UserWavetable::load (writeTestWavetable (outDir), error);
    if (imported == nullptr || imported->getNumFrames() != 4)
    {
        std::printf ("wavetable import FAIL: %s\n", error.toRawUTF8());
        return 1;
    }
    std::printf ("wavetable import: %d frames OK\n", imported->getNumFrames());

    presets.loadPreset (juce::jmin (1, presets.getNumFactoryPresets() - 1));
    if (auto* table = host.parameters.getParameter (ForgeID::osc ('A', ForgeID::table)))
        table->setValueNotifyingHost (table->convertTo0to1 ((float) Forge::importedTable()));
    ok = renderCase ("Imported Table", host, imported.get(), outDir) && ok;

    return ok ? 0 : 1;
}
