#pragma once

#include "../PresetManager.h"

// Magic Sound: makes a new riddim patch every time it's asked. The result is a preset like any
// factory one (plain parameter values plus macro names and drawn LFOs), so it loads, saves
// and undoes the same way.
namespace ForgeMagic
{
    // `style` picks a family ("Wobble", "Growl", "Tearout", ...); empty means any.
    FactoryPreset generate (juce::Random&, const juce::String& style = {});

    const juce::StringArray& styles();
}
