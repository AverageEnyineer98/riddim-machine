// Renders riddim patterns through the engine and FX chain offline, writes them as WAVs
// and checks the output is finite, audible and not clipping constantly.
//
//   RiddimRenderTest <output folder>

#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/dsp/RiddimEngine.h"
#include "../Source/dsp/RiddimFx.h"
#include "../Source/Parameters.h"

namespace
{
    constexpr double sampleRate = 48000.0, bpm = 150.0;
    constexpr int blockSize = 256;

    // One hit on the 1/8-triplet grid: riddim leaves gaps and changes the wobble rate per hit.
    struct Hit { int step, length, note, lfoRate; };

    // LFO rates: 3 1/4T, 5 1/8T, 7 1/16T, 8 1/32.
    const std::vector<Hit> riddimBar { { 0, 2, 30, 5 }, { 3, 1, 30, 7 }, { 4, 1, 30, 7 },
                                       { 6, 2, 30, 5 }, { 9, 1, 30, 8 }, { 10, 2, 33, 3 } };
    constexpr int stepsPerBar = 12;

    struct TestCase
    {
        const char* name;
        EngineSettings synth;
        FxSettings fx;
        float lfoToVowel = 0.0f;
        bool combKeyTrack = false;
        int fxLfoShape = -1, fxLfoRate = 0; // the FX plugin's movement LFO, when >= 0
    };

    EngineSettings basic (int table, float cutoff)
    {
        EngineSettings s;
        s.osc[0] = { table, 0.0f, 0.8f, 0, 0, 1, 0.2f };
        s.subLevel = 0.55f;
        s.cutoff = cutoff;
        return s;
    }

    FxSettings chain (float drive, float smash, float clip)
    {
        FxSettings f;
        f.distDrive = drive; f.smash = smash; f.clip = clip; f.lowKeepHz = 110.0f; f.outputDb = -6.0f;
        return f;
    }

    std::vector<TestCase> makeCases()
    {
        std::vector<TestCase> cases;

        // The plain square, as a reference for the FX.
        auto raw = basic (5, 20000.0f);
        raw.releaseMs = 25.0f;
        FxSettings dry;
        dry.outputDb = -6.0f;
        cases.push_back ({ "square_raw", raw, dry });

        // The FX plugin's "Square To Riddim" chain on that square.
        auto squareFx = chain (0.6f, 0.55f, 0.45f);
        squareFx.vowelMix = 0.75f; squareFx.vowel = 0.45f; squareFx.heat = 0.1f;
        cases.push_back ({ "square_fx_chain", raw, squareFx, 0.5f, false, 1, 5 });

        // Synth preset "Riddim Square Yoi": the LFO sweeps the formant instead of the filter.
        auto yoi = basic (5, 20000.0f);
        yoi.releaseMs = 25.0f; yoi.resonance = 0.1f; yoi.lfoShape = 1; yoi.lfoToCutoff = 0.0f;
        auto yoiFx = chain (0.55f, 0.6f, 0.45f);
        yoiFx.vowelMix = 0.85f; yoiFx.vowel = 0.5f; yoiFx.combMix = 0.25f; yoiFx.combFeedback = 0.5f;
        cases.push_back ({ "riddim_square_yoi", yoi, yoiFx, 0.5f, true });

        // Synth preset "Riddim Square Wob".
        auto wob = basic (5, 120.0f);
        wob.releaseMs = 25.0f; wob.resonance = 0.5f; wob.lfoShape = 0; wob.lfoToCutoff = 0.8f;
        auto wobFx = chain (0.6f, 0.55f, 0.4f);
        wobFx.vowelMix = 0.4f; wobFx.vowel = 0.1f;
        cases.push_back ({ "riddim_square_wob", wob, wobFx });

        // Synth preset "Classic Riddim Wobble".
        auto classic = basic (0, 150.0f);
        classic.osc[0].position = 0.3f; classic.subLevel = 0.6f; classic.resonance = 0.45f; classic.lfoToCutoff = 0.75f;
        cases.push_back ({ "riddim_wobble", classic, chain (0.45f, 0.4f, 0.3f) });

        // Synth preset "Tearout Growl".
        auto growl = basic (1, 400.0f);
        growl.osc[0].position = 0.5f; growl.osc[1] = { 3, 0.0f, 0.4f, -1, 0, 1, 0.2f };
        growl.resonance = 0.35f; growl.lfoShape = 5; growl.lfoToCutoff = 0.55f; growl.lfoToPosA = 0.5f;
        auto growlFx = chain (0.55f, 0.6f, 0.45f);
        growlFx.distType = 2; growlFx.combMix = 0.3f; growlFx.combFeedback = 0.7f; growlFx.heat = 0.15f;
        cases.push_back ({ "riddim_growl", growl, growlFx, 0.0f, true });

        // The style presets: old school, metallic tearout, dark robotic.
        auto oldSchool = basic (7, 220.0f);
        oldSchool.osc[0].position = 0.66f; oldSchool.osc[0].warpMode = 1; oldSchool.osc[0].warp = 0.35f;
        oldSchool.subLevel = 0.6f; oldSchool.resonance = 0.4f; oldSchool.releaseMs = 25.0f;
        oldSchool.pitchEnvSemis = 12.0f; oldSchool.pitchDecayMs = 25.0f;
        oldSchool.lfoShape = 0; oldSchool.lfoToCutoff = 0.7f; oldSchool.lfoToWarpA = 0.45f;
        auto oldSchoolFx = chain (0.55f, 0.7f, 0.5f);
        oldSchoolFx.resample = true;
        cases.push_back ({ "style_old_school", oldSchool, oldSchoolFx });

        auto metal = basic (4, 3000.0f);
        metal.osc[0].position = 0.5f; metal.osc[0].warpMode = 2; metal.osc[0].warp = 0.4f;
        metal.osc[1] = { 1, 0.0f, 0.0f, 0, 0, 1, 0.0f };
        metal.fmAmount = 0.35f; metal.subLevel = 0.5f; metal.resonance = 0.2f; metal.releaseMs = 25.0f;
        metal.pitchEnvSemis = -12.0f; metal.pitchDecayMs = 15.0f;
        metal.lfoShape = 3; metal.lfoToPosA = 0.6f; metal.lfoToFm = 0.3f; metal.lfoToWarpA = 0.3f;
        auto metalFx = chain (0.55f, 0.75f, 0.6f);
        metalFx.distType = 1; metalFx.vowelMix = 0.3f; metalFx.vowel = 0.6f;
        metalFx.combMix = 0.45f; metalFx.combFeedback = 0.8f; metalFx.tilt = 0.25f; metalFx.resample = true;
        cases.push_back ({ "style_metallic_tearout", metal, metalFx, 0.0f, true });

        auto robot = basic (5, 20000.0f);
        robot.osc[0].position = 0.2f; robot.osc[0].warpMode = 4; robot.osc[0].warp = 0.4f;
        robot.osc[1] = { 7, 0.0f, 0.0f, -1, 0, 1, 0.0f };
        robot.fmAmount = 0.45f; robot.subLevel = 0.6f; robot.releaseMs = 25.0f;
        robot.pitchEnvSemis = 7.0f; robot.pitchDecayMs = 20.0f;
        robot.lfoShape = 4; robot.lfoToFm = 0.35f;
        auto robotFx = chain (0.6f, 0.7f, 0.55f);
        robotFx.vowelMix = 0.8f; robotFx.vowel = 0.2f; robotFx.combMix = 0.3f; robotFx.combFeedback = 0.6f;
        robotFx.tilt = -0.2f; robotFx.resample = true;
        cases.push_back ({ "style_dark_robot", robot, robotFx, 0.6f, true });

        return cases;
    }

    void writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio)
    {
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        juce::WavAudioFormat wav;
        const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (24);
        if (auto writer = wav.createWriterFor (stream, options))
            writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }
}

int main (int argc, char* argv[])
{
    const juce::File outDir (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                      : juce::File::getCurrentWorkingDirectory());
    outDir.createDirectory();

    const auto stepSamples = (int) (sampleRate * 60.0 / bpm / 3.0);
    constexpr int bars = 4;
    bool ok = true;

    for (const auto& test : makeCases())
    {
        RiddimEngine engine;
        engine.prepare (sampleRate, blockSize);
        RiddimFx fx;
        fx.prepare (sampleRate, blockSize);

        const auto total = bars * stepsPerBar * stepSamples + (int) sampleRate / 2;
        juce::AudioBuffer<float> output (2, total);
        juce::AudioBuffer<float> block (2, blockSize);

        // Note events, plus the LFO rate each hit switches to (as a producer would automate it).
        juce::MidiBuffer all;
        std::vector<std::pair<int, int>> rateChanges;
        for (int bar = 0; bar < bars; ++bar)
            for (const auto& hit : riddimBar)
            {
                const auto start = (bar * stepsPerBar + hit.step) * stepSamples;
                all.addEvent (juce::MidiMessage::noteOn (1, hit.note, 0.9f), start);
                all.addEvent (juce::MidiMessage::noteOff (1, hit.note), start + (int) (hit.length * stepSamples * 0.9));
                rateChanges.emplace_back (start, hit.lfoRate);
            }

        auto synth = test.synth;
        std::vector<float> fxLfo ((size_t) blockSize);
        double fxLfoPhase = 0.0;
        const auto startMs = juce::Time::getMillisecondCounterHiRes();

        for (int pos = 0; pos < total; pos += blockSize)
        {
            const auto n = juce::jmin (blockSize, total - pos);

            for (const auto& [at, rate] : rateChanges)
                if (at >= pos && at < pos + n)
                    synth.lfoRate = rate;

            juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, n);
            juce::MidiBuffer midi;
            for (const auto m : all)
                if (m.samplePosition >= pos && m.samplePosition < pos + n)
                    midi.addEvent (m.getMessage(), m.samplePosition - pos);

            engine.process (view, midi, synth, bpm, std::nullopt);

            // The same wiring as the synth processor.
            auto fxSettings = test.fx;
            fxSettings.vowelModTrace = engine.getLfoTrace();
            if (test.fxLfoShape >= 0)
            {
                // The FX plugin's movement LFO instead of the synth's.
                const auto inc = Choices::lfoCyclesPerBeat[test.fxLfoRate] * bpm / 60.0 / sampleRate;
                for (int i = 0; i < n; ++i)
                {
                    fxLfo[(size_t) i] = lfoShapeValue (test.fxLfoShape, fxLfoPhase);
                    fxLfoPhase = std::fmod (fxLfoPhase + inc, 1.0);
                }
                fxSettings.vowelModTrace = fxLfo.data();
            }
            fxSettings.vowelModDepth = test.lfoToVowel;
            if (test.combKeyTrack)
                fxSettings.combTrackHz = (float) engine.getCurrentHz();
            fx.process (view, fxSettings);

            for (int ch = 0; ch < 2; ++ch)
                output.copyFrom (ch, pos, view, ch, 0, n);
        }

        const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - startMs;
        const auto audioMs = total / sampleRate * 1000.0;

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
        const auto peak = output.getMagnitude (0, total);
        const auto clipPercent = 100.0 * clipped / (2.0 * total);
        const auto pass = finite && rms > 0.01f && clipPercent < 5.0;
        ok = ok && pass;

        std::printf ("%-18s rms %.3f  peak %.3f  clipped %.2f%%  cpu %.1f%% of realtime  %s\n", test.name, rms, peak,
                     clipPercent, 100.0 * elapsedMs / audioMs, pass ? "OK" : "FAIL");

        writeWav (outDir.getChildFile (juce::String (test.name) + ".wav"), output);
    }

    return ok ? 0 : 1;
}
