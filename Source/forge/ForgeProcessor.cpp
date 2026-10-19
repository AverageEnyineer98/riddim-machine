#include "ForgeProcessor.h"
#include "ForgeEditor.h"
#include "../Parameters.h"

ForgeProcessor::ForgeProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "RiddimForge", createForgeParameterLayout())
{
    presets.loadPreset (0);
}

void ForgeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    fx.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (fx.getLatencySamples());

    keyboardState.reset();
    visualFeed.allNotesOff();
    visualFeed.setSampleRate (sampleRate);
    monoScratch.setSize (1, samplesPerBlock);
}

bool ForgeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void ForgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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
    bool playing = false;

    if (auto* host = getPlayHead())
    {
        if (const auto position = host->getPosition())
        {
            if (const auto hostBpm = position->getBpm())
                bpm = *hostBpm;

            playing = position->getIsPlaying();
            if (playing)
                if (const auto hostPpq = position->getPpqPosition())
                    ppq = *hostPpq;
        }
    }

    // The hit sequencer starts from step 1 whenever the song starts.
    if (playing && ! wasPlaying)
        engine.restartHitSequence();
    wasPlaying = playing;

    engine.process (buffer, midi, reader.read(), bpm, ppq);
    hitStep.store (engine.getHitStep());

    auto fxSettings = readFxSettings (parameters);
    fxSettings.vowelModTrace = engine.getVowelTrace();
    fxSettings.vowelModDepth = 1.0f;
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
bool ForgeProcessor::importWavetable (int oscillator, const juce::File& file, juce::String& error, bool select)
{
    auto table = UserWavetable::load (file, error);
    if (table == nullptr)
        return false;

    imported[oscillator] = table.get();
    engine.setUserTable (oscillator, table.get());
    importedTables.push_back (std::move (table));

    parameters.state.setProperty (tableProperty (oscillator), file.getFullPathName(), nullptr);

    if (auto* p = parameters.getParameter (ForgeID::osc (oscillator == 0 ? 'A' : 'B', ForgeID::table)); p != nullptr && select)
        p->setValueNotifyingHost (p->convertTo0to1 ((float) Forge::userTable));

    return true;
}

//==============================================================================
int ForgeProcessor::getNumPrograms()                      { return juce::jmax (1, presets.getPresetNames().size()); }
int ForgeProcessor::getCurrentProgram()                   { return presets.getCurrentIndex(); }
const juce::String ForgeProcessor::getProgramName (int i) { return presets.getPresetNames()[i]; }
void ForgeProcessor::setCurrentProgram (int index)        { presets.loadPreset (index); }

void ForgeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("preset", presets.getCurrentName(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ForgeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (parameters.state.getType()))
        {
            const auto tree = juce::ValueTree::fromXml (*xml);
            parameters.replaceState (tree);
            presets.setCurrentIndexFromName (tree.getProperty ("preset").toString());

            // Imported wavetables are saved by path; reload any that still exist.
            for (int o = 0; o < 2; ++o)
            {
                const juce::File file (tree.getProperty (tableProperty (o)).toString());
                juce::String error;
                if (file.existsAsFile())
                    importWavetable (o, file, error, false);
            }
        }
    }
}

juce::AudioProcessorEditor* ForgeProcessor::createEditor()
{
    return new ForgeEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ForgeProcessor();
}
