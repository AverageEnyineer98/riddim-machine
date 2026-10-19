#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

RiddimAudioProcessor::RiddimAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "RiddimMachine", createParameterLayout())
{
    presets.loadPreset (0);
}

void RiddimAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    fx.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (fx.getLatencySamples());

    keyboardState.reset();
    visualFeed.allNotesOff();
    visualFeed.setSampleRate (sampleRate);
    monoScratch.setSize (1, samplesPerBlock);
}

bool RiddimAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

EngineSettings RiddimAudioProcessor::readSettings() const
{
    auto get = [this] (const juce::String& id) { return parameters.getRawParameterValue (id)->load(); };
    auto getInt = [&] (const juce::String& id) { return juce::roundToInt (get (id)); };

    EngineSettings s;

    for (int o = 0; o < 2; ++o)
    {
        const auto which = o == 0 ? 'A' : 'B';
        auto& osc = s.osc[o];
        osc.table    = getInt (ParamID::osc (which, ParamID::table));
        osc.position = get (ParamID::osc (which, ParamID::pos));
        osc.level    = get (ParamID::osc (which, ParamID::level));
        osc.octave   = getInt (ParamID::osc (which, ParamID::octave));
        osc.semitone = getInt (ParamID::osc (which, ParamID::semi));
        osc.unison   = juce::jlimit (1, RiddimEngine::maxUnison, getInt (ParamID::osc (which, ParamID::unison)));
        osc.detune   = get (ParamID::osc (which, ParamID::detune));
        osc.warpMode = getInt (ParamID::osc (which, ParamID::warpMode));
        osc.warp     = get (ParamID::osc (which, ParamID::warp));
    }

    s.fmAmount      = get (ParamID::fmAmount);
    s.subLevel      = get (ParamID::subLevel);
    s.noiseLevel    = get (ParamID::noiseLevel);
    s.filterType    = getInt (ParamID::filterType);
    s.cutoff        = get (ParamID::cutoff);
    s.resonance     = get (ParamID::resonance);
    s.filterEnv     = get (ParamID::filterEnv);
    s.filterDecayMs = get (ParamID::filterDecay);
    s.attackMs      = get (ParamID::attack);
    s.decayMs       = get (ParamID::decay);
    s.sustain       = get (ParamID::sustain);
    s.releaseMs     = get (ParamID::release);
    s.lfoShape      = getInt (ParamID::lfoShape);
    s.lfoRate       = getInt (ParamID::lfoRate);
    s.lfoRetrigger  = get (ParamID::lfoRetrig) > 0.5f;
    s.lfoToCutoff   = get (ParamID::lfoCutoff);
    s.lfoToPosA     = get (ParamID::lfoPosA);
    s.lfoToPosB     = get (ParamID::lfoPosB);
    s.lfoToFm       = get (ParamID::lfoFm);
    s.lfoToVolume   = get (ParamID::lfoVolume);
    s.lfoToWarpA    = get (ParamID::lfoWarpA);
    s.pitchEnvSemis = get (ParamID::pitchEnv);
    s.pitchDecayMs  = get (ParamID::pitchDecay);
    s.mono          = get (ParamID::mono) > 0.5f;
    s.glideMs       = get (ParamID::glide);
    s.bendRange     = getInt (ParamID::bendRange);
    return s;
}

void RiddimAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Merge on-screen keyboard presses into the host MIDI.
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();

        if (msg.isNoteOn())
            visualFeed.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())
            visualFeed.noteOff (msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            visualFeed.allNotesOff();
    }

    double bpm = 140.0;
    std::optional<double> ppq;

    if (auto* host = getPlayHead())
    {
        if (const auto position = host->getPosition())
        {
            if (const auto hostBpm = position->getBpm())
                bpm = *hostBpm;

            if (position->getIsPlaying())
                if (const auto hostPpq = position->getPpqPosition())
                    ppq = *hostPpq;
        }
    }

    engine.process (buffer, midi, readSettings(), bpm, ppq);
    auto fxSettings = readFxSettings (parameters);
    fxSettings.vowelModTrace = engine.getLfoTrace();
    fxSettings.vowelModDepth = parameters.getRawParameterValue (ParamID::lfoVowel)->load();
    if (parameters.getRawParameterValue (ParamID::combTrack)->load() > 0.5f)
        fxSettings.combTrackHz = (float) engine.getCurrentHz();
    fx.process (buffer, fxSettings);
    midi.clear();

    const auto numSamples = buffer.getNumSamples();
    monoScratch.setSize (1, numSamples, false, false, true);
    monoScratch.copyFrom (0, 0, buffer, 0, 0, numSamples);

    if (buffer.getNumChannels() > 1)
    {
        monoScratch.addFrom (0, 0, buffer, 1, 0, numSamples);
        monoScratch.applyGain (0.5f);
    }

    visualFeed.setLevel (monoScratch.getRMSLevel (0, 0, numSamples));
    visualFeed.pushSamples (monoScratch.getReadPointer (0), numSamples);
    visualFeed.setLfoPhase (engine.getLfoPhase());
}

//==============================================================================
int RiddimAudioProcessor::getNumPrograms()                   { return juce::jmax (1, presets.getPresetNames().size()); }
int RiddimAudioProcessor::getCurrentProgram()                { return presets.getCurrentIndex(); }
const juce::String RiddimAudioProcessor::getProgramName (int i) { return presets.getPresetNames()[i]; }

void RiddimAudioProcessor::setCurrentProgram (int index)
{
    presets.loadPreset (index);
}

void RiddimAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("preset", presets.getCurrentName(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void RiddimAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
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

juce::AudioProcessorEditor* RiddimAudioProcessor::createEditor()
{
    return new RiddimAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RiddimAudioProcessor();
}
