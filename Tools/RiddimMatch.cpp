// Sound matching: searches for Riddim Machine settings that sound like a reference bass clip
// and writes them as a user preset.
//
// It finds the hits and their pitch in the reference, plays the same notes through the synth
// and FX chain, compares log-mel spectrograms at two resolutions, and searches the parameters
// with CMA-ES (diagonal covariance). Works best on an isolated bass loop or one-shot, not a
// full mix with drums.
//
//   RiddimMatch <reference audio> <output.rmpreset> [--bpm 150] [--note 30] [--evals 2400]
//               [--seconds 6] [--seed 1]
//
// Writes the preset, plus <output>.wav (the matched render) next to it.

#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/dsp/RiddimEngine.h"
#include "../Source/dsp/RiddimFx.h"
#include "../Source/Parameters.h"
#include <numeric>
#include <random>
#include <thread>

namespace
{
    constexpr int blockSize = 256;
    constexpr int numThreads = 4;

    struct Note { int start, end, midi; };

    struct Reference
    {
        double sampleRate = 48000.0;
        std::vector<float> audio;
        std::vector<Note> notes;
    };

    // Everything one render needs: the synth settings plus the synth-only FX routing.
    struct Patch
    {
        EngineSettings synth;
        FxSettings fx;
        float lfoToVowel = 0.0f;
    };

    Patch basePatch()
    {
        Patch p;
        p.synth.lfoRetrigger = true;
        p.synth.mono = true;
        p.fx.lowKeepHz = 110.0f;
        p.fx.gate = true;
        p.fx.outputDb = -6.0f;
        return p;
    }

    // Settings the search leaves alone, written to the preset as they are.
    const std::vector<std::pair<juce::String, float>> fixedValues {
        { ParamID::lfoRetrig, 1.0f }, { ParamID::mono, 1.0f }, { ParamID::glide, 0.0f },
        { ParamID::combTrack, 1.0f }, { ParamID::lowKeep, 110.0f }, { ParamID::gate, 1.0f },
        { ParamID::heat, 0.0f }, { ParamID::fxMix, 1.0f }, { ParamID::master, -6.0f }
    };

    // One searched parameter, normalised to 0..1 for the optimiser.
    struct Dim
    {
        enum Scale { linear, logarithmic, integer };

        juce::String id;
        float min, max;
        Scale scale;
        std::function<void (Patch&, float)> apply;

        float toPlain (double x) const
        {
            const auto t = (float) juce::jlimit (0.0, 1.0, x);

            if (scale == logarithmic)
                return min * std::pow (max / min, t);
            if (scale == integer)
                return juce::jmin (max, std::floor (min + t * (max - min + 1.0f)));
            return min + t * (max - min);
        }
    };

    std::vector<Dim> makeDims()
    {
        using S = Dim::Scale;
        std::vector<Dim> dims;
        auto add = [&dims] (juce::String id, float lo, float hi, S scale, std::function<void (Patch&, float)> apply)
        {
            dims.push_back ({ std::move (id), lo, hi, scale, std::move (apply) });
        };

        const auto lastTable = (float) WavetableBank::getTableNames().size() - 1.0f;

        for (int o = 0; o < 2; ++o)
        {
            const auto which = o == 0 ? 'A' : 'B';
            add (ParamID::osc (which, ParamID::table), 0, lastTable, S::integer, [o] (Patch& p, float v) { p.synth.osc[o].table = (int) v; });
            add (ParamID::osc (which, ParamID::pos), 0, 1, S::linear, [o] (Patch& p, float v) { p.synth.osc[o].position = v; });
            add (ParamID::osc (which, ParamID::level), 0, 1, S::linear, [o] (Patch& p, float v) { p.synth.osc[o].level = v; });
            add (ParamID::osc (which, ParamID::octave), -2, 1, S::integer, [o] (Patch& p, float v) { p.synth.osc[o].octave = (int) v; });
            add (ParamID::osc (which, ParamID::unison), 1, 5, S::integer, [o] (Patch& p, float v) { p.synth.osc[o].unison = (int) v; });
            add (ParamID::osc (which, ParamID::detune), 0, 1, S::linear, [o] (Patch& p, float v) { p.synth.osc[o].detune = v; });
            add (ParamID::osc (which, ParamID::warpMode), 0, 4, S::integer, [o] (Patch& p, float v) { p.synth.osc[o].warpMode = (int) v; });
            add (ParamID::osc (which, ParamID::warp), 0, 1, S::linear, [o] (Patch& p, float v) { p.synth.osc[o].warp = v; });
        }

        add (ParamID::osc ('B', ParamID::semi), -12, 12, S::integer, [] (Patch& p, float v) { p.synth.osc[1].semitone = (int) v; });

        add (ParamID::fmAmount, 0, 1, S::linear, [] (Patch& p, float v) { p.synth.fmAmount = v; });
        add (ParamID::subLevel, 0, 1, S::linear, [] (Patch& p, float v) { p.synth.subLevel = v; });
        add (ParamID::noiseLevel, 0, 0.5f, S::linear, [] (Patch& p, float v) { p.synth.noiseLevel = v; });

        add (ParamID::filterType, 0, 3, S::integer, [] (Patch& p, float v) { p.synth.filterType = (int) v; });
        add (ParamID::cutoff, 40, 20000, S::logarithmic, [] (Patch& p, float v) { p.synth.cutoff = v; });
        add (ParamID::resonance, 0, 1, S::linear, [] (Patch& p, float v) { p.synth.resonance = v; });
        add (ParamID::filterEnv, -1, 1, S::linear, [] (Patch& p, float v) { p.synth.filterEnv = v; });
        add (ParamID::filterDecay, 5, 3000, S::logarithmic, [] (Patch& p, float v) { p.synth.filterDecayMs = v; });

        add (ParamID::attack, 0.5f, 200, S::logarithmic, [] (Patch& p, float v) { p.synth.attackMs = v; });
        add (ParamID::decay, 5, 3000, S::logarithmic, [] (Patch& p, float v) { p.synth.decayMs = v; });
        add (ParamID::sustain, 0, 1, S::linear, [] (Patch& p, float v) { p.synth.sustain = v; });
        add (ParamID::release, 5, 500, S::logarithmic, [] (Patch& p, float v) { p.synth.releaseMs = v; });

        add (ParamID::lfoShape, 0, (float) Choices::lfoShapes.size() - 1.0f, S::integer, [] (Patch& p, float v) { p.synth.lfoShape = (int) v; });
        add (ParamID::lfoRate, 0, (float) Choices::lfoRates.size() - 1.0f, S::integer, [] (Patch& p, float v) { p.synth.lfoRate = (int) v; });
        add (ParamID::lfoCutoff, -1, 1, S::linear, [] (Patch& p, float v) { p.synth.lfoToCutoff = v; });
        add (ParamID::lfoPosA, -1, 1, S::linear, [] (Patch& p, float v) { p.synth.lfoToPosA = v; });
        add (ParamID::lfoPosB, -1, 1, S::linear, [] (Patch& p, float v) { p.synth.lfoToPosB = v; });
        add (ParamID::lfoFm, -1, 1, S::linear, [] (Patch& p, float v) { p.synth.lfoToFm = v; });
        add (ParamID::lfoVolume, 0, 1, S::linear, [] (Patch& p, float v) { p.synth.lfoToVolume = v; });
        add (ParamID::lfoWarpA, -1, 1, S::linear, [] (Patch& p, float v) { p.synth.lfoToWarpA = v; });
        add (ParamID::lfoVowel, -1, 1, S::linear, [] (Patch& p, float v) { p.lfoToVowel = v; });
        add (ParamID::pitchEnv, -24, 24, S::linear, [] (Patch& p, float v) { p.synth.pitchEnvSemis = v; });
        add (ParamID::pitchDecay, 1, 500, S::logarithmic, [] (Patch& p, float v) { p.synth.pitchDecayMs = v; });

        add (ParamID::distType, 0, (float) Choices::distTypes.size() - 1.0f, S::integer, [] (Patch& p, float v) { p.fx.distType = (int) v; });
        add (ParamID::distDrive, 0, 1, S::linear, [] (Patch& p, float v) { p.fx.distDrive = v; });
        add (ParamID::distMix, 0, 1, S::linear, [] (Patch& p, float v) { p.fx.distMix = v; });
        add (ParamID::vowelMix, 0, 1, S::linear, [] (Patch& p, float v) { p.fx.vowelMix = v; });
        add (ParamID::vowel, 0, 1, S::linear, [] (Patch& p, float v) { p.fx.vowel = v; });
        add (ParamID::combMix, 0, 1, S::linear, [] (Patch& p, float v) { p.fx.combMix = v; });
        add (ParamID::combFeedback, 0, 0.95f, S::linear, [] (Patch& p, float v) { p.fx.combFeedback = v; });
        add (ParamID::squash, 0, 1, S::linear, [] (Patch& p, float v) { p.fx.smash = v; });
        add (ParamID::tilt, -1, 1, S::linear, [] (Patch& p, float v) { p.fx.tilt = v; });
        add (ParamID::clip, 0, 1, S::linear, [] (Patch& p, float v) { p.fx.clip = v; });
        add (ParamID::resample, 0, 1, S::integer, [] (Patch& p, float v) { p.fx.resample = v > 0.5f; });

        return dims;
    }

    Patch decode (const std::vector<Dim>& dims, const std::vector<double>& x)
    {
        auto patch = basePatch();
        for (size_t i = 0; i < dims.size(); ++i)
            dims[i].apply (patch, dims[i].toPlain (x[i]));
        return patch;
    }

    //==============================================================================
    int midiFromHz (double hz) { return juce::roundToInt (69.0 + 12.0 * std::log2 (hz / 440.0)); }

    // Fundamental of a bass segment from the autocorrelation of its low end (27 to 110 Hz).
    std::optional<int> estimateNote (const std::vector<float>& lows, int start, int end, double sampleRate)
    {
        const auto minLag = (int) (sampleRate / 110.0), maxLag = (int) (sampleRate / 27.0);
        const auto length = juce::jmin (end - start, 8192) - maxLag;
        if (length < maxLag)
            return std::nullopt;

        std::vector<double> r ((size_t) maxLag + 1, 0.0);
        auto best = 0.0;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double xy = 0, xx = 0, yy = 0;
            for (int i = start; i < start + length; ++i)
            {
                const auto a = lows[(size_t) i], b = lows[(size_t) (i + lag)];
                xy += a * b; xx += a * a; yy += b * b;
            }
            r[(size_t) lag] = xx > 0 && yy > 0 ? xy / std::sqrt (xx * yy) : 0.0;
            best = juce::jmax (best, r[(size_t) lag]);
        }

        if (best < 0.5)
            return std::nullopt;

        // The shortest lag that is nearly as strong as the best, so subharmonics don't win.
        for (int lag = minLag + 1; lag < maxLag; ++lag)
            if (r[(size_t) lag] >= 0.9 * best && r[(size_t) lag] >= r[(size_t) lag - 1] && r[(size_t) lag] >= r[(size_t) lag + 1])
                return midiFromHz (sampleRate / lag);

        return std::nullopt;
    }

    std::optional<Reference> loadReference (const juce::File& file, double maxSeconds, int forcedNote)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
            return std::nullopt;

        Reference ref;
        ref.sampleRate = reader->sampleRate;
        const auto length = (int) juce::jmin ((juce::int64) (maxSeconds * ref.sampleRate), reader->lengthInSamples);
        juce::AudioBuffer<float> buffer ((int) reader->numChannels, length);
        reader->read (&buffer, 0, length, 0, true, true);

        ref.audio.assign ((size_t) length, 0.0f);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < length; ++i)
                ref.audio[(size_t) i] += buffer.getSample (ch, i) / (float) buffer.getNumChannels();

        // Hits: 5 ms energy frames, on above peak - 24 dB, off below peak - 32 dB, and a new
        // hit wherever the level jumps by 9 dB within 30 ms (legato re-attacks).
        const auto hop = juce::roundToInt (ref.sampleRate * 0.005);
        std::vector<float> db;
        for (int i = 0; i + hop <= length; i += hop)
        {
            double sum = 0;
            for (int j = i; j < i + hop; ++j)
                sum += ref.audio[(size_t) j] * ref.audio[(size_t) j];
            db.push_back ((float) (10.0 * std::log10 (sum / hop + 1e-12)));
        }

        const auto peak = *std::max_element (db.begin(), db.end());
        std::vector<std::pair<int, int>> hits;
        int onAt = -1;
        auto close = [&] (int frame)
        {
            if (frame - onAt >= 6)
                hits.emplace_back (onAt * hop, frame * hop);
            onAt = -1;
        };

        for (int f = 0; f < (int) db.size(); ++f)
        {
            if (onAt < 0)
            {
                if (db[(size_t) f] > peak - 24.0f)
                    onAt = f;
                continue;
            }

            if (db[(size_t) f] < peak - 32.0f)
            {
                close (f);
                continue;
            }

            const auto recentMin = *std::min_element (db.begin() + juce::jmax (onAt, f - 6), db.begin() + f);
            if (f - onAt >= 6 && db[(size_t) f] - recentMin > 9.0f)
            {
                close (f);
                onAt = f;
            }
        }
        if (onAt >= 0)
            close ((int) db.size());

        // Pitch from the low end only, where the fundamental of a riddim bass lives.
        std::vector<float> lows = ref.audio;
        for (int pass = 0; pass < 2; ++pass)
        {
            juce::dsp::IIR::Filter<float> lp (juce::dsp::IIR::Coefficients<float>::makeLowPass (ref.sampleRate, 200.0));
            for (auto& s : lows)
                s = lp.processSample (s);
        }

        std::vector<std::optional<int>> pitches;
        std::vector<int> found;
        for (const auto& [start, end] : hits)
        {
            const auto note = forcedNote >= 0 ? std::optional<int> (forcedNote)
                                              : estimateNote (lows, start + (int) (ref.sampleRate * 0.02), end, ref.sampleRate);
            pitches.push_back (note);
            if (note)
                found.push_back (*note);
        }

        auto fallback = 30; // F#1, a common riddim key
        if (! found.empty())
        {
            std::nth_element (found.begin(), found.begin() + (long) found.size() / 2, found.end());
            fallback = found[found.size() / 2];
        }

        for (size_t i = 0; i < hits.size(); ++i)
            ref.notes.push_back ({ hits[i].first, hits[i].second, pitches[i].value_or (fallback) });

        return ref;
    }

    //==============================================================================
    std::vector<float> render (const Patch& patch, const Reference& ref, double bpm)
    {
        RiddimEngine engine;
        engine.prepare (ref.sampleRate, blockSize);
        RiddimFx fx;
        fx.prepare (ref.sampleRate, blockSize);

        const auto latency = fx.getLatencySamples();
        const auto length = (int) ref.audio.size();
        const auto total = length + latency;

        juce::MidiBuffer all;
        for (const auto& note : ref.notes)
        {
            all.addEvent (juce::MidiMessage::noteOn (1, note.midi, 0.9f), note.start);
            all.addEvent (juce::MidiMessage::noteOff (1, note.midi), note.end);
        }

        std::vector<float> out ((size_t) length, 0.0f);
        juce::AudioBuffer<float> block (2, blockSize);

        for (int pos = 0; pos < total; pos += blockSize)
        {
            const auto n = juce::jmin (blockSize, total - pos);
            juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, n);

            juce::MidiBuffer midi;
            for (const auto m : all)
                if (m.samplePosition >= pos && m.samplePosition < pos + n)
                    midi.addEvent (m.getMessage(), m.samplePosition - pos);

            engine.process (view, midi, patch.synth, bpm, std::nullopt);

            // The same wiring as the synth processor.
            auto fxSettings = patch.fx;
            fxSettings.vowelModTrace = engine.getLfoTrace();
            fxSettings.vowelModDepth = patch.lfoToVowel;
            fxSettings.combTrackHz = (float) engine.getCurrentHz();
            fx.process (view, fxSettings);

            for (int i = 0; i < n; ++i)
            {
                const auto at = pos + i - latency;
                if (at >= 0 && at < length)
                    out[(size_t) at] = 0.5f * (view.getSample (0, i) + view.getSample (1, i));
            }
        }

        return out;
    }

    //==============================================================================
    // Log-mel spectrogram at one resolution.
    class MelAnalyser
    {
    public:
        MelAnalyser (int fftOrder, int hopSize, int numBands, double lowHz, double sampleRate)
            : fft (fftOrder), size (1 << fftOrder), hop (hopSize), bands (numBands), window ((size_t) size)
        {
            for (int i = 0; i < size; ++i)
                window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) size);

            auto mel = [] (double hz) { return 2595.0 * std::log10 (1.0 + hz / 700.0); };
            auto hzFromMel = [] (double m) { return 700.0 * (std::pow (10.0, m / 2595.0) - 1.0); };
            const auto highHz = juce::jmin (16000.0, sampleRate * 0.5 - 1.0);
            const auto binHz = sampleRate / size;

            for (int b = 0; b < bands; ++b)
            {
                const auto m0 = mel (lowHz), m1 = mel (highHz);
                const auto lo = hzFromMel (m0 + (m1 - m0) * b / (bands + 1));
                const auto mid = hzFromMel (m0 + (m1 - m0) * (b + 1) / (bands + 1));
                const auto hi = hzFromMel (m0 + (m1 - m0) * (b + 2) / (bands + 1));

                std::vector<std::pair<int, float>> weights;
                for (int k = (int) std::ceil (lo / binHz); k <= (int) (hi / binHz) && k <= size / 2; ++k)
                {
                    const auto f = k * binHz;
                    weights.emplace_back (k, (float) (f < mid ? (f - lo) / (mid - lo) : (hi - f) / (hi - mid)));
                }
                if (weights.empty())
                    weights.emplace_back (juce::jlimit (0, size / 2, juce::roundToInt (mid / binHz)), 1.0f);
                filters.push_back (std::move (weights));
            }
        }

        // Frames x bands, in dB.
        std::vector<float> analyse (const std::vector<float>& audio) const
        {
            const auto frames = (int) audio.size() / hop;
            std::vector<float> result ((size_t) (frames * bands));
            std::vector<float> buffer ((size_t) size * 2);

            for (int f = 0; f < frames; ++f)
            {
                std::fill (buffer.begin(), buffer.end(), 0.0f);
                const auto start = f * hop - size / 2;
                for (int i = 0; i < size; ++i)
                    if (const auto at = start + i; at >= 0 && at < (int) audio.size())
                        buffer[(size_t) i] = audio[(size_t) at] * window[(size_t) i];

                fft.performFrequencyOnlyForwardTransform (buffer.data(), true);

                for (int b = 0; b < bands; ++b)
                {
                    double energy = 0;
                    for (const auto& [bin, w] : filters[(size_t) b])
                        energy += w * buffer[(size_t) bin] * buffer[(size_t) bin];
                    result[(size_t) (f * bands + b)] = (float) (10.0 * std::log10 (energy + 1e-12));
                }
            }

            return result;
        }

    private:
        juce::dsp::FFT fft;
        int size, hop, bands;
        std::vector<float> window;
        std::vector<std::vector<std::pair<int, float>>> filters;
    };

    struct Analysers
    {
        explicit Analysers (double sampleRate)
            : fine (11, 256, 64, 30.0, sampleRate), fast (9, 128, 32, 150.0, sampleRate) {}

        MelAnalyser fine; // pitch and timbre
        MelAnalyser fast; // transients and gaps
    };

    struct Target
    {
        double rms = 0;
        std::vector<float> fine, fast;
    };

    double rmsOf (const std::vector<float>& audio)
    {
        double sum = 0;
        for (auto s : audio)
            sum += s * s;
        return std::sqrt (sum / juce::jmax ((size_t) 1, audio.size()));
    }

    // Mean absolute dB difference, ignoring anything 80 dB under the reference's loudest band.
    double spectralDistance (const std::vector<float>& ref, const std::vector<float>& candidate)
    {
        const auto floor = *std::max_element (ref.begin(), ref.end()) - 80.0f;
        double sum = 0;
        for (size_t i = 0; i < ref.size(); ++i)
            sum += std::abs (juce::jmax (ref[i], floor) - juce::jmax (candidate[i], floor));
        return sum / (double) ref.size();
    }

    // Level-matched to the reference first, since the preset's output gain is easy to set by ear.
    double loss (std::vector<float> audio, const Target& target, const Analysers& analysers)
    {
        const auto rms = rmsOf (audio);
        if (rms < 1e-6)
            return 100.0;

        const auto gain = (float) (target.rms / rms);
        for (auto& s : audio)
            s *= gain;

        return 0.5 * (spectralDistance (target.fine, analysers.fine.analyse (audio))
                      + spectralDistance (target.fast, analysers.fast.analyse (audio)));
    }

    //==============================================================================
    void writePreset (const juce::File& file, const std::vector<Dim>& dims, const std::vector<double>& x)
    {
        juce::XmlElement root ("RiddimMachine");
        auto addValue = [&root] (const juce::String& id, float value)
        {
            auto* child = root.createNewChildElement ("PARAM");
            child->setAttribute ("id", id);
            child->setAttribute ("value", value);
        };

        for (size_t i = 0; i < dims.size(); ++i)
            addValue (dims[i].id, dims[i].toPlain (x[i]));
        for (const auto& [id, value] : fixedValues)
            addValue (id, value);

        root.writeTo (file);
    }

    void writeWav (const juce::File& file, const std::vector<float>& audio, double sampleRate)
    {
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        juce::WavAudioFormat wav;
        const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate).withNumChannels (1).withBitsPerSample (24);
        if (auto writer = wav.createWriterFor (stream, options))
        {
            const float* channels[] { audio.data() };
            writer->writeFromFloatArrays (channels, 1, (int) audio.size());
        }
    }

    juce::String argValue (int argc, char* argv[], const char* name, const juce::String& fallback)
    {
        for (int i = 3; i + 1 < argc; ++i)
            if (juce::String (argv[i]) == name)
                return argv[i + 1];
        return fallback;
    }
}

int main (int argc, char* argv[])
{
    if (argc < 3)
    {
        std::printf ("usage: RiddimMatch <reference audio> <output.rmpreset> [--bpm 150] [--note 30] [--evals 2400] [--seconds 6] [--seed 1]\n");
        return 2;
    }

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto refFile = cwd.getChildFile (argv[1]);
    const auto outFile = cwd.getChildFile (argv[2]);
    const auto bpm = argValue (argc, argv, "--bpm", "150").getDoubleValue();
    const auto forcedNote = argValue (argc, argv, "--note", "-1").getIntValue();
    const auto maxEvals = argValue (argc, argv, "--evals", "2400").getIntValue();
    const auto seconds = argValue (argc, argv, "--seconds", "6").getDoubleValue();
    std::mt19937 rng ((unsigned) argValue (argc, argv, "--seed", "1").getIntValue());

    // Build the wavetables once and keep them alive for every render.
    juce::SharedResourcePointer<WavetableBank> bank;

    const auto ref = loadReference (refFile, seconds, forcedNote);
    if (! ref || ref->notes.empty())
    {
        std::printf ("Couldn't read %s, or found no hits in it.\n", refFile.getFullPathName().toRawUTF8());
        return 1;
    }

    std::printf ("Reference: %.2f s at %.0f Hz, %d hits:", ref->audio.size() / ref->sampleRate, ref->sampleRate, (int) ref->notes.size());
    for (const auto& note : ref->notes)
        std::printf (" %s@%.2fs", juce::MidiMessage::getMidiNoteName (note.midi, true, true, 3).toRawUTF8(), note.start / ref->sampleRate);
    std::printf ("\n");

    const Analysers analysers (ref->sampleRate);
    Target target;
    target.rms = rmsOf (ref->audio);
    target.fine = analysers.fine.analyse (ref->audio);
    target.fast = analysers.fast.analyse (ref->audio);

    const auto dims = makeDims();
    const auto n = (int) dims.size();
    int evals = 0;

    // Scores a batch of candidates in parallel; points outside 0..1 are clamped and penalised.
    auto evaluate = [&] (const std::vector<std::vector<double>>& points)
    {
        std::vector<double> scores (points.size());
        std::vector<std::thread> workers;
        for (int t = 0; t < numThreads; ++t)
            workers.emplace_back ([&, t]
            {
                const Analysers local (ref->sampleRate);
                for (size_t i = (size_t) t; i < points.size(); i += numThreads)
                {
                    double penalty = 0;
                    for (auto v : points[i])
                        penalty += juce::square (v - juce::jlimit (0.0, 1.0, v));
                    scores[i] = loss (render (decode (dims, points[i]), *ref, bpm), target, local) + 10.0 * penalty;
                }
            });
        for (auto& w : workers)
            w.join();
        evals += (int) points.size();
        return scores;
    };

    // Sep-CMA-ES (Ros and Hansen 2008).
    const auto lambda = 16, mu = lambda / 2;
    std::vector<double> weights ((size_t) mu);
    for (int i = 0; i < mu; ++i)
        weights[(size_t) i] = std::log (mu + 0.5) - std::log (i + 1.0);
    const auto weightSum = std::accumulate (weights.begin(), weights.end(), 0.0);
    for (auto& w : weights)
        w /= weightSum;
    const auto mueff = 1.0 / std::inner_product (weights.begin(), weights.end(), weights.begin(), 0.0);

    const auto cc = 4.0 / (n + 4.0);
    const auto cs = (mueff + 2.0) / (n + mueff + 3.0);
    const auto c1 = (n + 2.0) / 3.0 * 2.0 / (juce::square (n + 1.3) + mueff);
    const auto cmu = juce::jmin (1.0 - c1, (n + 2.0) / 3.0 * 2.0 * (mueff - 2.0 + 1.0 / mueff) / (juce::square (n + 2.0) + mueff));
    const auto damps = 1.0 + 2.0 * juce::jmax (0.0, std::sqrt ((mueff - 1.0) / (n + 1.0)) - 1.0) + cs;
    const auto chiN = std::sqrt ((double) n) * (1.0 - 1.0 / (4.0 * n) + 1.0 / (21.0 * n * n));

    // Start from the best of a batch of random patches.
    std::uniform_real_distribution<double> uniform (0.0, 1.0);
    std::normal_distribution<double> normal;
    std::vector<std::vector<double>> seeds ((size_t) lambda * 3, std::vector<double> ((size_t) n));
    for (auto& s : seeds)
        for (auto& v : s)
            v = uniform (rng);
    const auto seedScores = evaluate (seeds);
    const auto firstBest = (size_t) (std::min_element (seedScores.begin(), seedScores.end()) - seedScores.begin());

    auto mean = seeds[firstBest];
    auto best = mean;
    auto bestScore = seedScores[firstBest];
    std::printf ("Best random start: %.2f dB\n", bestScore);

    std::vector<double> variance ((size_t) n, 1.0), pc ((size_t) n, 0.0), ps ((size_t) n, 0.0);
    auto sigma = 0.25;
    const auto startMs = juce::Time::getMillisecondCounterHiRes();

    for (int generation = 0; evals < maxEvals; ++generation)
    {
        std::vector<std::vector<double>> z ((size_t) lambda, std::vector<double> ((size_t) n)), points = z;
        for (int k = 0; k < lambda; ++k)
            for (int i = 0; i < n; ++i)
            {
                z[(size_t) k][(size_t) i] = normal (rng);
                points[(size_t) k][(size_t) i] = mean[(size_t) i] + sigma * std::sqrt (variance[(size_t) i]) * z[(size_t) k][(size_t) i];
            }

        const auto scores = evaluate (points);
        std::vector<int> order ((size_t) lambda);
        std::iota (order.begin(), order.end(), 0);
        std::sort (order.begin(), order.end(), [&] (int a, int b) { return scores[(size_t) a] < scores[(size_t) b]; });

        if (scores[(size_t) order[0]] < bestScore)
        {
            bestScore = scores[(size_t) order[0]];
            best = points[(size_t) order[0]];
        }

        std::vector<double> zMean ((size_t) n, 0.0), yMean ((size_t) n, 0.0);
        for (int j = 0; j < mu; ++j)
            for (int i = 0; i < n; ++i)
            {
                const auto zi = z[(size_t) order[(size_t) j]][(size_t) i];
                zMean[(size_t) i] += weights[(size_t) j] * zi;
                yMean[(size_t) i] += weights[(size_t) j] * std::sqrt (variance[(size_t) i]) * zi;
            }

        double psNorm = 0;
        for (int i = 0; i < n; ++i)
        {
            mean[(size_t) i] += sigma * yMean[(size_t) i];
            ps[(size_t) i] = (1.0 - cs) * ps[(size_t) i] + std::sqrt (cs * (2.0 - cs) * mueff) * zMean[(size_t) i];
            psNorm += juce::square (ps[(size_t) i]);
        }
        psNorm = std::sqrt (psNorm);

        const auto hsig = psNorm / std::sqrt (1.0 - std::pow (1.0 - cs, 2.0 * (generation + 1))) / chiN < 1.4 + 2.0 / (n + 1.0);

        for (int i = 0; i < n; ++i)
        {
            pc[(size_t) i] = (1.0 - cc) * pc[(size_t) i] + (hsig ? std::sqrt (cc * (2.0 - cc) * mueff) : 0.0) * yMean[(size_t) i];

            double rankMu = 0;
            for (int j = 0; j < mu; ++j)
                rankMu += weights[(size_t) j] * variance[(size_t) i] * juce::square (z[(size_t) order[(size_t) j]][(size_t) i]);

            variance[(size_t) i] = (1.0 - c1 - cmu) * variance[(size_t) i]
                                 + c1 * (juce::square (pc[(size_t) i]) + (hsig ? 0.0 : cc * (2.0 - cc) * variance[(size_t) i]))
                                 + cmu * rankMu;
        }

        sigma *= std::exp ((cs / damps) * (psNorm / chiN - 1.0));
        sigma = juce::jlimit (1e-4, 0.5, sigma);

        if (generation % 10 == 0)
            std::printf ("gen %3d  evals %4d  best %.2f dB  step %.3f  %.0f s\n", generation, evals, bestScore, sigma,
                         (juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0);
        std::fflush (stdout);
    }

    writePreset (outFile, dims, best);
    const auto matched = render (decode (dims, best), *ref, bpm);
    writeWav (outFile.withFileExtension ("wav"), matched, ref->sampleRate);

    std::printf ("Done: %.2f dB average spectral difference after %d renders.\n", bestScore, evals);
    for (size_t i = 0; i < dims.size(); ++i)
        std::printf ("  %-16s %g\n", dims[i].id.toRawUTF8(), dims[i].toPlain (best[i]));
    std::printf ("Wrote %s\n", outFile.getFullPathName().toRawUTF8());
    return 0;
}
