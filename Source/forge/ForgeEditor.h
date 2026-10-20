#pragma once

#include "ForgeProcessor.h"
#include "ForgeLookAndFeel.h"
#include "ForgeUI.h"
#include "ForgeVisual.h"

// Laid out like Serum: a header with the preset browser and Magic Sound, the OSC / FX /
// MATRIX / GLOBAL pages, the preset's 3D form on the right, then the envelopes, LFOs and
// macros along the bottom with their drag handles, and a keyboard. The whole window scales.
class ForgeEditor : public juce::AudioProcessorEditor,
                    public juce::DragAndDropContainer,
                    private juce::Timer
{
public:
    explicit ForgeEditor (ForgeProcessor&);
    ~ForgeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int baseWidth = 1400, baseHeight = 880;

private:
    void timerCallback() override;
    void showPage (int index);
    void setZoom (float zoom);

    ForgeProcessor& processor;
    ForgeLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    juce::Component content; // laid out at base size, scaled to the window
    std::unique_ptr<juce::Component> header, pages[4], visualCard, envelopes, lfos, macros, playControls;
    juce::TextButton tabs[4];
    juce::MidiKeyboardComponent keyboard;
    ForgeVisual* visual = nullptr;

    int currentPage = 0;
    float zoom = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeEditor)
};
