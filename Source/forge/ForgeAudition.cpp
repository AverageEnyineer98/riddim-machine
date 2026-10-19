#include "ForgeAudition.h"
#include "ForgeRenderer.h"
#include <juce_dsp/juce_dsp.h>

namespace
{
    constexpr int blockSize = 256;
    constexpr int stepsPerBar = 12; // 1/8 triplets

    struct Hit { int step, length, note; };
    const std::vector<Hit> bassBar { { 0, 2, 30 }, { 3, 1, 30 }, { 4, 1, 30 }, { 6, 2, 30 }, { 9, 1, 30 }, { 10, 2, 33 } };
    const std::vector<Hit> melodyBar { { 0, 3, 60 }, { 3, 3, 63 }, { 6, 2, 67 }, { 9, 3, 65 } };
}

using namespace ForgeAudition;

bool ForgeAudition::isMelodic (const juce::String& category)
{
    return category == "Lead" || category == "Pad" || category == "Pluck";
}

juce::AudioBuffer<float> ForgeAudition::render (const juce::AudioProcessorValueTreeState& state, bool melodic, int bars)
{
    ForgeRenderer renderer (state);
    renderer.prepare (sampleRate, blockSize);
    for (int l = 0; l < Forge::numLfos; ++l)
        renderer.setLfoCurve (l, Forge::LfoCurve::fromString (state.state.getProperty (ForgeID::lfoCurveProperty (l)).toString()));

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

ForgeAudition::Features ForgeAudition::analyse (const juce::AudioBuffer<float>& audio)
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

    Features f;
    for (size_t b = 0; b < bandPower.size(); ++b)
        f.bands[b] = (float) juce::jlimit (-120.0, 0.0, 10.0 * std::log10 (bandPower[b] / power + 1.0e-12));

    f.rmsDb = juce::Decibels::gainToDecibels ((float) rms, -120.0f);
    f.peak = audio.getMagnitude (0, total);
    f.clipPercent = (float) (100.0 * clipped / (2.0 * total));
    f.finite = finite;
    f.centroid = (float) (weighted / power);
    f.flatness = (float) flatness;
    f.lowRatio = (float) (low / power);
    f.movement = (float) movement;
    return f;
}

juce::var ForgeAudition::toVar (const Features& f)
{
    juce::Array<juce::var> bands;
    for (auto b : f.bands)
        bands.add (b);

    auto* result = new juce::DynamicObject();
    result->setProperty ("rmsDb", f.rmsDb);
    result->setProperty ("peak", f.peak);
    result->setProperty ("clipPercent", f.clipPercent);
    result->setProperty ("finite", f.finite);
    result->setProperty ("centroid", f.centroid);
    result->setProperty ("flatness", f.flatness);
    result->setProperty ("lowRatio", f.lowRatio);
    result->setProperty ("movement", f.movement);
    result->setProperty ("bands", bands);
    return juce::var (result);
}
