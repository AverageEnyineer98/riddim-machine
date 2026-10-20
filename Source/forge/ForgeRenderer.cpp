#include "ForgeRenderer.h"
#include "../Parameters.h"

using Forge::ModDest;

namespace
{
    constexpr int fxSlice = 64; // samples per effects update, so the mod matrix can move them
}

ForgeRenderer::ForgeRenderer (const juce::AudioProcessorValueTreeState& state)
    : parameters (state), reader (state)
{
    for (int l = 0; l < Forge::numLfos; ++l)
        setLfoCurve (l, {});
}

void ForgeRenderer::prepare (double sampleRate, int maxBlockSize)
{
    engine.prepare (sampleRate, maxBlockSize);
    fx.prepare (sampleRate, maxBlockSize);
    rack.prepare (sampleRate, maxBlockSize);
}

void ForgeRenderer::setLfoCurve (int lfo, const Forge::LfoCurve& curve)
{
    // Render into the table the engine isn't reading, then hand it over.
    const auto next = 1 - liveLfoTable[lfo];
    curve.render (lfoTables[(size_t) lfo][(size_t) next].data(), Forge::lfoTableSize);
    engine.setLfoTable (lfo, lfoTables[(size_t) lfo][(size_t) next].data());
    liveLfoTable[lfo] = next;
}

void ForgeRenderer::applyModulation (FxSettings& fxs, Forge::RackSettings& r, int sample) const
{
    auto m = [this, sample] (ModDest d) { return engine.getFxTrace (d)[sample]; };
    auto unit = [] (float base, float mod) { return juce::jlimit (0.0f, 1.0f, base + mod); };

    fxs.distDrive = unit (fxs.distDrive, m (ModDest::distDrive));
    fxs.distMix   = unit (fxs.distMix, m (ModDest::distMix));
    fxs.combMix   = unit (fxs.combMix, m (ModDest::combMix));
    fxs.smash     = unit (fxs.smash, m (ModDest::smash));
    fxs.heat      = unit (fxs.heat, m (ModDest::heat));

    r.filter.cutoff    = juce::jlimit (20.0f, 20000.0f, r.filter.cutoff * std::exp2 (m (ModDest::fxFilterCutoff) * 8.0f));
    r.filter.resonance = unit (r.filter.resonance, m (ModDest::fxFilterRes));
    r.filter.mix       = unit (r.filter.mix, m (ModDest::fxFilterMix));
    r.eqGain[0] = juce::jlimit (-24.0f, 24.0f, r.eqGain[0] + m (ModDest::eqGain1) * 24.0f);
    r.eqGain[1] = juce::jlimit (-24.0f, 24.0f, r.eqGain[1] + m (ModDest::eqGain2) * 24.0f);
    r.eqFreq[0] = juce::jlimit (20.0f, 20000.0f, r.eqFreq[0] * std::exp2 (m (ModDest::eqFreq1) * 6.0f));
    r.eqFreq[1] = juce::jlimit (20.0f, 20000.0f, r.eqFreq[1] * std::exp2 (m (ModDest::eqFreq2) * 6.0f));
    r.bodeShift = juce::jlimit (-1.0f, 1.0f, r.bodeShift + m (ModDest::bodeShift));
    r.bodeMix   = unit (r.bodeMix, m (ModDest::bodeMix));
    r.hyperMix  = unit (r.hyperMix, m (ModDest::hyperMix));
    r.dimensionMix = unit (r.dimensionMix, m (ModDest::dimensionMix));
    r.chorusMix  = unit (r.chorusMix, m (ModDest::chorusMix));
    r.phaserMix  = unit (r.phaserMix, m (ModDest::phaserMix));
    r.flangerMix = unit (r.flangerMix, m (ModDest::flangerMix));
    r.delayMix   = unit (r.delayMix, m (ModDest::delayMix));
    r.reverbMix  = unit (r.reverbMix, m (ModDest::reverbMix));
}

void ForgeRenderer::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, double bpm, std::optional<double> ppq)
{
    engine.process (buffer, midi, reader.read(), bpm, ppq);

    // Effects run in short slices, each with the mod matrix's values at its start.
    const auto baseFx = readFxSettings (parameters);
    const auto baseRack = reader.readRack();
    const auto combTrack = parameters.getRawParameterValue (ParamID::combTrack)->load() > 0.5f;
    const auto numSamples = buffer.getNumSamples();

    for (int start = 0; start < numSamples; start += fxSlice)
    {
        const auto count = juce::jmin (fxSlice, numSamples - start);
        juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, count);

        auto fxSettings = baseFx;
        auto rackSettings = baseRack;
        applyModulation (fxSettings, rackSettings, start);

        fxSettings.vowelModTrace = engine.getFxTrace (ModDest::fxVowel) + start;
        fxSettings.vowelModDepth = 1.0f;
        if (combTrack)
            fxSettings.combTrackHz = (float) engine.getCurrentHz();

        juce::dsp::AudioBlock<float> block (slice);
        rack.processPre (block, rackSettings);
        fx.process (slice, fxSettings);
        rack.processPost (block, rackSettings, bpm);
    }
}
