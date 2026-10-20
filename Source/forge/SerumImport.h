#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../PresetManager.h"

// Reads a Serum 2 preset (.SerumPreset) the user owns and turns it into the nearest Riddim
// Forge patch. Serum 2 stores a JSON header and a zstd-compressed CBOR document of named
// settings; only the documented container is read, never Serum's code.
//
// The result is an approximation: Forge has three wavetable oscillators, two filters, four
// LFOs and 16 mod slots, and its filters and effects are its own. Settings Serum leaves at
// their defaults are not stored in the file, so those take Serum-like guesses. Anything
// that couldn't be carried over is listed in `notes`.
//
// Wavetables are loaded from the user's own Serum install at runtime (or, for tables embedded
// in the preset, written next to their user presets). Nothing is bundled with the plugin.
namespace SerumImport
{
    struct Result
    {
        FactoryPreset preset;
        juce::StringArray notes;
    };

    // Reads the file into a tree of juce::var (maps become DynamicObjects, byte strings
    // MemoryBlocks). Returns a void var and sets `error` when it can't.
    juce::var readDocument (const juce::File&, juce::String& error);

    // `tableFolder` is where wavetables embedded in the preset are written as WAV files.
    // Returns false and sets `error` when the file isn't a readable Serum 2 preset.
    bool convert (const juce::File& serumPreset, const juce::File& tableFolder, Result&, juce::String& error);

    // Plays the converted sound once offline and sets its output level to sit with the
    // factory presets (convert() already does this).
    void levelMatch (Result&);

    // Folders searched for the wavetables a preset names, newest Serum first.
    juce::Array<juce::File> tableSearchFolders (const juce::File& serumPreset);
}
