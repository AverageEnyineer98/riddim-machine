#include "Wavetables.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>

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

    const Generator generators[] = { syncSaw, growl, formant, tearoutFold, metalScreech, pulse, reese, basicShapes };
}

const juce::StringArray& WavetableBank::getTableNames()
{
    static const juce::StringArray names { "Riddim Sync Saw", "Growl FM", "Yoi Formant", "Tearout Fold",
                                           "Metal Screech", "Pulse Width", "Reese Stack", "Basic Shapes" };
    return names;
}

WavetableBank::WavetableBank()
{
    std::vector<float> raw ((size_t) (numFrames * frameSize));

    for (auto generator : generators)
    {
        for (int frame = 0; frame < numFrames; ++frame)
        {
            const auto morph = (double) frame / (double) (numFrames - 1);
            for (int i = 0; i < frameSize; ++i)
                raw[(size_t) (frame * frameSize + i)] = (float) generator ((double) i / (double) frameSize, morph);
        }

        tables.push_back (buildLevels (raw, numFrames, true));
    }
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
