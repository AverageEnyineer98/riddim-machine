#include "FxEditor.h"

namespace
{
    constexpr int headerHeight = 48;
    constexpr int controlsHeight = 140;
    constexpr int margin = 10;
}

RiddimFxEditor::RiddimFxEditor (RiddimFxProcessor& p)
    : AudioProcessorEditor (&p),
      presetBar (p.presets),
      visualiser (p.visualFeed, p.keyboardState),
      controls (p.parameters, FxPanel::Variant::fxPlugin)
{
    setLookAndFeel (&lookAndFeel);
    visualiser.setShowKeyboard (false);

    for (auto* c : std::initializer_list<juce::Component*> { &presetBar, &visualiser, &controls })
        addAndMakeVisible (c);

    setResizable (true, true);
    setResizeLimits (820, 420, 2400, 1400);
    setSize (1180, 540);
}

RiddimFxEditor::~RiddimFxEditor()
{
    setLookAndFeel (nullptr);
}

void RiddimFxEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (16, 0);
    g.setColour (Palette::accent);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("RIDDIM MACHINE FX", header, juce::Justification::centredLeft);
}

void RiddimFxEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (headerHeight);
    presetBar.setBounds (header.removeFromRight (420).reduced (16, 10));

    area.removeFromBottom (margin);
    controls.setBounds (area.removeFromBottom (controlsHeight).reduced (margin, 0));
    area.removeFromBottom (margin);
    visualiser.setBounds (area);
}
