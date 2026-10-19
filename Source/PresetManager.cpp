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

juce::String PresetManager::getCurrentName() const
{
    return getPresetNames()[currentIndex];
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
        resetToDefaults();

        for (const auto& [id, value] : factory[(size_t) index].values)
            if (auto* p = state.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (value));
    }
    else if (index - numFactory < userPresets.size())
    {
        if (auto xml = juce::XmlDocument::parse (userPresets[index - numFactory]))
        {
            const auto tree = juce::ValueTree::fromXml (*xml);

            if (tree.hasType (state.state.getType()))
            {
                resetToDefaults();

                for (const auto& child : tree)
                    if (auto* p = state.getParameter (child.getProperty ("id").toString()))
                        p->setValueNotifyingHost (p->convertTo0to1 ((float) child.getProperty ("value")));
            }
        }
    }
    else
    {
        return;
    }

    currentIndex = index;
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
        currentIndex = index;
}
