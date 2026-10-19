#include "PresetManager.h"

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s, const std::vector<FactoryPreset>& f,
                              const juce::String& folder)
    : state (s), factory (f), folderName (folder)
{
    refreshUserPresets();
}

juce::File PresetManager::getUserPresetFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Riddim Machine").getChildFile (folderName);
}

void PresetManager::refreshUserPresets()
{
    userPresets = getUserPresetFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension);
    userPresets.sort();
}

int PresetManager::getNumFactoryPresets() const
{
    return (int) factory.size();
}

juce::StringArray PresetManager::getPresetNames() const
{
    juce::StringArray names;
    for (const auto& p : factory)
        names.add (p.name);
    for (const auto& f : userPresets)
        names.add (f.getFileNameWithoutExtension());
    return names;
}

juce::String PresetManager::getCategory (int index) const
{
    if (index >= 0 && index < getNumFactoryPresets())
        return factory[(size_t) index].category;
    return index >= 0 ? "User" : juce::String();
}

juce::String PresetManager::getCurrentName() const
{
    return customName.isNotEmpty() ? customName : getPresetNames()[currentIndex];
}

void PresetManager::loadUnlisted (const FactoryPreset& preset)
{
    if (onBeforeLoad)
        onBeforeLoad();
    resetToDefaults();

    for (const auto& [id, value] : preset.values)
        if (auto* p = state.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));

    for (const auto& [key, text] : preset.text)
        state.state.setProperty (juce::Identifier (key), text, nullptr);

    setCustomName (preset.name);
}

void PresetManager::resetToDefaults()
{
    for (auto* param : state.processor.getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (param))
            p->setValueNotifyingHost (p->getDefaultValue());
}

void PresetManager::loadPreset (int index)
{
    const auto numFactory = getNumFactoryPresets();

    if (index < numFactory)
    {
        if (onBeforeLoad)
            onBeforeLoad();
        resetToDefaults();

        const auto& preset = factory[(size_t) index];
        for (const auto& [id, value] : preset.values)
            if (auto* p = state.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (value));

        for (const auto& [key, text] : preset.text)
            state.state.setProperty (juce::Identifier (key), text, nullptr);
    }
    else if (index - numFactory < userPresets.size())
    {
        if (auto xml = juce::XmlDocument::parse (userPresets[index - numFactory]))
        {
            const auto tree = juce::ValueTree::fromXml (*xml);

            if (tree.hasType (state.state.getType()))
            {
                if (onBeforeLoad)
                    onBeforeLoad();
                resetToDefaults();

                for (const auto& child : tree)
                    if (auto* p = state.getParameter (child.getProperty ("id").toString()))
                        p->setValueNotifyingHost (p->convertTo0to1 ((float) child.getProperty ("value")));

                for (int i = 0; i < tree.getNumProperties(); ++i)
                {
                    const auto key = tree.getPropertyName (i);
                    if (key.toString() != "preset")
                        state.state.setProperty (key, tree.getProperty (key), nullptr);
                }
            }
        }
    }
    else
    {
        return;
    }

    currentIndex = index;
    customName = {};
}

void PresetManager::saveUserPreset (const juce::String& name)
{
    const auto safeName = juce::File::createLegalFileName (name.trim());
    if (safeName.isEmpty())
        return;

    auto folder = getUserPresetFolder();
    folder.createDirectory();

    if (auto xml = state.copyState().createXml())
        xml->writeTo (folder.getChildFile (safeName + fileExtension));

    refreshUserPresets();
    setCurrentIndexFromName (safeName);
}

void PresetManager::setCurrentIndexFromName (const juce::String& name)
{
    const auto index = getPresetNames().indexOf (name);
    if (index >= 0)
    {
        currentIndex = index;
        customName = {};
    }
    else if (name.isNotEmpty())
    {
        setCustomName (name);
    }
}
