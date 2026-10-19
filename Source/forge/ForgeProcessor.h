#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../VisualFeed.h"
#include "../PresetManager.h"
#include "../dsp/RiddimFx.h"
#include "ForgeEngine.h"
#include "ForgeParameters.h"

class ForgeProcessor : public juce::AudioProcessor
{
public:
    ForgeProcessor();

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
    double getTailLengthSeconds() const override { return 0.5; }

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

    int getHitStep() const noexcept { return hitStep.load(); }

    juce::AudioProcessorValueTreeState parameters;
    PresetManager presets { parameters, forgeFactoryPresets(), "Forge Presets" };

    juce::MidiKeyboardState keyboardState;
    VisualFeed visualFeed;

private:
    static juce::Identifier tableProperty (int oscillator) { return oscillator == 0 ? "importedTableA" : "importedTableB"; }

    ForgeSettingsReader reader { parameters };
    ForgeEngine engine;
    RiddimFx fx;
    juce::AudioBuffer<float> monoScratch;
    bool wasPlaying = false;
    std::atomic<int> hitStep { -1 };

    // Every imported table stays alive until the plugin closes, so the audio thread
    // never reads one that has been freed.
    std::vector<std::unique_ptr<UserWavetable>> importedTables;
    const UserWavetable* imported[2] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeProcessor)
};
