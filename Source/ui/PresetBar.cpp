#include "PresetBar.h"

PresetBar::PresetBar (PresetManager& p) : presets (p)
{
    menu.setJustificationType (juce::Justification::centred);
    menu.onChange = [this]
    {
        const auto index = menu.getSelectedId() - 1;
        if (index >= 0 && index != presets.getCurrentIndex())
            presets.loadPreset (index);
    };

    previous.onClick = [this] { presets.loadPrevious(); refresh(); };
    next.onClick     = [this] { presets.loadNext(); refresh(); };
    save.onClick     = [this] { showSaveDialog(); };
    save.setTooltip ("Save the current sound to " + presets.getUserPresetFolder().getFullPathName());

    for (auto* c : std::initializer_list<juce::Component*> { &previous, &menu, &next, &save })
        addAndMakeVisible (c);

    refresh();
    startTimerHz (5);
}

void PresetBar::resized()
{
    auto area = getLocalBounds();
    previous.setBounds (area.removeFromLeft (28));
    save.setBounds (area.removeFromRight (56));
    area.removeFromRight (6);
    next.setBounds (area.removeFromRight (28));
    menu.setBounds (area.reduced (6, 0));
}

void PresetBar::refresh()
{
    menu.clear (juce::dontSendNotification);

    const auto names = presets.getPresetNames();
    const auto numFactory = presets.getNumFactoryPresets();

    for (int i = 0; i < names.size(); ++i)
    {
        if (i == 0)
            menu.addSectionHeading ("Factory");
        else if (i == numFactory)
            menu.addSectionHeading ("User");

        menu.addItem (names[i], i + 1);
    }

    menu.setSelectedId (presets.getCurrentIndex() + 1, juce::dontSendNotification);
}

void PresetBar::timerCallback()
{
    // Keeps the menu in step with host program changes.
    if (menu.getSelectedId() != presets.getCurrentIndex() + 1)
        menu.setSelectedId (presets.getCurrentIndex() + 1, juce::dontSendNotification);
}

void PresetBar::showSaveDialog()
{
    auto* window = new juce::AlertWindow ("Save preset", "Name your sound:", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", presets.getCurrentName());
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
    {
        if (result == 1)
        {
            presets.saveUserPreset (window->getTextEditorContents ("name"));
            refresh();
        }
    }), true);
}
