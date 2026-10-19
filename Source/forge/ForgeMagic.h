#pragma once

#include "../PresetManager.h"
#include "ForgeAudition.h"

// Magic Sound: a small neural network (a conditional VAE trained on our own presets, see
// Tools/train_magic.py) dreams up a new patch from a random point in its latent space. Each
// candidate is then played offline and measured: it must be finite, audible and close to the
// spectrum of real presets in its family. The best of several is level-matched and returned.
// Recent latent points are remembered, so consecutive results never repeat.
namespace ForgeMagic
{
    // "Any", then the sound families the model knows.
    const juce::StringArray& styles();

    class Generator
    {
    public:
        Generator();
        ~Generator();

        bool isReady() const;

        // Slow (renders a few candidates): call off the message thread. `style` is one of
        // styles(), or empty / "Any" for a random family.
        FactoryPreset make (juce::Random&, const juce::String& style = {});

        struct Stats { float score = 0.0f, rmsDb = 0.0f; int candidates = 0; };
        Stats getLastStats() const { return lastStats; }

    private:
        struct Model;
        std::unique_ptr<Model> model;
        ForgeAudition::Host host;
        std::vector<FactoryPreset> empty;
        PresetManager presets { host.parameters, empty, "Forge Magic" };
        std::vector<std::vector<float>> recent;
        Stats lastStats;
        std::mutex lock;
    };
}
