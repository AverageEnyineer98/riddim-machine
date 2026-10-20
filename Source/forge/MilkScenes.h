#pragma once

#include <juce_core/juce_core.h>

struct VisualDNA;

// Riddim Forge's own MilkDrop scenes for projectM. Each scene is a .milk preset written here,
// with the sound's VisualDNA baked into its q variables (colours, fold count, twist, grit, ...),
// so every preset and every settings change gets its own variation, and projectM's audio
// analysis (bass / mid / treble, beats) plus the burnt-in 3D form drive it from there.
namespace MilkScenes
{
    enum Scene { tunnel, grid, storm, burst, numScenes };

    juce::String getName (int scene);

    // The scene that suits a sound: metallic or rippled patches burst, fast-twisting ones storm,
    // gritty or wide ones tunnel, the smooth rest ride the grid.
    int pickFor (const VisualDNA&);

    juce::String build (int scene, const VisualDNA&);
}
