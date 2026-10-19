#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/RiddimFx.h"

// Every automatable parameter, by ID. Saved in the plugin state and in presets.
namespace ParamID
{
    // Oscillators A and B share a layout; prefix with "oscA_" / "oscB_".
    inline juce::String osc (char which, const char* name) { return juce::String ("osc") + which + "_" + name; }

    inline constexpr const char* table   = "table";
    inline constexpr const char* pos     = "pos";
    inline constexpr const char* level   = "level";
    inline constexpr const char* octave  = "octave";
    inline constexpr const char* semi    = "semi";
    inline constexpr const char* unison  = "unison";
    inline constexpr const char* detune  = "detune";
    inline constexpr const char* warpMode = "warp_mode";
    inline constexpr const char* warp     = "warp";

    inline constexpr const char* fmAmount   = "fm_amount";
    inline constexpr const char* subLevel   = "sub_level";
    inline constexpr const char* noiseLevel = "noise_level";

    inline constexpr const char* filterType   = "filter_type";
    inline constexpr const char* cutoff       = "filter_cutoff";
    inline constexpr const char* resonance    = "filter_res";
    inline constexpr const char* filterEnv    = "filter_env";
    inline constexpr const char* filterDecay  = "filter_decay";

    inline constexpr const char* attack  = "amp_attack";
    inline constexpr const char* decay   = "amp_decay";
    inline constexpr const char* sustain = "amp_sustain";
    inline constexpr const char* release = "amp_release";

    inline constexpr const char* lfoShape   = "lfo_shape";
    inline constexpr const char* lfoRate    = "lfo_rate";
    inline constexpr const char* lfoRetrig  = "lfo_retrig";
    inline constexpr const char* lfoCutoff  = "lfo_to_cutoff";
    inline constexpr const char* lfoPosA    = "lfo_to_posA";
    inline constexpr const char* lfoPosB    = "lfo_to_posB";
    inline constexpr const char* lfoFm      = "lfo_to_fm";
    inline constexpr const char* lfoVolume  = "lfo_to_volume";
    inline constexpr const char* lfoWarpA   = "lfo_to_warpA";
    inline constexpr const char* pitchEnv   = "pitch_env";
    inline constexpr const char* pitchDecay = "pitch_decay";

    inline constexpr const char* distType  = "dist_type";
    inline constexpr const char* distDrive = "dist_drive";
    inline constexpr const char* distMix   = "dist_mix";
    inline constexpr const char* squash    = "squash"; // shown as "Smash"
    inline constexpr const char* vowelMix     = "vowel_mix";
    inline constexpr const char* vowel        = "vowel";
    inline constexpr const char* lfoVowel     = "lfo_to_vowel"; // synth only
    inline constexpr const char* combTrack    = "comb_track";   // synth only
    inline constexpr const char* fxLfoShape   = "fxlfo_shape";  // FX plugin only: its own movement LFO
    inline constexpr const char* fxLfoRate    = "fxlfo_rate";
    inline constexpr const char* fxLfoVowel   = "fxlfo_vowel";
    inline constexpr const char* combMix      = "comb_mix";
    inline constexpr const char* combFreq     = "comb_freq";
    inline constexpr const char* combFeedback = "comb_feedback";
    inline constexpr const char* lowKeep   = "low_keep";
    inline constexpr const char* tilt      = "tilt";
    inline constexpr const char* clip      = "clip";
    inline constexpr const char* heat      = "heat";
    inline constexpr const char* resample  = "resample";
    inline constexpr const char* gate      = "gate";
    inline constexpr const char* fxMix     = "fx_mix";

    inline constexpr const char* mono      = "mono";
    inline constexpr const char* glide     = "glide";
    inline constexpr const char* bendRange = "bend_range";
    inline constexpr const char* master    = "master_gain";
}

namespace Choices
{
    inline const juce::StringArray filterTypes { "Low Pass", "Band Pass", "High Pass", "Notch" };
    inline const juce::StringArray lfoShapes   { "Sine", "Triangle", "Saw Down", "Saw Up", "Square",
                                                 "Riddim Step", "Half Wob", "Stutter" };
    inline const juce::StringArray lfoRates    { "1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    inline const double lfoCyclesPerBeat[]     { 0.25, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0 };
    inline const juce::StringArray warpModes   { "No Warp", "Sync", "Bend", "Mirror", "Quantize" };
    inline const juce::StringArray distTypes   { "Soft Clip", "Hard Clip", "Wavefold", "Bitcrush" };
}

// The synth: oscillators, filter, envelopes, LFO, voice settings and the FX chain.
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// The FX plugin: only the riddim processing chain.
juce::AudioProcessorValueTreeState::ParameterLayout createFxParameterLayout();

// The FX chain's parameters, for any plugin that carries the chain.
void addFxParameters (juce::AudioProcessorValueTreeState::ParameterLayout&, float defaultOutputDb);

// Reads the FX chain parameters (present in both plugins) for one block.
FxSettings readFxSettings (const juce::AudioProcessorValueTreeState&);
