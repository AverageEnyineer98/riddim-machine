#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/Wavetables.h"
#include "../VisualFeed.h"
#include "RiddimLookAndFeel.h"

using APVTS = juce::AudioProcessorValueTreeState;

// A rotary knob with its name underneath, bound to one parameter.
class Knob : public juce::Component
{
public:
    Knob (APVTS&, const juce::String& paramID, const juce::String& name, juce::Colour);
    void resized() override;

private:
    juce::Slider slider;
    juce::Label label;
    APVTS::SliderAttachment attachment;
};

// A titled panel: an optional strip of widgets (menus, toggles, displays) over a row of knobs.
class Section : public juce::Component
{
public:
    Section (const juce::String& title, juce::Colour);

    void addKnob (APVTS&, const juce::String& paramID, const juce::String& name);
    void addToStrip (juce::Component&, float weight);
    int getNumKnobs() const { return (int) knobs.size(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String title;
    juce::Colour colour;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::pair<juce::Component*, float>> strip;
};

// Draws the selected wavetable frame at the oscillator's position.
class WavetablePreview : public juce::Component, private juce::Timer
{
public:
    WavetablePreview (APVTS&, char oscillator, juce::Colour);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    juce::SharedResourcePointer<WavetableBank> bank;
    std::atomic<float>& table;
    std::atomic<float>& position;
    juce::Colour colour;
    int lastTable = -1, lastFrame = -1;
};

// Draws the LFO shape with a dot following the live phase.
class LfoDisplay : public juce::Component, private juce::Timer
{
public:
    LfoDisplay (APVTS&, VisualFeed&);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { repaint(); }

    std::atomic<float>& shape;
    VisualFeed& feed;
};

struct ParamMenu
{
    ParamMenu (APVTS&, const juce::String& paramID, const juce::StringArray& items);
    juce::ComboBox box;
    APVTS::ComboBoxAttachment attachment;
};

struct ParamToggle
{
    ParamToggle (APVTS&, const juce::String& paramID, const juce::String& text);
    juce::ToggleButton button;
    APVTS::ButtonAttachment attachment;
};

// Lays sections out side by side, each as wide as its share of the knobs.
void layoutSectionRow (juce::Rectangle<int> row, std::initializer_list<Section*> sections);

// The riddim processing chain controls, used by both the synth and the FX plugin.
class FxPanel : public juce::Component
{
public:
    // The synth adds its comb key-tracking toggle; the FX plugin adds its movement LFO instead.
    enum class Variant { synth, fxPlugin };
    FxPanel (APVTS&, Variant);
    void resized() override;

private:
    Section distortion, formant, comb, dynamics, tone, macro;
    ParamMenu distType;
    std::unique_ptr<ParamToggle> combTrack;
    std::unique_ptr<ParamMenu> movementShape, movementRate;
    ParamToggle resample, gate;
};

// Every synth control: oscillators, then filter / envelope / LFO / voice, then the FX chain.
class ControlPanel : public juce::Component
{
public:
    ControlPanel (APVTS&, VisualFeed&);
    void resized() override;

private:
    Section oscA, oscB, source, filter, amp, lfo, voice;
    ParamMenu tableA, tableB, warpA, warpB, filterType, lfoShape, lfoRate;
    ParamToggle lfoRetrig, mono;
    WavetablePreview previewA, previewB;
    LfoDisplay lfoDisplay;
    FxPanel fxPanel;
};
