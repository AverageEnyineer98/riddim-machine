#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../VisualFeed.h"
#include "../PresetManager.h"
#include "ForgeEngine.h"
#include "ForgeParameters.h"
#include "ForgeRenderer.h"

class ForgeProcessor : public juce::AudioProcessor, private juce::ValueTree::Listener
{
public:
    ForgeProcessor();
    ~ForgeProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Message thread: loads a wavetable file into an oscillator, and selects it unless
    // it is being restored from saved state.
    bool importWavetable (int oscillator, const juce::File&, juce::String& error, bool select = true);
    const UserWavetable* getImportedTable (int oscillator) const { return imported[oscillator]; }

    // Message thread: the drawn LFO curves and the macro names (both saved with presets).
    Forge::LfoCurve getLfoCurve (int lfo) const { return curves[(size_t) lfo]; }
    void setLfoCurve (int lfo, const Forge::LfoCurve&);
    juce::String getMacroName (int macro) const;
    void setMacroName (int macro, const juce::String&);

    // Message thread: loads a brand-new Magic Sound patch and returns its name.
    juce::String makeMagicSound (const juce::String& style = {});

    int getHitStep() const noexcept { return hitStep.load(); }
    float getLfoValue (int lfo) const noexcept { return renderer.getEngine().getLfoValue (lfo); }
    float getLfoPhase (int lfo) const noexcept { return renderer.getEngine().getLfoPhase (lfo); }
    int getNumActiveVoices() const noexcept { return renderer.getEngine().getNumActiveVoices(); }

    juce::AudioProcessorValueTreeState parameters;
    PresetManager presets { parameters, forgeFactoryPresets(), "Forge Presets" };

    juce::MidiKeyboardState keyboardState;
    VisualFeed visualFeed;

    // Message thread: called after a preset or state changes the drawn curves or names.
    std::function<void()> onTextStateChanged;

private:
    static juce::Identifier tableProperty (int oscillator) { return juce::Identifier (juce::String ("importedTable") + ForgeID::oscLetter (oscillator)); }

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void rebuildLfoTable (int lfo);

    ForgeRenderer renderer { parameters };
    juce::AudioBuffer<float> monoScratch;
    bool wasPlaying = false;
    std::atomic<int> hitStep { -1 };

    // Every imported table stays alive until the plugin closes, so the audio thread
    // never reads one that has been freed.
    std::vector<std::unique_ptr<UserWavetable>> importedTables;
    const UserWavetable* imported[Forge::numOscs] {};

    std::array<Forge::LfoCurve, Forge::numLfos> curves;
    juce::Random magicRandom;
    std::shared_ptr<bool> aliveToken = std::make_shared<bool> (true); // lets async callbacks see we're gone

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeProcessor)
};
