#pragma once

#include "ForgeProcessor.h"
#include "ForgeLookAndFeel.h"
#include "ForgeUI.h"
#include "../Visualiser3D.h"
#include "../ui/PresetBar.h"

// Header, status strip, 3D view, then one of three pages (SYNTH, MOD, FX), then the keyboard.
class ForgeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ForgeEditor (ForgeProcessor&);
    ~ForgeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showPage (int index);

    ForgeProcessor& processor;
    ForgeLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    PresetBar presetBar;
    juce::TextButton tabs[3];
    Visualiser3D visualiser;
    std::unique_ptr<juce::Component> pages[3];
    juce::MidiKeyboardComponent keyboard;

    juce::Rectangle<int> statusArea;
    float pulse = 0.0f;
    int shownHit = -2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeEditor)
};
