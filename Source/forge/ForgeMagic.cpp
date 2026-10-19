#include "ForgeMagic.h"
#include "ForgeParameters.h"

namespace
{
    const char* adjectives[] { "Rusty", "Toxic", "Heavy", "Wonky", "Broken", "Filthy", "Hollow", "Grim", "Molten", "Feral",
                               "Crooked", "Static", "Gnarly", "Vile", "Sunken", "Rabid", "Murky", "Twisted", "Sour", "Brutal" };
    const char* nouns[] { "Wobble", "Gremlin", "Furnace", "Drone", "Engine", "Swamp", "Saw", "Throat", "Riddim", "Robot",
                          "Grinder", "Crusher", "Goblin", "Reactor", "Sludge", "Hammer", "Choir", "Siren", "Chopper", "Beast" };
}

const juce::StringArray& ForgeMagic::styles()
{
    static const juce::StringArray names { "Any", "Wobble", "Growl", "Tearout", "Yoi", "Screech", "Sub", "Riddim Chop" };
    return names;
}

FactoryPreset ForgeMagic::generate (juce::Random& random, const juce::String&)
{
    // Starting point until the trained model lands: mutate a random factory sound.
    const auto& factory = forgeFactoryPresets();
    FactoryPreset preset = factory.size() > 1 ? factory[(size_t) (1 + random.nextInt ((int) factory.size() - 1))] : factory.front();

    for (auto& [id, value] : preset.values)
        if (! id.contains ("_type") && ! id.contains ("table") && ! id.contains ("mode") && ! id.contains ("src") && ! id.contains ("dst"))
            value *= 0.85f + 0.3f * random.nextFloat();

    preset.name = juce::String ("Magic ") + adjectives[random.nextInt (20)] + " " + nouns[random.nextInt (20)];
    preset.category = "Magic";
    return preset;
}
