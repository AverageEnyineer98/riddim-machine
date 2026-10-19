// Renders Riddim Forge presets offline and measures them, for levelling the factory set,
// checking it, and training Magic Sound.
//
//   ForgePresetLab <presets.json> <analysis.json> [--wav <folder>] [--bars N]
//
// Each preset plays a riddim bar (bass sounds) or a short phrase (leads, pads, plucks) at
// 150 BPM. The analysis lists, per preset: loudness, peak, clipping, whether the output stayed
// finite, and a timbre fingerprint (log-band spectrum, centroid, flatness, low-end share and
// how much the level moves), used to compare presets with each other.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "../Source/forge/ForgeRenderer.h"
#include "../Source/PresetManager.h"

namespace
{
    constexpr double sampleRate = 48000.0, bpm = 150.0;
    constexpr int blockSize = 256, numBands = 16;

    struct Hit { int step, length, note; };
    const std::vector<Hit> bassBar { { 0, 2, 30 }, { 3, 1, 30 }, { 4, 1, 30 }, { 6, 2, 30 }, { 9, 1, 30 }, { 10, 2, 33 } };
    const std::vector<Hit> melodyBar { { 0, 3, 60 }, { 3, 3, 63 }, { 6, 2, 67 }, { 9, 3, 65 } };
    constexpr int stepsPerBar = 12; // 1/8 triplets

    struct ParameterHost : juce::AudioProcessor
    {
        ParameterHost() : parameters (*this, nullptr, "RiddimForge", createForgeParameterLayout()) {}

        const juce::String getName() const override { return "lab"; }
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

    juce::AudioBuffer<float> render (ParameterHost& host, bool melodic, int bars)
    {
        ForgeRenderer renderer (host.parameters);
        renderer.prepare (sampleRate, blockSize);
        for (int l = 0; l < Forge::numLfos; ++l)
            renderer.setLfoCurve (l, Forge::LfoCurve::fromString (host.parameters.state.getProperty (ForgeID::lfoCurveProperty (l)).toString()));

        const auto stepSamples = (int) (sampleRate * 60.0 / bpm / 3.0);
        const auto total = bars * stepsPerBar * stepSamples + (int) sampleRate / 2;

        juce::MidiBuffer all;
        for (int bar = 0; bar < bars; ++bar)
            for (const auto& hit : melodic ? melodyBar : bassBar)
            {
                const auto start = (bar * stepsPerBar + hit.step) * stepSamples;
                all.addEvent (juce::MidiMessage::noteOn (1, hit.note, 0.9f), start);
                all.addEvent (juce::MidiMessage::noteOff (1, hit.note), start + (int) (hit.length * stepSamples * 0.9));
            }

        juce::AudioBuffer<float> output (2, total), block (2, blockSize);
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
        return output;
    }

    juce::var analyse (const juce::AudioBuffer<float>& audio)
    {
        const auto total = audio.getNumSamples();
        bool finite = true;
        int clipped = 0;
        double sumSquares = 0.0;
        std::vector<float> mono ((size_t) total);

        for (int i = 0; i < total; ++i)
        {
            const auto l = audio.getSample (0, i), r = audio.getSample (1, i);
            finite = finite && std::isfinite (l) && std::isfinite (r);
            clipped += (std::abs (l) >= 0.999f) + (std::abs (r) >= 0.999f);
            sumSquares += 0.5 * ((double) l * l + (double) r * r);
            mono[(size_t) i] = std::isfinite (l + r) ? 0.5f * (l + r) : 0.0f;
        }

        const auto rms = std::sqrt (sumSquares / juce::jmax (1, total));

        // Level movement: spread of 10 ms frame levels relative to their mean.
        const auto frame = (int) (sampleRate * 0.01);
        std::vector<double> levels;
        for (int start = 0; start + frame <= total; start += frame)
        {
            double s = 0.0;
            for (int i = 0; i < frame; ++i)
                s += (double) mono[(size_t) (start + i)] * mono[(size_t) (start + i)];
            levels.push_back (std::sqrt (s / frame));
        }
        double mean = 0.0, var = 0.0;
        for (auto v : levels) mean += v;
        mean /= juce::jmax ((size_t) 1, levels.size());
        for (auto v : levels) var += (v - mean) * (v - mean);
        const auto movement = mean > 1.0e-6 ? std::sqrt (var / juce::jmax ((size_t) 1, levels.size())) / mean : 0.0;

        // Average spectrum over 4096-sample windows.
        constexpr int order = 12, size = 1 << order;
        juce::dsp::FFT fft (order);
        juce::dsp::WindowingFunction<float> window ((size_t) size, juce::dsp::WindowingFunction<float>::hann);
        std::vector<double> spectrum (size / 2, 0.0);
        std::vector<float> data ((size_t) size * 2);
        int windows = 0;
        for (int start = 0; start + size <= total; start += size / 2, ++windows)
        {
            std::fill (data.begin(), data.end(), 0.0f);
            std::copy_n (mono.begin() + start, size, data.begin());
            window.multiplyWithWindowingTable (data.data(), (size_t) size);
            fft.performFrequencyOnlyForwardTransform (data.data(), true);
            for (int k = 0; k < size / 2; ++k)
                spectrum[(size_t) k] += (double) data[(size_t) k] * data[(size_t) k];
        }

        double power = 1.0e-20, weighted = 0.0, low = 0.0, logSum = 0.0;
        int counted = 0;
        juce::Array<juce::var> bands;
        std::array<double, numBands> bandPower {};
        for (int k = 1; k < size / 2; ++k)
        {
            const auto hz = k * sampleRate / size;
            const auto p = spectrum[(size_t) k] / juce::jmax (1, windows);
            power += p;
            weighted += p * hz;
            if (hz < 120.0)
                low += p;
            if (hz > 40.0 && hz < 16000.0)
            {
                logSum += std::log (p + 1.0e-20);
                ++counted;
            }
            const auto band = (int) (std::log (hz / 30.0) / std::log (16000.0 / 30.0) * numBands);
            if (band >= 0 && band < numBands)
                bandPower[(size_t) band] += p;
        }

        double inBand = 0.0;
        for (int k = 1; k < size / 2; ++k)
        {
            const auto hz = k * sampleRate / size;
            if (hz > 40.0 && hz < 16000.0)
                inBand += spectrum[(size_t) k] / juce::jmax (1, windows);
        }
        const auto flatness = counted > 0 ? std::exp (logSum / counted) / (inBand / counted + 1.0e-20) : 0.0;

        for (auto p : bandPower)
            bands.add (juce::jlimit (-120.0, 0.0, 10.0 * std::log10 (p / power + 1.0e-12)));

        auto* result = new juce::DynamicObject();
        result->setProperty ("rmsDb", juce::Decibels::gainToDecibels ((float) rms, -120.0f));
        result->setProperty ("peak", audio.getMagnitude (0, total));
        result->setProperty ("clipPercent", 100.0 * clipped / (2.0 * total));
        result->setProperty ("finite", finite);
        result->setProperty ("centroid", weighted / power);
        result->setProperty ("flatness", flatness);
        result->setProperty ("lowRatio", low / power);
        result->setProperty ("movement", movement);
        result->setProperty ("bands", bands);
        return juce::var (result);
    }

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

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;
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
    ParameterHost host;
    PresetManager manager (host.parameters, presets, "Forge Presets Lab");
    juce::Array<juce::var> results;
    const auto startMs = juce::Time::getMillisecondCounterHiRes();

    for (int i = 0; i < (int) presets.size(); ++i)
    {
        manager.loadPreset (i);
        const auto& preset = presets[(size_t) i];
        const auto melodic = preset.category == "Lead" || preset.category == "Pad" || preset.category == "Pluck";
        const auto audio = render (host, melodic, bars);

        auto entry = analyse (audio);
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
