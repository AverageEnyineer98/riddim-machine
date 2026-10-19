#include "PresetManager.h"
#include "Parameters.h"

namespace
{
    using namespace ParamID;
    juce::String A (const char* n) { return osc ('A', n); }
    juce::String B (const char* n) { return osc ('B', n); }
}

// Wavetable indices: 0 Sync Saw, 1 Growl FM, 2 Yoi Formant, 3 Tearout Fold,
// 4 Metal Screech, 5 Pulse Width, 6 Reese Stack, 7 Basic Shapes.
// LFO rates: 0 1/1, 1 1/2, 2 1/4, 3 1/4T, 4 1/8, 5 1/8T, 6 1/16, 7 1/16T, 8 1/32.
// LFO shapes: 0 Sine, 1 Tri, 2 Saw Down, 3 Saw Up, 4 Square, 5 Riddim Step, 6 Half Wob, 7 Stutter.
// Distortion: 0 Soft, 1 Hard, 2 Fold, 3 Crush.
const std::vector<FactoryPreset>& synthFactoryPresets()
{
    static const std::vector<FactoryPreset> presets {
        { "Old School Riddim", {
            { A (table), 7 }, { A (pos), 0.66f }, { A (warpMode), 1 }, { A (warp), 0.35f }, { subLevel, 0.6f },
            { cutoff, 220 }, { resonance, 0.4f }, { release, 25 }, { pitchEnv, 12 }, { pitchDecay, 25 },
            { lfoShape, 0 }, { lfoRate, 5 }, { lfoCutoff, 0.7f }, { lfoWarpA, 0.45f },
            { distType, 0 }, { distDrive, 0.55f }, { squash, 0.7f }, { clip, 0.5f }, { resample, 1 } } },

        { "Metallic Tearout", {
            { A (table), 4 }, { A (pos), 0.5f }, { A (warpMode), 2 }, { A (warp), 0.4f },
            { B (table), 1 }, { B (level), 0.0f }, { fmAmount, 0.35f }, { subLevel, 0.5f },
            { cutoff, 3000 }, { resonance, 0.2f }, { release, 25 }, { pitchEnv, -12 }, { pitchDecay, 15 },
            { lfoShape, 3 }, { lfoRate, 7 }, { lfoCutoff, 0.0f }, { lfoPosA, 0.6f }, { lfoFm, 0.3f }, { lfoWarpA, 0.3f },
            { distType, 1 }, { distDrive, 0.55f }, { vowelMix, 0.3f }, { vowel, 0.6f },
            { combMix, 0.45f }, { combFeedback, 0.8f }, { squash, 0.75f }, { clip, 0.6f }, { tilt, 0.25f }, { resample, 1 } } },

        { "Dark Robot Riddim", {
            { A (table), 5 }, { A (pos), 0.2f }, { A (warpMode), 4 }, { A (warp), 0.4f },
            { B (table), 7 }, { B (level), 0.0f }, { B (octave), -1 }, { fmAmount, 0.45f }, { subLevel, 0.6f },
            { cutoff, 20000 }, { release, 25 }, { pitchEnv, 7 }, { pitchDecay, 20 },
            { lfoShape, 4 }, { lfoRate, 5 }, { lfoCutoff, 0.0f }, { lfoFm, 0.35f }, { lfoVowel, 0.6f },
            { distType, 0 }, { distDrive, 0.6f }, { vowelMix, 0.8f }, { vowel, 0.2f },
            { combMix, 0.3f }, { combFeedback, 0.6f }, { squash, 0.7f }, { clip, 0.55f }, { tilt, -0.2f }, { resample, 1 } } },

        { "Classic Riddim Wobble", {
            { A (table), 0 }, { A (pos), 0.3f }, { subLevel, 0.6f },
            { cutoff, 150 }, { resonance, 0.45f },
            { lfoShape, 0 }, { lfoRate, 4 }, { lfoCutoff, 0.75f },
            { distType, 0 }, { distDrive, 0.45f }, { squash, 0.4f }, { clip, 0.3f } } },

        { "Tearout Growl", {
            { A (table), 1 }, { A (pos), 0.5f }, { B (table), 3 }, { B (level), 0.4f }, { B (octave), -1 },
            { subLevel, 0.5f }, { cutoff, 400 }, { resonance, 0.35f },
            { lfoShape, 5 }, { lfoRate, 4 }, { lfoCutoff, 0.55f }, { lfoPosA, 0.5f },
            { distType, 2 }, { distDrive, 0.55f }, { combMix, 0.3f }, { combFreq, 180 }, { combFeedback, 0.7f },
            { squash, 0.6f }, { clip, 0.45f }, { heat, 0.15f } } },

        { "Yoi Talker", {
            { A (table), 2 }, { A (pos), 0.0f }, { A (unison), 2 }, { A (detune), 0.15f },
            { subLevel, 0.45f }, { cutoff, 3000 }, { resonance, 0.2f },
            { lfoShape, 1 }, { lfoRate, 2 }, { lfoCutoff, 0.2f }, { lfoPosA, 1.0f },
            { distType, 0 }, { distDrive, 0.35f }, { vowelMix, 0.5f }, { vowel, 0.6f }, { lfoVowel, 0.4f },
            { combMix, 0.2f }, { squash, 0.4f }, { tilt, 0.2f } } },

        { "Metal Screech", {
            { A (table), 4 }, { A (pos), 0.4f }, { subLevel, 0.3f },
            { cutoff, 2500 }, { resonance, 0.3f },
            { lfoShape, 3 }, { lfoRate, 6 }, { lfoCutoff, 0.3f }, { lfoPosA, 0.6f },
            { distType, 1 }, { distDrive, 0.5f }, { combMix, 0.5f }, { combFreq, 600 }, { combFeedback, 0.8f },
            { squash, 0.5f }, { tilt, 0.3f } } },

        { "Reese Sub", {
            { A (table), 6 }, { A (pos), 0.4f }, { A (unison), 3 }, { A (detune), 0.35f },
            { B (table), 0 }, { B (level), 0.3f }, { B (octave), -1 },
            { subLevel, 0.8f }, { cutoff, 600 }, { resonance, 0.2f },
            { lfoShape, 0 }, { lfoRate, 1 }, { lfoCutoff, 0.3f }, { lfoPosA, 0.4f },
            { distDrive, 0.2f }, { squash, 0.3f }, { lowKeep, 150 } } },

        { "Half-Time Wub", {
            { A (table), 0 }, { A (pos), 0.6f }, { subLevel, 0.6f },
            { cutoff, 120 }, { resonance, 0.5f },
            { lfoShape, 6 }, { lfoRate, 2 }, { lfoCutoff, 0.85f },
            { distType, 0 }, { distDrive, 0.5f }, { squash, 0.5f }, { clip, 0.35f } } },

        { "Stutter Riddim", {
            { A (table), 5 }, { A (pos), 0.3f }, { B (table), 1 }, { B (level), 0.5f }, { fmAmount, 0.2f },
            { subLevel, 0.5f }, { cutoff, 300 }, { resonance, 0.4f },
            { lfoShape, 7 }, { lfoRate, 4 }, { lfoCutoff, 0.7f }, { lfoVolume, 0.6f },
            { distType, 3 }, { distDrive, 0.35f }, { squash, 0.4f } } },

        { "FM Destroyer", {
            { A (table), 7 }, { A (pos), 0.0f }, { B (table), 7 }, { B (octave), 1 }, { B (level), 0.0f },
            { fmAmount, 0.55f }, { subLevel, 0.5f }, { cutoff, 5000 }, { resonance, 0.1f },
            { lfoShape, 4 }, { lfoRate, 6 }, { lfoCutoff, 0.0f }, { lfoFm, 0.4f },
            { distType, 2 }, { distDrive, 0.4f }, { squash, 0.6f }, { heat, 0.2f } } },

        { "Riddim Square Yoi", {
            { A (table), 5 }, { A (pos), 0.0f }, { subLevel, 0.55f }, { cutoff, 20000 }, { resonance, 0.1f },
            { lfoShape, 1 }, { lfoRate, 5 }, { lfoCutoff, 0.0f }, { lfoVowel, 0.5f },
            { distType, 0 }, { distDrive, 0.55f }, { vowelMix, 0.85f }, { vowel, 0.5f },
            { combMix, 0.25f }, { combFeedback, 0.5f }, { squash, 0.6f }, { clip, 0.45f }, { release, 25 } } },

        { "Riddim Square Wob", {
            { A (table), 5 }, { A (pos), 0.0f }, { subLevel, 0.55f }, { cutoff, 120 }, { resonance, 0.5f },
            { lfoShape, 0 }, { lfoRate, 5 }, { lfoCutoff, 0.8f },
            { distType, 0 }, { distDrive, 0.6f }, { vowelMix, 0.4f }, { vowel, 0.1f },
            { squash, 0.55f }, { clip, 0.4f }, { release, 25 } } },

        { "Clean Sub", {
            { A (level), 0.0f }, { subLevel, 1.0f }, { cutoff, 20000 }, { lfoCutoff, 0.0f },
            { distDrive, 0.0f }, { squash, 0.15f }, { clip, 0.1f } } },
    };

    return presets;
}

// Chains for the FX plugin, meant to go on any bass, one-shot or square patch.
const std::vector<FactoryPreset>& fxFactoryPresets()
{
    static const std::vector<FactoryPreset> presets {
        { "Riddim Chain", {
            { distType, 0 }, { distDrive, 0.45f }, { squash, 0.55f }, { lowKeep, 110 }, { clip, 0.4f }, { tilt, 0.1f } } },

        { "Square To Riddim", {
            { distType, 0 }, { distDrive, 0.6f }, { vowelMix, 0.75f }, { vowel, 0.45f },
            { fxLfoShape, 1 }, { fxLfoRate, 5 }, { fxLfoVowel, 0.5f },
            { squash, 0.55f }, { clip, 0.45f }, { heat, 0.1f } } },

        { "Yoi Machine", {
            { distType, 0 }, { distDrive, 0.5f }, { vowelMix, 0.9f }, { vowel, 0.5f },
            { fxLfoShape, 6 }, { fxLfoRate, 4 }, { fxLfoVowel, 0.5f },
            { squash, 0.6f }, { clip, 0.4f } } },

        { "Tearout Smash", {
            { distType, 2 }, { distDrive, 0.6f }, { combMix, 0.3f }, { combFreq, 180 }, { combFeedback, 0.7f },
            { squash, 0.8f }, { clip, 0.6f }, { heat, 0.2f } } },

        { "Metal Comb", {
            { distType, 1 }, { distDrive, 0.4f }, { combMix, 0.6f }, { combFreq, 440 }, { combFeedback, 0.85f },
            { squash, 0.5f }, { tilt, 0.3f } } },

        { "Grit Light", {
            { distType, 0 }, { distDrive, 0.25f }, { squash, 0.3f }, { clip, 0.2f } } },

        { "Crushed Lo-Fi", {
            { distType, 3 }, { distDrive, 0.5f }, { squash, 0.4f }, { tilt, -0.3f } } },

        { "Bypass (Clean)", {
            { distDrive, 0.0f }, { squash, 0.0f }, { clip, 0.0f }, { lowKeep, 0 } } },
    };

    return presets;
}
