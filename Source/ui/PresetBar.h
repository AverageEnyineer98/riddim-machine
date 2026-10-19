#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PresetManager.h"

// Previous / menu / next / save controls for a PresetManager.
class PresetBar : public juce::Component, private juce::Timer
{
public:
    explicit PresetBar (PresetManager&);
    void resized() override;

private:
    void timerCallback() override;
    void refresh();
    void showSaveDialog();

    PresetManager& presets;
    juce::TextButton previous { "<" }, next { ">" }, save { "SAVE" };
    juce::ComboBox menu;
};
