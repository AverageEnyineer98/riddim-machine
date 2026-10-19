#include "../PresetManager.h"
#include "ForgeParameters.h"
#include "ForgeBinaryData.h"

// Riddim Forge's factory presets live in Presets/forge_factory.json (built by
// Tools/make_forge_presets.py), compiled into the plugin as binary data:
//   [ { "name": ..., "category": ..., "values": { id: value }, "text": { property: text } }, ... ]
const std::vector<FactoryPreset>& forgeFactoryPresets()
{
    static const auto presets = []
    {
        std::vector<FactoryPreset> list;
        const auto json = juce::JSON::parse (juce::String::fromUTF8 (ForgeBinaryData::forge_factory_json,
                                                                     ForgeBinaryData::forge_factory_jsonSize));

        if (const auto* array = json.getArray())
        {
            for (const auto& item : *array)
            {
                FactoryPreset preset;
                preset.name = item["name"].toString();
                preset.category = item["category"].toString();

                if (const auto* values = item["values"].getDynamicObject())
                    for (const auto& property : values->getProperties())
                        preset.values.emplace_back (property.name.toString(), (float) (double) property.value);

                if (const auto* text = item["text"].getDynamicObject())
                    for (const auto& property : text->getProperties())
                        preset.text.emplace_back (property.name.toString(), property.value.toString());

                if (preset.name.isNotEmpty())
                    list.push_back (std::move (preset));
            }
        }

        if (list.empty())
            list.push_back ({ "Init", {} });
        return list;
    }();
    return presets;
}
