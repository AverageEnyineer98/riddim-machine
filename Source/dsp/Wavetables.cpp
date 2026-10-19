#include "Wavetables.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <thread>

namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    double saw (double p)    { return 2.0 * (p - std::floor (p)) - 1.0; }
    double sine (double p)   { return std::sin (twoPi * p); }
    double square (double p, double width = 0.5) { return (p - std::floor (p)) < width ? 1.0 : -1.0; }
    double triangle (double p)
    {
        const auto x = p - std::floor (p);
        return x < 0.5 ? 4.0 * x - 1.0 : 3.0 - 4.0 * x;
    }

    double gauss (double x, double width) { return std::exp (-(x * x) / (2.0 * width * width)); }

    // Vowel formants (F1, F2) for A, E, I, O, U.
    constexpr double vowels[5][2] = { { 800, 1150 }, { 400, 1600 }, { 300, 2300 }, { 450, 800 }, { 325, 700 } };

    using Generator = double (*) (double phase, double morph);

    // Hard-sync sweep: the classic riddim saw that gets nastier as it morphs.
    double syncSaw (double p, double t)     { return saw (p * (1.0 + t * 7.0)); }

    // Self-FM sine: smooth at the start, growling at the end.
    double growl (double p, double t)       { return std::sin (twoPi * p + (0.5 + t * 8.0) * sine (p)) * 0.8 + 0.2 * saw (p); }

    // Vowel formants across the frames, for "yoi" and talking basses.
    double formant (double p, double t)
    {
        const auto v = t * 4.0;
        const auto i = juce::jmin (3, (int) v);
        const auto mix = v - (double) i;
        const auto f1 = vowels[i][0] + mix * (vowels[i + 1][0] - vowels[i][0]);
        const auto f2 = vowels[i][1] + mix * (vowels[i + 1][1] - vowels[i][1]);
        constexpr double baseHz = 110.0;

        double s = 0.0;
        for (int n = 1; n <= 96; ++n)
        {
            const auto hz = baseHz * n;
            const auto amp = (gauss (hz - f1, 130.0) + 0.7 * gauss (hz - f2, 170.0) + 0.04) / std::sqrt ((double) n);
            s += amp * std::sin (twoPi * p * n);
        }
        return s;
    }

    // Sine wavefolder for tearout-style harmonics.
    double tearoutFold (double p, double t) { return std::sin ((1.0 + t * 9.0) * sine (p) * juce::MathConstants<double>::halfPi); }

    // Ring-modulated, inharmonic screech.
    double metalScreech (double p, double t) { return 0.7 * sine (p) * sine (p * (2.0 + t * 13.0)) + 0.3 * saw (p * 2.0); }

    // Pulse width narrows from square to thin pulse.
    double pulse (double p, double t)       { return square (p, 0.5 - t * 0.45); }

    // Two phase-offset saws: shifting comb notches like a reese.
    double reese (double p, double t)       { return 0.5 * (saw (p) + saw (p + t * 0.5)) + 0.15 * triangle (p * 3.0); }

    // Sine to triangle to saw to square.
    double basicShapes (double p, double t)
    {
        const auto v = t * 3.0;
        const auto i = juce::jmin (2, (int) v);
        const auto mix = v - (double) i;
        const double shapes[4] = { sine (p), triangle (p), saw (p), square (p) };
        return shapes[i] + mix * (shapes[i + 1] - shapes[i]);
    }

    // Additive helper: sums harmonics 1..n with amplitude amp(n).
    // Generators are sampled at p = i / frameSize, so harmonics come from one sine table.
    const std::vector<double>& sineTable()
    {
        static const auto table = []
        {
            std::vector<double> t ((size_t) WavetableBank::frameSize);
            for (size_t i = 0; i < t.size(); ++i)
                t[i] = std::sin (twoPi * (double) i / (double) t.size());
            return t;
        }();
        return table;
    }

    template <typename Amp>
    double additive (double p, int count, Amp&& amp)
    {
        const auto& table = sineTable();
        const auto mask = WavetableBank::frameSize - 1;
        const auto index = (int) std::lround ((p - std::floor (p)) * WavetableBank::frameSize);
        double s = 0.0;
        for (int n = 1; n <= count; ++n)
            s += amp (n) * table[(size_t) ((index * n) & mask)];
        return s;
    }

    // Formant voice with a peak at `peak` harmonics and a second at `peak2`.
    double formantVoice (double p, double peak, double peak2, double width)
    {
        return additive (p, 120, [&] (int n) { return (gauss (n - peak, width) + 0.6 * gauss (n - peak2, width * 1.4) + 0.03) / std::sqrt ((double) n); });
    }

    // Saw that softens like an analog filter at the start and opens up by the end.
    double analogSaw (double p, double t)   { return additive (p, 200, [t] (int n) { return std::pow ((double) n, -(2.2 - t * 1.2)) * (n % 2 ? 1.0 : 0.94); }); }
    double analogSquare (double p, double t) { return additive (p, 200, [t] (int n) { return n % 2 ? std::pow ((double) n, -(2.2 - t * 1.2)) : 0.0; }); }
    double sawToSquare (double p, double t) { return 0.5 * (saw (p) - saw (p + 0.5 * t)) + (1.0 - t) * 0.5 * saw (p); }
    double sineHarmonics (double p, double t)
    {
        const auto count = 1.0 + t * 31.0;
        return additive (p, 32, [count] (int n) { return n <= count ? 1.0 / n : (n < count + 1.0 ? (count + 1.0 - n) / n : 0.0); });
    }
    double drawbars (double p, double t)
    {
        static const double bars[9][2] = { { 1, 1 }, { 2, 0.8 }, { 3, 0.6 }, { 4, 0.5 }, { 6, 0.4 }, { 8, 0.35 }, { 10, 0.3 }, { 12, 0.25 }, { 16, 0.2 } };
        double s = 0.0;
        for (int i = 0; i < 9; ++i)
            s += (i == 0 ? 1.0 : juce::jlimit (0.0, 1.0, t * 9.0 - i + 1.0)) * bars[i][1] * sine (p * bars[i][0]);
        return s;
    }
    double fm21 (double p, double t)       { return std::sin (twoPi * p + t * 6.0 * sine (2.0 * p)); }
    double fm32 (double p, double t)       { return std::sin (twoPi * 2.0 * p + t * 7.0 * sine (3.0 * p)); }
    double fm71 (double p, double t)       { return std::sin (twoPi * p + t * 4.0 * sine (7.0 * p)); }
    double wetGrowl (double p, double t)   { return formantVoice (p, 2.0 + t * 30.0, 6.0 + t * 50.0, 1.8 + t * 2.0); }
    double vowelAOU (double p, double t)
    {
        // Harmonic numbers for a 110 Hz voice.
        const double a[2] = { 7.3, 10.5 }, o[2] = { 4.1, 7.3 }, u[2] = { 3.0, 6.4 };
        const auto x = t * 2.0;
        const auto* from = x < 1.0 ? a : o;
        const auto* to = x < 1.0 ? o : u;
        const auto m = x < 1.0 ? x : x - 1.0;
        return formantVoice (p, from[0] + m * (to[0] - from[0]), from[1] + m * (to[1] - from[1]), 1.2);
    }
    double vowelIEA (double p, double t)
    {
        const double i[2] = { 2.7, 20.9 }, e[2] = { 3.6, 14.5 }, a[2] = { 7.3, 10.5 };
        const auto x = t * 2.0;
        const auto* from = x < 1.0 ? i : e;
        const auto* to = x < 1.0 ? e : a;
        const auto m = x < 1.0 ? x : x - 1.0;
        return formantVoice (p, from[0] + m * (to[0] - from[0]), from[1] + m * (to[1] - from[1]), 1.2);
    }
    double digitalCrush (double p, double t) { const auto steps = std::exp2 (8.0 - t * 6.0); return std::round (saw (p) * steps) / steps; }
    double hardFold (double p, double t)   { const auto x = (1.0 + t * 14.0) * sine (p); return std::asin (std::sin (x)) / juce::MathConstants<double>::halfPi; }
    double spectralComb (double p, double t) { return additive (p, 96, [t] (int n) { return std::abs (std::cos (juce::MathConstants<double>::pi * n * (0.05 + t * 0.2))) / n; }); }
    double harmonicSaws (double p, double t) { return saw (p) + t * 0.6 * saw (2.0 * p) + t * t * 0.45 * saw (3.0 * p) + t * t * t * 0.3 * saw (5.0 * p); }
    double pulseSync (double p, double t)  { return square (p * (1.0 + t * 7.0), 0.3); }
    double triSync (double p, double t)    { return triangle (p * (1.0 + t * 7.0)); }
    double squareSync (double p, double t) { return square (p * (1.0 + t * 7.0)); }
    double robotBuzz (double p, double t)
    {
        const auto x = p - std::floor (p);
        const auto pulseWidth = 0.15 - t * 0.12;
        return (x < pulseWidth ? 1.0 : -pulseWidth / (1.0 - pulseWidth)) * 0.6 + 0.4 * formantVoice (p, 5.0 + t * 12.0, 14.0, 1.5) * 0.4;
    }
    double dirtySaw (double p, double t)   { return std::tanh (saw (p) * (1.0 + t * 12.0) + 0.3 * t * sine (3.0 * p)); }
    double phaseDistSaw (double p, double t) { const auto x = p - std::floor (p); const auto d = 0.5 - t * 0.48;
                                               const auto q = x < d ? 0.5 * x / d : 0.5 + 0.5 * (x - d) / (1.0 - d);
                                               return std::cos (twoPi * q); }
    double resonantSweep (double p, double t) { const auto x = p - std::floor (p); return std::sin (twoPi * x * (1.0 + t * 15.0)) * (1.0 - x); }
    double noiseCycle (double p, double t)
    {
        // A fixed random spectrum (seeded) that gets brighter across the frames.
        static const auto phases = [] { std::array<double, 128> a {}; juce::Random r (4321); for (auto& v : a) v = r.nextDouble(); return a; }();
        double s = 0.0;
        const auto top = 8.0 + t * 120.0;
        for (int n = 1; n <= 128 && n <= top; ++n)
            s += std::sin (twoPi * (p * n + phases[(size_t) n - 1])) / std::sqrt ((double) n);
        return s;
    }
    double tearoutScreech (double p, double t) { return std::sin ((1.0 + t * 4.0) * std::sin (twoPi * p + t * 5.0 * sine (5.0 * p)) * 1.5); }
    double chirpGrowl (double p, double t) { const auto x = p - std::floor (p); return std::sin (twoPi * (x + t * 6.0 * x * x)); }
    double warmSub (double p, double t)    { return sine (p) + t * 0.5 * sine (2.0 * p) + t * t * 0.25 * sine (3.0 * p); }

    const Generator generators[] = { syncSaw, growl, formant, tearoutFold, metalScreech, pulse, reese, basicShapes,
                                     analogSaw, analogSquare, sawToSquare, sineHarmonics, drawbars, fm21, fm32, fm71,
                                     wetGrowl, vowelAOU, vowelIEA, digitalCrush, hardFold, spectralComb, harmonicSaws,
                                     pulseSync, triSync, squareSync, robotBuzz, dirtySaw, phaseDistSaw, resonantSweep,
                                     noiseCycle, tearoutScreech, chirpGrowl, warmSub };
}

const juce::StringArray& WavetableBank::getTableNames()
{
    static const juce::StringArray names { "Riddim Sync Saw", "Growl FM", "Yoi Formant", "Tearout Fold",
                                           "Metal Screech", "Pulse Width", "Reese Stack", "Basic Shapes",
                                           "Analog Saw", "Analog Square", "Saw To Square", "Sine Harmonics", "Drawbars",
                                           "FM 2:1", "FM 3:2", "FM 7:1", "Wet Growl", "Vowel A-O-U", "Vowel I-E-A",
                                           "Digital Crush", "Hard Fold", "Spectral Comb", "Harmonic Saws", "Pulse Sync",
                                           "Tri Sync", "Square Sync", "Robot Buzz", "Dirty Saw", "Phase Dist", "Resonant Sweep",
                                           "Noise Cycle", "Tearout Screech", "Chirp Growl", "Warm Sub" };
    return names;
}

WavetableBank::WavetableBank()
{
    // Tables are independent, so build them on every core; startup is otherwise seconds long.
    constexpr auto numTables = (int) std::size (generators);
    tables.resize ((size_t) numTables);
    std::atomic<int> next { 0 };

    auto work = [this, &next]
    {
        std::vector<float> raw ((size_t) (numFrames * frameSize));

        for (int t = next++; t < numTables; t = next++)
        {
            const auto generator = generators[t];
            for (int frame = 0; frame < numFrames; ++frame)
            {
                const auto morph = (double) frame / (double) (numFrames - 1);
                for (int i = 0; i < frameSize; ++i)
                    raw[(size_t) (frame * frameSize + i)] = (float) generator ((double) i / (double) frameSize, morph);
            }

            tables[(size_t) t] = buildLevels (raw, numFrames, true);
        }
    };

    std::vector<std::thread> threads;
    const auto numThreads = juce::jlimit (1, 16, (int) std::thread::hardware_concurrency()) - 1;
    for (int i = 0; i < numThreads; ++i)
        threads.emplace_back (work);
    work();
    for (auto& thread : threads)
        thread.join();
}

std::vector<float> WavetableBank::buildLevels (const std::vector<float>& rawFrames, int frameCount, bool normalisePerFrame)
{
    constexpr int fftOrder = 11;
    static_assert ((1 << fftOrder) == frameSize, "FFT size must match the frame size");

    juce::dsp::FFT fft (fftOrder);
    std::vector<float> spectrum ((size_t) frameSize * 2), work ((size_t) frameSize * 2);
    std::vector<float> table ((size_t) (frameCount * numLevels * frameSize));
    float tablePeak = 0.0f;

    for (int frame = 0; frame < frameCount; ++frame)
    {
        std::fill (spectrum.begin(), spectrum.end(), 0.0f);
        std::copy_n (rawFrames.begin() + frame * frameSize, frameSize, spectrum.begin());

        fft.performRealOnlyForwardTransform (spectrum.data());
        spectrum[0] = spectrum[1] = 0.0f; // remove DC

        float scale = 1.0f;

        for (int level = 0; level < numLevels; ++level)
        {
            const auto maxHarmonic = 1024 >> level;
            work = spectrum;

            // Zero every bin above the limit, on both sides of the spectrum.
            for (int bin = maxHarmonic + 1; bin <= frameSize - maxHarmonic - 1; ++bin)
                work[(size_t) bin * 2] = work[(size_t) bin * 2 + 1] = 0.0f;

            fft.performRealOnlyInverseTransform (work.data());

            // Normalise by the full-band frame so every level has the same gain.
            if (level == 0)
            {
                float peak = 0.0f;
                for (int i = 0; i < frameSize; ++i)
                    peak = juce::jmax (peak, std::abs (work[(size_t) i]));
                tablePeak = juce::jmax (tablePeak, peak);
                scale = normalisePerFrame ? (peak > 0.0f ? 1.0f / peak : 0.0f) : 1.0f;
            }

            auto* dest = table.data() + ((size_t) (frame * numLevels + level)) * frameSize;
            for (int i = 0; i < frameSize; ++i)
                dest[i] = work[(size_t) i] * scale;
        }
    }

    // Imported tables keep their frames' relative levels, scaled so the loudest one peaks at 1.
    if (! normalisePerFrame && tablePeak > 0.0f)
        for (auto& v : table)
            v /= tablePeak;

    return table;
}

//==============================================================================
namespace
{
    // The frame size named in a "clm " chunk ("<!>2048 ..."), when there is one.
    int frameSizeFromClmChunk (const juce::File& file)
    {
        juce::FileInputStream in (file);
        if (! in.openedOk())
            return 0;

        in.setPosition (12); // past "RIFF", the size and "WAVE"

        while (! in.isExhausted())
        {
            char id[5] {};
            if (in.read (id, 4) != 4)
                break;
            const auto size = (juce::int64) (juce::uint32) in.readInt();

            if (juce::String (id) == "clm ")
            {
                juce::MemoryBlock data;
                in.readIntoMemoryBlock (data, (ssize_t) std::min<juce::int64> (size, 64));
                const auto text = data.toString();
                if (text.startsWith ("<!>"))
                    return text.substring (3).getIntValue();
                return 0;
            }

            in.setPosition (in.getPosition() + size + (size & 1));
        }

        return 0;
    }
}

std::unique_ptr<UserWavetable> UserWavetable::load (const juce::File& file, juce::String& error)
{
    constexpr auto size = WavetableBank::frameSize;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
    {
        error = "Couldn't read " + file.getFileName() + " as audio.";
        return nullptr;
    }

    const auto length = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) size * 4096);
    if (length < 16)
    {
        error = file.getFileName() + " is too short to be a wavetable.";
        return nullptr;
    }

    juce::AudioBuffer<float> audio (1, length);
    reader->read (&audio, 0, length, 0, true, false);

    auto sourceFrame = file.hasFileExtension ("wav") ? frameSizeFromClmChunk (file) : 0;
    if (sourceFrame <= 0)
        sourceFrame = length >= size ? size : length;

    const auto frameCount = juce::jlimit (1, maxFrames, length / sourceFrame);

    // Resample each frame to our frame size (linear, then band-limited by buildLevels).
    std::vector<float> raw ((size_t) (frameCount * size));
    const auto* src = audio.getReadPointer (0);
    for (int f = 0; f < frameCount; ++f)
        for (int i = 0; i < size; ++i)
        {
            const auto pos = (double) i * sourceFrame / size;
            const auto i0 = (int) pos;
            const auto i1 = (i0 + 1) % sourceFrame;
            const auto frac = (float) (pos - i0);
            const auto* frame = src + f * sourceFrame;
            raw[(size_t) (f * size + i)] = frame[i0] + frac * (frame[i1] - frame[i0]);
        }

    auto table = std::make_unique<UserWavetable>();
    table->name = file.getFileNameWithoutExtension();
    table->path = file.getFullPathName();
    table->frameCount = frameCount;
    table->levels = WavetableBank::buildLevels (raw, frameCount, false);
    return table;
}
