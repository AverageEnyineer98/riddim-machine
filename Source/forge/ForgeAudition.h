#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ForgeParameters.h"

// Plays a Forge sound offline and measures it. The preset tools use this to level and check
// the factory set, and Magic Sound uses it to test every patch before you hear it.
namespace ForgeAudition
{
    constexpr double sampleRate = 48000.0, bpm = 150.0;
    constexpr int numBands = 16;

    // Just enough of a processor to own a Forge parameter tree, off the audio thread.
    struct Host : juce::AudioProcessor
    {
        Host() : parameters (*this, nullptr, "RiddimForge", createForgeParameterLayout()) {}

        const juce::String getName() const override { return "audition"; }
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        double getTailLengthSeconds() const override { return 0; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}

        juce::AudioProcessorValueTreeState parameters;
    };

    struct Features
    {
        float rmsDb = -120.0f, peak = 0.0f, clipPercent = 0.0f;
        bool finite = true;
        float centroid = 0.0f, flatness = 0.0f, lowRatio = 0.0f, movement = 0.0f;
        std::array<float, numBands> bands {};
    };

    // Bass families play a riddim bar on F#1; leads, pads and plucks a short phrase around C4.
    bool isMelodic (const juce::String& category);

    juce::AudioBuffer<float> render (const juce::AudioProcessorValueTreeState&, bool melodic, int bars);
    Features analyse (const juce::AudioBuffer<float>&);
    juce::var toVar (const Features&);
}
