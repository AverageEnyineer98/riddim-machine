#pragma once

#include "PluginProcessor.h"
#include "Visualiser3D.h"
#include "ui/ControlPanel.h"
#include "ui/PresetBar.h"
#include "ui/RiddimLookAndFeel.h"

class RiddimAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit RiddimAudioProcessorEditor (RiddimAudioProcessor&);
    ~RiddimAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    RiddimLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this };

    PresetBar presetBar;
    Visualiser3D visualiser;
    ControlPanel controls;
    juce::MidiKeyboardComponent keyboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RiddimAudioProcessorEditor)
};
