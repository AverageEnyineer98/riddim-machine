#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

struct FactoryPreset
{
    juce::String name;
    std::vector<std::pair<juce::String, float>> values;        // plain (not normalised) values; the rest stay default
    std::vector<std::pair<juce::String, juce::String>> text {}; // state-tree properties, e.g. macro names and LFO curves
    juce::String category {};
};

const std::vector<FactoryPreset>& synthFactoryPresets();
const std::vector<FactoryPreset>& fxFactoryPresets();
const std::vector<FactoryPreset>& forgeFactoryPresets();

// Reads presets in the forge_factory.json layout (used by the plugin and the preset tools).
std::vector<FactoryPreset> parseForgePresets (const juce::String& json);

// Factory presets built into the plugin, plus user presets saved as .rmpreset
// files in Documents/Riddim Machine/<folder>. The list is factory first, then user.
class PresetManager
{
public:
    static constexpr const char* fileExtension = ".rmpreset";

    PresetManager (juce::AudioProcessorValueTreeState&, const std::vector<FactoryPreset>& factory, const juce::String& folderName);

    juce::StringArray getPresetNames() const;
    int getNumFactoryPresets() const;
    int getCurrentIndex() const { return currentIndex; }
    juce::String getCurrentName() const;

    void loadPreset (int index);

    // Loads a sound that isn't in the list (e.g. one Magic Sound made). It shows under its own
    // name until another preset loads or it is saved.
    void loadUnlisted (const FactoryPreset&);
    void setCustomName (const juce::String& name) { customName = name; currentIndex = -1; }
    void loadNext()     { loadPreset ((currentIndex + 1) % getPresetNames().size()); }
    void loadPrevious() { const auto n = getPresetNames().size(); loadPreset ((juce::jmax (0, currentIndex) + n - 1) % n); }

    // Saves the current sound as a user preset and selects it.
    void saveUserPreset (const juce::String& name);
    void refreshUserPresets();

    juce::File getUserPresetFolder() const;
    juce::String getCategory (int index) const;

    // Called before any preset loads, so a plugin can clear text properties the preset may not set.
    std::function<void()> onBeforeLoad;

    // Restores the selected preset name from saved plugin state.
    void setCurrentIndexFromName (const juce::String& name);

private:
    void resetToDefaults();

    juce::AudioProcessorValueTreeState& state;
    const std::vector<FactoryPreset>& factory;
    juce::String folderName;
    juce::Array<juce::File> userPresets;
    int currentIndex = 0;
    juce::String customName;
};
