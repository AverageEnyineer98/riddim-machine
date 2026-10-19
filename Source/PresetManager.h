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
    void loadNext()     { loadPreset ((currentIndex + 1) % getPresetNames().size()); }
    void loadPrevious() { const auto n = getPresetNames().size(); loadPreset ((currentIndex + n - 1) % n); }

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
};
