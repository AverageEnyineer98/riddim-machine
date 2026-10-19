#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../VisualFeed.h"
#include "../PresetManager.h"
#include "../dsp/RiddimFx.h"

// "Riddim Machine FX": the synth's riddim processing chain as an effect,
// for any bass, one-shot, sample or mixer channel.
class RiddimFxProcessor : public juce::AudioProcessor
{
public:
    RiddimFxProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override { return juce::jmax (1, presets.getPresetNames().size()); }
    int getCurrentProgram() override { return presets.getCurrentIndex(); }
    void setCurrentProgram (int index) override { presets.loadPreset (index); }
    const juce::String getProgramName (int index) override { return presets.getPresetNames()[index]; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;
    PresetManager presets { parameters, fxFactoryPresets(), "FX Presets" };

    VisualFeed visualFeed;
    juce::MidiKeyboardState keyboardState; // unused; the visualiser expects one

private:
    RiddimFx fx;
    juce::AudioBuffer<float> monoScratch;

    // The movement LFO, tempo-synced to the host.
    double lfoPhase = 0.0;
    std::vector<float> lfoTrace;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RiddimFxProcessor)
};
