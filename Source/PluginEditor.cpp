#include "PluginEditor.h"

namespace
{
    constexpr int headerHeight = 48;
    constexpr int keyboardHeight = 64;
    constexpr int controlsHeight = 430;
    constexpr int margin = 10;
    constexpr float numWhiteKeys = 43.0f; // C1 to C7
}

RiddimAudioProcessorEditor::RiddimAudioProcessorEditor (RiddimAudioProcessor& p)
    : AudioProcessorEditor (&p),
      presetBar (p.presets),
      visualiser (p.visualFeed, p.keyboardState),
      controls (p.parameters, p.visualFeed),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&lookAndFeel);

    keyboard.setAvailableRange (24, 96);
    keyboard.setLowestVisibleKey (24);
    keyboard.setKeyPressBaseOctave (3);
    keyboard.setOctaveForMiddleC (4);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, Palette::accent.withAlpha (0.7f));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Palette::accent.withAlpha (0.25f));

    for (auto* c : std::initializer_list<juce::Component*> { &presetBar, &visualiser, &controls, &keyboard })
        addAndMakeVisible (c);

    setResizable (true, true);
    setResizeLimits (1100, 820, 2400, 1600);
    setSize (1240, 900);
}

RiddimAudioProcessorEditor::~RiddimAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void RiddimAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (16, 0);
    g.setColour (Palette::accent);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("RIDDIM MACHINE", header, juce::Justification::centredLeft);

    g.setColour (Palette::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("v0.5", header, juce::Justification::centredRight);
}

void RiddimAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    presetBar.setBounds (area.removeFromTop (headerHeight).withSizeKeepingCentre (420, 28));

    keyboard.setBounds (area.removeFromBottom (keyboardHeight));
    keyboard.setKeyWidth ((float) keyboard.getWidth() / numWhiteKeys);

    area.removeFromBottom (margin);
    controls.setBounds (area.removeFromBottom (controlsHeight).reduced (margin, 0));
    area.removeFromBottom (margin);
    visualiser.setBounds (area);
}
