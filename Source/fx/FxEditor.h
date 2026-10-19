#pragma once

#include "FxProcessor.h"
#include "../Visualiser3D.h"
#include "../ui/ControlPanel.h"
#include "../ui/PresetBar.h"
#include "../ui/RiddimLookAndFeel.h"

class RiddimFxEditor : public juce::AudioProcessorEditor
{
public:
    explicit RiddimFxEditor (RiddimFxProcessor&);
    ~RiddimFxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    RiddimLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this };

    PresetBar presetBar;
    Visualiser3D visualiser;
    FxPanel controls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RiddimFxEditor)
};
