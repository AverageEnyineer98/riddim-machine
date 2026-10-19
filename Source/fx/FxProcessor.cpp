#include "FxProcessor.h"
#include "FxEditor.h"
#include "../Parameters.h"
#include "../dsp/RiddimEngine.h"

RiddimFxProcessor::RiddimFxProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "RiddimMachineFX", createFxParameterLayout())
{
    presets.loadPreset (0);
}

void RiddimFxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fx.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (fx.getLatencySamples());
    visualFeed.setSampleRate (sampleRate);
    monoScratch.setSize (1, samplesPerBlock);
    lfoTrace.assign ((size_t) samplesPerBlock, 0.0f);
}

bool RiddimFxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        && layouts.getMainInputChannelSet() == out;
}

void RiddimFxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const auto numSamples = buffer.getNumSamples();

    // Movement LFO: locked to the host bar while playing, free-running otherwise.
    double bpm = 140.0;
    std::optional<double> ppq;
    if (auto* host = getPlayHead())
        if (const auto position = host->getPosition())
        {
            if (const auto hostBpm = position->getBpm())
                bpm = *hostBpm;
            if (position->getIsPlaying())
                if (const auto hostPpq = position->getPpqPosition())
                    ppq = *hostPpq;
        }

    const auto shape = juce::roundToInt (parameters.getRawParameterValue (ParamID::fxLfoShape)->load());
    const auto cyclesPerBeat = Choices::lfoCyclesPerBeat[juce::roundToInt (parameters.getRawParameterValue (ParamID::fxLfoRate)->load())];
    const auto increment = cyclesPerBeat * bpm / 60.0 / getSampleRate();

    if (ppq.has_value())
        lfoPhase = *ppq * cyclesPerBeat;
    lfoPhase -= std::floor (lfoPhase);

    if ((int) lfoTrace.size() < numSamples)
        lfoTrace.resize ((size_t) numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        lfoTrace[(size_t) i] = lfoShapeValue (shape, lfoPhase);
        lfoPhase += increment;
        if (lfoPhase >= 1.0)
            lfoPhase -= 1.0;
    }

    auto fxSettings = readFxSettings (parameters);
    fxSettings.vowelModTrace = lfoTrace.data();
    fxSettings.vowelModDepth = parameters.getRawParameterValue (ParamID::fxLfoVowel)->load();
    fx.process (buffer, fxSettings);
    visualFeed.setLfoPhase ((float) lfoPhase);

    monoScratch.setSize (1, numSamples, false, false, true);
    monoScratch.copyFrom (0, 0, buffer, 0, 0, numSamples);

    if (buffer.getNumChannels() > 1)
    {
        monoScratch.addFrom (0, 0, buffer, 1, 0, numSamples);
        monoScratch.applyGain (0.5f);
    }

    visualFeed.setLevel (monoScratch.getRMSLevel (0, 0, numSamples));
    visualFeed.pushSamples (monoScratch.getReadPointer (0), numSamples);
}

void RiddimFxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("preset", presets.getCurrentName(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void RiddimFxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (parameters.state.getType()))
        {
            const auto tree = juce::ValueTree::fromXml (*xml);
            parameters.replaceState (tree);
            presets.setCurrentIndexFromName (tree.getProperty ("preset").toString());
        }
    }
}

juce::AudioProcessorEditor* RiddimFxProcessor::createEditor()
{
    return new RiddimFxEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RiddimFxProcessor();
}
