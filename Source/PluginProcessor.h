#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "VisualFeed.h"
#include "PresetManager.h"
#include "dsp/RiddimEngine.h"
#include "dsp/RiddimFx.h"

class RiddimAudioProcessor : public juce::AudioProcessor
{
public:
    RiddimAudioProcessor();

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

    juce::AudioProcessorValueTreeState parameters;
    PresetManager presets { parameters, synthFactoryPresets(), "Presets" };

    // Shared with the editor: the on-screen keyboard writes here, the visualiser reads.
    juce::MidiKeyboardState keyboardState;
    VisualFeed visualFeed;

private:
    EngineSettings readSettings() const;

    RiddimEngine engine;
    RiddimFx fx;
    juce::AudioBuffer<float> monoScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RiddimAudioProcessor)
};
