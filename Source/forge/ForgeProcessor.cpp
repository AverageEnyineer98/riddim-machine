#include "ForgeProcessor.h"
#include "ForgeEditor.h"
#include "../Parameters.h"

using Forge::ModDest;

ForgeProcessor::ForgeProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "RiddimForge", createForgeParameterLayout())
{
    for (int l = 0; l < Forge::numLfos; ++l)
        rebuildLfoTable (l);

    presets.onBeforeLoad = [this]
    {
        for (int m = 0; m < Forge::numMacros; ++m)
            parameters.state.removeProperty (ForgeID::macroNameProperty (m), nullptr);
        for (int l = 0; l < Forge::numLfos; ++l)
            parameters.state.removeProperty (ForgeID::lfoCurveProperty (l), nullptr);
    };

    parameters.state.addListener (this);
    presets.loadPreset (0);
}

ForgeProcessor::~ForgeProcessor()
{
    parameters.state.removeListener (this);
}

void ForgeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    renderer.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (renderer.getLatencySamples());

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
        renderer.getEngine().restartHitSequence();
    wasPlaying = playing;

    renderer.process (buffer, midi, bpm, ppq);
    hitStep.store (renderer.getEngine().getHitStep());
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
    visualFeed.setLfoPhase (renderer.getEngine().getLfoPhase (0));
}

//==============================================================================
bool ForgeProcessor::importWavetable (int oscillator, const juce::File& file, juce::String& error, bool select)
{
    auto table = UserWavetable::load (file, error);
    if (table == nullptr)
        return false;

    imported[oscillator] = table.get();
    renderer.setUserTable (oscillator, table.get());
    importedTables.push_back (std::move (table));

    parameters.state.setProperty (tableProperty (oscillator), file.getFullPathName(), nullptr);

    if (auto* p = parameters.getParameter (ForgeID::osc (ForgeID::oscLetter (oscillator), ForgeID::table)); p != nullptr && select)
        p->setValueNotifyingHost (p->convertTo0to1 ((float) Forge::importedTable()));

    return true;
}

void ForgeProcessor::makeMagicSound (const juce::String& style, std::function<void (const juce::String&)> done)
{
    if (magicBusy.exchange (true))
        return;

    magicWorker.addJob ([this, style, done = std::move (done), alive = std::weak_ptr<bool> (aliveToken)]
    {
        if (magic == nullptr)
            magic = std::make_unique<ForgeMagic::Generator>();

        auto preset = magic->isReady() ? magic->make (magicRandom, style) : FactoryPreset {};

        juce::MessageManager::callAsync ([this, alive, preset = std::move (preset), done]
        {
            if (alive.expired())
                return;
            if (! preset.values.empty())
                presets.loadUnlisted (preset);
            magicBusy = false;
            if (done)
                done (preset.name);
        });
    });
}

void ForgeProcessor::setLfoCurve (int lfo, const Forge::LfoCurve& curve)
{
    parameters.state.setProperty (ForgeID::lfoCurveProperty (lfo), curve.toString(), nullptr);
}

juce::String ForgeProcessor::getMacroName (int macro) const
{
    const auto name = parameters.state.getProperty (ForgeID::macroNameProperty (macro)).toString();
    return name.isNotEmpty() ? name : "Macro " + juce::String (macro + 1);
}

void ForgeProcessor::setMacroName (int macro, const juce::String& name)
{
    parameters.state.setProperty (ForgeID::macroNameProperty (macro), name, nullptr);
}

void ForgeProcessor::rebuildLfoTable (int lfo)
{
    const auto text = parameters.state.getProperty (ForgeID::lfoCurveProperty (lfo)).toString();
    curves[(size_t) lfo] = text.isNotEmpty() ? Forge::LfoCurve::fromString (text) : Forge::LfoCurve();

    renderer.setLfoCurve (lfo, curves[(size_t) lfo]);
}

void ForgeProcessor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree != parameters.state)
        return;

    for (int l = 0; l < Forge::numLfos; ++l)
        if (property == ForgeID::lfoCurveProperty (l))
            rebuildLfoTable (l);

    for (int o = 0; o < Forge::numOscs; ++o)
        if (property == tableProperty (o))
        {
            const juce::File file (tree.getProperty (property).toString());
            if (file.existsAsFile() && (imported[o] == nullptr || imported[o]->getPath() != file.getFullPathName()))
            {
                juce::String error;
                importWavetable (o, file, error, false);
            }
        }

    if (onTextStateChanged != nullptr)
    {
        if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
            && juce::MessageManager::getInstance()->isThisTheMessageThread())
            onTextStateChanged();
        else
            juce::MessageManager::callAsync ([this, alive = std::weak_ptr<bool> (aliveToken)]
            {
                if (! alive.expired() && onTextStateChanged)
                    onTextStateChanged();
            });
    }
}

//==============================================================================
int ForgeProcessor::getNumPrograms()                      { return juce::jmax (1, presets.getPresetNames().size()); }
int ForgeProcessor::getCurrentProgram()                   { return juce::jmax (0, presets.getCurrentIndex()); }
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

            // replaceState swaps the tree without property callbacks, so refresh what hangs off it.
            for (int l = 0; l < Forge::numLfos; ++l)
                rebuildLfoTable (l);

            // Imported wavetables are saved by path; reload any that still exist.
            for (int o = 0; o < Forge::numOscs; ++o)
            {
                const juce::File file (tree.getProperty (tableProperty (o)).toString());
                juce::String error;
                if (file.existsAsFile())
                    importWavetable (o, file, error, false);
            }

            if (onTextStateChanged)
                onTextStateChanged();
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
