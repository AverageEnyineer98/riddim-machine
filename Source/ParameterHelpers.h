#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Helpers for building parameter layouts, shared by the plugins' layouts.
namespace ParamHelpers
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    using juce::ParameterID;

    constexpr int version = 1;

    inline void addFloat (Layout& layout, const juce::String& id, const juce::String& name,
                          juce::NormalisableRange<float> range, float defaultValue, const juce::String& label = {})
    {
        // Wide ranges (Hz, ms) read as whole numbers; 0..1 style ranges get two decimals.
        const auto decimals = range.end - range.start > 50.0f ? 0 : 2;
        auto toText = [decimals, label] (float value, int)
        {
            return juce::String (value, decimals) + (label.isNotEmpty() ? " " + label : juce::String());
        };

        layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { id, version }, name, range, defaultValue,
                                                                 juce::AudioParameterFloatAttributes().withLabel (label)
                                                                     .withStringFromValueFunction (toText)));
    }

    inline void addChoice (Layout& layout, const juce::String& id, const juce::String& name,
                           const juce::StringArray& choices, int defaultIndex)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { id, version }, name, choices, defaultIndex));
    }

    inline void addInt (Layout& layout, const juce::String& id, const juce::String& name, int min, int max, int defaultValue)
    {
        layout.add (std::make_unique<juce::AudioParameterInt> (ParameterID { id, version }, name, min, max, defaultValue));
    }

    inline void addBool (Layout& layout, const juce::String& id, const juce::String& name, bool defaultValue)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { id, version }, name, defaultValue));
    }

    inline juce::NormalisableRange<float> timeRange (float minMs, float maxMs)
    {
        juce::NormalisableRange<float> r { minMs, maxMs };
        r.setSkewForCentre (minMs + (maxMs - minMs) * 0.08f);
        return r;
    }
}
