#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../ui/ControlPanel.h" // ParamMenu, ParamToggle
#include "ForgeLookAndFeel.h"
#include "ForgeProcessor.h"

namespace ForgeUI
{
    using APVTS = juce::AudioProcessorValueTreeState;

    // A HUD dial with its name and a live mono readout of the value.
    class Knob : public juce::Component
    {
    public:
        Knob (APVTS&, const juce::String& paramID, const juce::String& name, juce::Colour);
        void resized() override;

    private:
        void updateReadout();

        juce::Slider slider;
        juce::Label name, value;
        APVTS::SliderAttachment attachment;
    };

    // A glass panel with a lit top edge, corner brackets and a mono title. Its owner adds the
    // children and lays them out inside getContentBounds().
    class Card : public juce::Component
    {
    public:
        Card (const juce::String& title, juce::Colour accent);

        juce::Rectangle<int> getContentBounds() const;
        juce::Colour getAccent() const { return accent; }

        std::function<void (juce::Rectangle<int>)> layout;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        juce::String title;
        juce::Colour accent;
    };

    // Lays components side by side in equal columns.
    void layoutRow (juce::Rectangle<int> area, std::initializer_list<juce::Component*>, int gap = 4);

    // The oscillator's current frame drawn through both of its warps, as it will play.
    class WarpScope : public juce::Component, private juce::Timer
    {
    public:
        WarpScope (ForgeProcessor&, char oscillator, juce::Colour);
        void paint (juce::Graphics&) override;

    private:
        void timerCallback() override;

        ForgeProcessor& processor;
        int oscIndex;
        juce::SharedResourcePointer<WavetableBank> bank;
        std::atomic<float> *table, *position, *warpMode[2], *warp[2];
        juce::Colour colour;
        std::array<float, 8> last {};
    };

    // An LFO's shape, with a dot following the live phase for LFO 1.
    class LfoScope : public juce::Component, private juce::Timer
    {
    public:
        LfoScope (APVTS&, int lfo, VisualFeed*, juce::Colour);
        void paint (juce::Graphics&) override;

    private:
        void timerCallback() override { repaint(); }

        std::atomic<float>& shape;
        VisualFeed* feed;
        juce::Colour colour;
    };

    // An ADSR curve drawn from four parameters.
    class EnvelopeScope : public juce::Component, private juce::Timer
    {
    public:
        EnvelopeScope (APVTS&, const char* attack, const char* decay, const char* sustain, const char* release, juce::Colour);
        void paint (juce::Graphics&) override;

    private:
        void timerCallback() override { repaint(); }

        std::atomic<float> &attack, &decay, &sustain, &release;
        juce::Colour colour;
    };

    // Eight steps, one LFO 1 rate per hit; the step that will play next is lit.
    class HitSequencer : public juce::Component, private juce::Timer
    {
    public:
        HitSequencer (ForgeProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void timerCallback() override;
        juce::Rectangle<int> cellBounds (int step) const;

        ForgeProcessor& processor;
        std::atomic<float>& length;
        std::vector<std::unique_ptr<ParamMenu>> rates;
        int shownStep = -2, shownLength = 0;
    };

    // Eight routing rows: source, destination, bipolar amount.
    class ModMatrix : public juce::Component
    {
    public:
        explicit ModMatrix (APVTS&);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        struct Row
        {
            Row (APVTS&, int index);
            ParamMenu source, dest;
            juce::Slider amount;
            APVTS::SliderAttachment attachment;
        };

        std::vector<std::unique_ptr<Row>> rows;
    };
}
